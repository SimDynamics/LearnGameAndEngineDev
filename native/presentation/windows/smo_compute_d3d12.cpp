#include "presentation/windows/smo_compute.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
using Microsoft::WRL::ComPtr;

std::string hresult(HRESULT value) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(value);
    return text.str();
}
void require_d3d12(HRESULT value, const char* operation) {
    if (FAILED(value)) throw std::runtime_error(std::string(operation) + " (" + hresult(value) + ")");
}
void validate_dxbc(std::span<const std::byte> bytes,UINT expected_minor){
    ComPtr<ID3D12ShaderReflection> reflection;require_d3d12(D3DReflect(bytes.data(),bytes.size(),IID_PPV_ARGS(reflection.GetAddressOf())),"Inspeccionar shader Direct3D12 preparado");
    D3D12_SHADER_DESC description{};require_d3d12(reflection->GetDesc(&description),"Leer shader Direct3D12 preparado");
    if(D3D12_SHVER_GET_TYPE(description.Version)!=D3D12_SHVER_COMPUTE_SHADER||D3D12_SHVER_GET_MAJOR(description.Version)!=5||D3D12_SHVER_GET_MINOR(description.Version)!=expected_minor)
        throw std::runtime_error("El bytecode Direct3D12 preparado no es un kernel cs_5_"+std::to_string(expected_minor)+".");
    for(UINT index=0;index<description.BoundResources;++index){D3D12_SHADER_INPUT_BIND_DESC binding{};require_d3d12(reflection->GetResourceBindingDesc(index,&binding),"Leer enlaces Direct3D12 preparados");
        if(binding.Type==D3D_SIT_CBUFFER){if(binding.BindPoint||binding.BindCount!=1||binding.Space)throw std::runtime_error("Las constantes Direct3D12 preparadas exceden b0, espacio0.");
            D3D12_SHADER_BUFFER_DESC buffer{};require_d3d12(reflection->GetConstantBufferByName(binding.Name)->GetDesc(&buffer),"Leer constantes Direct3D12 preparadas");
            if(buffer.Size!=64||std::strcmp(binding.Name,"Params")!=0)throw std::runtime_error("El shader Direct3D12 preparado no usa Params de 64 bytes.");}
        else if(binding.Type!=D3D_SIT_UAV_RWBYTEADDRESS||binding.BindPoint>=8||binding.BindCount!=1||binding.Space)
            throw std::runtime_error("El shader Direct3D12 preparado usa recursos fuera de u0..u7, espacio0.");}
    UINT x=0,y=0,z=0;const auto threads=reflection->GetThreadGroupSize(&x,&y,&z);
    if(!threads||threads>1024||!x||!y||!z)throw std::runtime_error("Las dimensiones locales del shader Direct3D12 preparado son inválidas.");
}
std::string adapter_text(const wchar_t* value) {
    const auto length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}
void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("Prueba SMO Direct3D 12 cancelada.");
}
struct Event {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Event() {
        if (!value) throw std::runtime_error("No se pudo crear la señal de finalización Direct3D 12.");
    }
    ~Event() { CloseHandle(value); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};

class D3D12Compute final : public SmoCompute {
    struct Buffer {
        ComPtr<ID3D12Resource> resource;
        std::size_t bytes = 0, allocated = 0;
    };
    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12DescriptorHeap> descriptors_;
    ComPtr<ID3D12QueryHeap> queries_;
    ComPtr<ID3D12Resource> query_readback_, upload_, readback_;
    std::vector<Buffer> buffers_;
    std::vector<ComPtr<ID3D12PipelineState>> kernels_{1};
    SmoComputeInfo info_;
    std::stop_token stop_;
    Event completion_;
    std::size_t upload_size_ = 0, readback_size_ = 0;
    UINT descriptor_count_ = 0, descriptor_stride_ = 0;
    UINT64 frequency_ = 0, submitted_ = 0;
    bool pending_ = false, broken_ = false;

    static D3D12_RESOURCE_DESC description(std::size_t bytes, D3D12_RESOURCE_FLAGS flags) {
        D3D12_RESOURCE_DESC result{};
        result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        result.Width = static_cast<UINT64>(bytes);
        result.Height = 1;
        result.DepthOrArraySize = 1;
        result.MipLevels = 1;
        result.SampleDesc.Count = 1;
        result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        result.Flags = flags;
        return result;
    }
    ComPtr<ID3D12Resource> resource(std::size_t bytes, D3D12_HEAP_TYPE type,
        D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = type;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        const auto desc = description(bytes, flags);
        ComPtr<ID3D12Resource> result;
        require_d3d12(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            state, nullptr, IID_PPV_ARGS(result.GetAddressOf())), "Crear almacenamiento Direct3D 12");
        return result;
    }
    void available() const {
        check_stop(stop_);
        if (broken_) throw std::runtime_error("El ejecutor Direct3D 12 ya no está disponible tras un fallo de GPU.");
    }
    void abandon_device() noexcept {
        broken_ = true;
        // A timed-out logical device must not keep using resources that are
        // about to be released. This does not change PC/driver configuration.
        ComPtr<ID3D12Device5> removable;
        if (device_ && SUCCEEDED(device_.As(&removable))) removable->RemoveDevice();
        pending_ = false;
    }
    void wait(bool observe_stop) {
        if (!pending_) return;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool canceled = false;
        require_d3d12(fence_->SetEventOnCompletion(submitted_, completion_.value), "Esperar la señal Direct3D 12");
        for (;;) {
            const auto completed = fence_->GetCompletedValue();
            if (completed == std::numeric_limits<UINT64>::max()) {
                broken_ = true;
                pending_ = false;
                throw std::runtime_error("Se retiró el dispositivo Direct3D 12 (" + hresult(device_->GetDeviceRemovedReason()) + ").");
            }
            canceled = canceled || (observe_stop && stop_.stop_requested());
            if (completed >= submitted_) break;
            if (std::chrono::steady_clock::now() >= deadline) {
                abandon_device();
                throw std::runtime_error("La GPU Direct3D 12 no completó la secuencia dentro de 10 segundos.");
            }
            // This waits for the actual GPU fence. It is never a substitute
            // for a GPU clock or an artificial delay in the measured work.
            const auto status = WaitForSingleObject(completion_.value, 10);
            if (status == WAIT_FAILED) {
                abandon_device();
                throw std::runtime_error("Falló la espera de finalización Direct3D 12.");
            }
        }
        pending_ = false;
        if (canceled) throw std::runtime_error("Prueba SMO Direct3D 12 cancelada después de liberar la secuencia enviada.");
    }
    void begin() {
        available();
        wait(true);
        require_d3d12(allocator_->Reset(), "Restablecer asignador Direct3D 12");
        require_d3d12(list_->Reset(allocator_.Get(), nullptr), "Restablecer comandos Direct3D 12");
    }
    void submit() {
        require_d3d12(list_->Close(), "Cerrar comandos Direct3D 12");
        check_stop(stop_);
        ID3D12CommandList* commands[]{list_.Get()};
        queue_->ExecuteCommandLists(1, commands);
        ++submitted_;
        const auto signaled = queue_->Signal(fence_.Get(), submitted_);
        if (FAILED(signaled)) {
            abandon_device();
            require_d3d12(signaled, "Señalar la finalización Direct3D 12");
        }
        pending_ = true;
        wait(true);
    }
    void transition(ID3D12Resource* item, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = item;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        list_->ResourceBarrier(1, &barrier);
    }
    void uav_order() {
        // A null resource orders every previous UAV write before later UAV
        // accesses, including dependencies between different shader passes.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        list_->ResourceBarrier(1, &barrier);
    }
    void ensure_upload(std::size_t bytes) {
        if (upload_size_ >= bytes) return;
        auto next = resource(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        upload_ = std::move(next);
        upload_size_ = bytes;
    }
    void ensure_readback(std::size_t bytes) {
        if (readback_size_ >= bytes) return;
        auto next = resource(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        readback_ = std::move(next);
        readback_size_ = bytes;
    }
    Buffer& stored(SmoBuffer id, bool dummy = false) {
        if ((!dummy && id == 0) || id >= buffers_.size())
            throw std::invalid_argument("Identificador de buffer Direct3D 12 no válido.");
        return buffers_[id];
    }
    void copy_in(Buffer& target, std::span<const std::byte> bytes, bool initialize) {
        const auto count = initialize ? target.allocated : bytes.size();
        if (count == 0) return;
        ensure_upload(count);
        D3D12_RANGE no_read{0, 0};
        void* mapped = nullptr;
        require_d3d12(upload_->Map(0, &no_read, &mapped), "Mapear carga Direct3D 12");
        if (initialize) std::memset(mapped, 0, count);
        if (!bytes.empty()) std::memcpy(mapped, bytes.data(), bytes.size());
        const D3D12_RANGE changed{0, count};
        upload_->Unmap(0, &changed);
        begin();
        transition(target.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        list_->CopyBufferRegion(target.resource.Get(), 0, upload_.Get(), 0, static_cast<UINT64>(count));
        transition(target.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        submit();
    }
    Buffer make_buffer(std::size_t bytes, std::span<const std::byte> initial) {
        if (bytes == 0 || bytes > std::numeric_limits<std::size_t>::max() - 3 || initial.size() > bytes)
            throw std::invalid_argument("Tamaño inicial de buffer Direct3D 12 no válido.");
        const auto aligned = (bytes + 3) & ~std::size_t{3};
        if (aligned / 4 > std::numeric_limits<UINT>::max())
            throw std::length_error("El buffer excede el número de elementos RAW Direct3D 12.");
        Buffer result{resource(aligned, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), bytes, aligned};
        copy_in(result, initial, true);
        return result;
    }
    void descriptor_heap(std::size_t count) {
        if (count <= descriptor_count_) return;
        if (count > static_cast<std::size_t>(D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1))
            throw std::length_error("La secuencia excede el heap de descriptores Direct3D 12.");
        const auto capacity = static_cast<UINT>(std::max(count, std::size_t{64}));
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = capacity;
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> next;
        require_d3d12(device_->CreateDescriptorHeap(&description, IID_PPV_ARGS(next.GetAddressOf())), "Crear tabla UAV Direct3D 12");
        descriptors_ = std::move(next);
        descriptor_count_ = capacity;
    }
    void choose_adapter(const SmoComputeOptions& options) {
        require_d3d12(CreateDXGIFactory1(IID_PPV_ARGS(factory_.GetAddressOf())), "Crear catálogo DXGI Direct3D 12");
        if (options.software) {
            require_d3d12(factory_->EnumWarpAdapter(IID_PPV_ARGS(adapter_.GetAddressOf())), "Seleccionar WARP Direct3D 12");
            return;
        }
        ComPtr<IDXGIAdapter1> first;
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            const auto found = factory_->EnumAdapters1(index, candidate.GetAddressOf());
            if (found == DXGI_ERROR_NOT_FOUND) break;
            require_d3d12(found, "Enumerar GPU Direct3D 12");
            DXGI_ADAPTER_DESC1 desc{};
            require_d3d12(candidate->GetDesc1(&desc), "Consultar GPU Direct3D 12");
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) continue;
            if (!first) first = candidate;
            if (!options.adapter_name.empty() && adapter_text(desc.Description) == options.adapter_name) {
                adapter_ = std::move(candidate);
                return;
            }
        }
        if (!options.adapter_name.empty())
            throw std::runtime_error("La GPU solicitada no está disponible para Direct3D 12: " + options.adapter_name);
        if (!first) throw std::runtime_error("No hay una GPU física compatible con Direct3D 12.");
        adapter_ = std::move(first);
    }
    void create_root() {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 8;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 16;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = 2;
        description.pParameters = parameters;
        ComPtr<ID3DBlob> serialized, errors;
        const auto result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
            serialized.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result) && errors)
            throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        require_d3d12(result, "Serializar raíz Direct3D 12");
        require_d3d12(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(root_.GetAddressOf())), "Crear raíz Direct3D 12");
    }
public:
    D3D12Compute(const SmoComputeOptions& options, std::stop_token stop) : stop_(stop) {
        check_stop(stop_);
        choose_adapter(options);
        require_d3d12(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.GetAddressOf())), "Crear dispositivo Direct3D 12");
        D3D12_COMMAND_QUEUE_DESC queue{};
        queue.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        require_d3d12(device_->CreateCommandQueue(&queue, IID_PPV_ARGS(queue_.GetAddressOf())), "Crear cola de cómputo Direct3D 12");
        require_d3d12(device_->CreateCommandAllocator(queue.Type, IID_PPV_ARGS(allocator_.GetAddressOf())), "Crear asignador Direct3D 12");
        require_d3d12(device_->CreateCommandList(0, queue.Type, allocator_.Get(), nullptr, IID_PPV_ARGS(list_.GetAddressOf())), "Crear lista Direct3D 12");
        require_d3d12(list_->Close(), "Preparar lista Direct3D 12");
        require_d3d12(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf())), "Crear fence Direct3D 12");
        create_root();
        descriptor_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        DXGI_ADAPTER_DESC1 desc{};
        require_d3d12(adapter_->GetDesc1(&desc), "Consultar adaptador Direct3D 12 activo");
        info_.api = "Direct3D 12";
        info_.adapter = adapter_text(desc.Description);
        info_.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        info_.vendor_id = desc.VendorId;
        info_.device_id = desc.DeviceId;
        LARGE_INTEGER driver{};
        if (SUCCEEDED(adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) {
            info_.driver = std::to_string(HIWORD(driver.HighPart)) + "." + std::to_string(LOWORD(driver.HighPart)) + "." +
                std::to_string(HIWORD(driver.LowPart)) + "." + std::to_string(LOWORD(driver.LowPart));
        } else info_.driver = "Versión UMD no disponible mediante DXGI";
        // Official queue-domain timestamps; no CPU interval, forced clocks or
        // PC power/driver setting changes are used by this adapter.
        // https://learn.microsoft.com/windows/win32/direct3d12/timing
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = 2;
        if (SUCCEEDED(queue_->GetTimestampFrequency(&frequency_)) && frequency_ != 0 &&
            SUCCEEDED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(queries_.GetAddressOf())))) {
            query_readback_ = resource(2 * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            info_.timestamps = true;
        }
        // ID0 is the documented four-byte zero dummy, not a student resource.
        buffers_.push_back(make_buffer(4, {}));
    }
    ~D3D12Compute() override {
        try { wait(false); } catch (...) { abandon_device(); }
    }
    SmoComputeInfo info() const override { return info_; }
    SmoBuffer buffer(std::size_t bytes, std::span<const std::byte> initial) override {
        available();
        if (buffers_.size() >= std::numeric_limits<SmoBuffer>::max()) throw std::length_error("Demasiados buffers Direct3D 12.");
        auto result = make_buffer(bytes, initial);
        buffers_.push_back(std::move(result));
        return static_cast<SmoBuffer>(buffers_.size() - 1);
    }
    SmoKernel kernel(const SmoKernelSource& source) override {
        available();
        const bool compatible50=source.dxbc51.empty()&&source.dxbc50_d3d12_compatible&&!source.dxbc.empty();
        if (source.hlsl.empty()&&source.dxbc51.empty()&&!compatible50) throw std::invalid_argument("Falta el kernel HLSL Direct3D 12.");
        if (kernels_.size() >= std::numeric_limits<SmoKernel>::max()) throw std::length_error("Demasiados kernels Direct3D 12.");
        ComPtr<ID3DBlob> code, errors;const void* bytecode=source.dxbc51.data();std::size_t byte_count=source.dxbc51.size();
        if(compatible50){
            // Direct3D12 accepts older compiled shaders when their bindings
            // fit this root signature. Keep and inspect the actual cs_5_0
            // profile instead of relabelling it or recompiling identical code.
            validate_dxbc(source.dxbc,0);
            bytecode=source.dxbc.data();byte_count=source.dxbc.size();
        }else if(source.dxbc51.empty()){
            const auto compiled = D3DCompile(source.hlsl.data(), source.hlsl.size(), source.name.c_str(), nullptr, nullptr,
                "CSMain", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0, code.GetAddressOf(), errors.GetAddressOf());
            if (FAILED(compiled) && errors)
                throw std::runtime_error("Direct3D 12 · " + source.name + ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
            require_d3d12(compiled, "Compilar kernel HLSL Direct3D 12");bytecode=code->GetBufferPointer();byte_count=code->GetBufferSize();
        }else validate_dxbc(source.dxbc51,1);
        check_stop(stop_);
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = root_.Get();
        description.CS = {bytecode,byte_count};
        ComPtr<ID3D12PipelineState> result;
        require_d3d12(device_->CreateComputePipelineState(&description, IID_PPV_ARGS(result.GetAddressOf())), "Crear kernel Direct3D 12");
        kernels_.push_back(std::move(result));
        return static_cast<SmoKernel>(kernels_.size() - 1);
    }
    void write(SmoBuffer id, std::span<const std::byte> bytes) override {
        available();
        auto& target = stored(id);
        if (bytes.size() > target.bytes) throw std::invalid_argument("La carga excede el buffer Direct3D 12.");
        copy_in(target, bytes, false);
    }
    std::vector<std::byte> read(SmoBuffer id) override {
        available();
        auto& source = stored(id);
        std::vector<std::byte> result(source.bytes);
        ensure_readback(source.allocated);
        begin();
        transition(source.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list_->CopyBufferRegion(readback_.Get(), 0, source.resource.Get(), 0, static_cast<UINT64>(source.bytes));
        transition(source.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        submit();
        const D3D12_RANGE range{0, source.bytes};
        void* mapped = nullptr;
        require_d3d12(readback_->Map(0, &range, &mapped), "Mapear lectura Direct3D 12");
        std::memcpy(result.data(), mapped, source.bytes);
        const D3D12_RANGE no_write{0, 0};
        readback_->Unmap(0, &no_write);
        return result;
    }
    double execute(std::span<const SmoDispatch> dispatches, bool timed) override {
        available();
        const auto invalid = std::numeric_limits<double>::quiet_NaN();
        if (dispatches.empty()) return invalid;
        if (dispatches.size() > static_cast<std::size_t>(D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1) / 8)
            throw std::length_error("Demasiados pases en la secuencia Direct3D 12.");
        descriptor_heap(dispatches.size() * 8);
        auto cpu = descriptors_->GetCPUDescriptorHandleForHeapStart();
        // Every dispatch gets immutable descriptors until its fence completes;
        // rewriting a single eight-slot table would bind the final pass to all.
        for (const auto& dispatch : dispatches) {
            check_stop(stop_);
            if (dispatch.kernel == 0 || dispatch.kernel >= kernels_.size()) throw std::invalid_argument("Kernel Direct3D 12 no válido.");
            for (const auto dimension : dispatch.groups)
                if (dimension > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
                    throw std::invalid_argument("El despacho excede los grupos permitidos por Direct3D 12.");
            for (const auto id : dispatch.buffers) {
                const auto& item = stored(id, true);
                D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_R32_TYPELESS;
                view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                view.Buffer.NumElements = static_cast<UINT>(item.allocated / 4);
                view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                device_->CreateUnorderedAccessView(item.resource.Get(), nullptr, &view, cpu);
                cpu.ptr += descriptor_stride_;
            }
        }
        begin();
        ID3D12DescriptorHeap* heaps[]{descriptors_.Get()};
        list_->SetDescriptorHeaps(1, heaps);
        list_->SetComputeRootSignature(root_.Get());
        auto gpu = descriptors_->GetGPUDescriptorHandleForHeapStart();
        const bool clock = timed && info_.timestamps;
        if (clock) list_->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        for (std::size_t index = 0; index < dispatches.size(); ++index) {
            const auto& dispatch = dispatches[index];
            list_->SetPipelineState(kernels_[dispatch.kernel].Get());
            list_->SetComputeRoot32BitConstants(0, 16, dispatch.constants.data(), 0);
            list_->SetComputeRootDescriptorTable(1, gpu);
            list_->Dispatch(dispatch.groups[0], dispatch.groups[1], dispatch.groups[2]);
            if (index + 1 < dispatches.size()) uav_order();
            gpu.ptr += static_cast<UINT64>(descriptor_stride_) * 8;
        }
        if (clock) {
            list_->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list_->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, query_readback_.Get(), 0);
        }
        submit();
        if (!clock) return invalid;
        const D3D12_RANGE range{0, 2 * sizeof(UINT64)};
        void* mapped = nullptr;
        require_d3d12(query_readback_->Map(0, &range, &mapped), "Leer timestamps Direct3D 12");
        std::array<UINT64, 2> ticks{};
        std::memcpy(ticks.data(), mapped, sizeof(ticks));
        const D3D12_RANGE no_write{0, 0};
        query_readback_->Unmap(0, &no_write);
        if (ticks[1] <= ticks[0]) return invalid;
        const auto elapsed = static_cast<double>(ticks[1] - ticks[0]) * 1000. / static_cast<double>(frequency_);
        return elapsed > 0 ? elapsed : invalid;
    }
};
} // namespace

std::unique_ptr<SmoCompute> create_smo_d3d12_compute(const SmoComputeOptions& options, std::stop_token stop) {
    return std::make_unique<D3D12Compute>(options, stop);
}
} // namespace lgaed::presentation
