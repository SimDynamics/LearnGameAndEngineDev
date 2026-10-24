#include "presentation/windows/smo_compute.hpp"

#include <stdexcept>

#if defined(LGAED_HAS_VULKAN) && LGAED_HAS_VULKAN
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace lgaed::presentation {
namespace {
double no_gpu_clock() {return std::numeric_limits<double>::quiet_NaN();}
void checked(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" (VkResult "+std::to_string(result)+").");
}
std::string lower_ascii(std::string text) {
    for(auto& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return text;
}
std::string version(std::uint32_t value) {
    return std::to_string(VK_API_VERSION_MAJOR(value))+'.'+std::to_string(VK_API_VERSION_MINOR(value))+'.'+
        std::to_string(VK_API_VERSION_PATCH(value));
}

#define SMO_VK_DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) \
    X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(FreeCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdCopyBuffer) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateFence) X(DestroyFence) X(WaitForFences) X(QueueSubmit) X(DeviceWaitIdle) \
    X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults)
struct DeviceFunctions {
#define SMO_VK_MEMBER(name) PFN_vk##name name=nullptr;
    SMO_VK_DEVICE_FUNCTIONS(SMO_VK_MEMBER)
#undef SMO_VK_MEMBER
};

class VulkanCompute final : public SmoCompute {
    struct Buffer {
        VkBuffer handle=VK_NULL_HANDLE;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        VkDeviceSize size=0;
        VkMemoryPropertyFlags properties=0;
    };
    // One submission owns distinct primary command buffers and descriptor sets
    // for every dispatch. Push constants and descriptors cannot be overwritten
    // by a later pass before the earlier pass is actually executed.
    struct Submission {
        VulkanCompute& owner;
        std::vector<VkCommandBuffer> commands;
        VkDescriptorPool descriptors=VK_NULL_HANDLE;
        VkQueryPool queries=VK_NULL_HANDLE;
        VkFence fence=VK_NULL_HANDLE;
        bool pending=false;
        explicit Submission(VulkanCompute& value):owner(value) {}
        ~Submission() {
            // Cancellation waits for the submitted frame's fence before
            // throwing. On a driver/device failure, retire outstanding work
            // before releasing resources that a submitted command may use.
            if(pending&&owner.vk_.DeviceWaitIdle)(void)owner.vk_.DeviceWaitIdle(owner.device_);
            if(fence)owner.vk_.DestroyFence(owner.device_,fence,nullptr);
            if(queries)owner.vk_.DestroyQueryPool(owner.device_,queries,nullptr);
            if(descriptors)owner.vk_.DestroyDescriptorPool(owner.device_,descriptors,nullptr);
            if(!commands.empty())owner.vk_.FreeCommandBuffers(owner.device_,owner.command_pool_,
                static_cast<std::uint32_t>(commands.size()),commands.data());
        }
    };
    HMODULE loader_=nullptr;
    PFN_vkGetInstanceProcAddr instance_proc_=nullptr;
    PFN_vkGetDeviceProcAddr device_proc_=nullptr;
    PFN_vkDestroyInstance destroy_instance_=nullptr;
    VkInstance instance_=VK_NULL_HANDLE;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceMemoryProperties memory_{};
    VkDevice device_=VK_NULL_HANDLE;
    VkQueue queue_=VK_NULL_HANDLE;
    std::uint32_t family_=0,timestamp_bits_=0;
    DeviceFunctions vk_;
    VkDescriptorSetLayout descriptor_layout_=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_=VK_NULL_HANDLE;
    VkCommandPool command_pool_=VK_NULL_HANDLE;
    std::vector<Buffer> buffers_;
    std::vector<VkPipeline> pipelines_{VK_NULL_HANDLE};
    SmoComputeInfo info_;
    std::stop_token stop_;

    void cancel()const {
        if(stop_.stop_requested())throw std::runtime_error("Prueba Vulkan cancelada.");
    }
    template<class Function> Function instance_function(const char* name)const {
        const auto result=reinterpret_cast<Function>(instance_proc_(instance_,name));
        if(!result)throw std::runtime_error(std::string("El controlador Vulkan no ofrece ")+name+'.');
        return result;
    }
    template<class Function> Function device_function(const char* name)const {
        const auto result=reinterpret_cast<Function>(device_proc_(device_,name));
        if(!result)throw std::runtime_error(std::string("El dispositivo Vulkan no ofrece ")+name+'.');
        return result;
    }
    std::uint32_t memory_type(std::uint32_t accepted,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred,
        bool unmapped_device_local)const {
        int best=-1,score=-1;
        for(std::uint32_t i=0;i<memory_.memoryTypeCount;++i) {
            const auto flags=memory_.memoryTypes[i].propertyFlags;
            if(!(accepted&(1u<<i))||(flags&required)!=required)continue;
            int candidate=0;
            if((flags&preferred)==preferred)candidate+=4;
            if(unmapped_device_local&&!(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))candidate+=8;
            if(candidate>score) {best=static_cast<int>(i);score=candidate;}
        }
        if(best<0)throw std::runtime_error("No existe un tipo de memoria Vulkan compatible con el recurso solicitado.");
        return static_cast<std::uint32_t>(best);
    }
    Buffer make_buffer(VkDeviceSize bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred=0,bool local=false) {
        Buffer result;result.size=bytes;
        try {
            VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            create.size=bytes;create.usage=usage;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            checked(vk_.CreateBuffer(device_,&create,nullptr,&result.handle),"Crear buffer Vulkan");
            VkMemoryRequirements requirements{};vk_.GetBufferMemoryRequirements(device_,result.handle,&requirements);
            const auto type=memory_type(requirements.memoryTypeBits,required,preferred,local);
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=type;
            result.properties=memory_.memoryTypes[type].propertyFlags;
            checked(vk_.AllocateMemory(device_,&allocate,nullptr,&result.memory),"Reservar memoria Vulkan");
            checked(vk_.BindBufferMemory(device_,result.handle,result.memory,0),"Enlazar memoria Vulkan");
            return result;
        } catch(...) {destroy_buffer(result);throw;}
    }
    void destroy_buffer(Buffer& value)noexcept {
        if(value.handle)vk_.DestroyBuffer(device_,value.handle,nullptr);
        if(value.memory)vk_.FreeMemory(device_,value.memory,nullptr);
        value={};
    }
    Buffer& get_buffer(SmoBuffer id) {
        if(id>=buffers_.size())throw std::invalid_argument("Identificador de buffer Vulkan inexistente.");
        return buffers_[id];
    }
    void commands(Submission& job,std::uint32_t count) {
        job.commands.resize(count,VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool=command_pool_;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=count;
        const auto result=vk_.AllocateCommandBuffers(device_,&allocate,job.commands.data());
        if(result!=VK_SUCCESS) {job.commands.clear();checked(result,"Reservar comandos Vulkan");}
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        checked(vk_.CreateFence(device_,&fence,nullptr,&job.fence),"Crear señal de finalización Vulkan");
    }
    void begin(VkCommandBuffer command) {
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(vk_.BeginCommandBuffer(command,&info),"Iniciar comandos Vulkan");
    }
    void end(VkCommandBuffer command) {checked(vk_.EndCommandBuffer(command),"Cerrar comandos Vulkan");}
    std::chrono::steady_clock::duration submit(Submission& job) {
        cancel();
        VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount=static_cast<std::uint32_t>(job.commands.size());submit_info.pCommandBuffers=job.commands.data();
        const auto started=std::chrono::steady_clock::now();
        checked(vk_.QueueSubmit(queue_,1,&submit_info,job.fence),"Enviar trabajo real a Vulkan");
        job.pending=true;
        const auto deadline=started+std::chrono::seconds(10);
        // The wait is a real Vulkan fence wait, not a delay used as a GPU
        // measurement. A submitted GPU frame cannot be recalled; a stop request
        // is honored as soon as its resources can safely be retired.
        // https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html
        bool canceled=false;
        for(;;) {
            canceled=canceled||stop_.stop_requested();
            const auto status=vk_.WaitForFences(device_,1,&job.fence,VK_TRUE,1000000);
            if(status==VK_SUCCESS)break;
            if(status!=VK_TIMEOUT)checked(status,"Esperar finalización real de Vulkan");
            if(std::chrono::steady_clock::now()>=deadline)
                throw std::runtime_error("El dispositivo Vulkan no completó el fotograma en 10 segundos.");
        }
        job.pending=false;
        const auto elapsed=std::chrono::steady_clock::now()-started;
        if(canceled)throw std::runtime_error("Prueba Vulkan cancelada después de liberar el fotograma enviado.");
        return elapsed;
    }
    void buffer_barrier(VkCommandBuffer command,const Buffer& buffer,VkAccessFlags from,VkAccessFlags to,
        VkPipelineStageFlags source,VkPipelineStageFlags destination) {
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask=from;barrier.dstAccessMask=to;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer=buffer.handle;barrier.offset=0;barrier.size=VK_WHOLE_SIZE;
        vk_.CmdPipelineBarrier(command,source,destination,0,0,nullptr,1,&barrier,0,nullptr);
    }
    void upload(Buffer& destination,std::span<const std::byte> data) {
        if(data.empty())return;
        if(data.size()>destination.size)throw std::invalid_argument("La carga excede el buffer Vulkan.");
        auto staging=make_buffer(data.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        try {
            void* mapping=nullptr;
            checked(vk_.MapMemory(device_,staging.memory,0,VK_WHOLE_SIZE,0,&mapping),"Abrir staging de carga Vulkan");
            std::memcpy(mapping,data.data(),data.size());
            if(!(staging.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=staging.memory;range.size=VK_WHOLE_SIZE;
                const auto status=vk_.FlushMappedMemoryRanges(device_,1,&range);
                vk_.UnmapMemory(device_,staging.memory);checked(status,"Vaciar caché de carga Vulkan");
            } else vk_.UnmapMemory(device_,staging.memory);
            Submission job(*this);commands(job,1);const auto command=job.commands[0];begin(command);
            buffer_barrier(command,staging,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
            buffer_barrier(command,destination,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
            const VkBufferCopy copy{0,0,data.size()};vk_.CmdCopyBuffer(command,staging.handle,destination.handle,1,&copy);
            buffer_barrier(command,destination,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            end(command);(void)submit(job);destroy_buffer(staging);
        } catch(...) {destroy_buffer(staging);throw;}
    }

public:
    explicit VulkanCompute(std::stop_token stop):stop_(stop) {}
    ~VulkanCompute()override {
        if(device_&&vk_.DeviceWaitIdle)(void)vk_.DeviceWaitIdle(device_);
        if(device_) {
            for(auto pipeline:pipelines_)if(pipeline&&vk_.DestroyPipeline)vk_.DestroyPipeline(device_,pipeline,nullptr);
            for(auto& buffer:buffers_)destroy_buffer(buffer);
            if(command_pool_)vk_.DestroyCommandPool(device_,command_pool_,nullptr);
            if(pipeline_layout_)vk_.DestroyPipelineLayout(device_,pipeline_layout_,nullptr);
            if(descriptor_layout_)vk_.DestroyDescriptorSetLayout(device_,descriptor_layout_,nullptr);
            if(vk_.DestroyDevice)vk_.DestroyDevice(device_,nullptr);
        }
        if(instance_&&destroy_instance_)destroy_instance_(instance_,nullptr);
        if(loader_)FreeLibrary(loader_);
    }
    void initialize(const SmoComputeOptions& options);
    SmoComputeInfo info()const override {return info_;}
    SmoBuffer buffer(std::size_t bytes,std::span<const std::byte> initial)override;
    SmoKernel kernel(const SmoKernelSource&)override;
    void write(SmoBuffer id,std::span<const std::byte> data)override {cancel();upload(get_buffer(id),data);}
    std::vector<std::byte> read(SmoBuffer id)override;
    double execute(std::span<const SmoDispatch>,bool timed)override;
};

void VulkanCompute::initialize(const SmoComputeOptions& options) {
    cancel();
    loader_=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!loader_)throw std::runtime_error("No está disponible el cargador oficial Vulkan de Windows (vulkan-1.dll). Código "+std::to_string(GetLastError())+'.');
    instance_proc_=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader_,"vkGetInstanceProcAddr"));
    if(!instance_proc_)throw std::runtime_error("El cargador Vulkan no ofrece vkGetInstanceProcAddr.");
    std::uint32_t loader_version=VK_API_VERSION_1_0;
    const auto enumerate_version=reinterpret_cast<PFN_vkEnumerateInstanceVersion>(instance_proc_(VK_NULL_HANDLE,"vkEnumerateInstanceVersion"));
    if(enumerate_version)checked(enumerate_version(&loader_version),"Consultar versión real de Vulkan");
    if(loader_version<VK_API_VERSION_1_1)throw std::runtime_error("La prueba requiere Vulkan 1.1 o posterior.");
    const auto create_instance=instance_function<PFN_vkCreateInstance>("vkCreateInstance");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="LearnGameAndEngineDev SMO compute";
    app.applicationVersion=1;app.pEngineName="Native SMO laboratory";app.engineVersion=1;app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&app;
    checked(create_instance(&create,nullptr,&instance_),"Crear instancia Vulkan 1.1 sin superficie");
    destroy_instance_=instance_function<PFN_vkDestroyInstance>("vkDestroyInstance");
    const auto enumerate=instance_function<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    const auto device_properties=instance_function<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
    const auto device_memory=instance_function<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties");
    const auto queue_families=instance_function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
    const auto create_device=instance_function<PFN_vkCreateDevice>("vkCreateDevice");
    device_proc_=instance_function<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    std::vector<VkPhysicalDevice> devices;
    for(;;) {
        cancel();std::uint32_t count=0;checked(enumerate(instance_,&count,nullptr),"Enumerar dispositivos Vulkan");
        if(!count)throw std::runtime_error("El cargador Vulkan no encontró dispositivos.");
        devices.resize(count);const auto status=enumerate(instance_,&count,devices.data());
        if(status==VK_INCOMPLETE)continue;
        checked(status,"Leer dispositivos Vulkan");devices.resize(count);break;
    }
    int best=-1;
    for(const auto candidate:devices) {
        cancel();VkPhysicalDeviceProperties details{};device_properties(candidate,&details);
        if(details.apiVersion<VK_API_VERSION_1_1)continue;
        const auto candidate_name=lower_ascii(details.deviceName);
        const bool software=details.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU||candidate_name.find("llvmpipe")!=std::string::npos||
            candidate_name.find("lavapipe")!=std::string::npos||candidate_name.find("swiftshader")!=std::string::npos;
        if(software!=options.software)continue;
        if(!options.adapter_name.empty()&&candidate_name.find(lower_ascii(options.adapter_name))==std::string::npos)continue;
        if(details.limits.maxPerStageDescriptorStorageBuffers<8||details.limits.maxDescriptorSetStorageBuffers<8||details.limits.maxPushConstantsSize<64)continue;
        std::uint32_t count=0;queue_families(candidate,&count,nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);queue_families(candidate,&count,queues.data());
        int queue_score=-1;std::uint32_t selected=0;
        for(std::uint32_t i=0;i<count;++i) {
            if(!queues[i].queueCount||!(queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT))continue;
            const int score=((queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)?2:0)+(queues[i].timestampValidBits?1:0);
            if(score>queue_score) {queue_score=score;selected=i;}
        }
        if(queue_score<0)continue;
        const int score=(details.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?1000:
            details.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?500:100)+queue_score;
        if(score>best) {
            best=score;physical_=candidate;properties_=details;family_=selected;timestamp_bits_=queues[selected].timestampValidBits;
            info_.software=software;
        }
    }
    if(!physical_) {
        if(options.software)throw std::runtime_error("No hay un dispositivo Vulkan de software real compatible con esta prueba; Vulkan no utiliza WARP.");
        if(!options.adapter_name.empty())throw std::runtime_error("No se encontró el adaptador Vulkan solicitado con las ocho vinculaciones de cómputo: "+options.adapter_name);
        throw std::runtime_error("No hay una GPU Vulkan 1.1 con ocho buffers de almacenamiento y cómputo compatibles.");
    }
    device_memory(physical_,&memory_);
    const float priority=1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family_;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo device_create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_create.queueCreateInfoCount=1;device_create.pQueueCreateInfos=&queue;
    checked(create_device(physical_,&device_create,nullptr,&device_),"Crear dispositivo Vulkan de cómputo");
#define SMO_VK_LOAD(name) vk_.name=device_function<PFN_vk##name>("vk" #name);
    SMO_VK_DEVICE_FUNCTIONS(SMO_VK_LOAD)
#undef SMO_VK_LOAD
    vk_.GetDeviceQueue(device_,family_,0,&queue_);
    std::array<VkDescriptorSetLayoutBinding,8> bindings{};
    for(std::uint32_t i=0;i<bindings.size();++i) {
        bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptors.bindingCount=static_cast<std::uint32_t>(bindings.size());descriptors.pBindings=bindings.data();
    checked(vk_.CreateDescriptorSetLayout(device_,&descriptors,nullptr,&descriptor_layout_),"Crear ocho bindings de almacenamiento Vulkan");
    const VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT,0,64};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1;layout.pSetLayouts=&descriptor_layout_;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&constants;
    checked(vk_.CreatePipelineLayout(device_,&layout,nullptr,&pipeline_layout_),"Crear parámetros de 64 bytes Vulkan");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=family_;pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    checked(vk_.CreateCommandPool(device_,&pool,nullptr,&command_pool_),"Crear comandos Vulkan independientes");
    info_.api="Vulkan 1.1";info_.adapter=properties_.deviceName;info_.vendor_id=properties_.vendorID;info_.device_id=properties_.deviceID;
    info_.timestamps=timestamp_bits_>0&&std::isfinite(properties_.limits.timestampPeriod)&&properties_.limits.timestampPeriod>0;
    if(properties_.vendorID==0x10de) {
        const auto value=properties_.driverVersion;
        info_.driver="NVIDIA "+std::to_string((value>>22)&0x3ff)+'.'+std::to_string((value>>14)&0xff)+'.'+
            std::to_string((value>>6)&0xff)+'.'+std::to_string(value&0x3f);
    } else info_.driver="driverVersion "+std::to_string(properties_.driverVersion);
    info_.driver+="; API dispositivo "+version(properties_.apiVersion)+"; cola "+std::to_string(family_)+"; timestampBits "+std::to_string(timestamp_bits_);
    auto dummy=make_buffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,true);
    try {buffers_.push_back(dummy);}catch(...) {destroy_buffer(dummy);throw;}
    const std::array<std::byte,256> zeros{};upload(buffers_[0],zeros);
}

SmoBuffer VulkanCompute::buffer(std::size_t bytes,std::span<const std::byte> initial) {
    cancel();
    if(!bytes||bytes%4||bytes>properties_.limits.maxStorageBufferRange||initial.size()>bytes)
        throw std::invalid_argument("Tamaño de buffer Vulkan inválido: se requieren palabras uint y un rango admitido por la GPU.");
    if(buffers_.size()>=std::numeric_limits<SmoBuffer>::max())throw std::length_error("Se agotaron los identificadores de buffers Vulkan.");
    auto created=make_buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,true);
    try {buffers_.push_back(created);}catch(...) {destroy_buffer(created);throw;}
    const auto id=static_cast<SmoBuffer>(buffers_.size()-1);
    if(!initial.empty())upload(buffers_[id],initial);
    return id;
}

SmoKernel VulkanCompute::kernel(const SmoKernelSource& source) {
    cancel();
    if(source.spirv.size()<5||source.spirv[0]!=0x07230203)
        throw std::invalid_argument("Falta SPIR-V válido precompilado para el kernel Vulkan: "+source.name);
    if(pipelines_.size()>=std::numeric_limits<SmoKernel>::max())throw std::length_error("Se agotaron los identificadores de kernels Vulkan.");
    VkShaderModule module=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
    try {
        VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        create.codeSize=source.spirv.size()*sizeof(std::uint32_t);create.pCode=source.spirv.data();
        checked(vk_.CreateShaderModule(device_,&create,nullptr,&module),"Cargar SPIR-V Vulkan precompilado");
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=pipeline_layout_;
        compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module=module;compute.stage.pName="main";
        checked(vk_.CreateComputePipelines(device_,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline),"Crear kernel Vulkan real");
        pipelines_.push_back(pipeline);vk_.DestroyShaderModule(device_,module,nullptr);
        return static_cast<SmoKernel>(pipelines_.size()-1);
    } catch(...) {
        if(pipeline)vk_.DestroyPipeline(device_,pipeline,nullptr);
        if(module)vk_.DestroyShaderModule(device_,module,nullptr);
        throw;
    }
}

std::vector<std::byte> VulkanCompute::read(SmoBuffer id) {
    cancel();auto& source=get_buffer(id);
    auto staging=make_buffer(source.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    try {
        Submission job(*this);commands(job,1);const auto command=job.commands[0];begin(command);
        buffer_barrier(command,source,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        const VkBufferCopy copy{0,0,source.size};vk_.CmdCopyBuffer(command,source.handle,staging.handle,1,&copy);
        buffer_barrier(command,staging,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT);
        end(command);(void)submit(job);
        std::vector<std::byte> result(static_cast<std::size_t>(source.size));void* mapping=nullptr;
        checked(vk_.MapMemory(device_,staging.memory,0,VK_WHOLE_SIZE,0,&mapping),"Abrir staging de lectura Vulkan");
        if(!(staging.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            // Offset zero and VK_WHOLE_SIZE cover the complete allocation,
            // respecting nonCoherentAtomSize without truncating the last atom.
            // https://docs.vulkan.org/refpages/latest/refpages/source/vkFlushMappedMemoryRanges.html
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=staging.memory;range.size=VK_WHOLE_SIZE;
            const auto status=vk_.InvalidateMappedMemoryRanges(device_,1,&range);
            if(status!=VK_SUCCESS) {vk_.UnmapMemory(device_,staging.memory);checked(status,"Invalidar caché de lectura Vulkan");}
        }
        std::memcpy(result.data(),mapping,result.size());vk_.UnmapMemory(device_,staging.memory);destroy_buffer(staging);return result;
    }catch(...) {destroy_buffer(staging);throw;}
}

double VulkanCompute::execute(std::span<const SmoDispatch> dispatches,bool timed) {
    cancel();
    if(dispatches.empty())return no_gpu_clock();
    if(dispatches.size()>std::numeric_limits<std::uint32_t>::max()/8)
        throw std::length_error("Demasiadas vinculaciones para una secuencia Vulkan.");
    for(const auto& pass:dispatches) {
        if(!pass.kernel||pass.kernel>=pipelines_.size())throw std::invalid_argument("Kernel Vulkan inexistente.");
        for(unsigned axis=0;axis<3;++axis)
            if(pass.groups[axis]>properties_.limits.maxComputeWorkGroupCount[axis])
                throw std::invalid_argument("El dispatch excede los grupos de cómputo admitidos por Vulkan.");
        for(const auto id:pass.buffers)(void)get_buffer(id);
    }
    const auto count=static_cast<std::uint32_t>(dispatches.size());
    Submission job(*this);commands(job,count);
    VkDescriptorPoolSize storage{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*8};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=count;pool.poolSizeCount=1;pool.pPoolSizes=&storage;
    checked(vk_.CreateDescriptorPool(device_,&pool,nullptr,&job.descriptors),"Reservar descriptores Vulkan por dispatch");
    std::vector<VkDescriptorSetLayout> layouts(count,descriptor_layout_);
    std::vector<VkDescriptorSet> sets(count,VK_NULL_HANDLE);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool=job.descriptors;allocate.descriptorSetCount=count;allocate.pSetLayouts=layouts.data();
    checked(vk_.AllocateDescriptorSets(device_,&allocate,sets.data()),"Reservar un conjunto Vulkan por dispatch");
    std::vector<VkDescriptorBufferInfo> buffers(static_cast<std::size_t>(count)*8);
    std::vector<VkWriteDescriptorSet> writes(buffers.size());
    for(std::uint32_t pass=0;pass<count;++pass)for(std::uint32_t binding=0;binding<8;++binding) {
        const auto at=static_cast<std::size_t>(pass)*8+binding;
        const auto& resource=get_buffer(dispatches[pass].buffers[binding]);
        buffers[at]={resource.handle,0,resource.size};
        auto& write=writes[at];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[pass];write.dstBinding=binding;
        write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffers[at];
    }
    // No set is updated after command recording begins; each pass keeps its
    // own eight resources until the fence completes, including reused IDs.
    // https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html
    vk_.UpdateDescriptorSets(device_,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);
    const bool clock=timed&&info_.timestamps;
    if(clock) {
        VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};queries.queryType=VK_QUERY_TYPE_TIMESTAMP;queries.queryCount=2;
        checked(vk_.CreateQueryPool(device_,&queries,nullptr,&job.queries),"Crear consultas reales de tiempo Vulkan");
    }
    for(std::uint32_t pass=0;pass<count;++pass) {
        cancel();const auto command=job.commands[pass];begin(command);
        if(clock&&pass==0) {
            vk_.CmdResetQueryPool(command,job.queries,0,2);
            vk_.CmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,job.queries,0);
        }
        // A storage buffer may be output in one dispatch and input in the next
        // (or in the previous execute). Submission order alone is not a memory
        // dependency; make all prior compute/transfer writes visible here.
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vk_.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        vk_.CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines_[dispatches[pass].kernel]);
        vk_.CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout_,0,1,&sets[pass],0,nullptr);
        vk_.CmdPushConstants(command,pipeline_layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,64,dispatches[pass].constants.data());
        const auto& groups=dispatches[pass].groups;vk_.CmdDispatch(command,groups[0],groups[1],groups[2]);
        if(clock&&pass+1==count)vk_.CmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,job.queries,1);
        end(command);
    }
    const auto submitted_time=submit(job);
    if(!clock)return no_gpu_clock();
    struct Query {std::uint64_t value,available;};
    std::array<Query,2> timestamps{};
    const auto status=vk_.GetQueryPoolResults(device_,job.queries,0,2,sizeof(timestamps),timestamps.data(),sizeof(Query),
        VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if(status==VK_NOT_READY)return no_gpu_clock();
    checked(status,"Consultar marcas de tiempo reales de Vulkan");
    if(!timestamps[0].available||!timestamps[1].available)return no_gpu_clock();
    // Vulkan specifies the queue's valid counter bits and a nanosecond period.
    // Timestamps may wrap; no calibrated CPU clock replaces this GPU result.
    // https://docs.vulkan.org/spec/latest/chapters/queries.html#queries-timestamps
    // https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html
    const auto bits=timestamp_bits_;
    if(!bits||bits>64)return no_gpu_clock();
    if(bits<64) {
        const long double wrap_ns=std::ldexp(static_cast<long double>(properties_.limits.timestampPeriod),static_cast<int>(bits));
        // CPU time is only a conservative ambiguity bound: if even the whole
        // submission could span a counter cycle, reject rather than guessing
        // how many wraps occurred. It never becomes the reported GPU duration.
        if(std::chrono::duration<long double,std::nano>(submitted_time).count()>=wrap_ns)return no_gpu_clock();
    }
    auto delta=timestamps[1].value-timestamps[0].value;
    if(bits<64)delta&=(std::uint64_t{1}<<bits)-1;
    if(!delta)return no_gpu_clock();
    const double milliseconds=static_cast<double>(delta)*properties_.limits.timestampPeriod/1000000.;
    return std::isfinite(milliseconds)&&milliseconds>0?milliseconds:no_gpu_clock();
}
} // namespace

std::unique_ptr<SmoCompute> create_smo_vulkan_compute(const SmoComputeOptions& options,std::stop_token stop) {
    auto result=std::make_unique<VulkanCompute>(stop);
    result->initialize(options);
    return result;
}
} // namespace lgaed::presentation

#undef SMO_VK_DEVICE_FUNCTIONS
#else
namespace lgaed::presentation {
std::unique_ptr<SmoCompute> create_smo_vulkan_compute(const SmoComputeOptions&,std::stop_token stop) {
    if(stop.stop_requested())throw std::runtime_error("Prueba Vulkan cancelada.");
    throw std::runtime_error("Esta compilación no incluye los encabezados Vulkan oficiales; no se simula otro backend.");
}
}
#endif
