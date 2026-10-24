#include "presentation/windows/smo_benchmark.hpp"
#include "presentation/windows/rich_text.hpp"
#include "core/smo.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

namespace lgaed::presentation {
namespace {
using Microsoft::WRL::ComPtr;
using Float4 = std::array<float, 4>;
struct Input { Float4 a, b, c, d; };
static_assert(sizeof(Input) == 64 && sizeof(Float4) == 16);
constexpr unsigned iterations = 16;
// Batch enough real dispatches to avoid judging a microsecond-scale workload
// mostly by query/dispatch granularity. Both variants use the same hot inputs,
// repeat count and UAV dependency; reported times cover the entire batch.
constexpr unsigned dispatches_per_sample = 64;
constexpr double tolerance = 2e-4;

void require_hr(HRESULT value, const char* operation) {
    if (FAILED(value)) throw std::runtime_error(std::string(operation) + " (HRESULT " +
        std::to_string(static_cast<unsigned long>(value)) + ")");
}
void cancel(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("Prueba SMO cancelada.");
}

// The two variants use the SAME input, layout, arithmetic precision, compiler
// flags, iteration count and output stores. Only the selected SMO arithmetic
// differs. This manually lowered harness is not a compiler for SMO programs.
constexpr char compute_source[] = R"hlsl(
struct Input { float4 a; float4 b; float4 c; float4 d; };
StructuredBuffer<Input> input_values : register(t0);
RWStructuredBuffer<float4> output_values : register(u0);
cbuffer Parameters : register(b0) { uint count; uint repetitions; uint spare0; uint spare1; };

float multiply_value(float a, float b) {
#if SMO_VARIANT
    if (a == 0 || b == 0) return 0;
    float magnitude = abs(a) * abs(b);
    return (a < 0 || b < 0) ? -magnitude : magnitude;
#else
    return a * b;
#endif
}
float divide_value(float a, float b) {
#if SMO_VARIANT
    if (b == 0) return a;
    if (a == 0) return 0;
    float magnitude = abs(a) / abs(b);
    return (a < 0 || b < 0) ? -magnitude : magnitude;
#else
    return a / b;
#endif
}
float dot_value(float3 a, float3 b) {
    precise float x = multiply_value(a.x,b.x);
    precise float y = multiply_value(a.y,b.y);
    precise float z = multiply_value(a.z,b.z);
    precise float sum = (x+y)+z;
    return sum;
}
[numthreads(64,1,1)]
void CSMain(uint3 dispatch : SV_DispatchThreadID) {
    uint at = dispatch.x;
    if (at >= count) return;
    Input item = input_values[at];
#if SCENARIO == 0
    precise float u = abs(item.a.x)+0.25;
    precise float v = abs(item.a.y)+0.25;
    precise float sum = 0;
    for (uint k=0;k<repetitions;++k) {
        u = multiply_value(u,0.5)+multiply_value(v,0.125);
        v = multiply_value(v,0.5)+0.0625;
        sum += u;
    }
    output_values[at] = float4(u,v,sum,multiply_value(u,v));
#elif SCENARIO == 1
    precise float3 q = abs(item.a.xyz);
    precise float sum = 0;
    precise float squared = 0;
    for (uint k=0;k<repetitions;++k) {
        squared = dot_value(q,q);
        sum += squared;
        q += float3(0.03125,0.015625,0.0625);
    }
    // sqrt is a shared conventional graphics support operation, not a new
    // definition of an SMO square root.
    output_values[at] = float4(sum,squared,sqrt(squared),1);
#elif SCENARIO == 2
    precise float3 first = item.a.xyz;
    precise float3 second = item.b.xyz;
    precise float radius = (abs(item.a.w)*0.125+0.25)+item.b.w;
    precise float radius_squared = multiply_value(radius,radius);
    precise float nearest = 1e10;
    precise float squared = 0;
    uint collisions = 0;
    for (uint k=0;k<repetitions;++k) {
        precise float3 delta = abs(first-second);
        squared = dot_value(delta,delta);
        collisions += squared <= radius_squared ? 1 : 0;
        nearest = min(nearest,squared);
        second += float3(0.03125,-0.015625,0.0625);
    }
    output_values[at] = float4((float)collisions,squared,radius_squared,nearest);
#elif SCENARIO == 3
    precise float3 origin = item.a.xyz;
    precise float3 direction = item.d.xyz;
    precise float3 center = item.b.xyz;
    precise float a = dot_value(direction,direction);
    precise float discriminant = 0;
    precise float nearest = 1e10;
    uint hits = 0;
    for (uint k=0;k<repetitions;++k) {
        precise float3 relative = origin-center;
        precise float b = multiply_value(2,dot_value(relative,direction));
        precise float c = dot_value(relative,relative)-multiply_value(item.b.w,item.b.w);
        discriminant = multiply_value(b,b)-multiply_value(multiply_value(4,a),c);
        if (discriminant >= 0 && abs(a) > 0.000001) {
            precise float distance = divide_value(-b-sqrt(discriminant),multiply_value(2,a));
            if (distance >= 0) { ++hits; nearest = min(nearest,distance); }
        }
        origin += float3(0.03125,-0.015625,0.0625);
    }
    output_values[at] = float4((float)hits,nearest==1e10 ? -1 : nearest,discriminant,a);
#elif SCENARIO == 4
    output_values[at] = float4(multiply_value(item.a.x,item.a.y),
        multiply_value(item.a.y,item.a.x),multiply_value(item.a.x,0),multiply_value(0,item.a.y));
#endif
}
)hlsl";

// This controlled geometry case has independent local positions and one common
// procedural displacement per emitter. There are deliberately no individual
// forces/collisions: grouping would be invalid if those changed the trajectory.
constexpr char particle_source[] = R"hlsl(
struct Input { float4 a; float4 b; float4 c; float4 d; };
StructuredBuffer<Input> input_values : register(t0);
StructuredBuffer<float4> emitter_values : register(t1);
RWStructuredBuffer<float4> output_values : register(u0);
cbuffer Parameters : register(b0) {
    uint count; uint groups; uint group_size; uint terms;
    float frame_time; uint spare0; uint spare1; uint spare2;
};
float multiply_value(float a, float b) {
#if SMO_VARIANT
    if (a == 0 || b == 0) return 0;
    float magnitude = abs(a) * abs(b);
    return (a < 0 || b < 0) ? -magnitude : magnitude;
#else
    return a*b;
#endif
}
float3 movement(uint emitter) {
    precise float frequency = 0.5 + (emitter % 17)*0.03125;
    precise float phase_base = (emitter % 65521)*0.000244140625;
    precise float3 sum = float3(0,0,0);
    for (uint k=0;k<terms;++k) {
        precise float phase = (frame_time*frequency+phase_base)+k*0.125;
        precise float amplitude = 0.25+((emitter+k)%13)*0.0625;
        // Trigonometry/clamping is common conventional graphics support, not
        // an SMO trig definition. Exact zeros remain zero under core's current
        // default product; the negative-absorbing branch is not rewritten.
        precise float x = max(0,0.5+0.5*sin(phase));
        precise float y = max(0,0.5+0.5*cos(phase));
        precise float z = max(0,0.5+0.5*sin(phase*0.5+1));
        sum.x += multiply_value(amplitude,x);
        sum.y += multiply_value(amplitude,y);
        sum.z += multiply_value(amplitude,z);
    }
    return sum;
}
[numthreads(64,1,1)]
void Naive(uint3 dispatch : SV_DispatchThreadID) {
    uint at=dispatch.x;
    if (at>=count) return;
    uint emitter=at/group_size;
    output_values[at]=float4(input_values[at].a.xyz+movement(emitter),1);
}
[numthreads(64,1,1)]
void Emitters(uint3 dispatch : SV_DispatchThreadID) {
    uint at=dispatch.x;
    if (at>=groups) return;
    output_values[at]=float4(movement(at),1);
}
[numthreads(64,1,1)]
void Expand(uint3 dispatch : SV_DispatchThreadID) {
    uint at=dispatch.x;
    if (at>=count) return;
    output_values[at]=float4(input_values[at].a.xyz+emitter_values[at/group_size].xyz,1);
}
[numthreads(64,1,1)]
void Rules(uint3 dispatch : SV_DispatchThreadID) {
    uint at=dispatch.x;
    if (at>=count) return;
    float a=input_values[at].a.x, b=input_values[at].a.y;
    output_values[at]=float4(multiply_value(a,b),multiply_value(b,a),multiply_value(a,0),multiply_value(0,b));
}
)hlsl";

struct alignas(16) ParticleArguments {
    std::uint32_t count, groups, group_size, terms;
    float frame_time;
    std::uint32_t spare[3]{};
};
static_assert(sizeof(ParticleArguments)==32);

struct Device {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Buffer> input, output[3], staging, parameters, emitter_output;
    ComPtr<ID3D11ShaderResourceView> input_view;
    ComPtr<ID3D11ShaderResourceView> emitter_view;
    ComPtr<ID3D11UnorderedAccessView> output_view[3], emitter_write_view;
    ComPtr<ID3D11Query> started, ended, disjoint, complete;
    std::uint32_t count;
    bool software = false, timestamps = false;
    std::string description;
    std::stop_token stop;

    Device(GraphicsBackend requested, const std::vector<Input>& values, std::stop_token token)
        : count(static_cast<std::uint32_t>(values.size())), stop(token) {
        cancel(stop);
        constexpr D3D_FEATURE_LEVEL supported[]{D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL selected{};
        const auto create = [&](D3D_DRIVER_TYPE type) {
            return D3D11CreateDevice(nullptr,type,nullptr,0,supported,1,D3D11_SDK_VERSION,
                device.ReleaseAndGetAddressOf(),&selected,context.ReleaseAndGetAddressOf());
        };
        software = requested == GraphicsBackend::warp;
        auto result = create(software ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE);
        if (FAILED(result) && requested == GraphicsBackend::automatic) { software = true; result = create(D3D_DRIVER_TYPE_WARP); }
        require_hr(result,"Crear dispositivo de cómputo Direct3D 11");
        ComPtr<IDXGIDevice> dxgi; require_hr(device.As(&dxgi),"Consultar dispositivo DXGI");
        ComPtr<IDXGIAdapter> adapter; require_hr(dxgi->GetAdapter(adapter.GetAddressOf()),"Consultar adaptador de cómputo");
        DXGI_ADAPTER_DESC adapter_info{}; require_hr(adapter->GetDesc(&adapter_info),"Consultar nombre del adaptador");
        ComPtr<IDXGIAdapter1> adapter1;
        if (SUCCEEDED(adapter.As(&adapter1))) {
            DXGI_ADAPTER_DESC1 details{};
            if (SUCCEEDED(adapter1->GetDesc1(&details)))
                software = software || (details.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        }
        description = "Direct3D 11 · " + utf8(adapter_info.Description) + (software ? " · WARP/software" : " · hardware");

        D3D11_BUFFER_DESC source{}; source.ByteWidth=count*sizeof(Input); source.Usage=D3D11_USAGE_IMMUTABLE;
        source.BindFlags=D3D11_BIND_SHADER_RESOURCE; source.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        source.StructureByteStride=sizeof(Input); D3D11_SUBRESOURCE_DATA initial{values.data(),0,0};
        require_hr(device->CreateBuffer(&source,&initial,input.GetAddressOf()),"Crear datos idénticos de entrada");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=DXGI_FORMAT_UNKNOWN; srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements=count; require_hr(device->CreateShaderResourceView(input.Get(),&srv,input_view.GetAddressOf()),"Crear vista de entrada");
        D3D11_BUFFER_DESC destination{}; destination.ByteWidth=count*sizeof(Float4); destination.Usage=D3D11_USAGE_DEFAULT;
        destination.BindFlags=D3D11_BIND_UNORDERED_ACCESS; destination.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        destination.StructureByteStride=sizeof(Float4);
        for (unsigned i=0;i<3;++i) {
            require_hr(device->CreateBuffer(&destination,nullptr,output[i].GetAddressOf()),"Crear salida de cómputo");
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format=DXGI_FORMAT_UNKNOWN; uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements=count; require_hr(device->CreateUnorderedAccessView(output[i].Get(),&uav,output_view[i].GetAddressOf()),"Crear vista de salida");
        }
        auto readable=destination; readable.Usage=D3D11_USAGE_STAGING; readable.BindFlags=0;
        readable.MiscFlags=0; readable.StructureByteStride=0; readable.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        require_hr(device->CreateBuffer(&readable,nullptr,staging.GetAddressOf()),"Crear lectura real de resultados GPU");
        const std::array<std::uint32_t,4> arguments{count,iterations,0,0};
        D3D11_BUFFER_DESC constants{}; constants.ByteWidth=sizeof(arguments); constants.Usage=D3D11_USAGE_IMMUTABLE;
        constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER; D3D11_SUBRESOURCE_DATA argument_data{arguments.data(),0,0};
        require_hr(device->CreateBuffer(&constants,&argument_data,parameters.GetAddressOf()),"Crear parámetros comunes");
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        require_hr(device->CreateQuery(&query,complete.GetAddressOf()),"Crear consulta de finalización");
        query.Query=D3D11_QUERY_TIMESTAMP;
        const auto first=device->CreateQuery(&query,started.GetAddressOf());
        const auto last=device->CreateQuery(&query,ended.GetAddressOf());
        query.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
        const auto reliable=device->CreateQuery(&query,disjoint.GetAddressOf());
        timestamps=SUCCEEDED(first)&&SUCCEEDED(last)&&SUCCEEDED(reliable);
    }
    ~Device() { if(context) { context->ClearState(); context->Flush(); } }

    template<class T> void wait(ID3D11Query* query, T& result) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        for (;;) {
            cancel(stop);
            const auto status=context->GetData(query,&result,sizeof(result),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(status==S_OK)return;
            require_hr(status,"Leer consulta GPU");
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("La consulta de cómputo GPU no respondió dentro de 10 segundos.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ComPtr<ID3D11ComputeShader> compile_source(const char* source,std::size_t length,const char* entry,
        const D3D_SHADER_MACRO* macros,ComPtr<ID3DBlob>& bytecode) {
        cancel(stop);
        ComPtr<ID3DBlob> errors;
        // IEEE strictness prevents reassociation that would change the authored
        // operation order; both variants use precisely the same compiler flags.
        // https://learn.microsoft.com/windows/win32/direct3dhlsl/d3dcompile-constants
        const auto result=D3DCompile(source,length,"SMO-arithmetic-paired-kernels",macros,nullptr,
            entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,bytecode.ReleaseAndGetAddressOf(),errors.GetAddressOf());
        if(FAILED(result)&&errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
        require_hr(result,"Compilar variante de cómputo GPU");
        ComPtr<ID3D11ComputeShader> shader;
        require_hr(device->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,shader.GetAddressOf()),"Crear variante de cómputo GPU");
        return shader;
    }
    ComPtr<ID3D11ComputeShader> compile(unsigned scenario,bool smo,ComPtr<ID3DBlob>& bytecode) {
        const auto chosen=std::to_string(scenario);
        const D3D_SHADER_MACRO macros[]{{"SCENARIO",chosen.c_str()},{"SMO_VARIANT",smo?"1":"0"},{nullptr,nullptr}};
        return compile_source(compute_source,sizeof(compute_source)-1,"CSMain",macros,bytecode);
    }
    ComPtr<ID3D11ComputeShader> compile_particles(const char* entry,bool smo) {
        const D3D_SHADER_MACRO macros[]{{"SMO_VARIANT",smo?"1":"0"},{nullptr,nullptr}};
        ComPtr<ID3DBlob> bytecode;
        return compile_source(particle_source,sizeof(particle_source)-1,entry,macros,bytecode);
    }
    void start_timing() {
        if(timestamps) { context->Begin(disjoint.Get()); context->End(started.Get()); }
    }
    double finish_timing() {
        if(timestamps) { context->End(ended.Get()); context->End(disjoint.Get()); }
        else context->End(complete.Get());
        context->Flush();
        if(!timestamps) { BOOL done=FALSE; wait(complete.Get(),done); return std::numeric_limits<double>::quiet_NaN(); }
        // A GPU timestamp is usable only inside a non-disjoint interval with
        // nonzero Frequency. No CPU wall clock substitutes for invalid data.
        // https://learn.microsoft.com/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT valid{}; UINT64 begin=0,end=0;
        wait(disjoint.Get(),valid); wait(started.Get(),begin); wait(ended.Get(),end);
        if(valid.Disjoint||!valid.Frequency||end<=begin)return std::numeric_limits<double>::quiet_NaN();
        return static_cast<double>(end-begin)*1000./static_cast<double>(valid.Frequency);
    }
    double dispatch(ID3D11ComputeShader* shader, unsigned variant) {
        cancel(stop); auto* source=input_view.Get(); auto* destination=output_view[variant].Get(); auto* constants=parameters.Get();
        context->CSSetShader(shader,nullptr,0); context->CSSetShaderResources(0,1,&source);
        context->CSSetUnorderedAccessViews(0,1,&destination,nullptr); context->CSSetConstantBuffers(0,1,&constants);
        start_timing();
        for(unsigned repeat=0;repeat<dispatches_per_sample;++repeat){cancel(stop);context->Dispatch((count+63)/64,1,1);}
        return finish_timing();
    }
    void prepare_particles() {
        D3D11_BUFFER_DESC group{}; group.ByteWidth=count*sizeof(Float4); group.Usage=D3D11_USAGE_DEFAULT;
        group.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
        group.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; group.StructureByteStride=sizeof(Float4);
        require_hr(device->CreateBuffer(&group,nullptr,emitter_output.GetAddressOf()),"Crear movimientos por emisor");
        D3D11_UNORDERED_ACCESS_VIEW_DESC write{}; write.Format=DXGI_FORMAT_UNKNOWN;
        write.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; write.Buffer.NumElements=count;
        require_hr(device->CreateUnorderedAccessView(emitter_output.Get(),&write,emitter_write_view.GetAddressOf()),"Crear escritura de emisores");
        D3D11_SHADER_RESOURCE_VIEW_DESC read{}; read.Format=DXGI_FORMAT_UNKNOWN;
        read.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; read.Buffer.NumElements=count;
        require_hr(device->CreateShaderResourceView(emitter_output.Get(),&read,emitter_view.GetAddressOf()),"Crear lectura de emisores");
        D3D11_BUFFER_DESC constants{}; constants.ByteWidth=sizeof(ParticleArguments); constants.Usage=D3D11_USAGE_DYNAMIC;
        constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER; constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        require_hr(device->CreateBuffer(&constants,nullptr,parameters.ReleaseAndGetAddressOf()),"Crear parámetros de un fotograma de partículas");
    }
    void clear_particle_bindings() {
        ID3D11UnorderedAccessView* no_write=nullptr;
        ID3D11ShaderResourceView* no_reads[2]{};
        context->CSSetUnorderedAccessViews(0,1,&no_write,nullptr);
        context->CSSetShaderResources(0,2,no_reads);
    }
    double particle_dispatch(unsigned variant,ID3D11ComputeShader* naive,ID3D11ComputeShader* emitters,
        ID3D11ComputeShader* expand,const ParticleArguments& arguments) {
        cancel(stop); clear_particle_bindings();
        D3D11_MAPPED_SUBRESOURCE mapping{};
        require_hr(context->Map(parameters.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapping),"Actualizar tiempo real del fotograma");
        std::memcpy(mapping.pData,&arguments,sizeof(arguments));context->Unmap(parameters.Get(),0);
        auto* constants=parameters.Get();context->CSSetConstantBuffers(0,1,&constants);
        // ONE interval covers one frame, including all emitter work and the
        // subsequent expansion. No repeated frame batch can inflate its cost.
        start_timing();
        if(variant==0) {
            auto* source=input_view.Get();auto* target=output_view[variant].Get();
            context->CSSetShader(naive,nullptr,0);context->CSSetShaderResources(0,1,&source);
            context->CSSetUnorderedAccessViews(0,1,&target,nullptr);context->Dispatch((count+63)/64,1,1);
        } else {
            auto* temporary=emitter_write_view.Get();context->CSSetShader(emitters,nullptr,0);
            context->CSSetUnorderedAccessViews(0,1,&temporary,nullptr);context->Dispatch((arguments.groups+63)/64,1,1);
            // Unbind the emitter UAV before consuming its SRV. D3D's resource
            // dependency belongs to this same measured single-frame interval.
            clear_particle_bindings();
            ID3D11ShaderResourceView* reads[]{input_view.Get(),emitter_view.Get()};
            auto* target=output_view[variant].Get();context->CSSetShader(expand,nullptr,0);
            context->CSSetShaderResources(0,2,reads);context->CSSetUnorderedAccessViews(0,1,&target,nullptr);
            context->Dispatch((count+63)/64,1,1);
        }
        const auto elapsed=finish_timing();clear_particle_bindings();return elapsed;
    }
    std::vector<Float4> read(unsigned variant) {
        cancel(stop); ID3D11UnorderedAccessView* none=nullptr;
        context->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        context->CopyResource(staging.Get(),output[variant].Get()); context->End(complete.Get()); context->Flush();
        BOOL done=FALSE; wait(complete.Get(),done);
        std::vector<Float4> result(count);
        D3D11_MAPPED_SUBRESOURCE mapping{}; require_hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapping),"Leer todos los valores de cómputo GPU");
        std::memcpy(result.data(),mapping.pData,result.size()*sizeof(Float4));
        context->Unmap(staging.Get(),0); return result;
    }
};

std::vector<Input> inputs(std::uint32_t count,std::stop_token stop={}) {
    std::uint32_t seed=0x534d4f31u;
    const auto number=[&]() {
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        return static_cast<float>(static_cast<int>(seed%129)-64)/16.f;
    };
    std::vector<Input> result(count);
    std::uint32_t generated=0;
    for(auto& item:result) {
        if((generated++&255)==0)cancel(stop);
        for(auto* vector:{&item.a,&item.b,&item.c,&item.d})for(auto& value:*vector)value=number();
        item.b[3]=.5f+std::abs(item.b[3])*.25f;
        if(item.d[0]==0&&item.d[1]==0&&item.d[2]==0)item.d[2]=1;
    }
    constexpr std::array<std::array<float,2>,12> boundary{{{0,0},{0,2},{2,0},{0,-2},{-2,0},{2,3},{-2,3},{2,-3},{-2,-3},{-1,-1},{.5f,-2},{-.5f,-2}}};
    for(std::size_t i=0;i<boundary.size()&&i<result.size();++i) {result[i].a[0]=boundary[i][0];result[i].a[1]=boundary[i][1];}
    return result;
}
float multiply(float a,float b,bool smo) {
    return smo?static_cast<float>(lgaed::smo::multiply(a,b).value):a*b;
}
float divide(float a,float b,bool smo) {
    return smo?static_cast<float>(lgaed::smo::divide(a,b).value):a/b;
}
using Float3=std::array<float,3>;
float dot(const Float3& a,const Float3& b,bool smo) {
    const auto x=multiply(a[0],b[0],smo),y=multiply(a[1],b[1],smo),z=multiply(a[2],b[2],smo);
    return (x+y)+z;
}
Float4 oracle(const Input& item,unsigned scenario,bool smo) {
    if(scenario==0) {
        float u=std::abs(item.a[0])+.25f,v=std::abs(item.a[1])+.25f,sum=0;
        for(unsigned k=0;k<iterations;++k) {u=multiply(u,.5f,smo)+multiply(v,.125f,smo);v=multiply(v,.5f,smo)+.0625f;sum+=u;}
        return {u,v,sum,multiply(u,v,smo)};
    }
    if(scenario==1) {
        Float3 q{std::abs(item.a[0]),std::abs(item.a[1]),std::abs(item.a[2])};float sum=0,squared=0;
        for(unsigned k=0;k<iterations;++k) {squared=dot(q,q,smo);sum+=squared;q[0]+=.03125f;q[1]+=.015625f;q[2]+=.0625f;}
        return {sum,squared,std::sqrt(squared),1};
    }
    if(scenario==2) {
        const Float3 first{item.a[0],item.a[1],item.a[2]};Float3 second{item.b[0],item.b[1],item.b[2]};
        const float radius=(std::abs(item.a[3])*.125f+.25f)+item.b[3],radius_squared=multiply(radius,radius,smo);
        float nearest=1e10f,squared=0;unsigned collisions=0;
        for(unsigned k=0;k<iterations;++k) {
            const Float3 delta{std::abs(first[0]-second[0]),std::abs(first[1]-second[1]),std::abs(first[2]-second[2])};
            squared=dot(delta,delta,smo);collisions+=squared<=radius_squared?1u:0u;nearest=std::min(nearest,squared);
            second[0]+=.03125f;second[1]-=.015625f;second[2]+=.0625f;
        }
        return {static_cast<float>(collisions),squared,radius_squared,nearest};
    }
    if(scenario==3) {
        Float3 origin{item.a[0],item.a[1],item.a[2]};const Float3 direction{item.d[0],item.d[1],item.d[2]},center{item.b[0],item.b[1],item.b[2]};
        const float a=dot(direction,direction,smo);float discriminant=0,nearest=1e10f;unsigned hits=0;
        for(unsigned k=0;k<iterations;++k) {
            const Float3 relative{origin[0]-center[0],origin[1]-center[1],origin[2]-center[2]};
            const float b=multiply(2,dot(relative,direction,smo),smo),c=dot(relative,relative,smo)-multiply(item.b[3],item.b[3],smo);
            discriminant=multiply(b,b,smo)-multiply(multiply(4,a,smo),c,smo);
            if(discriminant>=0&&std::abs(a)>.000001f) {
                const auto distance=divide(-b-std::sqrt(discriminant),multiply(2,a,smo),smo);
                if(distance>=0) {++hits;nearest=std::min(nearest,distance);}
            }
            origin[0]+=.03125f;origin[1]-=.015625f;origin[2]+=.0625f;
        }
        return {static_cast<float>(hits),nearest==1e10f?-1.f:nearest,discriminant,a};
    }
    return {multiply(item.a[0],item.a[1],smo),multiply(item.a[1],item.a[0],smo),multiply(item.a[0],0,smo),multiply(0,item.a[1],smo)};
}
bool close(float actual,float expected) {
    return std::isfinite(actual)&&std::isfinite(expected)&&
        std::abs(static_cast<double>(actual)-expected)<=tolerance*std::max(1.,std::abs(static_cast<double>(expected)));
}
void verify(const std::vector<Input>& values,const std::vector<Float4>& observed,unsigned scenario,bool smo,std::stop_token stop) {
    if(observed.size()!=values.size())throw std::runtime_error("La lectura GPU omitió resultados de la carga.");
    for(std::size_t i=0;i<values.size();++i) {
        if((i&255)==0)cancel(stop);
        const auto expected=oracle(values[i],scenario,smo);
        for(std::size_t component=0;component<4;++component)if(!close(observed[i][component],expected[component]))
            throw std::runtime_error("El kernel GPU "+std::string(smo?"SMO":"ordinario")+" no coincide con su oráculo CPU en escenario "+
                std::to_string(scenario+1)+", dato "+std::to_string(i)+", componente "+std::to_string(component)+
                ": observado="+std::to_string(observed[i][component])+", esperado="+std::to_string(expected[component])+".");
    }
}
double median(const std::vector<double>& source) {
    if(source.empty()||std::ranges::any_of(source,[](double value){return !std::isfinite(value)||value<=0;}))return std::numeric_limits<double>::quiet_NaN();
    auto values=source;std::ranges::sort(values);const auto middle=values.size()/2;
    return values.size()%2?values[middle]:(values[middle-1]+values[middle])*.5;
}

struct ParticleShaders {
    ComPtr<ID3D11ComputeShader> naive, emitters[2], expand[2];
    explicit ParticleShaders(Device& gpu) {
        naive=gpu.compile_particles("Naive",false);
        for(unsigned variant=0;variant<2;++variant) {
            emitters[variant]=gpu.compile_particles("Emitters",variant==1);
            expand[variant]=gpu.compile_particles("Expand",variant==1);
        }
    }
    double frame(Device& gpu,unsigned variant,const ParticleArguments& args) const {
        return gpu.particle_dispatch(variant,naive.Get(),variant?emitters[variant-1].Get():nullptr,
            variant?expand[variant-1].Get():nullptr,args);
    }
};

// Independent untimed CPU oracle. sin(a+b) separates the exact mathematical
// sum into weighted coefficients. The actual GPU kernels still evaluate ALL
// authored terms per particle/emitter; this oracle never supplies their data.
// Comparing FP32 against this double sum uses the same explicit 2e-4 relative
// tolerance as the arithmetic harness. A direct FP32/core probe checks each
// coefficient family at every tested frame before validating every output.
struct ParticleOracle {
    struct Coefficients {double sum=0,cosine=0,sine=0,half_cosine=0,half_sine=0;};
    std::array<Coefficients,13> coefficients;
    unsigned terms;
    explicit ParticleOracle(unsigned term_count) : terms(term_count) {
        for(unsigned family=0;family<coefficients.size();++family) {
            auto& c=coefficients[family];
            for(unsigned k=0;k<terms;++k) {
                const double amplitude=.25+((family+k)%13)*.0625;
                c.sum+=amplitude;
                c.cosine+=amplitude*std::cos(k*.125);c.sine+=amplitude*std::sin(k*.125);
                c.half_cosine+=amplitude*std::cos(k*.0625);c.half_sine+=amplitude*std::sin(k*.0625);
            }
        }
    }
    static float base(std::uint32_t group,float time) {
        const float frequency=.5f+static_cast<float>(group%17)*.03125f;
        const float phase_base=static_cast<float>(group%65521)*.000244140625f;
        return time*frequency+phase_base;
    }
    Float3 movement(std::uint32_t group,float time) const {
        const double phase=base(group,time),half=phase*.5+1;
        const auto& c=coefficients[group%coefficients.size()];
        return {static_cast<float>(.5*(c.sum+std::sin(phase)*c.cosine+std::cos(phase)*c.sine)),
            static_cast<float>(.5*(c.sum+std::cos(phase)*c.cosine-std::sin(phase)*c.sine)),
            static_cast<float>(.5*(c.sum+std::sin(half)*c.half_cosine+std::cos(half)*c.half_sine))};
    }
    Float3 direct(std::uint32_t group,float time) const {
        Float3 sum{};const auto phase_base=base(group,time);
        for(unsigned k=0;k<terms;++k) {
            const float phase=phase_base+static_cast<float>(k)*.125f;
            const float amplitude=.25f+static_cast<float>((group+k)%13)*.0625f;
            const Float3 value{std::max(0.f,.5f+.5f*std::sin(phase)),std::max(0.f,.5f+.5f*std::cos(phase)),
                std::max(0.f,.5f+.5f*std::sin(phase*.5f+1))};
            for(unsigned axis=0;axis<3;++axis) {
                const auto ordinary=amplitude*value[axis];
                const auto defined=static_cast<float>(lgaed::smo::multiply(amplitude,value[axis]).value);
                if(ordinary!=defined)throw std::runtime_error("El dominio no negativo de partículas dejó de coincidir con el núcleo SMO.");
                sum[axis]+=ordinary;
            }
        }
        return sum;
    }
    void verify_probe(std::uint32_t group,float time) const {
        const auto expected=movement(group,time),observed=direct(group,time);
        for(unsigned axis=0;axis<3;++axis)if(!close(observed[axis],expected[axis]))
            throw std::runtime_error("El oráculo trigonométrico independiente no coincide con la fórmula directa FP32/core SMO.");
    }
};

void verify_particles(const std::vector<Input>& values,const std::array<std::vector<Float4>,3>& actual,
    const ParticleArguments& arguments,const ParticleOracle& oracle,SmoParticleRow& row,std::stop_token stop) {
    for(const auto& variant:actual)if(variant.size()!=values.size())
        throw std::runtime_error("La lectura de partículas no incluyó todas las salidas de A/B/C.");
    for(std::uint32_t group=0;group<std::min(arguments.groups,17u);++group)oracle.verify_probe(group,arguments.frame_time);
    oracle.verify_probe(arguments.groups-1,arguments.frame_time);
    std::vector<Float3> movements(arguments.groups);
    for(std::uint32_t group=0;group<arguments.groups;++group) {
        if((group&255)==0)cancel(stop);
        movements[group]=oracle.movement(group,arguments.frame_time);
    }
    for(std::size_t i=0;i<values.size();++i) {
        if((i&255)==0)cancel(stop);
        const auto& motion=movements[i/arguments.group_size];
        const Float4 expected{values[i].a[0]+motion[0],values[i].a[1]+motion[1],values[i].a[2]+motion[2],1};
        for(unsigned component=0;component<4;++component)for(unsigned variant=0;variant<3;++variant) {
            const auto value=actual[variant][i][component],reference=actual[0][i][component];
            ++row.values;
            if(!std::isfinite(value)||!std::isfinite(reference))row.max_error=std::numeric_limits<double>::infinity();
            else row.max_error=std::max({row.max_error,std::abs(static_cast<double>(value)-expected[component]),
                std::abs(static_cast<double>(value)-reference)});
            if(!close(value,expected[component])||!close(value,reference))++row.mismatches;
        }
    }
}

std::array<std::vector<Float4>,3> read_particles(Device& gpu) {
    return {gpu.read(0),gpu.read(1),gpu.read(2)};
}
struct ParticleReadback {
    ParticleArguments arguments;
    std::array<std::vector<Float4>,3> values;
};
} // namespace

SmoBenchmarkReport run_smo_benchmark(GraphicsBackend requested,std::uint32_t work_items,unsigned rounds,std::stop_token stop) {
    cancel(stop);
    if(work_items<256||work_items>262144||rounds<3||rounds>15)
        throw std::invalid_argument("La prueba admite entre 256 y 262144 datos y entre 3 y 15 rondas emparejadas.");
    const auto values=inputs(work_items,stop);Device gpu(requested,values,stop);
    SmoBenchmarkReport report;report.backend=gpu.description;report.software=gpu.software;report.gpu_timestamps=true;report.dispatches_per_sample=dispatches_per_sample;
    constexpr std::array<const char*,5> names{
        "Productos positivos", "Longitud al cuadrado (magnitudes)", "Colisiones esfera/esfera (distancia)",
        "Mini rayos/esfera (componentes con signo)", "Cero y signos: conformidad semántica"};
    for(unsigned scenario=0;scenario<names.size();++scenario) {
        cancel(stop);SmoBenchmarkRow row;row.name=names[scenario];row.values=static_cast<std::size_t>(work_items)*4;
        ComPtr<ID3DBlob> ordinary_code,smo_code;
        auto ordinary=gpu.compile(scenario,false,ordinary_code),smo=gpu.compile(scenario,true,smo_code);
        // Two genuinely executed warm-up dispatches per variant; their clocks
        // never enter the measured samples. Readback/oracles are untimed.
        for(unsigned warm=0;warm<2;++warm) {(void)gpu.dispatch(ordinary.Get(),0);(void)gpu.dispatch(smo.Get(),1);}
        const auto initial_ordinary=gpu.read(0),initial_smo=gpu.read(1);
        verify(values,initial_ordinary,scenario,false,stop);verify(values,initial_smo,scenario,true,stop);
        row.ordinary_ms.reserve(rounds);row.smo_ms.reserve(rounds);
        for(unsigned round=0;round<rounds;++round) {
            if(round%2==0) {row.ordinary_ms.push_back(gpu.dispatch(ordinary.Get(),0));row.smo_ms.push_back(gpu.dispatch(smo.Get(),1));}
            else {const auto changed=gpu.dispatch(smo.Get(),1);row.ordinary_ms.push_back(gpu.dispatch(ordinary.Get(),0));row.smo_ms.push_back(changed);}
        }
        const auto actual_ordinary=gpu.read(0),actual_smo=gpu.read(1);
        verify(values,actual_ordinary,scenario,false,stop);verify(values,actual_smo,scenario,true,stop);
        for(std::size_t i=0;i<values.size();++i)for(std::size_t component=0;component<4;++component) {
            row.max_error=std::max(row.max_error,std::abs(static_cast<double>(actual_ordinary[i][component])-actual_smo[i][component]));
            if(!close(actual_ordinary[i][component],actual_smo[i][component]))++row.mismatches;
        }
        row.equivalent=row.mismatches==0;
        row.ordinary_median_ms=median(row.ordinary_ms);row.smo_median_ms=median(row.smo_ms);
        row.timing_valid=std::isfinite(row.ordinary_median_ms)&&std::isfinite(row.smo_median_ms);
        report.gpu_timestamps=report.gpu_timestamps&&row.timing_valid;
        row.note=row.equivalent?"Resultados equivalentes en todos los valores comprobados (tolerancia absoluta/relativa 2e-4); comparación acotada a esta carga.":
            "Resultados diferentes: estos tiempos NO permiten afirmar que SMO resuelva más rápido el mismo problema.";
        if(!row.timing_valid)row.note+=" Consulta GPU disjunta, no disponible o inválida: no hay tiempos utilizables; no se sustituyen por tiempo CPU.";
        if(ordinary_code->GetBufferSize()==smo_code->GetBufferSize()&&std::memcmp(ordinary_code->GetBufferPointer(),smo_code->GetBufferPointer(),ordinary_code->GetBufferSize())==0)
            row.note+=" El compilador generó el mismo bytecode para ambas variantes.";
        if(scenario==3)row.note+=" Son intersecciones analíticas rayo/esfera, no un ray tracer completo.";
        if(scenario==4)row.note+=" Es una prueba de reglas de cero/signos, no una carga de renderizado.";
        report.rows.push_back(std::move(row));
    }
    report.conclusion="Arnés experimental de operaciones SMO reducidas manualmente a HLSL; no es un compilador GPU del lenguaje ni una comparación entre motores completos. ";
    if(report.software)report.conclusion+="WARP ejecutó la carga por software: no acredita el rendimiento de una GPU física. ";
    if(!report.gpu_timestamps)report.conclusion+="No todas las consultas de tiempo GPU fueron válidas. ";
    report.conclusion+="Cada muestra mide un lote de 64 Dispatch reales sobre entradas calientes idénticas, no un frame ni una sola operación. Las mediciones excluyen compilación, lectura y oráculos CPU. Sólo las filas equivalentes con tiempos válidos admiten comparar sus medianas; base doce no demuestra por sí misma una ventaja de velocidad.";
    return report;
}

SmoParticleReport run_smo_particle_benchmark(GraphicsBackend requested,bool stress,std::stop_token stop) {
    cancel(stop);
    constexpr std::uint32_t maximum_particles=2097152;
    constexpr unsigned maximum_terms=8192;
    SmoParticleReport report;
    std::uint32_t count=stress?65536u:1024u;
    unsigned terms=stress?32u:8u;
    auto values=inputs(count,stop);
    auto gpu=std::make_unique<Device>(requested,values,stop);
    const bool full_stress=stress&&!gpu->software;
    report.stress=full_stress;
    // A software fallback is never silently subjected to the physical-GPU
    // stress sweep or presented as hardware performance evidence.
    if(!full_stress&&stress) {
        count=1024;terms=8;gpu.reset();values=inputs(count,stop);
        gpu=std::make_unique<Device>(GraphicsBackend::warp,values,stop);
    }
    const unsigned rounds=full_stress?9u:3u;
    gpu->prepare_particles();
    auto shaders=std::make_unique<ParticleShaders>(*gpu);
    {
        // This very shader source must agree with the current core on exact
        // zero and negative boundary inputs before timing the restricted
        // nonnegative product domain of the particle movement.
        auto rules=gpu->compile_particles("Rules",true);
        const ParticleArguments arguments{count,count,1,terms,.125f,{}};
        (void)gpu->particle_dispatch(0,rules.Get(),nullptr,nullptr,arguments);
        verify(values,gpu->read(0),4,true,stop);
    }
    if(full_stress)for(;;) {
        cancel(stop);
        ParticleArguments arguments{count,count,1,terms,.125f,{}};
        // Match the final hot workload's ABC/BCA/CAB cadence and untimed
        // readbacks, rather than calibrating an isolated cold A dispatch.
        for(unsigned warm=0;warm<2;++warm)
            for(unsigned variant=0;variant<3;++variant)(void)shaders->frame(*gpu,variant,arguments);
        const auto warm_readback=read_particles(*gpu);
        if(warm_readback[0].size()!=count)throw std::runtime_error("La calibración perdió la lectura de calentamiento.");
        std::vector<double> samples;
        std::vector<ParticleReadback> calibration_frames;
        calibration_frames.reserve(3);
        for(unsigned round=0;round<3;++round) {
            arguments.frame_time=.125f+static_cast<float>(round)*.03125f;
            for(unsigned step=0;step<3;++step) {
                const unsigned variant=(round+step)%3;
                const auto elapsed=shaders->frame(*gpu,variant,arguments);
                if(variant==0)samples.push_back(elapsed);
            }
            calibration_frames.push_back({arguments,read_particles(*gpu)});
        }
        const auto elapsed=median(samples);
        report.calibration.push_back({count,terms,elapsed,std::isfinite(elapsed)});
        if(!std::isfinite(elapsed)||elapsed>=report.target_ms)break;
        if(count<maximum_particles) {
            count=std::min(count*2,maximum_particles);
            shaders.reset();gpu.reset();values=inputs(count,stop);
            // Physical stress cannot silently become a WARP test mid-sweep.
            gpu=std::make_unique<Device>(GraphicsBackend::hardware,values,stop);
            gpu->prepare_particles();shaders=std::make_unique<ParticleShaders>(*gpu);
        } else if(terms<maximum_terms) {
            // Only a valid sample below 33.33 ms reaches this branch. The
            // next single dispatch at most doubles its arithmetic work
            // (expected below 66.67 ms, not a multi-second synthetic batch).
            terms*=2;
        }
        else break;
    }
    report.backend=gpu->description;report.software=gpu->software;report.gpu_timestamps=gpu->timestamps;
    for(const auto& stage:report.calibration)report.gpu_timestamps=report.gpu_timestamps&&stage.timing_valid;
    const ParticleOracle oracle(terms);
    for(const std::uint32_t group_size:{1u,256u}) {
        cancel(stop);
        const auto groups=(count+group_size-1)/group_size;
        ParticleArguments arguments{count,groups,group_size,terms,0,{}};
        SmoParticleRow row;row.particles=count;row.group_size=group_size;row.groups=groups;row.terms=terms;
        row.naive_term_evaluations=static_cast<std::uint64_t>(count)*terms;
        row.grouped_term_evaluations=static_cast<std::uint64_t>(groups)*terms;
        // Two complete, real, unmeasured frames for EACH of A/B/C. Save the
        // warm-up result in RAM as well: no lengthy CPU oracle can cool the
        // GPU between warm-up and the continuous sequence of measured frames.
        for(unsigned warm=0;warm<2;++warm) {
            arguments.frame_time=static_cast<float>(warm)*.0625f;
            for(unsigned variant=0;variant<3;++variant)(void)shaders->frame(*gpu,variant,arguments);
        }
        const ParticleReadback warm_readback{arguments,read_particles(*gpu)};
        std::array<std::vector<double>*,3> times{&row.naive_ms,&row.grouped_ordinary_ms,&row.grouped_smo_ms};
        for(auto* samples:times)samples->reserve(rounds);
        std::vector<ParticleReadback> frames;
        frames.reserve(rounds);
        for(unsigned round=0;round<rounds;++round) {
            arguments.frame_time=.125f+static_cast<float>(round)*.03125f;
            // ABC / BCA / CAB balances first/last position, while ALL variants
            // in the same round compute the same frame time and full outputs.
            for(unsigned step=0;step<3;++step) {
                const unsigned variant=(round+step)%3;
                times[variant]->push_back(shaders->frame(*gpu,variant,arguments));
            }
            // Keep ALL frame outputs, not just the final frame. Readback is
            // outside the timestamps; the independent CPU oracle runs only
            // after the complete measured sequence. At the hard limit the
            // nine A/B/C frames use 864 MiB of temporary RAM, with no files.
            frames.push_back({arguments,read_particles(*gpu)});
        }
        SmoParticleRow warm_results;
        verify_particles(values,warm_readback.values,warm_readback.arguments,oracle,warm_results,stop);
        if(warm_results.mismatches)throw std::runtime_error("Las partículas GPU no coinciden con su oráculo independiente durante el calentamiento.");
        for(const auto& frame:frames)
            verify_particles(values,frame.values,frame.arguments,oracle,row,stop);
        row.naive_median_ms=median(row.naive_ms);
        row.grouped_ordinary_median_ms=median(row.grouped_ordinary_ms);
        row.grouped_smo_median_ms=median(row.grouped_smo_ms);
        row.timing_valid=std::isfinite(row.naive_median_ms)&&std::isfinite(row.grouped_ordinary_median_ms)&&std::isfinite(row.grouped_smo_median_ms);
        row.equivalent=row.mismatches==0;
        row.target_reached=row.timing_valid&&row.naive_median_ms>=report.target_ms;
        report.gpu_timestamps=report.gpu_timestamps&&row.timing_valid;
        row.note="A recalcula la curva completa por partícula; B la calcula por emisor en aritmética ordinaria; C usa la misma agrupación con el producto SMO. ";
        row.note+=group_size==1?"Grupo 1: no se ahorra ningún término y B/C pagan un prepaso adicional. ":
            "Grupo 256: B y C evalúan exactamente el mismo número reducido de términos en GPU, conservando todas las posiciones locales. ";
        row.note+="Cada muestra mide un único fotograma GPU, incluido el prepaso. Las rondas ABC/BCA/CAB se ejecutan consecutivamente con lecturas fuera del tiempo; todas sus salidas se conservan en RAM y el oráculo CPU se ejecuta al terminar la fila, sin pausas de validación entre fotogramas. Se comprueban todas las salidas A/B/C en todas las rondas, tolerancia relativa 2e-4. ";
        row.note+="La curva es común por emisor: esta reducción no incluye fuerzas o colisiones individuales. A es deliberadamente redundante; no representa el mejor algoritmo ordinario posible. ";
        row.note+="La identidad trigonométrica del oráculo muestra otra reducción algebraica posible; no se usa para generar las salidas GPU medidas. ";
        if(!row.target_reached)row.note+="NO se alcanzó el objetivo de 33,33 ms por fotograma ingenuo en esta fila. ";
        if(!row.timing_valid)row.note+="Consultas GPU inválidas: no se sustituyen por relojes CPU. ";
        if(!row.equivalent)row.note+="Las discrepancias impiden afirmar una mejora para el mismo problema. ";
        report.rows.push_back(std::move(row));
    }
    report.conclusion="Prueba experimental de partículas con kernels HLSL escritos manualmente, no un compilador GPU completo de SMO. ";
    if(report.software)report.conclusion+="WARP/software: únicamente acredita corrección; no rendimiento físico ni una carga de 20/30 FPS. ";
    else if(!full_stress)report.conclusion+="Carga pequeña de verificación, no una prueba de saturación. ";
    if(full_stress)report.conclusion+="La calibración usa la misma cadencia A/B/C y lecturas sin oráculo intermedio, aumentando trabajo real hasta 33,33 ms GPU o el límite de 2097152 partículas y 8192 términos; si no se alcanzó, queda indicado. ";
    report.conclusion+="Tiempos de un fotograma de cómputo, sin CPU/readback/compilación ni renderizado: no son FPS de la aplicación completa. ";
    report.conclusion+="Se almacenan en RAM todos los fotogramas medidos y se verifican después de cada secuencia; no se escriben archivos temporales ni se añade espera artificial. ";
    report.conclusion+="B es el control imprescindible: la reducción por agrupación pertenece al algoritmo y también existe en aritmética ordinaria, no demuestra una propiedad exclusiva ni superioridad general de SMO. ";
    report.conclusion+="Sin/cos y la suma de posiciones son soporte gráfico convencional. El oráculo independiente usa otra identidad de suma; A no es el mejor ordinario optimizado posible. ";
    report.conclusion+="Los recuentos son términos realmente evaluados por la GPU, no FLOPS estimados; no se atribuye una aceleración a la notación en base doce.";
    return report;
}
} // namespace lgaed::presentation
