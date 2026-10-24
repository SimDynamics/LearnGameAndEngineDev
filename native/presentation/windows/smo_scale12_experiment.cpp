#include "presentation/windows/smo_scale12_experiment.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <span>
#include <sstream>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using U=std::uint32_t;
void stopped(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Experimento de escala12 cancelado.");}
std::string source(std::string_view file){const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/file,512u*1024u);std::string text;text.reserve(raw.size());
    for(std::size_t i=0;i<raw.size();++i){if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;text.push_back(static_cast<char>(raw[i]));}return text;}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& values){return std::as_bytes(std::span(values));}
std::uint64_t hash(std::span<const std::byte> raw){std::uint64_t value=14695981039346656037ull;for(const auto byte:raw){value^=std::to_integer<unsigned char>(byte);value*=1099511628211ull;}return value;}
std::vector<SmoScale12Output> read(SmoCompute& gpu,SmoBuffer buffer,std::size_t count){const auto raw=gpu.read(buffer);if(raw.size()!=count*sizeof(SmoScale12Output))throw std::runtime_error("Salida de escala12 truncada.");std::vector<SmoScale12Output> result(count);std::memcpy(result.data(),raw.data(),raw.size());return result;}
void verify(std::span<const SmoScale12Output> actual,std::span<const SmoScale12Output> expected,std::string_view mode,std::uint64_t& checked){if(actual.size()!=expected.size())throw std::runtime_error("La escala12 omitió registros.");
    for(std::size_t i=0;i<actual.size();++i){if(actual[i]!=expected[i]){std::ostringstream error;error<<"Escala12 "<<mode<<" difiere del oráculo CPU en registro"<<i<<" diagnóstico GPU/CPU="<<actual[i].value.diagnostic<<'/'<<expected[i].value.diagnostic<<" coeficientes/etiqueta=";
            for(unsigned word=0;word<4u;++word)error<<actual[i].value.digits[word]<<'/'<<expected[i].value.digits[word]<<',';error<<" signo="<<actual[i].value.negative<<'/'<<expected[i].value.negative;throw std::runtime_error(error.str());}checked+=8u;}}
SmoF12 packet(U low,U middle,U high,int exponent,U sign=0,U diagnostic=0){return {{low,middle,high,smo_f12_tag|static_cast<U>(exponent+192)},sign,diagnostic};}
SmoScale12Input input(SmoF12 value,int exponent){SmoScale12Input result{};result.value=value;result.factor=smo_scale12_power(exponent);result.exponent_hint=exponent;return result;}
U next(U& state){state^=state<<13u;state^=state>>17u;state^=state<<5u;return state;}
void sample(SmoScale12Timing& time,double value){if(time.gpu_ms.size()<1048576u)time.gpu_ms.push_back(value);else ++time.discarded_samples;}
void summarize(SmoScale12Timing& time){time.valid=!time.discarded_samples&&!time.gpu_ms.empty()&&std::ranges::all_of(time.gpu_ms,[](double value){return std::isfinite(value)&&value>0;});if(time.valid){auto sorted=time.gpu_ms;std::ranges::sort(sorted);time.median_ms=sorted[sorted.size()/2u];}}
struct Work {std::array<SmoDispatch,3> calls{};std::array<SmoBuffer,3> outputs{};std::vector<SmoScale12Output> expected,binary_expected;std::vector<SmoScale12Input> integers;};
std::array<U,2> flags(SmoCompute& gpu,SmoBuffer buffer){const auto raw=gpu.read(buffer);if(raw.size()!=sizeof(std::array<U,2>))throw std::runtime_error("Las banderas del verificador escala12 están truncadas.");std::array<U,2> result{};std::memcpy(result.data(),raw.data(),raw.size());return result;}
void verifier_fault_test(SmoCompute& gpu,SmoKernel kernel,std::stop_token stop){std::array<U,8> actual{},expected{};const std::array<U,2> clear{};
    const auto output=gpu.buffer(sizeof(actual),std::as_bytes(std::span(actual))),reference=gpu.buffer(sizeof(expected),std::as_bytes(std::span(expected))),counter=gpu.buffer(sizeof(clear),std::as_bytes(std::span(clear)));
    SmoDispatch call{kernel,{0,output,reference,0,0,0,0,counter},{1,0},{1,1,1}};
    for(U part=0;part<8u;++part){stopped(stop);actual.fill(0);actual[part]=1u;gpu.write(output,std::as_bytes(std::span(actual)));gpu.write(counter,std::as_bytes(std::span(clear)));const U variant=part%2u;call.constants[1]=variant;
        (void)gpu.execute(std::span(&call,1),false);auto observed=flags(gpu,counter);if(observed[variant]!=1u||observed[1u-variant]!=0u)throw std::runtime_error("El verificador escala12 omitió un campo o mezcló variantes.");
        actual.fill(0);gpu.write(output,std::as_bytes(std::span(actual)));(void)gpu.execute(std::span(&call,1),false);observed=flags(gpu,counter);if(observed[variant]!=1u||observed[1u-variant]!=0u)throw std::runtime_error("El verificador escala12 olvidó una discrepancia anterior.");}
    gpu.write(counter,std::as_bytes(std::span(clear)));(void)gpu.execute(std::span(&call,1),false);if(flags(gpu,counter)!=clear)throw std::runtime_error("El verificador escala12 marcó diferencias inexistentes.");}
} //namespace

SmoF12 smo_scale12_power(std::int32_t exponent){if(exponent< -181||exponent>203)throw std::invalid_argument("12^k solo cabe como factor F12 para -181<=k<=203.");return packet(0,0,1728,exponent-11);}
bool smo_scale12_fast_eligible(const SmoScale12Input& value){return smo_f12_valid(value.value)&&smo_f12_valid(value.factor)&&value.factor.negative==0u&&value.factor.digits[0]==0u&&value.factor.digits[1]==0u&&value.factor.digits[2]==1728u;}
SmoScale12Output smo_scale12_reference(const SmoScale12Input& value){SmoScale12Output result{};result.value=smo_f12_mul(value.value,value.factor);return result;}
std::vector<SmoScale12Input> smo_scale12_validation_inputs(){std::vector<SmoScale12Input> result;const auto zero=smo_f12_zero();
    const std::array<SmoF12,9> values{zero,smo_f12_uint(1),smo_f12_parse("1.3"),smo_f12_parse("-1.3"),packet(0,0,1728,-192),packet(20735,20735,20735,192),packet(1,0,1728,-191,1),packet(20735,20735,20735,0,1),packet(1,2,1729,5,0,smo_d12_inexact)};
    for(const auto value:values)for(const auto exponent:{-181,-180,-1,0,1,180,203})result.push_back(input(value,exponent));
    auto unusual=input(smo_f12_parse("-1.6"),0);unusual.factor=smo_f12_negate(smo_scale12_power(2));result.push_back(unusual); //negative*negative keeps SMO's sign
    unusual.factor=smo_f12_parse("1.3");result.push_back(unusual);unusual.factor=zero;result.push_back(unusual);
    unusual=input(zero,203);unusual.value.negative=1u;result.push_back(unusual); //F12 has canonical+0; raw-0 is an encoding error
    unusual=input(smo_f12_uint(1),0);unusual.factor=zero;unusual.factor.negative=1u;result.push_back(unusual);
    unusual=input(smo_f12_uint(1),1);unusual.value.negative=2u;result.push_back(unusual);
    unusual=input(smo_f12_uint(1),1);unusual.factor.digits[3]^=0x1000u;result.push_back(unusual);
    unusual=input(smo_f12_uint(1),1);unusual.value.digits[0]=20736u;result.push_back(unusual);
    unusual=input(smo_f12_uint(1),1);unusual.value.diagnostic=smo_d12_domain;result.push_back(unusual);
    unusual=input(smo_f12_uint(1),1);unusual.factor.diagnostic=smo_d12_inexact;result.push_back(unusual);
    unusual=input(smo_f12_parse("-1.3"),1);unusual.exponent_hint=std::numeric_limits<std::int32_t>::min();result.push_back(unusual);
    unusual.exponent_hint=std::numeric_limits<std::int32_t>::max();result.push_back(unusual);return result;}
std::vector<SmoScale12Input> smo_scale12_inputs(U count){if(!count||count>1048576u)throw std::invalid_argument("La escala12 admite1..1048576 registros.");std::vector<SmoScale12Input> values;values.reserve(count);U random=0x513cae27u;
    for(U i=0;i<count;++i){if(i%4u==0u){const U native=i%16u==0u?0u:next(random)%100000u;const int exponent=static_cast<int>(next(random)%5u);auto value=input(smo_f12_uint(native),exponent);U factor=1u;for(int k=0;k<exponent;++k)factor*=12u;value.binary_value=native;value.binary_factor=factor;value.binary_eligible=1u;values.push_back(value);}
        else{const U low=next(random)%20736u,middle=next(random)%20736u,high=1728u+next(random)%(20736u-1728u);const int exponent=static_cast<int>(next(random)%121u)-60,shift=static_cast<int>(next(random)%97u)-48;
            values.push_back(input(packet(low,middle,high,exponent,next(random)&1u,i%17u==0u?smo_d12_inexact:0u),shift));}}
    return values;}
SmoKernelSource smo_scale12_experiment_kernel(unsigned stage){if(stage>=smo_scale12_experiment_stages)throw std::invalid_argument("Etapa escala12 inexistente.");const auto hlsl=source("smo_shader_hlsl.inc"),glsl=source("smo_shader_glsl.inc"),arithmetic=source("smo_float12.inc"),body=source("smo_scale12_experiment.inc");const auto define="#define SMO_SCALE12_STAGE "+std::to_string(stage)+"\n";
    SmoKernelSource kernel;kernel.name="scale12_stage"+std::to_string(stage);kernel.hlsl=define+hlsl+arithmetic+body;kernel.glsl="#version 430\n"+define+glsl+arithmetic+body;
#ifdef LGAED_SMO_SCALE12_SHADER_ROOT
    const auto root=std::filesystem::path(LGAED_SMO_SCALE12_SHADER_ROOT);const auto raw_manifest=state::read_bytes(root/"source.sha256",256);std::string manifest(raw_manifest.begin(),raw_manifest.end());while(!manifest.empty()&&(manifest.back()=='\r'||manifest.back()=='\n'||manifest.back()==' '))manifest.pop_back();if(manifest!=state::sha256(glsl+arithmetic+body))throw std::runtime_error("Los shaders escala12 cambiaron: recompila el preset existente.");
    const auto raw=state::read_bytes(root/"shaders"/(kernel.name+".spv"),4u*1024u*1024u);if(raw.size()<20u||raw.size()%4u)throw std::runtime_error("SPIR-V escala12 truncado.");kernel.spirv.resize(raw.size()/4u);std::memcpy(kernel.spirv.data(),raw.data(),raw.size());if(kernel.spirv.front()!=0x07230203u)throw std::runtime_error("SPIR-V escala12 inválido.");
    for(std::size_t at=5;at<kernel.spirv.size();){const auto words=kernel.spirv[at]>>16u,opcode=kernel.spirv[at]&0xffffu;if(!words||words>kernel.spirv.size()-at||opcode==22u)throw std::runtime_error("El shader escala12 contiene instrucciones truncadas o tipos flotantes.");at+=words;}
#endif
    std::filesystem::path dxbc;
#ifdef LGAED_SMO_SCALE12_HLSL_SHADER_ROOT
    dxbc=LGAED_SMO_SCALE12_HLSL_SHADER_ROOT;
#endif
    load_smo_dxbc_assets(kernel,dxbc,hlsl+arithmetic+body,true);return kernel;}

SmoScale12Report run_smo_scale12_experiment(const SmoScale12Options& options,std::stop_token stop){if(options.items==0u||options.items>1048576u||options.rounds==0u||options.rounds>100u||options.warmups>20u||(options.sustained_seconds!=0u&&options.sustained_seconds!=60u))throw std::invalid_argument("Opciones escala12 fuera de sus límites.");stopped(stop);
    const auto inputs=smo_scale12_inputs(options.items),validation=smo_scale12_validation_inputs();Work work;work.expected.reserve(inputs.size());std::vector<SmoScale12Output> expected_validation;expected_validation.reserve(validation.size());
    SmoScale12Report report;report.items=options.items;report.validation_cases=static_cast<U>(validation.size());report.input_hash=hash(bytes(inputs));
    // CPU reference arithmetic and integer-domain proofs precede every GPU
    // timing. Neither parsing nor conversion belongs to a measured dispatch.
    for(const auto& value:validation)expected_validation.push_back(smo_scale12_reference(value));
    for(const auto& value:inputs){work.expected.push_back(smo_scale12_reference(value));if(smo_scale12_fast_eligible(value))++report.fast_cases;
        if(!smo_f12_valid(work.expected.back().value))throw std::logic_error("El conjunto medido no debe producir errores de rango.");
        if(value.binary_eligible){const auto product=static_cast<std::uint64_t>(value.binary_value)*value.binary_factor;if(product>std::numeric_limits<U>::max()||smo_f12_uint(static_cast<U>(product))!=work.expected.back().value)throw std::logic_error("El subconjunto entero no conserva el resultado exacto F12.");
            work.integers.push_back(value);SmoScale12Output integer{};integer.value={};integer.value.digits[0]=static_cast<U>(product);work.binary_expected.push_back(integer);}}
    report.binary_items=static_cast<U>(work.integers.size());auto gpu=create_smo_compute(options.api,options.device,stop);report.device=gpu->info();std::array<SmoKernel,smo_scale12_experiment_stages> kernels{};for(unsigned stage=0;stage<kernels.size();++stage){stopped(stop);kernels[stage]=gpu->kernel(smo_scale12_experiment_kernel(stage));}
    verifier_fault_test(*gpu,kernels[3],stop);report.verifier_fault_cases=8u;
    const auto validation_buffer=gpu->buffer(validation.size()*sizeof(SmoScale12Input),bytes(validation)),validation_output=gpu->buffer(validation.size()*sizeof(SmoScale12Output));
    for(unsigned stage=0;stage<2u;++stage){SmoDispatch call{kernels[stage],{validation_buffer,validation_output},{static_cast<U>(validation.size())},{(static_cast<U>(validation.size())+63u)/64u,1,1}};(void)gpu->execute(std::span(&call,1),false);const auto actual=read(*gpu,validation_output,validation.size());verify(actual,expected_validation,stage?"specialized edge cases":"general edge cases",report.verified_words);}
    const auto input_buffer=gpu->buffer(inputs.size()*sizeof(SmoScale12Input),bytes(inputs));for(unsigned stage=0;stage<2u;++stage){work.outputs[stage]=gpu->buffer(inputs.size()*sizeof(SmoScale12Output));work.calls[stage]={kernels[stage],{input_buffer,work.outputs[stage]},{options.items},{(options.items+63u)/64u,1,1}};}
    if(!work.integers.empty()){const auto integers_buffer=gpu->buffer(work.integers.size()*sizeof(SmoScale12Input),bytes(work.integers));work.outputs[2]=gpu->buffer(work.integers.size()*sizeof(SmoScale12Output));work.calls[2]={kernels[2],{integers_buffer,work.outputs[2]},{report.binary_items},{(report.binary_items+63u)/64u,1,1}};}
    const auto check_work=[&](unsigned stage,bool final=false){const auto count=stage==2u?work.integers.size():inputs.size();if(!count)return;const auto actual=read(*gpu,work.outputs[stage],count);verify(actual,stage==2u?std::span<const SmoScale12Output>(work.binary_expected):std::span<const SmoScale12Output>(work.expected),stage==0u?"general":stage==1u?"exponent":"native integer",report.verified_words);if(final)(stage==0u?report.general:stage==1u?report.exponent:report.binary).output_hash=hash(std::as_bytes(std::span(actual)));};
    report.resident_verification=options.sustained_seconds!=0u;std::array<SmoDispatch,2> verifier{};SmoBuffer sticky=0;
    if(report.resident_verification){const auto expected_buffer=gpu->buffer(work.expected.size()*sizeof(SmoScale12Output),bytes(work.expected));const std::array<U,2> clear{};sticky=gpu->buffer(sizeof(clear),std::as_bytes(std::span(clear)));
        for(U stage=0;stage<2u;++stage)verifier[stage]={kernels[3],{0,work.outputs[stage],expected_buffer,0,0,0,0,sticky},{options.items,stage},{(options.items+63u)/64u,1,1}};}
    const auto check_sticky=[&]{report.gpu_mismatch_flags=flags(*gpu,sticky);++report.verifier_polls;if(report.gpu_mismatch_flags[0]||report.gpu_mismatch_flags[1])throw std::runtime_error("El verificador GPU escala12 conservó una discrepancia de alguna ronda: general="+std::to_string(report.gpu_mismatch_flags[0])+", exponente="+std::to_string(report.gpu_mismatch_flags[1])+". No se acreditan sus tiempos.");};
    // Real GPU correctness before timing, including the separate native range.
    for(unsigned stage=0;stage<3u;++stage){if(stage==2u&&work.integers.empty())continue;(void)gpu->execute(std::span(&work.calls[stage],1),false);check_work(stage);}
    for(U warmup=0;warmup<options.warmups;++warmup)for(unsigned stage=0;stage<3u;++stage){stopped(stop);if(stage==2u&&work.integers.empty())continue;(void)gpu->execute(std::span(&work.calls[stage],1),false);}
    const auto begin=std::chrono::steady_clock::now();do{stopped(stop);for(unsigned step=0;step<2u;++step){const unsigned stage=report.rounds_actual%2u?1u-step:step;sample(stage?report.exponent:report.general,gpu->execute(std::span(&work.calls[stage],1),true));}
        // The sustained verifier checks all8 words on the GPU before either
        // output is reused. Its two dispatches are OUTSIDE producer queries,
        // but included in wall duration. Sticky atomics occur on errors only.
        if(report.resident_verification){(void)gpu->execute(verifier,false);report.gpu_verified_words+=static_cast<std::uint64_t>(options.items)*8u*2u;}
        else{check_work(0);check_work(1);}++report.rounds_actual;
        if(report.resident_verification&&report.rounds_actual%128u==0u)check_sticky();report.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    }while(report.rounds_actual<options.rounds||report.elapsed_seconds<options.sustained_seconds);
    if(report.resident_verification)check_sticky();
    // Fixed uint32 control has a different, explicitly smaller domain/count.
    // It is reported separately; no cross-precision performance ratio is made.
    if(!work.integers.empty())for(U round=0;round<options.rounds;++round){stopped(stop);sample(report.binary,gpu->execute(std::span(&work.calls[2],1),true));}
    for(unsigned stage=0;stage<3u;++stage){if(stage==2u&&work.integers.empty())continue;check_work(stage,true);}
    summarize(report.general);summarize(report.exponent);summarize(report.binary);report.equivalent=true;if(report.general.valid&&report.exponent.valid)report.general_over_exponent=report.general.median_ms/report.exponent.median_ms;return report;}
} //namespace lgaed::presentation
