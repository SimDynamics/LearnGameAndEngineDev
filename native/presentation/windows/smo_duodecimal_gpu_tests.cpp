#include "presentation/windows/smo_duodecimal_gpu.hpp"
#include "presentation/windows/smo_float12.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace lgaed::presentation;
void check(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
std::string quantity(SmoD12 value,bool floating12=false){try{return floating12?smo_f12_format(value):smo_d12_format(value);}catch(const std::exception&){return "[sin valor utilizable]";}}
SmoComputeApi api(std::string_view name){return smo_compute_api(name);}
void print_validation(const SmoD12Validation& report){std::cout<<"D12 exact conformance api="<<report.device.api<<" adapter="<<std::quoted(report.device.adapter)<<" software="<<report.device.software<<" source="<<report.identity
    <<" floating12="<<report.floating12<<" cases="<<report.cases<<" profiles="<<report.profiles.size()<<" mismatches="<<report.failures.size()<<'\n';
    std::size_t shown=0;for(const auto& failure:report.failures){if(shown++>=24)break;std::cout<<"FAIL "<<failure.name<<" expected="<<quantity(failure.expected.primary,report.floating12)<<" actual="<<quantity(failure.actual.primary,report.floating12)
        <<" expected_diagnostic="<<failure.expected.primary.diagnostic<<" actual_diagnostic="<<failure.actual.primary.diagnostic<<" flags="<<failure.expected.flags<<'/'<<failure.actual.flags
        <<" actual_limbs="<<failure.actual.primary.digits[0]<<','<<failure.actual.primary.digits[1]<<','<<failure.actual.primary.digits[2]<<','<<failure.actual.primary.digits[3]<<" sign="<<failure.actual.primary.negative<<'\n';}
    std::array<std::size_t,11> by_operation{};for(const auto& failure:report.failures)++by_operation[std::min(10u,failure.operation)];
    for(unsigned op=0;op<by_operation.size();++op)if(by_operation[op])std::cout<<"FAIL operation="<<op<<" count="<<by_operation[op]<<'\n';
    if(!report.failures.empty()){const auto& first=report.failures.front();std::cout<<"First mismatch alternatives expected="<<quantity(first.expected.alternative,report.floating12)
        <<" actual="<<quantity(first.actual.alternative,report.floating12)<<" actual_words="<<first.actual.alternative.digits[0]<<','<<first.actual.alternative.digits[1]<<','<<first.actual.alternative.digits[2]<<','<<first.actual.alternative.digits[3]<<" sign="<<first.actual.alternative.negative<<" diagnostic="<<first.actual.alternative.diagnostic<<'\n';}
    if(report.failures.size()>24)std::cout<<"Se muestran24 de "<<report.failures.size()<<" diferencias; todas permanecen en el resultado y hacen fallar la comprobación.\n";}
void print_scene(const SmoD12SceneReport& report){std::cout<<std::setprecision(10)<<(report.options.fp32_control?"FP32 ABI-matched control scene api=":report.options.floating12?"F12 adaptive radix12 scene api=":"D12 fixed integer scene api=")<<report.device.api<<" adapter="<<std::quoted(report.device.adapter)<<" software="<<report.device.software
    <<" floating12="<<report.options.floating12<<" fp32_control="<<report.options.fp32_control<<" source="<<report.identity<<" width="<<report.options.width<<" height="<<report.options.height<<" bodies="<<report.options.bodies
    <<" wall_seconds="<<report.seconds<<" GPU_seconds="<<std::accumulate(report.gpu_ms.begin(),report.gpu_ms.end(),0.)/1000<<" frames="<<report.frames<<" changed_images="<<report.dynamic_frames<<" contacts="<<report.contacts<<" ray_tests="<<report.ray_tests
    <<" fatal_values="<<report.fatal_values<<" inexact_warnings="<<report.inexact_values<<" GPU_ms_median="<<report.median_ms<<" p95="<<report.p95_ms<<" p99="<<report.p99_ms<<" timing_valid="<<report.timing_valid<<" completed="<<report.completed
    <<" checked_transitions="<<report.validated_body_steps<<" checked_rays="<<report.validated_rays<<" checked_checkpoints="<<report.validated_checkpoints<<'\n';
    for(const auto& failure:report.failures)std::cout<<"D12 SCENE FAILURE: "<<failure<<'\n';
    std::cout<<"GPU times cover the four compute passes; frame counts include CPU readback/inspection and are not rendered application FPS.\n";
    if(report.options.fp32_control)std::cout<<"FP32 uses the same equations and diagnostic ABI; this is not the best packed native FP32 engine.\n";}
std::string adapter_key(std::string_view name){std::string result;for(const unsigned char c:name)if(std::isalnum(c))result.push_back(static_cast<char>(std::tolower(c)));return result;}
bool same_adapter(const SmoComputeInfo& a,const SmoComputeInfo& b){if(a.software!=b.software)return false;
    if(a.vendor_id&&b.vendor_id&&a.vendor_id!=b.vendor_id)return false;if(a.device_id&&b.device_id&&a.device_id!=b.device_id)return false;
    const auto left=adapter_key(a.adapter),right=adapter_key(b.adapter);return left.size()>4&&right.size()>4&&(left.find(right)!=left.npos||right.find(left)!=right.npos);}
bool fp32_close(SmoD12 a,SmoD12 b){const auto left=std::bit_cast<float>(a.digits[0]),right=std::bit_cast<float>(b.digits[0]);
    return a.diagnostic==b.diagnostic&&std::isfinite(left)&&std::isfinite(right)&&std::abs(static_cast<double>(left)-right)<=.0002*std::max(1.,std::abs(static_cast<double>(left)));}
bool control_equal(const SmoD12Checkpoint& a,const SmoD12Checkpoint& b){if(a.bodies.size()!=b.bodies.size()||a.pixels.size()!=b.pixels.size())return false;
    for(std::size_t i=0;i<a.bodies.size();++i){const auto& x=a.bodies[i];const auto& y=b.bodies[i];if(x.contacts!=y.contacts||x.diagnostic!=y.diagnostic||x.reserved!=y.reserved||!fp32_close(x.radius,y.radius))return false;
        for(unsigned k=0;k<3;++k)if(!fp32_close(x.position[k],y.position[k])||!fp32_close(x.velocity[k],y.velocity[k]))return false;}
    for(std::size_t i=0;i<a.pixels.size();++i){const auto& x=a.pixels[i];const auto& y=b.pixels[i];if(x.hit!=y.hit||x.shadow!=y.shadow||x.tests!=y.tests||x.diagnostic!=y.diagnostic||x.reserved!=y.reserved||!fp32_close(x.distance,y.distance))return false;
        for(unsigned k=0;k<3;++k)if(!fp32_close(x.color[k],y.color[k]))return false;}
    return true;}
bool compare(const SmoD12SceneReport& baseline,const SmoD12SceneReport& other){
    check(same_adapter(baseline.device,other.device),"La comparación entre APIs no usa la misma GPU.");
    check(baseline.identity==other.identity&&baseline.scene_identity==other.scene_identity,"La comparación entre APIs usa fuentes distintas.");
    check(baseline.options.bodies==other.options.bodies&&baseline.options.width==other.options.width&&baseline.options.height==other.options.height&&baseline.options.fp32_control==other.options.fp32_control&&baseline.options.floating12==other.options.floating12,"La comparación entre APIs usa cargas o aritméticas distintas.");
    bool equal=true;std::uint64_t states=0,pixels=0;
    for(const auto& point:baseline.checkpoints){const auto found=std::ranges::find_if(other.checkpoints,[&](const auto& value){return value.frame==point.frame;});if(found==other.checkpoints.end())continue;
        ++states;pixels+=point.pixels.size();const bool matches=baseline.options.fp32_control?control_equal(point,*found):point.bodies==found->bodies&&point.pixels==found->pixels;
        if(!matches){equal=false;std::cout<<(baseline.options.fp32_control?"FP32 CROSS-API FAILURE at frame=":"D12 CROSS-API EXACT FAILURE at frame=")<<point.frame<<'\n';}}
    std::cout<<(baseline.options.fp32_control?"FP32 control cross_api=":"D12 cross_api=")<<baseline.device.api<<'/'<<other.device.api<<" common_checkpoints="<<states<<" pixels="<<pixels<<" equivalent="<<equal<<(baseline.options.fp32_control?" tolerance=0.0002\n":" identical_raw_words=required tolerance=NONE\n");return equal&&states>0;
}
void cpu_properties(){const auto parse=smo_d12_parse;check(smo_d12_format(parse("10"))=="10"&&parse("@")==parse("10"),"La base12 perdió doce o su alias.");
    check(smo_d12_format(smo_d12_mul(parse("2"),parse("0")))=="0","El perfil actual cambió el producto por cero.");
    const auto alternative=smo_d12_evaluate(2,parse("2"),parse("0"));check(smo_d12_format(alternative.alternative)=="2"&&(alternative.flags&smo_rule_has_alternative),"Se perdió la alternativa del cero.");
    check(smo_d12_format(smo_d12_pdir(parse("2"),parse("4")))=="14","La potencia no usa cantidades interpretadas en base12.");
    check(smo_d12_format(smo_d12_pinv(parse("10"),parse("3")))=="0.1"&&smo_d12_format(smo_d12_pinv(parse("10"),parse("4")))=="0.01","Las potencias invertidas no subdividen en duodécimos exactos.");
    check(smo_d12_format(smo_d12_pdir(parse("10"),parse("2")))=="100","La potencia directa no forma una centena duodecimal.");
    check(smo_d12_format(smo_d12_sqrt(parse("100")))=="10","La raíz gráfica no conserva la aritmética duodecimal.");
    const auto omitted=smo_d12_evaluate(3,parse("#"),parse("0"),smo_rule_divide_zero_unresolved);
    check((omitted.flags&smo_rule_unresolved)&&!(omitted.flags&smo_rule_has_primary),"Omitir división se presentó como un número resuelto.");
    const auto tiny=parse("0.00001495");
    check((smo_d12_pdir(tiny,parse("2")).diagnostic&smo_d12_underflow)!=0,"Se ocultó el underflow de una potencia aislada.");
    const auto fused=smo_d12_discriminant(tiny,parse("1"),parse("1"));
    check(smo_d12_valid(fused)&&smo_d12_format(fused)=="-1"&&(fused.diagnostic&smo_d12_inexact),"El discriminante pierde precisión intermedia duodecimal.");
    check((smo_d12_discriminant(tiny,parse("1"),parse("0")).diagnostic&smo_d12_underflow)!=0,"El discriminante ocultó un underflow en su resultado final.");
    check(smo_d12_format(smo_d12_magnitude2(tiny,parse("1"),parse("0")))=="1","La magnitud pierde un componente pequeño antes de acumularlo.");
    check((smo_d12_magnitude2(tiny,parse("0"),parse("0")).diagnostic&smo_d12_underflow)!=0,"La magnitud ocultó un resultado final inrepresentable.");
    check(smo_d12_norm3(tiny,parse("0"),parse("0"))==tiny&&smo_d12_format(smo_d12_norm3(parse("3"),parse("4"),parse("0")))=="5","La norma estrecha sus cuadrados antes de calcular la raíz.");
    // This individual product found at frame6680 lies below D12 resolution.
    // Supporting it requires a numerical-format or composite-algorithm change;
    // keep its diagnostic rather than accepting zero to make a long run pass.
    check((smo_d12_mul(parse("-0.0001971#"),parse("0.00002345")).diagnostic&smo_d12_underflow)!=0,"Se ocultó el límite de precisión encontrado en la prueba sostenida.");
    auto bodies=smo_duodecimal_scene_initial();for(unsigned frame=1;frame<=15;++frame)bodies=smo_duodecimal_scene_step_reference(bodies);
    check(!(smo_duodecimal_scene_ray_reference(bodies,987).diagnostic&smo_d12_fatal_mask),"Regresó el underflow intermedio del píxel987 en el frame15.");
    for(unsigned frame=16;frame<=512;++frame){bodies=smo_duodecimal_scene_step_reference(bodies);
        for(const auto& body:bodies)check(!(body.diagnostic&smo_d12_fatal_mask),"La física duodecimal salió del dominio en el fotograma "+std::to_string(frame));
        if(frame==45)check(!(smo_duodecimal_scene_ray_reference(bodies,532).diagnostic&smo_d12_fatal_mask),"Regresó el underflow del componente pequeño en el fotograma45/píxel532.");}
    const auto rejects=[](SmoD12SceneOptions options){bool rejected=false;try{(void)run_smo_duodecimal_scene(options);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"La escena admitió opciones inválidas.");};
    for(const auto number:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){SmoD12SceneOptions options;options.seconds=number;rejects(options);options={};options.warmup_seconds=number;rejects(options);}
    for(const auto count:{0u,1u}){SmoD12SceneOptions options;options.minimum_frames=count;rejects(options);}
}
void prepared_d3d12_compatibility(){
    // Real API acceptance, not a renamed file or a fabricated SM5.1 header.
    const auto source=smo_duodecimal_rules_kernel(0,true);
    check(source.dxbc50_d3d12_compatible&&!source.dxbc.empty()&&source.dxbc51.empty(),"El perfil F12 compartido perdió la identidad real cs_5_0.");
    SmoComputeOptions device;device.software=true;auto gpu=create_smo_compute(SmoComputeApi::direct3d12,device);
    (void)gpu->kernel(source);
    const auto rejects=[&](SmoKernelSource damaged){bool rejected=false;try{(void)gpu->kernel(damaged);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"Direct3D12 aceptó un perfil falsamente rotulado o bytecode corrupto.");};
    auto relabelled=source;relabelled.dxbc51=source.dxbc;rejects(std::move(relabelled));
    auto corrupted=source;corrupted.dxbc[0]^=std::byte{1};rejects(std::move(corrupted));
}
template<class T>std::vector<T> diagnostic_read(SmoCompute& gpu,SmoBuffer buffer,std::size_t count){
    const auto bytes=gpu.read(buffer);check(bytes.size()==count*sizeof(T),"Lectura incompleta del diagnóstico GPU.");
    std::vector<T> result(count);std::memcpy(result.data(),bytes.data(),bytes.size());return result;
}
SmoF12 diagnostic_control_input(SmoD12 value){
    auto result=smo_f12_from_binary32_bits(value.digits[0]);result.diagnostic|=value.diagnostic;
    if(value.digits[1]||value.digits[2]||value.digits[3]||value.negative!=(value.digits[0]>>31u)||
        (!(value.digits[0]&0x7fffffffu)&&value.negative))result.diagnostic|=smo_d12_encoding;
    return result;
}
unsigned diagnostic_index(std::string_view text,unsigned maximum,std::string_view message){
    check(!text.empty()&&std::ranges::all_of(text,[](unsigned char c){return std::isdigit(c)!=0;}),message);
    const auto number=std::stoull(std::string(text));check(number<=maximum,message);return static_cast<unsigned>(number);
}
// Test-only scalar trace. Volatile stores expose each individual FP32/FP64
// rounding step, preventing fused multiply-add or reassociation in this
// diagnostic. This is neither engine arithmetic nor a measured GPU control.
template<class Real>Real trace_round(Real value){volatile Real rounded=value;return rounded;}
template<class Real>Real trace_add(Real a,Real b){return trace_round<Real>(a+b);}
template<class Real>Real trace_sub(Real a,Real b){return trace_round<Real>(a-b);}
template<class Real>Real trace_mul(Real a,Real b){
    if(a==Real(0)||b==Real(0))return Real(0);
    const auto magnitude=trace_round<Real>(std::abs(a)*std::abs(b));return a<Real(0)||b<Real(0)?-magnitude:magnitude;
}
template<class Real>Real trace_square(Real a){return trace_round<Real>(std::abs(a)*std::abs(a));}
template<class Real>Real trace_magnitude2(const std::array<Real,3>& a){
    return trace_add(trace_add(trace_square(a[0]),trace_square(a[1])),trace_square(a[2]));
}
template<class Real>struct RayScalarTrace{
    std::array<Real,3> origin{},delta{};
    Real a=0,h=0,c=0,hh=0,ac=0,discriminant=0,root=0,t=0;
};
template<class Real>RayScalarTrace<Real> trace_intersection(const SmoD12SceneBody& raw_body,unsigned pixel_index,const SmoD12SceneWorkload& work){
    RayScalarTrace<Real> trace;
    const auto inverse_x=trace_round<Real>(Real(1)/Real(work.width)),inverse_y=trace_round<Real>(Real(1)/Real(work.height));
    const auto xcenter=trace_sub(Real((pixel_index%work.width)*2+1),Real(work.width));
    const auto ycenter=trace_sub(Real(work.height),Real((pixel_index/work.width)*2+1));
    trace.origin={trace_mul(trace_mul(xcenter,inverse_x),Real(4)),trace_mul(trace_mul(ycenter,inverse_y),Real(3)),Real(-6)};
    const std::array<Real,3> direction{Real(0),Real(0),Real(1)};
    for(unsigned axis=0;axis<3;++axis)trace.delta[axis]=trace_sub(trace.origin[axis],Real(std::bit_cast<float>(raw_body.position[axis].digits[0])));
    const auto radius=Real(std::bit_cast<float>(raw_body.radius.digits[0]));
    trace.a=trace_magnitude2(direction);
    trace.h=trace_add(trace_add(trace_mul(trace.delta[0],direction[0]),trace_mul(trace.delta[1],direction[1])),trace_mul(trace.delta[2],direction[2]));
    trace.c=trace_sub(trace_magnitude2(trace.delta),trace_square(radius));
    trace.hh=trace_square(trace.h);trace.ac=trace_mul(trace.a,trace.c);
    trace.discriminant=trace_sub(trace.hh,trace.ac);
    trace.root=trace.discriminant<Real(0)?std::numeric_limits<Real>::quiet_NaN():trace_round<Real>(std::sqrt(trace.discriminant));
    const auto inverse_a=trace_round<Real>(Real(1)/trace.a);
    trace.t=trace_mul(trace_sub(-trace.h,trace.root),inverse_a);
    const auto epsilon=trace_round<Real>(Real(1)/Real(20736));
    if(trace.t<epsilon)trace.t=trace_mul(trace_add(-trace.h,trace.root),inverse_a);
    return trace;
}
void print_intersection_trace(const SmoD12SceneBody& raw_body,unsigned body_index,unsigned pixel_index,
    const SmoD12SceneWorkload& work,SmoF12 reference_distance,SmoD12 gpu_distance){
    const auto single=trace_intersection<float>(raw_body,pixel_index,work);
    const auto extended=trace_intersection<double>(raw_body,pixel_index,work);
    std::cout<<"Test-only intersection scalar trace body="<<body_index<<" from identical raw GPU bodies; source-order rounding, no FMA.\n";
    const auto print=[](std::string_view name,float a,double b){std::cout<<std::setprecision(20)<<"Trace "<<name
        <<" CPU32="<<a<<" CPU64="<<b<<" CPU32_bits=0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<std::bit_cast<std::uint32_t>(a)
        <<std::dec<<std::setfill(' ')<<" difference32minus64="<<static_cast<double>(a)-b<<'\n';};
    for(unsigned axis=0;axis<3;++axis){print(std::array<std::string_view,3>{"origin.x","origin.y","origin.z"}[axis],single.origin[axis],extended.origin[axis]);
        print(std::array<std::string_view,3>{"delta.x","delta.y","delta.z"}[axis],single.delta[axis],extended.delta[axis]);}
    print("a",single.a,extended.a);print("h",single.h,extended.h);print("c",single.c,extended.c);
    print("h*h",single.hh,extended.hh);print("a*c",single.ac,extended.ac);
    print("discriminant",single.discriminant,extended.discriminant);print("sqrt(discriminant)",single.root,extended.root);print("t",single.t,extended.t);
    std::cout<<"Trace GPU_distance_matches_CPU32_bits="<<(gpu_distance.digits[0]==std::bit_cast<std::uint32_t>(single.t))
        <<" F12_distance_vs_CPU64_abs_error="<<std::abs(smo_f12_comparison_value(reference_distance)-static_cast<long double>(extended.t))
        <<"; diagnostic evidence only, original mismatch/outcome unchanged.\n";
}
bool diagnose_control_ray(SmoComputeApi target,unsigned frame_count,unsigned pixel_index){
    check(frame_count>0&&frame_count<=100000,"El diagnóstico acepta1..100000 fotogramas.");
    const SmoD12SceneWorkload work{16,64,36};check(pixel_index<work.width*work.height,"Píxel diagnóstico fuera de64x36.");
    SmoComputeOptions hardware;hardware.software=false;auto gpu=create_smo_compute(target,hardware);
    const auto info=gpu->info();check(!info.software,"El diagnóstico solicitado requiere una GPU física.");
    std::cout<<"FP32 control ray diagnostic api="<<info.api<<" adapter="<<std::quoted(info.adapter)
        <<" frames="<<frame_count<<" pixel="<<pixel_index<<"; replay only, GPU timestamps DISABLED, no performance credit.\n";
    std::array<SmoKernel,5> kernels{};
    for(unsigned stage=0;stage<kernels.size();++stage){std::cout<<"Control diagnostic kernel stage="<<stage<<'\n';
        kernels[stage]=gpu->kernel(smo_duodecimal_scene_kernel(stage,true,true));}
    auto current=gpu->buffer(work.bodies*sizeof(SmoD12SceneBody));
    const auto integrated=gpu->buffer(work.bodies*sizeof(SmoD12SceneBody));
    auto next=gpu->buffer(work.bodies*sizeof(SmoD12SceneBody));
    const auto output=gpu->buffer(static_cast<std::size_t>(work.width)*work.height*sizeof(SmoD12ScenePixel));
    const SmoDispatch initialize{kernels[0],{0,current},smo_duodecimal_scene_constants(work),{(work.bodies+63)/64,1,1}};
    (void)gpu->execute(std::span(&initialize,1),false);
    // The exact same state evolution and four-pass tree as the benchmark.
    // There is deliberately no per-frame CPU readback, oracle or stopwatch.
    for(unsigned frame=1;frame<=frame_count;++frame){const auto constants=smo_duodecimal_scene_constants(work,frame);
        const auto body_groups=(work.bodies+63)/64,pixel_groups=(work.width*work.height+63)/64;
        const std::array<SmoDispatch,4> calls{{
            {kernels[1],{current,integrated},constants,{body_groups,1,1}},
            {kernels[2],{integrated,next},constants,{body_groups,1,1}},
            {kernels[3],{next,output},constants,{pixel_groups,1,1}},
            {kernels[4],{next,output},constants,{pixel_groups,1,1}}}};
        (void)gpu->execute(calls,false);std::swap(current,next);
        if(frame%4096==0)std::cout<<"Control diagnostic replay frame="<<frame<<'\n';
    }
    const auto raw_bodies=diagnostic_read<SmoD12SceneBody>(*gpu,current,work.bodies);
    const auto pixels=diagnostic_read<SmoD12ScenePixel>(*gpu,output,static_cast<std::size_t>(work.width)*work.height);
    auto bodies=raw_bodies;bool input_valid=true;
    for(auto& body:bodies){for(auto& value:body.position){value=diagnostic_control_input(value);input_valid=smo_f12_valid(value)&&input_valid;}
        for(auto& value:body.velocity){value=diagnostic_control_input(value);input_valid=smo_f12_valid(value)&&input_valid;}
        body.radius=diagnostic_control_input(body.radius);input_valid=smo_f12_valid(body.radius)&&!(body.diagnostic&smo_d12_fatal_mask)&&input_valid;}
    const auto expected=smo_duodecimal_scene_ray_reference(bodies,pixel_index,work,true);
    const auto& actual=pixels[pixel_index];
    bool equal=input_valid&&!(expected.diagnostic&smo_d12_fatal_mask)&&!(actual.diagnostic&smo_d12_fatal_mask)&&
        expected.hit==actual.hit&&expected.shadow==actual.shadow&&expected.tests==actual.tests;
    std::cout<<"Reference F12 from identical GPU bodies: hit="<<expected.hit<<" shadow="<<expected.shadow<<" tests="<<expected.tests
        <<" diagnostic="<<expected.diagnostic<<" bgra=0x"<<std::hex<<expected.bgra<<std::dec<<'\n';
    std::cout<<"Actual GPU native FP32: hit="<<actual.hit<<" shadow="<<actual.shadow<<" tests="<<actual.tests
        <<" diagnostic="<<actual.diagnostic<<" bgra=0x"<<std::hex<<actual.bgra<<std::dec<<'\n';
    const auto scalar=[&](std::string_view name,SmoF12 reference,SmoD12 observed){
        const auto numeric=smo_f12_comparison_value(reference);const auto binary=std::bit_cast<float>(observed.digits[0]);
        const auto difference=std::abs(numeric-static_cast<long double>(binary));
        const auto bound=.0002L*std::max(1.L,std::abs(numeric));
        const bool matches=smo_f12_valid(reference)&&smo_f12_valid(diagnostic_control_input(observed))&&
            std::isfinite(binary)&&difference<=bound;equal=matches&&equal;
        std::cout<<std::setprecision(20)<<"Scalar "<<name<<" reference_base12="<<quantity(reference,true)<<" reference_decimal="<<numeric
            <<" actual_binary32="<<binary<<" actual_bits=0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<observed.digits[0]
            <<std::dec<<std::setfill(' ')<<" abs_error="<<difference<<" original_bound="<<bound<<" equivalent="<<matches
            <<" raw_words="<<observed.digits[0]<<','<<observed.digits[1]<<','<<observed.digits[2]<<','<<observed.digits[3]
            <<" sign="<<observed.negative<<" diagnostics="<<reference.diagnostic<<'/'<<observed.diagnostic<<'\n';
    };
    scalar("distance",expected.distance,actual.distance);
    for(unsigned channel=0;channel<3;++channel)scalar(std::array<std::string_view,3>{"red","green","blue"}[channel],expected.color[channel],actual.color[channel]);
    if(actual.hit<raw_bodies.size())print_intersection_trace(raw_bodies[actual.hit],actual.hit,pixel_index,work,expected.distance,actual.distance);
    std::cout<<"Control ray diagnostic equivalent="<<equal<<" input_valid="<<input_valid
        <<" tolerance=0.0002*max(1,abs(reference)); categorical hit/shadow/tests require exact agreement. No benchmark result changed.\n";
    return equal;
}
}
int main(int argc,char** argv){try{std::cout<<std::unitbuf;
    std::string_view mode=argc>1?argv[1]:"";const bool floating12=mode.starts_with("float12");
    if(floating12){mode.remove_prefix(7);if(mode.starts_with('-'))mode.remove_prefix(1);}
    if(mode=="control-ray"){
        check(floating12&&argc==5,"Uso: float12-control-ray d3d11|d3d12|vulkan|opengl frame pixel");
        const auto frames=diagnostic_index(argv[3],100000,"Fotograma diagnóstico fuera de presupuesto."),
            pixel=diagnostic_index(argv[4],2303,"Píxel diagnóstico fuera de presupuesto.");
        return diagnose_control_ray(api(argv[2]),frames,pixel)?0:1;
    }
    if(mode=="diagnose"){
        check(argc==2||argc==4,"Uso: diagnose [frame pixel]");
        const auto frame=argc==4?static_cast<unsigned>(std::stoul(argv[2])):15u,pixel_index=argc==4?static_cast<unsigned>(std::stoul(argv[3])):987u;
        check(frame>0&&frame<=10000,"Fotograma diagnóstico fuera de presupuesto.");
        const SmoD12SceneWorkload work;auto bodies=smo_duodecimal_scene_initial(work,floating12);
        const auto observe=floating12?smo_f12_observe:smo_d12_observe;observe([floating12](std::uint32_t operation,SmoD12 a,SmoD12 b,std::uint32_t profile,SmoD12Result result){
            std::cout<<"D12 CPU diagnostic operation="<<operation<<" a="<<quantity(a,floating12)<<" b="<<quantity(b,floating12)<<" profile="<<profile<<" diagnostic="<<result.primary.diagnostic<<'\n';});
        for(unsigned step=1;step<=frame;++step){bodies=smo_duodecimal_scene_step_reference(bodies,floating12);if(std::ranges::any_of(bodies,[](const auto& body){return (body.diagnostic&smo_d12_fatal_mask)!=0;})){
                observe({});std::cout<<"D12 CPU physical diagnostic frame="<<step<<'\n';return 1;}}
        const auto pixel=smo_duodecimal_scene_ray_reference(bodies,pixel_index,work,floating12);observe({});
        std::cout<<"D12 CPU diagnostic frame="<<frame<<" pixel="<<pixel_index<<" diagnostic="<<pixel.diagnostic<<'\n';
        return pixel.diagnostic&smo_d12_fatal_mask?1:0;
    }
    cpu_properties();
    const bool hardware=!mode.empty(),minute=mode=="sustained"||mode=="sustained-control",control=mode=="control"||mode=="sustained-control";
    if(floating12&&!hardware)prepared_d3d12_compatibility();
    check(argc<=3&&(!hardware||minute||control||mode=="probe"||mode=="rules"),"Uso: [float12-]probe|rules|sustained|control|sustained-control [d3d11|d3d12|vulkan|opengl|all]; float12 ejecuta WARP breve");
    const std::string_view selector=argc==3?argv[2]:hardware?"all":"d3d11";const bool all=selector=="all";
    std::optional<SmoD12SceneReport> baseline;std::optional<SmoD12Validation> reference;bool failed=false;
    for(const auto target:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){if(!all&&target!=api(selector))continue;
        try{SmoComputeOptions device;device.software=!hardware;if(reference)device.adapter_name=reference->device.adapter;else if(baseline)device.adapter_name=baseline->device.adapter;std::cout<<"BEGIN D12 "<<smo_compute_api_name(target)<<std::endl;
            if(!control){const auto validation=validate_smo_duodecimal(target,device,hardware,{},[](unsigned operation,bool done){std::cout<<"D12 shader preparation op="<<operation<<" done="<<done<<std::endl;},floating12);print_validation(validation);
                failed=failed||!validation.failures.empty();if(reference){const bool same=reference->outputs.size()==validation.outputs.size()&&std::memcmp(reference->outputs.data(),validation.outputs.data(),validation.outputs.size()*sizeof(SmoD12Result))==0;
                    check(same_adapter(reference->device,validation.device)&&reference->identity==validation.identity,"Las reglas se compararon en GPUs o fuentes distintas.");
                    std::cout<<"D12 rules cross_API identical_words="<<same<<" tolerance=NONE\n";failed=failed||!same;}else reference=validation;}
            if(mode=="rules")continue;
            SmoD12SceneOptions options;options.api=target;options.device=device;options.seconds=minute?60:.08;options.warmup_seconds=minute?1:0;options.minimum_frames=hardware?16:2;
            options.fp32_control=control;options.floating12=floating12;if(!hardware){options.bodies=2;options.width=12;options.height=8;}const auto report=run_smo_duodecimal_scene(options,[](SmoSceneProgress progress){if(progress.frames==0)std::cout<<"D12 scene preparation: "<<progress.phase<<std::endl;});print_scene(report);
            failed=failed||!report.failures.empty()||!report.completed||report.frames<options.minimum_frames||report.seconds<options.seconds||!report.ray_tests;
            check(report.timing_valid&&!report.checkpoints.empty()&&report.checkpoints.back().frame==report.frames&&report.validated_checkpoints==report.checkpoints.size()&&
                report.validated_body_steps==options.bodies*report.checkpoints.size()&&report.validated_rays==static_cast<std::uint64_t>(options.width)*options.height*report.checkpoints.size(),"La escena omitió el checkpoint final, su oráculo o tiempos válidos.");
            if(baseline)failed=(!compare(*baseline,report))||failed;else baseline=report;
        }catch(const std::exception& error){failed=true;std::cerr<<"D12 API FAILURE "<<smo_compute_api_name(target)<<": "<<error.what()<<'\n';}}
    return failed?1:0;
}catch(const std::exception& error){std::cerr<<"D12 failure: "<<error.what()<<'\n';return 1;}}
