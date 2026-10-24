#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/rich_text.hpp"
#include <d3d11_4.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

namespace lgaed::presentation { namespace {
using Microsoft::WRL::ComPtr;
void hr(HRESULT value,const char* action){if(FAILED(value))throw std::runtime_error(std::string(action)+" ("+std::to_string(static_cast<unsigned long>(value))+")");}
void validate_dxbc(std::span<const std::byte> bytes){
    ComPtr<ID3D11ShaderReflection> reflection;hr(D3DReflect(bytes.data(),bytes.size(),IID_PPV_ARGS(reflection.GetAddressOf())),"Reflect prepared Direct3D11 shader");
    D3D11_SHADER_DESC description{};hr(reflection->GetDesc(&description),"Read prepared Direct3D11 shader");
    if(D3D11_SHVER_GET_TYPE(description.Version)!=D3D11_SHVER_COMPUTE_SHADER||D3D11_SHVER_GET_MAJOR(description.Version)!=5||D3D11_SHVER_GET_MINOR(description.Version)!=0)
        throw std::runtime_error("Prepared Direct3D11 bytecode is not a cs_5_0 compute shader.");
    for(UINT index=0;index<description.BoundResources;++index){D3D11_SHADER_INPUT_BIND_DESC binding{};hr(reflection->GetResourceBindingDesc(index,&binding),"Read prepared Direct3D11 bindings");
        if(binding.Type==D3D_SIT_CBUFFER){if(binding.BindPoint||binding.BindCount!=1)throw std::runtime_error("Prepared Direct3D11 constants exceed b0.");
            D3D11_SHADER_BUFFER_DESC buffer{};hr(reflection->GetConstantBufferByName(binding.Name)->GetDesc(&buffer),"Read prepared Direct3D11 constant layout");
            if(buffer.Size!=64||std::strcmp(binding.Name,"Params")!=0)throw std::runtime_error("Prepared Direct3D11 shader does not use the 64-byte Params ABI.");}
        else if(binding.Type!=D3D_SIT_UAV_RWBYTEADDRESS||binding.BindPoint>=8||binding.BindCount!=1)
            throw std::runtime_error("Prepared Direct3D11 shader uses resources outside raw u0..u7.");}
    UINT x=0,y=0,z=0;const auto threads=reflection->GetThreadGroupSize(&x,&y,&z);
    if(!threads||threads>1024||!x||!y||!z)throw std::runtime_error("Prepared Direct3D11 shader has invalid compute group dimensions.");
}
class Compute final:public SmoCompute {
    struct Buffer{std::size_t bytes;ComPtr<ID3D11Buffer> object;ComPtr<ID3D11UnorderedAccessView> view;};
    struct FenceEvent{HANDLE value=nullptr;~FenceEvent(){if(value)CloseHandle(value);}};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Query> begin,end,disjoint,complete;
    ComPtr<ID3D11DeviceContext4> fenced_context;ComPtr<ID3D11Fence> fence;FenceEvent fence_event;UINT64 fence_value=0;
    std::vector<Buffer> buffers;std::vector<ComPtr<ID3D11ComputeShader>> kernels;std::array<SmoBuffer,8> dummy{};
    SmoComputeInfo description;std::stop_token stop;
    void check_stop()const{if(stop.stop_requested())throw std::runtime_error("Prueba GPU cancelada.");}
    Buffer& get(SmoBuffer id){if(!id||id>buffers.size())throw std::invalid_argument("Invalid GPU buffer");return buffers[id-1];}
    template<class Value>void wait(ID3D11Query* query,Value& value){
        const auto began=std::chrono::steady_clock::now(),deadline=began+std::chrono::seconds(10);bool cancelled=false;
        for(;;){cancelled=cancelled||stop.stop_requested();const auto result=context->GetData(query,&value,sizeof(value),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(result==S_OK)break;hr(result,"Read D3D11 GPU query");
            const auto now=std::chrono::steady_clock::now();if(now>=deadline)throw std::runtime_error("D3D11 GPU query did not finish");
            hr(device->GetDeviceRemovedReason(),"D3D11 device removed");
            // Older runtimes without fences first allow short real work to
            // finish without entering Windows' potentially coarse Sleep(1).
            // CPU polling remains bounded; no global timer setting changes.
            if(now-began<std::chrono::microseconds(200))YieldProcessor();
            else if(now-began<std::chrono::milliseconds(2))std::this_thread::yield();
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(cancelled)check_stop();
    }
    void finish(){
        if(!fence){context->End(complete.Get());context->Flush();BOOL done=FALSE;wait(complete.Get(),done);return;}
        // Signal follows the queries, copies, or dispatches on the immediate
        // context. An event wakes on actual GPU completion, independent of the
        // Windows timer quantum. The event never supplies elapsed GPU time.
        // https://learn.microsoft.com/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-signal
        const auto value=++fence_value;hr(fenced_context->Signal(fence.Get(),value),"Signal D3D11 GPU fence");context->Flush();
        bool cancelled=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        if(!ResetEvent(fence_event.value))throw std::runtime_error("Reset D3D11 GPU completion event failed");
        hr(fence->SetEventOnCompletion(value,fence_event.value),"Set D3D11 GPU completion event");
        for(;;){cancelled=cancelled||stop.stop_requested();const auto completed=fence->GetCompletedValue();
            if(completed==std::numeric_limits<UINT64>::max()){
                hr(device->GetDeviceRemovedReason(),"D3D11 device removed while waiting for GPU fence");
                throw std::runtime_error("D3D11 GPU fence reported a removed device");}
            if(completed>=value)break;
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("D3D11 GPU fence did not finish within 10 seconds");
            hr(device->GetDeviceRemovedReason(),"D3D11 device removed");
            if(WaitForSingleObject(fence_event.value,10)==WAIT_FAILED)throw std::runtime_error("D3D11 GPU fence event wait failed");}
        // Submitted work cannot be discarded safely. Drain it before honoring
        // cancellation, so readback/upload resources can be released normally.
        if(cancelled)check_stop();
    }
public:
    Compute(const SmoComputeOptions& options,std::stop_token token):stop(token){
        check_stop();ComPtr<IDXGIFactory1> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),"Create DXGI factory");
        ComPtr<IDXGIAdapter1> chosen;DXGI_ADAPTER_DESC1 best{};
        if(!options.software)for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> adapter;const auto result=factory->EnumAdapters1(i,adapter.GetAddressOf());if(result==DXGI_ERROR_NOT_FOUND)break;hr(result,"Enumerate GPU");DXGI_ADAPTER_DESC1 info{};hr(adapter->GetDesc1(&info),"Read GPU identity");
            if(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            const auto name=utf8(info.Description);if(!options.adapter_name.empty()){if(name==options.adapter_name){chosen=adapter;best=info;break;}}
            else if(!chosen||info.DedicatedVideoMemory>best.DedicatedVideoMemory){chosen=adapter;best=info;}}
        if(!options.software&&!chosen)throw std::runtime_error("Requested Direct3D11 GPU is unavailable");
        constexpr D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;D3D_FEATURE_LEVEL obtained{};
        hr(D3D11CreateDevice(chosen.Get(),options.software?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&level,1,D3D11_SDK_VERSION,device.GetAddressOf(),&obtained,context.GetAddressOf()),"Create D3D11 compute device");
        if(options.software){ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;hr(device.As(&dxgi),"Get software GPU");hr(dxgi->GetAdapter(adapter.GetAddressOf()),"Read software GPU");DXGI_ADAPTER_DESC info{};hr(adapter->GetDesc(&info),"Read software identity");description.adapter=utf8(info.Description);}
        else description.adapter=utf8(best.Description);
        description.api="Direct3D 11";description.software=options.software;description.vendor_id=best.VendorId;description.device_id=best.DeviceId;
        LARGE_INTEGER driver{};if(chosen&&SUCCEEDED(chosen->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver)))description.driver=std::to_string(driver.HighPart)+"."+std::to_string(driver.LowPart);
        D3D11_BUFFER_DESC constant_desc{};constant_desc.ByteWidth=64;constant_desc.Usage=D3D11_USAGE_DEFAULT;constant_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&constant_desc,nullptr,constants.GetAddressOf()),"Create compute constants");
        const auto query=[&](D3D11_QUERY kind,ComPtr<ID3D11Query>& destination){D3D11_QUERY_DESC desc{kind,0};return device->CreateQuery(&desc,destination.GetAddressOf());};
        hr(query(D3D11_QUERY_EVENT,complete),"Create GPU completion query");
        description.timestamps=SUCCEEDED(query(D3D11_QUERY_TIMESTAMP,begin))&&SUCCEEDED(query(D3D11_QUERY_TIMESTAMP,end))&&SUCCEEDED(query(D3D11_QUERY_TIMESTAMP_DISJOINT,disjoint));
        ComPtr<ID3D11Device5> fenced_device;
        if(SUCCEEDED(device.As(&fenced_device))&&SUCCEEDED(context.As(&fenced_context))&&
            SUCCEEDED(fenced_device->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(fence.GetAddressOf())))){
            fence_event.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!fence_event.value){fence.Reset();fenced_context.Reset();}
        }else{fence.Reset();fenced_context.Reset();}
        const std::array<std::byte,4> zero{};for(auto& id:dummy)id=buffer(4,zero);
    }
    ~Compute()override{if(context){context->ClearState();context->Flush();}}
    SmoComputeInfo info()const override{return description;}
    SmoBuffer buffer(std::size_t bytes,std::span<const std::byte> initial)override{
        check_stop();if(!bytes||bytes>256ull*1024*1024||initial.size()>bytes)throw std::invalid_argument("GPU buffer exceeds the scene budget");
        const auto padded=(bytes+3)&~std::size_t(3);std::vector<std::byte> contents(padded);std::copy(initial.begin(),initial.end(),contents.begin());
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(padded);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=contents.data();Buffer item;item.bytes=bytes;hr(device->CreateBuffer(&desc,&data,item.object.GetAddressOf()),"Create raw compute buffer");
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=static_cast<UINT>(padded/4);view.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        hr(device->CreateUnorderedAccessView(item.object.Get(),&view,item.view.GetAddressOf()),"Create raw UAV");buffers.push_back(std::move(item));return static_cast<SmoBuffer>(buffers.size());
    }
    SmoKernel kernel(const SmoKernelSource& source)override{
        check_stop();ComPtr<ID3DBlob> code,errors;const void* bytecode=source.dxbc.data();std::size_t byte_count=source.dxbc.size();
        if(source.dxbc.empty()){
            const auto result=D3DCompile(source.hlsl.data(),source.hlsl.size(),source.name.c_str(),nullptr,nullptr,"CSMain","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.GetAddressOf(),errors.GetAddressOf());
            if(FAILED(result)&&errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));hr(result,"Compile sustained scene shader");
            bytecode=code->GetBufferPointer();byte_count=code->GetBufferSize();
        }else validate_dxbc(source.dxbc);
        check_stop();ComPtr<ID3D11ComputeShader> shader;hr(device->CreateComputeShader(bytecode,byte_count,nullptr,shader.GetAddressOf()),"Create sustained scene shader");kernels.push_back(std::move(shader));return static_cast<SmoKernel>(kernels.size());
    }
    void write(SmoBuffer id,std::span<const std::byte> bytes)override{
        check_stop();auto& item=get(id);if(bytes.size()>item.bytes)throw std::invalid_argument("GPU write exceeds buffer");if(bytes.empty())return;
        D3D11_BOX box{0,0,0,static_cast<UINT>(bytes.size()),1,1};context->UpdateSubresource(item.object.Get(),0,&box,bytes.data(),0,0);finish();
    }
    std::vector<std::byte> read(SmoBuffer id)override{
        check_stop();auto& item=get(id);D3D11_BUFFER_DESC desc{};item.object->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Buffer> staging;hr(device->CreateBuffer(&desc,nullptr,staging.GetAddressOf()),"Create scene readback");context->CopyResource(staging.Get(),item.object.Get());finish();
        D3D11_MAPPED_SUBRESOURCE mapping{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapping),"Map scene readback");std::vector<std::byte> result(item.bytes);std::memcpy(result.data(),mapping.pData,item.bytes);context->Unmap(staging.Get(),0);return result;
    }
    double execute(std::span<const SmoDispatch> commands,bool timed)override{
        check_stop();const bool measure=timed&&description.timestamps;
        if(measure){context->Begin(disjoint.Get());context->End(begin.Get());}
        std::array<ID3D11UnorderedAccessView*,8> none{};
        for(const auto& command:commands){if(!command.kernel||command.kernel>kernels.size())throw std::invalid_argument("Invalid compute kernel");
            std::array<ID3D11UnorderedAccessView*,8> views{};for(std::size_t i=0;i<views.size();++i)views[i]=get(command.buffers[i]?command.buffers[i]:dummy[i]).view.Get();
            context->CSSetUnorderedAccessViews(0,8,views.data(),nullptr);context->CSSetShader(kernels[command.kernel-1].Get(),nullptr,0);
            context->UpdateSubresource(constants.Get(),0,nullptr,command.constants.data(),0,0);auto* block=constants.Get();context->CSSetConstantBuffers(0,1,&block);
            context->Dispatch(command.groups[0],command.groups[1],command.groups[2]);context->CSSetUnorderedAccessViews(0,8,none.data(),nullptr);
        }
        if(measure){context->End(end.Get());context->End(disjoint.Get());finish();D3D11_QUERY_DATA_TIMESTAMP_DISJOINT interval{};UINT64 a=0,b=0;wait(disjoint.Get(),interval);wait(begin.Get(),a);wait(end.Get(),b);
            if(!interval.Disjoint&&interval.Frequency&&b>a)return static_cast<double>(b-a)*1000./interval.Frequency;
        }else finish();
        return std::numeric_limits<double>::quiet_NaN();
    }
};
}
std::unique_ptr<SmoCompute> create_smo_d3d11_compute(const SmoComputeOptions& options,std::stop_token stop){return std::make_unique<Compute>(options,stop);}
}
