#include "presentation/windows/smo_scene.hpp"
#include "platform/windows/state_store.hpp"
#include "core/smo.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using Clock=std::chrono::steady_clock;
constexpr float dt=1.f/120.f,tolerance=2e-4f;
std::string source(std::string_view name){const auto bytes=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,256*1024);std::string result;result.reserve(bytes.size());for(std::size_t i=0;i<bytes.size();++i){if(bytes[i]=='\r'&&i+1<bytes.size()&&bytes[i+1]=='\n')continue;result+=static_cast<char>(bytes[i]);}return result;}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& value){return std::as_bytes(std::span(value));}
template<class T,std::size_t N>std::span<const std::byte> bytes(const std::array<T,N>& value){return std::as_bytes(std::span(value));}
void stopped(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Prueba sostenida cancelada.");}
bool close(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance*std::max(1.f,std::abs(a));}
double quantile(const std::vector<double>& values,double q){if(values.empty()||!std::ranges::all_of(values,[](double n){return std::isfinite(n)&&n>0;}))return std::numeric_limits<double>::quiet_NaN();auto sorted=values;std::sort(sorted.begin(),sorted.end());return sorted[static_cast<std::size_t>(std::ceil((sorted.size()-1)*q))];}
std::array<std::uint32_t,16> constants(const SmoSceneWorkload& scene,std::uint32_t frame,unsigned variant){std::array<std::uint32_t,16> p{};
    p[0]=scene.bodies;p[1]=scene.group_size;p[2]=(scene.bodies+scene.group_size-1)/scene.group_size;p[3]=scene.width;p[4]=scene.height;p[5]=frame;p[6]=variant;
    p[8]=std::bit_cast<std::uint32_t>(dt);p[9]=std::bit_cast<std::uint32_t>(-1.6f);p[10]=std::bit_cast<std::uint32_t>(.65f);p[12]=std::bit_cast<std::uint32_t>(tolerance);return p;
}
struct Programs{std::array<std::array<SmoKernel,4>,3> stage{};SmoKernel compare;
    explicit Programs(SmoCompute& gpu){for(unsigned mode=0;mode<3;++mode)for(unsigned s=0;s<4;++s)stage[mode][s]=gpu.kernel(smo_scene_kernel(s,mode));compare=gpu.kernel(smo_scene_kernel(4,0));}
};
struct Scene {
    struct Fields{SmoBuffer current,integrated,next,bounds,image;};
    SmoCompute& gpu;const Programs& code;SmoSceneWorkload work;std::array<Fields,3> fields;SmoBuffer summary;
    std::vector<SmoSceneBody> initial;
    Scene(SmoCompute& device,const Programs& programs,SmoSceneWorkload workload):gpu(device),code(programs),work(workload),initial(smo_scene_initial(work.bodies)){
        const auto groups=(work.bodies+work.group_size-1)/work.group_size;
        for(auto& f:fields){f.current=gpu.buffer(initial.size()*sizeof(SmoSceneBody),bytes(initial));f.integrated=gpu.buffer(initial.size()*sizeof(SmoSceneBody));f.next=gpu.buffer(initial.size()*sizeof(SmoSceneBody));f.bounds=gpu.buffer(groups*32);f.image=gpu.buffer(static_cast<std::size_t>(work.width)*work.height*32);}
        summary=gpu.buffer(32*sizeof(std::uint32_t));
    }
    void reset(){for(auto& f:fields)gpu.write(f.current,bytes(initial));}
    std::vector<SmoDispatch> dispatches(unsigned mode,std::uint32_t frame)const{
        const auto& f=fields[mode];const auto p=constants(work,frame,mode);const auto body_groups=(work.bodies+63)/64,cluster_groups=((work.bodies+work.group_size-1)/work.group_size+63)/64;
        std::vector<SmoDispatch> calls;
        const auto add=[&](unsigned stage,std::array<SmoBuffer,8> bindings,std::uint32_t groups){calls.push_back({code.stage[mode][stage],bindings,p,{groups,1,1}});};
        add(0,{f.current,f.integrated},body_groups);
        if(mode)add(1,{f.integrated,f.bounds},cluster_groups);
        add(2,{f.current,f.integrated,f.bounds,f.next},body_groups);
        if(mode)add(1,{f.next,f.bounds},cluster_groups);
        add(3,{f.next,f.bounds,f.image},(work.width*work.height+63)/64);return calls;
    }
    double frame(unsigned mode,std::uint32_t index,bool timed=true){const auto calls=dispatches(mode,index);const auto result=gpu.execute(calls,timed);std::swap(fields[mode].current,fields[mode].next);return result;}
    std::array<std::uint32_t,32> compare(std::uint32_t frame){std::array<std::uint32_t,32> result{};gpu.write(summary,bytes(result));
        SmoDispatch call{code.compare,{fields[0].current,fields[1].current,fields[2].current,fields[0].image,fields[1].image,fields[2].image,0,summary},constants(work,frame,0),{(std::max(work.bodies,work.width*work.height)+63)/64,1,1}};
        (void)gpu.execute(std::span(&call,1),false);const auto raw=gpu.read(summary);if(raw.size()!=sizeof(result))throw std::runtime_error("Incomplete GPU comparison readback");std::memcpy(result.data(),raw.data(),raw.size());return result;
    }
    SmoSceneCheckpoint capture(std::uint32_t frame){SmoSceneCheckpoint checkpoint;checkpoint.frame=frame;
        auto states=gpu.read(fields[0].current);if(states.size()!=initial.size()*sizeof(SmoSceneBody))throw std::runtime_error("Incomplete scene state");checkpoint.bodies.resize(initial.size());std::memcpy(checkpoint.bodies.data(),states.data(),states.size());
        const auto raw=gpu.read(fields[0].image);const auto count=static_cast<std::size_t>(work.width)*work.height;if(raw.size()!=count*32)throw std::runtime_error("Incomplete ray traced image");checkpoint.pixels.resize(count);
        for(std::size_t i=0;i<count;++i)std::memcpy(checkpoint.pixels[i].data(),raw.data()+i*32,16);return checkpoint;
    }
};
void verify_reference(const SmoSceneCheckpoint& checkpoint,const SmoSceneWorkload& work){
    if(checkpoint.frame==1){const auto expected=smo_scene_step_reference(smo_scene_initial(work.bodies),1);for(std::size_t i=0;i<expected.size();++i){std::array<float,8> a{},b{};std::memcpy(a.data(),&expected[i],32);std::memcpy(b.data(),&checkpoint.bodies[i],32);for(unsigned k=0;k<8;++k)if(!close(a[k],b[k]))throw std::runtime_error("GPU physics differs from the independent CPU reference at body "+std::to_string(i)+", component "+std::to_string(k));}}
    const auto count=checkpoint.pixels.size();const auto checks=count<=4096?count:std::size_t{256};for(std::size_t j=0;j<checks;++j){const auto pixel=checks==count?j:(j*count/checks+count/(checks*2));const auto expected=smo_scene_ray_reference(checkpoint.bodies,static_cast<std::uint32_t>(pixel),work);for(unsigned k=0;k<4;++k)if(!close(expected[k],checkpoint.pixels[pixel][k]))throw std::runtime_error("Ray traced image differs from CPU reference at frame "+std::to_string(checkpoint.frame)+", pixel "+std::to_string(pixel)+", component "+std::to_string(k));}
}
SmoSceneProgress progress_image(const SmoSceneCheckpoint& image,const SmoSceneWorkload& work,std::string name,double elapsed,std::uint64_t frames){SmoSceneProgress value;value.api=std::move(name);value.phase="Ray tracing + gravedad + colisiones";value.elapsed=elapsed;value.frames=frames;value.width=work.width;value.height=work.height;value.preview_bgra.resize(image.pixels.size());
    for(std::size_t i=0;i<image.pixels.size();++i){const auto& c=image.pixels[i];const auto channel=[](float f){return static_cast<std::uint32_t>(std::clamp(f,0.f,1.f)*255.f+.5f);};value.preview_bgra[i]=channel(c[2])|(channel(c[1])<<8)|(channel(c[0])<<16)|0xff000000u;}return value;
}
}
std::string smo_compute_api_name(SmoComputeApi api){switch(api){case SmoComputeApi::direct3d11:return "Direct3D 11";case SmoComputeApi::direct3d12:return "Direct3D 12";case SmoComputeApi::vulkan:return "Vulkan";case SmoComputeApi::opengl:return "OpenGL";}throw std::invalid_argument("Unknown graphics API");}
SmoComputeApi smo_compute_api(std::string_view name){if(name=="d3d11"||name=="dx11")return SmoComputeApi::direct3d11;if(name=="d3d12"||name=="dx12")return SmoComputeApi::direct3d12;if(name=="vulkan"||name=="vk")return SmoComputeApi::vulkan;if(name=="opengl"||name=="gl")return SmoComputeApi::opengl;throw std::invalid_argument("Expected d3d11, d3d12, vulkan or opengl");}
std::unique_ptr<SmoCompute> create_smo_compute(SmoComputeApi api,const SmoComputeOptions& options,std::stop_token stop){switch(api){case SmoComputeApi::direct3d11:return create_smo_d3d11_compute(options,stop);case SmoComputeApi::direct3d12:return create_smo_d3d12_compute(options,stop);case SmoComputeApi::vulkan:return create_smo_vulkan_compute(options,stop);case SmoComputeApi::opengl:return create_smo_opengl_compute(options,stop);}throw std::invalid_argument("Unknown graphics API");}
SmoKernelSource smo_scene_kernel(unsigned stage,unsigned mode){if(stage>5||mode>2)throw std::invalid_argument("Invalid sustained scene shader");const auto body=source("smo_scene_common.inc"),hlsl=source("smo_shader_hlsl.inc"),glsl=source("smo_shader_glsl.inc");const auto defines="#define STAGE "+std::to_string(stage)+"\n#define MODE "+std::to_string(mode)+"\n";
    SmoKernelSource result;result.name="stage"+std::to_string(stage)+"_mode"+std::to_string(mode);result.hlsl=defines+hlsl+body;result.glsl="#version 430\n"+defines+glsl+body;
#ifdef LGAED_HAS_VULKAN
    const auto root=std::filesystem::path(LGAED_SMO_SHADER_ROOT);const auto manifest=state::read_bytes(root/"source.sha256",256);std::string hash(manifest.begin(),manifest.end());while(!hash.empty()&&std::isspace(static_cast<unsigned char>(hash.back())))hash.pop_back();if(hash!=state::sha256(glsl+body))throw std::runtime_error("Vulkan shader sources changed: rebuild the existing Windows preset");
    const auto raw=state::read_bytes(root/"shaders"/(result.name+".spv"),1024*1024);if(raw.empty()||raw.size()%4)throw std::runtime_error("Invalid precompiled SPIR-V scene");result.spirv.resize(raw.size()/4);std::memcpy(result.spirv.data(),raw.data(),raw.size());if(result.spirv.front()!=0x07230203u)throw std::runtime_error("SPIR-V scene magic mismatch");
#endif
    return result;
}
void smo_compute_smoke(SmoComputeApi api,const SmoComputeOptions& options,std::stop_token stop){auto gpu=create_smo_compute(api,options,stop);const auto kernel=gpu->kernel(smo_scene_kernel(5,0));const std::vector<float> values{-2,0,1,3};const auto input=gpu->buffer(16,bytes(values)),middle=gpu->buffer(16),output=gpu->buffer(16);SmoDispatch a{kernel,{input,middle},{},{1,1,1}},b{kernel,{middle,output},{},{1,1,1}};a.constants[0]=b.constants[0]=4;
    const std::array<SmoDispatch,2> calls{a,b};(void)gpu->execute(calls);auto result=gpu->read(output);std::array<float,4> observed{};if(result.size()!=16)throw std::runtime_error("GPU smoke output is truncated");std::memcpy(observed.data(),result.data(),16);for(unsigned i=0;i<4;++i)if(observed[i]!=4*values[i]+3)throw std::runtime_error("GPU producer/consumer or descriptor ordering failed");
    const std::array<float,1> prefix{7};gpu->write(input,bytes(prefix));const auto patched=gpu->read(input);std::array<float,4> copied{};std::memcpy(copied.data(),patched.data(),16);if(copied!=std::array<float,4>{7,0,1,3})throw std::runtime_error("GPU prefix write changed unrelated bytes");
    const std::vector<float> pairs{0,2,2,0,-2,-3,-2,3,2,-3,2,3,0,0};const auto pairs_buffer=gpu->buffer(pairs.size()*4,bytes(pairs)),answer_buffer=gpu->buffer(pairs.size()/2*4);const auto semantics=gpu->kernel(smo_scene_kernel(5,2));
    for(unsigned operation=1;operation<=2;++operation){SmoDispatch call{semantics,{pairs_buffer,answer_buffer},{},{1,1,1}};call.constants[0]=static_cast<std::uint32_t>(pairs.size()/2);call.constants[1]=operation;(void)gpu->execute(std::span(&call,1));const auto raw=gpu->read(answer_buffer);for(std::size_t i=0;i<pairs.size()/2;++i){float value;std::memcpy(&value,raw.data()+i*4,4);const auto expected=operation==1?smo::multiply(pairs[i*2],pairs[i*2+1]).value:smo::divide(pairs[i*2],pairs[i*2+1]).value;if(!close(static_cast<float>(expected),value))throw std::runtime_error("GPU lowering changed SMO zero/sign arithmetic");}}
}
SmoSceneDiagnosticReport diagnose_smo_scene(std::span<const SmoComputeApi> apis,const SmoSceneWorkload& work,
    std::uint32_t frames,const SmoComputeOptions& options,std::stop_token stop){
    stopped(stop);
    if(apis.size()<2||apis.size()>4||!frames||frames>256||work.bodies<2||work.bodies>2048||
        !work.group_size||work.group_size>256||!work.width||!work.height||
        static_cast<std::uint64_t>(work.width)*work.height>2073600)
        throw std::invalid_argument("Invalid cross-API scene diagnostic workload");
    struct Probe {
        std::unique_ptr<SmoCompute> gpu;
        std::array<SmoKernel,4> kernels{};
        SmoBuffer current=0,integrated=0,next=0,image=0,trace=0;
        std::vector<std::byte> native_state,trace_zero;
        Probe(SmoComputeApi api,const SmoComputeOptions& option,const SmoSceneWorkload& workload,
            std::span<const std::byte> initial,std::stop_token token):gpu(create_smo_compute(api,option,token)),native_state(initial.begin(),initial.end()){
            for(unsigned i=0;i<3;++i)kernels[i]=gpu->kernel(smo_scene_kernel(i==0?0:i==1?2:3,0));
            kernels[3]=gpu->kernel(smo_scene_kernel(5,0));
            current=gpu->buffer(initial.size(),initial);integrated=gpu->buffer(initial.size());next=gpu->buffer(initial.size());
            image=gpu->buffer(static_cast<std::size_t>(workload.width)*workload.height*32);
            trace_zero.resize((static_cast<std::size_t>(workload.bodies)+1)*24*4);trace=gpu->buffer(trace_zero.size(),trace_zero);
        }
        void phase(unsigned part,const SmoSceneWorkload& workload,std::uint32_t frame){
            SmoDispatch call;call.kernel=kernels[part];call.constants=constants(workload,frame,0);
            if(part==0)call.buffers={current,integrated};
            else if(part==1)call.buffers={current,integrated,0,next};
            else call.buffers={next,0,image};
            const auto count=part==2?workload.width*workload.height:workload.bodies;
            call.groups={(count+63)/64,1,1};(void)gpu->execute(std::span(&call,1),false);
        }
        void trace_contact(const SmoSceneWorkload& workload,std::uint32_t frame){
            gpu->write(trace,trace_zero);SmoDispatch call;call.kernel=kernels[3];call.constants=constants(workload,frame,0);
            call.constants[7]=0;call.constants[15]=1;call.buffers={current,integrated,0,next,trace};call.groups={1,1,1};
            (void)gpu->execute(std::span(&call,1),false);
        }
    };
    const auto initial=smo_scene_initial(work.bodies);const auto initial_bytes=bytes(initial);
    std::vector<std::byte> common(initial_bytes.begin(),initial_bytes.end());
    SmoSceneDiagnosticReport report;report.workload=work;report.frames=frames;
    std::vector<std::unique_ptr<Probe>> probes;
    for(const auto api:apis){auto option=options;if(option.adapter_name.empty()&&!report.devices.empty())option.adapter_name=report.devices.front().adapter;
        auto probe=std::make_unique<Probe>(api,option,work,initial_bytes,stop);report.devices.push_back(probe->gpu->info());probes.push_back(std::move(probe));}
    const auto compare=[&](std::uint32_t api,std::string_view phase,std::uint32_t frame,
        const std::vector<std::byte>& expected,const std::vector<std::byte>& actual,unsigned components){
        if(expected.size()!=actual.size()||expected.size()%32)throw std::runtime_error("Diagnostic GPU readback has an unexpected size");
        const auto matches=[&](const SmoSceneDifference& item){return item.api==api&&item.phase==phase;};
        bool exact=std::ranges::any_of(report.first_exact,matches),outside=std::ranges::any_of(report.first_outside_tolerance,matches);
        for(std::size_t element=0;element<expected.size()/32&&!(exact&&outside);++element)for(unsigned component=0;component<components&&!(exact&&outside);++component){
            stopped(stop);SmoSceneDifference difference;difference.api=api;difference.phase=phase;difference.frame=frame;
            difference.element=static_cast<std::uint32_t>(element);difference.component=component;
            const auto offset=element*32+component*4;
            std::memcpy(&difference.expected_bits,expected.data()+offset,4);std::memcpy(&difference.actual_bits,actual.data()+offset,4);
            difference.expected=std::bit_cast<float>(difference.expected_bits);difference.actual=std::bit_cast<float>(difference.actual_bits);
            difference.outside_tolerance=!close(difference.expected,difference.actual);
            const auto ordered=[](std::uint32_t bits){return bits&0x80000000u?~bits:bits|0x80000000u;};
            const auto a=ordered(difference.expected_bits),b=ordered(difference.actual_bits);
            difference.ulps=a>b?static_cast<std::uint64_t>(a)-b:static_cast<std::uint64_t>(b)-a;
            if(!exact&&difference.expected_bits!=difference.actual_bits){report.first_exact.push_back(difference);exact=true;}
            if(!outside&&difference.outside_tolerance){report.first_outside_tolerance.push_back(difference);outside=true;}
        }
    };
    for(std::uint32_t frame=1;frame<=frames;++frame){
        stopped(stop);std::vector<std::byte> common_integrated,common_next,common_image;
        // First isolate integration: every API receives identical input bytes.
        for(std::size_t api=0;api<probes.size();++api){auto& probe=*probes[api];probe.gpu->write(probe.current,common);probe.phase(0,work,frame);
            auto output=probe.gpu->read(probe.integrated);if(api==0)common_integrated=std::move(output);
            else compare(static_cast<std::uint32_t>(api),"integration-common-input",frame,common_integrated,output,8);}
        // Then isolate contacts. Do not allow a prior API's sin/cos rounding
        // to masquerade as a different collision implementation.
        for(std::size_t api=0;api<probes.size();++api){auto& probe=*probes[api];probe.gpu->write(probe.integrated,common_integrated);probe.phase(1,work,frame);
            auto output=probe.gpu->read(probe.next);if(api==0)common_next=std::move(output);
            else compare(static_cast<std::uint32_t>(api),"contacts-common-input",frame,common_next,output,8);}
        if(frame==1){std::vector<std::byte> expected_trace;
            for(std::size_t api=0;api<probes.size();++api){auto& probe=*probes[api];const auto production=probe.gpu->read(probe.next);
                probe.trace_contact(work,frame);const auto replay=probe.gpu->read(probe.next);
                report.contact_trace_reproduced=report.contact_trace_reproduced&&production==replay;
                const auto actual_trace=probe.gpu->read(probe.trace);
                if(api==0){expected_trace=actual_trace;continue;}
                for(std::size_t item=0;item<actual_trace.size()/4;++item){SmoSceneDifference value;
                    std::memcpy(&value.expected_bits,expected_trace.data()+item*4,4);std::memcpy(&value.actual_bits,actual_trace.data()+item*4,4);
                    if(value.expected_bits==value.actual_bits)continue;
                    value.api=static_cast<std::uint32_t>(api);value.frame=frame;value.phase="contact-intermediate-body0";
                    value.element=static_cast<std::uint32_t>(item/24);value.component=static_cast<std::uint32_t>(item%24);
                    value.expected=std::bit_cast<float>(value.expected_bits);value.actual=std::bit_cast<float>(value.actual_bits);
                    value.outside_tolerance=!close(value.expected,value.actual);
                    const auto ordered=[](std::uint32_t bits){return bits&0x80000000u?~bits:bits|0x80000000u;};
                    const auto a=ordered(value.expected_bits),b=ordered(value.actual_bits);
                    value.ulps=a>b?static_cast<std::uint64_t>(a)-b:static_cast<std::uint64_t>(b)-a;report.contact_trace.push_back(value);
                }
            }
        }
        // Rays see precisely the same body positions, radii and velocities.
        for(std::size_t api=0;api<probes.size();++api){auto& probe=*probes[api];probe.gpu->write(probe.next,common_next);probe.phase(2,work,frame);
            auto output=probe.gpu->read(probe.image);if(api==0)common_image=std::move(output);
            else compare(static_cast<std::uint32_t>(api),"rays-common-input",frame,common_image,output,4);}
        // Separately retain native accumulation. The canonical input above is
        // diagnostic only; the production benchmark never resets trajectories.
        probes.front()->native_state=common_next;
        for(std::size_t api=1;api<probes.size();++api){auto& probe=*probes[api];probe.gpu->write(probe.current,probe.native_state);
            probe.phase(0,work,frame);probe.phase(1,work,frame);probe.native_state=probe.gpu->read(probe.next);
            compare(static_cast<std::uint32_t>(api),"trajectory-accumulated",frame,common_next,probe.native_state,8);}
        common=std::move(common_next);
    }
    return report;
}
SmoSustainedReport run_smo_sustained(const SmoSustainedOptions& options,std::function<void(SmoSceneProgress)> progress,std::stop_token stop){stopped(stop);
    if(!std::isfinite(options.seconds)||options.seconds<=0||options.seconds>120||!std::isfinite(options.warmup_seconds)||options.warmup_seconds<0||options.warmup_seconds>10)throw std::invalid_argument("Invalid sustained benchmark duration");
    auto gpu=create_smo_compute(options.api,options.device,stop);const auto info=gpu->info();if(info.software&&options.seconds>1)throw std::invalid_argument("Software execution is restricted to short correctness tests, not physical sustained performance");auto work=options.workload;
    if(options.calibrate){
        {Programs calibration(*gpu);const std::array<SmoSceneWorkload,5> candidates{{{320,180,128,16},{640,360,256,16},{960,540,512,16},{1280,720,1024,16},{1920,1080,2048,16}}};
            for(const auto candidate:candidates){stopped(stop);if(progress)progress({info.api,"Calibrando carga real",0,0,0,0,{}});Scene test(*gpu,calibration,candidate);std::vector<double> times;for(std::uint32_t frame=1;frame<=4;++frame){const auto t=test.frame(0,frame);if(frame>1)times.push_back(t);}work=candidate;if(quantile(times,.5)>=1000./30.)break;}}
        // Handles belong to the executor. Destroy it after calibration so no
        // candidate buffers remain resident during the measured scene. In an
        // all-API run only the first API calibrates; retaining its candidates
        // would otherwise make that API's memory footprint incomparable.
        gpu.reset();stopped(stop);auto selected=options.device;selected.adapter_name=info.adapter;gpu=create_smo_compute(options.api,selected,stop);
        const auto fresh=gpu->info();if(fresh.adapter!=info.adapter||fresh.software!=info.software||fresh.vendor_id!=info.vendor_id||fresh.device_id!=info.device_id)throw std::runtime_error("Calibration recreation selected a different graphics device");
    }
    if(work.bodies<2||work.bodies>2048||!work.group_size||work.group_size>256||!work.width||!work.height||static_cast<std::uint64_t>(work.width)*work.height>2073600)throw std::invalid_argument("Scene exceeds the declared workload budget");
    Programs programs(*gpu);Scene scene(*gpu,programs,work);const auto warm_begin=Clock::now();std::uint32_t warm_frame=1;while(std::chrono::duration<double>(Clock::now()-warm_begin).count()<options.warmup_seconds){stopped(stop);for(unsigned mode=0;mode<3;++mode)(void)scene.frame(mode,warm_frame,false);++warm_frame;}
    scene.reset();SmoSustainedReport report;report.device=info;report.workload=work;report.identity=state::sha256(source("smo_scene_common.inc")+source("smo_shader_hlsl.inc")+source("smo_shader_glsl.inc")+std::to_string(work.width)+":"+std::to_string(work.height)+":"+std::to_string(work.bodies)+":"+std::to_string(work.group_size)+":dt1/120:seed-fixed");
    const auto began=Clock::now();double last_preview=-5;std::uint32_t previous_hash=0;bool has_hash=false;
    do{stopped(stop);const auto frame=static_cast<std::uint32_t>(report.frames+1);for(unsigned step=0;step<3;++step){const unsigned mode=(frame-1+step)%3;const auto cpu=Clock::now();report.metrics[mode].gpu_ms.push_back(scene.frame(mode,frame));report.metrics[mode].cpu_ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-cpu).count());}
        const auto check=scene.compare(frame);report.state_mismatches+=check[0];report.pixel_mismatches+=check[1];report.max_error=std::max(report.max_error,static_cast<double>(std::bit_cast<float>(check[2])));report.invalid_values+=check[3];report.contacts+=check[5];if(has_hash&&previous_hash!=check[4])++report.dynamic_frames;previous_hash=check[4];has_hash=true;
        for(unsigned mode=0;mode<3;++mode)report.ray_sphere_tests[mode]+=(static_cast<std::uint64_t>(check[7+mode*2])<<32)|check[6+mode*2];for(unsigned mode=0;mode<2;++mode){report.ray_box_tests[mode]+=(static_cast<std::uint64_t>(check[13+mode*2])<<32)|check[12+mode*2];report.state_pair_mismatches[mode]+=check[17+mode];report.pixel_pair_mismatches[mode]+=check[19+mode];}
        ++report.frames;const double elapsed=std::chrono::duration<double>(Clock::now()-began).count();const bool checkpoint=frame==1||frame==32||frame==64||frame==128;
        if(checkpoint||(progress&&elapsed-last_preview>=5)){auto capture=scene.capture(frame);if(progress&&elapsed-last_preview>=5){progress(progress_image(capture,work,info.api,elapsed,report.frames));last_preview=elapsed;}if(checkpoint)report.checkpoints.push_back(std::move(capture));}
    }while(report.frames<2||std::chrono::duration<double>(Clock::now()-began).count()<options.seconds);
    report.elapsed_seconds=std::chrono::duration<double>(Clock::now()-began).count();
    if(progress){stopped(stop);progress(progress_image(scene.capture(static_cast<std::uint32_t>(report.frames)),work,info.api,report.elapsed_seconds,report.frames));}
    // Independent CPU oracles run only AFTER the sustained interval. The GPU
    // comparison inspected every state and pixel of every measured A/B/C frame.
    for(const auto& checkpoint:report.checkpoints){stopped(stop);verify_reference(checkpoint,work);}
    for(auto& metric:report.metrics){metric.median_ms=quantile(metric.gpu_ms,.5);metric.p95_ms=quantile(metric.gpu_ms,.95);metric.p99_ms=quantile(metric.gpu_ms,.99);metric.valid=std::isfinite(metric.median_ms)&&std::isfinite(metric.p95_ms)&&std::isfinite(metric.p99_ms);}
    report.equivalent=!report.state_mismatches&&!report.pixel_mismatches&&!report.invalid_values;report.target_reached=report.metrics[0].valid&&report.metrics[0].median_ms>=1000./30.;
    report.conclusion="Escena dinámica FP32: integración a 120 Hz de simulación, gravedad, contactos Jacobi de esferas y paredes, ray tracing primario ortográfico y sombras de un rayo direccional. A examina todos los candidatos; B y C usan las mismas cotas conservadoras por grupo, con ambos prepasos incluidos. La comparación GPU valida todos los estados y píxeles en cada ronda; después del minuto se contrasta el primer paso físico completo y rayos de control contra CPU. ";
    report.conclusion+="Los tiempos GPU cubren física, colisiones y ray tracing; excluyen oráculos, lecturas y presentación. No son FPS de la aplicación completa ni una prueba DXR/path tracing. La geometría usa magnitudes y coeficientes no negativos: no sustituye el producto SMO genérico de dos negativos. El arnés HLSL/GLSL es experimental y no un compilador GPU completo del lenguaje. ";
    report.conclusion+="B distingue la reducción del algoritmo de una ventaja exclusiva de SMO. Si aparecen discrepancias o relojes inválidos, no se acredita la aceleración. Semilla, resolución, cuerpos, dt, calidad y fuente deben coincidir para comparar APIs.";return report;
}
}
