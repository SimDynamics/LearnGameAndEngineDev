#include "presentation/windows/smo_duodecimal_gpu.hpp"
#include "presentation/windows/smo_float12.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "core/smo.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <bit>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using Clock=std::chrono::steady_clock;
void cancelled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Experimento duodecimal cancelado.");}
std::string source(std::string_view name){const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,512*1024);std::string result;
    for(std::size_t i=0;i<raw.size();++i){if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;result+=static_cast<char>(raw[i]);}return result;}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& values){return std::as_bytes(std::span(values));}
template<class T>std::vector<T> read(SmoCompute& gpu,SmoBuffer buffer,std::size_t count){const auto raw=gpu.read(buffer);if(raw.size()!=sizeof(T)*count)throw std::runtime_error("Lectura duodecimal incompleta.");std::vector<T> result(count);std::memcpy(result.data(),raw.data(),raw.size());return result;}
void require_integer_shader(const SmoKernelSource& shader){
    // SPIR-V's OpTypeFloat is opcode22. This inspectable compiled path must
    // contain none, including in helpers. The other API sources are the SAME
    // uint-only body, compiled by their native drivers without a CPU fallback.
    if(!shader.spirv.empty()&&shader.spirv.size()<5)throw std::runtime_error("Cabecera SPIR-V duodecimal truncada.");
    for(std::size_t i=5;i<shader.spirv.size();){const auto instruction=shader.spirv[i];const auto words=instruction>>16;
        if(!words||words>shader.spirv.size()-i)throw std::runtime_error("SPIR-V duodecimal truncado.");
        if((instruction&0xffffu)==22u)throw std::runtime_error("El shader duodecimal contiene un tipo flotante.");i+=words;}
}
double percentile(std::vector<double> values,double fraction){if(values.empty()||std::ranges::any_of(values,[](auto v){return !(v>0)||!std::isfinite(v);}))return std::numeric_limits<double>::quiet_NaN();std::ranges::sort(values);return values[static_cast<std::size_t>((values.size()-1)*fraction)];}
long double quantity(SmoD12 n,bool floating12){if(floating12)return smo_f12_comparison_value(n);std::uint64_t raw=0;for(int i=3;i>=0;--i)raw=raw*smo_d12_radix+n.digits[static_cast<std::size_t>(i)];return static_cast<long double>(raw)/smo_d12_scale*(n.negative?-1:1);}
bool control_close(SmoD12 reference,SmoD12 control,bool floating12){const auto actual=std::bit_cast<float>(control.digits[0]);if((reference.diagnostic&smo_d12_fatal_mask)||(control.diagnostic&smo_d12_fatal_mask)||!std::isfinite(actual))return false;const auto expected=quantity(reference,floating12);return std::abs(expected-actual)<=.0002L*std::max(1.L,std::abs(expected));}
bool control_body(const SmoD12SceneBody& reference,const SmoD12SceneBody& control,bool floating12){for(unsigned k=0;k<3;++k)if(!control_close(reference.position[k],control.position[k],floating12)||!control_close(reference.velocity[k],control.velocity[k],floating12))return false;return control_close(reference.radius,control.radius,floating12)&&reference.contacts==control.contacts;}
bool control_pixel(const SmoD12ScenePixel& reference,const SmoD12ScenePixel& control,bool floating12){for(unsigned k=0;k<3;++k)if(!control_close(reference.color[k],control.color[k],floating12))return false;return control_close(reference.distance,control.distance,floating12)&&reference.hit==control.hit&&reference.shadow==control.shadow&&reference.tests==control.tests;}
bool control_encoding(SmoD12 value){const auto bits=value.digits[0],exponent=(bits>>23u)&255u;
    return !value.digits[1]&&!value.digits[2]&&!value.digits[3]&&value.negative==(bits>>31u)&&exponent!=255u&&
        (exponent||!(bits&0x7fffffu))&&((bits&0x7fffffffu)||!value.negative)&&!(value.diagnostic&~(smo_d12_fatal_mask|smo_d12_inexact));}
bool control_encoding(const SmoD12SceneBody& body,std::uint32_t body_count){std::uint32_t combined=body.radius.diagnostic;
    if(!control_encoding(body.radius)||body.contacts>body_count||std::ranges::any_of(body.reserved,[](auto word){return word!=0;}))return false;
    for(unsigned k=0;k<3;++k){if(!control_encoding(body.position[k])||!control_encoding(body.velocity[k]))return false;combined|=body.position[k].diagnostic|body.velocity[k].diagnostic;}
    return (body.diagnostic&combined)==combined&&!(body.diagnostic&~(smo_d12_fatal_mask|smo_d12_inexact));}
std::uint32_t control_channel(float component){const float scaled=std::clamp(component,0.f,1.f)*255.f;auto whole=static_cast<std::uint32_t>(scaled);const float fraction=scaled-static_cast<float>(whole);
    if(fraction>.5f||(fraction==.5f&&(whole&1u)))++whole;return std::min(whole,255u);}
bool control_encoding(const SmoD12ScenePixel& pixel,std::uint32_t body_count){std::uint32_t combined=pixel.distance.diagnostic;
    if(!control_encoding(pixel.distance)||pixel.shadow>1u||(pixel.hit!=std::numeric_limits<std::uint32_t>::max()&&pixel.hit>=body_count)||
        pixel.tests>2u*body_count-1u||!pixel.tests||std::ranges::any_of(pixel.reserved,[](auto word){return word!=0;}))return false;
    for(const auto& n:pixel.color){if(!control_encoding(n))return false;combined|=n.diagnostic;}
    if((pixel.diagnostic&combined)!=combined||(pixel.diagnostic&~(smo_d12_fatal_mask|smo_d12_inexact)))return false;
    if(pixel.diagnostic&smo_d12_fatal_mask)return pixel.bgra==0xffff00ffu;
    // Validate display packing against this control's OWN native RGB, not the
    // quantized D12 image: a byte boundary can differ despite numeric closeness.
    const auto expected=0xff000000u|(control_channel(std::bit_cast<float>(pixel.color[0].digits[0]))<<16u)|
        (control_channel(std::bit_cast<float>(pixel.color[1].digits[0]))<<8u)|control_channel(std::bit_cast<float>(pixel.color[2].digits[0]));
    return pixel.bgra==expected;
}
// Decode the separately labelled binary32 control into the CPU oracle's
// domain. Integer rounding preserves the exact binary input, with no decimal
// text conversion and no change to either GPU representation or tolerance.
SmoD12 control_reference(SmoD12 value,bool floating12){
    if(floating12){auto result=smo_f12_from_binary32_bits(value.digits[0]);result.diagnostic|=value.diagnostic;
        if(value.digits[1]||value.digits[2]||value.digits[3]||value.negative!=(value.digits[0]>>31u)||
            (!(value.digits[0]&0x7fffffffu)&&value.negative))result.diagnostic|=smo_d12_encoding;
        return result;}
    const auto bits=value.digits[0],exponent=(bits>>23u)&255u;
    SmoD12 result;result.diagnostic=value.diagnostic;result.negative=bits>>31u;
    if(exponent==255u||value.digits[1]||value.digits[2]||value.digits[3]||value.negative!=result.negative||((bits&0x7fffffffu)==0u&&result.negative)){
        result.diagnostic|=smo_d12_encoding;return result;}
    const auto significand=(bits&0x7fffffu)|(exponent?0x800000u:0u);
    const auto numerator=static_cast<std::uint64_t>(significand)*smo_d12_scale;
    const int power=(exponent?static_cast<int>(exponent)-127:-126)-23;
    std::uint64_t raw=0;
    if(power>=0){if(power>=64||numerator>(smo_d12_max_raw>>power)){result.diagnostic|=smo_d12_overflow;return result;}raw=numerator<<power;}
    else{const auto shift=static_cast<unsigned>(-power);if(shift>=64){if(numerator)result.diagnostic|=smo_d12_underflow;}
        else{raw=numerator>>shift;const auto remainder=numerator&((std::uint64_t{1}<<shift)-1),half=std::uint64_t{1}<<(shift-1);
            if(remainder){result.diagnostic|=smo_d12_inexact;if(remainder>half||(remainder==half&&(raw&1)))++raw;}
            if(numerator&&!raw)result.diagnostic|=smo_d12_underflow;}}
    if(raw>smo_d12_max_raw){result.diagnostic|=smo_d12_overflow;return result;}
    for(auto& digit:result.digits){digit=static_cast<std::uint32_t>(raw%smo_d12_radix);raw/=smo_d12_radix;}
    if(std::ranges::all_of(result.digits,[](auto digit){return digit==0;}))result.negative=0;
    return result;
}
std::vector<SmoD12SceneBody> control_reference(std::span<const SmoD12SceneBody> values,bool floating12){
    std::vector<SmoD12SceneBody> result(values.begin(),values.end());for(auto& body:result){for(auto& n:body.position)n=control_reference(n,floating12);for(auto& n:body.velocity)n=control_reference(n,floating12);body.radius=control_reference(body.radius,floating12);}return result;
}
struct GpuCase {std::uint32_t operation,profile;SmoD12 left,right;};static_assert(sizeof(GpuCase)==56);
SmoD12Checkpoint capture(SmoCompute& gpu,SmoBuffer bodies,SmoBuffer pixels,const SmoD12SceneOptions& options,std::uint32_t frame){return {frame,read<SmoD12SceneBody>(gpu,bodies,options.bodies),read<SmoD12ScenePixel>(gpu,pixels,static_cast<std::size_t>(options.width)*options.height)};}
SmoSceneProgress image(const SmoD12Checkpoint& checkpoint,const SmoD12SceneOptions& options,std::string api,double seconds){SmoSceneProgress result;
    result.api=std::move(api);result.phase=options.fp32_control?"Control FP32 nativo · mismo ABI y fórmulas; no es el control compacto óptimo":options.floating12?"Base 12 adaptable · 12 cifras significativas · rayos y física":"Base 12 fija · potencias, rayos, gravedad y colisiones";result.elapsed=seconds;result.frames=checkpoint.frame;result.width=options.width;result.height=options.height;
    for(const auto& pixel:checkpoint.pixels)result.preview_bgra.push_back(pixel.bgra);return result;}
}
SmoKernelSource smo_duodecimal_rules_kernel(unsigned operation,bool floating12){if(operation>10)throw std::invalid_argument("Operación de shader duodecimal inválida.");const auto glsl=source("smo_shader_glsl.inc"),body=source(floating12?"smo_float12.inc":"smo_duodecimal.inc")+source("smo_duodecimal_rules.inc");SmoKernelSource result;const auto define="#define D12_ONLY_OPERATION "+std::to_string(operation)+"\n";
    result.name=std::string(floating12?"float12_rules":"d12_rules")+std::to_string(operation);result.hlsl=define+source("smo_shader_hlsl.inc")+body;result.glsl="#version 430\n"+define+glsl+body;
std::filesystem::path root;
    if(floating12){
#ifdef LGAED_SMO_FLOAT12_RULE_SHADER_ROOT
        root=std::filesystem::path(LGAED_SMO_FLOAT12_RULE_SHADER_ROOT);
#endif
    }else{
#ifdef LGAED_SMO_D12_RULE_SHADER_ROOT
        root=std::filesystem::path(LGAED_SMO_D12_RULE_SHADER_ROOT);
#endif
    }
    if(!root.empty()){const auto raw_hash=state::read_bytes(root/"source.sha256",128);std::string hash(raw_hash.begin(),raw_hash.end());while(!hash.empty()&&(hash.back()=='\n'||hash.back()=='\r'))hash.pop_back();
    if(hash!=state::sha256(glsl+body))throw std::runtime_error("Las reglas base12 cambiaron: recompila el preset existente.");const auto raw=state::read_bytes(root/"shaders"/(result.name+".spv"),4*1024*1024);
    if(raw.empty()||raw.size()%4)throw std::runtime_error("SPIR-V duodecimal inválido.");result.spirv.resize(raw.size()/4);std::memcpy(result.spirv.data(),raw.data(),raw.size());
    }
    if(floating12){
#ifdef LGAED_SMO_FLOAT12_RULE_HLSL_SHADER_ROOT
        load_smo_dxbc_assets(result,std::filesystem::path(LGAED_SMO_FLOAT12_RULE_HLSL_SHADER_ROOT),source("smo_shader_hlsl.inc")+body,true);
#endif
    }
    require_integer_shader(result);return result;
}
std::vector<SmoD12Case> smo_duodecimal_cases(bool exhaustive,bool floating12){std::vector<SmoD12Case> result;const auto parse=floating12?smo_f12_parse:smo_d12_parse;
    const auto append=[&](std::uint32_t op,std::string_view a,std::string_view b,std::uint32_t profile){result.push_back({std::string(a)+" op"+std::to_string(op)+" "+std::string(b)+" perfil "+std::to_string(profile),op,profile,parse(a),parse(b)});};
    const std::array<std::string_view,7> numbers{"-3","-.6","0",".6","1","2","10"};
    for(std::uint32_t profile=0;profile<64;++profile){if((profile&12)==12||(!exhaustive&&profile!=0&&profile!=1&&profile!=2&&profile!=8&&profile!=16&&profile!=32))continue;
        for(std::uint32_t op=0;op<4;++op)for(const auto a:numbers)for(const auto b:numbers)append(op,a,b,profile);
        for(std::uint32_t op=4;op<=5;++op)for(const auto a:{".6","1","2","10"})for(const auto n:{"-54","-3","-2","-1","0","1","2","3","54"})append(op,a,n,profile);
    }
    if(exhaustive)for(std::uint32_t op=4;op<=5;++op)for(int exponent=-64;exponent<=64;++exponent)for(const auto a:{"2","10"})append(op,a,smo::format_number(exponent),0);
    for(const auto a:{"0",".01",".1","1","2","4","#","100","-1"})append(9,a,"0",0);
    append(2,"~~~~~~~~.~~~~~~~~","2",0);append(3,"~~~~~~~~.~~~~~~~~",".01",0);append(2,".00000001",".00000001",0);
    for(const auto op:{0u,2u,3u,4u,5u,9u}){append(op,"1","1",128);append(op,"1","1",12);}
    if(floating12){
        const auto minimum=std::string("0.")+std::string(180,'0')+"1";
        const auto maximum=std::string(12,'~')+std::string(192,'0');
        append(2,minimum,".1",0);append(2,maximum,"10",0);
        append(9,minimum,"0",0);append(4,minimum,"2",0);
        append(5,std::string("0.")+std::string(99,'0')+"1","4",0);
        append(0,std::string(12,'~'),".6",0);
        for(const auto& small:{std::string(".06"),std::string("-.06"),std::string("-.060000000001"),std::string("-.006")})append(0,"100000000000",small,0);
        append(2,"-0.0001971#","0.00002345",0);
        // Exercise full-width coefficients and the COMPLETE exponent range,
        // not only the short educational literals used by the rule matrix.
        // Deterministic integer packets are shared by every backend; the CPU
        // oracle remains the independent uint128 implementation.
        std::uint32_t seed=0x12c0ffeeu;
        const auto random=[&](){seed^=seed<<13u;seed^=seed>>17u;seed^=seed<<5u;return seed;};
        const auto packet=[&](){auto value=smo_f12_zero();const auto high=random(),low=random();
            auto coefficient=smo_f12_min_coefficient+
                ((static_cast<std::uint64_t>(high)<<32u)|low)%
                (smo_f12_max_coefficient-smo_f12_min_coefficient+1u);
            for(unsigned limb=0;limb<3;++limb){value.digits[limb]=static_cast<std::uint32_t>(coefficient%smo_f12_radix);coefficient/=smo_f12_radix;}
            value.digits[3]=smo_f12_tag|(random()%385u);value.negative=random()&1u;return value;};
        for(unsigned i=0;i<(exhaustive?128u:16u);++i){const auto left=packet(),right=packet();auto profile=random()&63u;
            if((profile&12u)==12u)profile&=~4u;
            for(const auto operation:{0u,1u,2u,3u,9u})result.push_back({"F12 coeficiente/escala completos "+std::to_string(i),operation,profile,left,right});}
        // Test-fixture slots6/7 ONLY: their raw left.words are constructor
        // arguments, not F12 values and NOT extra SMO arithmetic operations.
        // The general CPU/GPU evaluators must continue to reject opcodes6/7.
        const auto integer_fixture=[&](std::uint32_t integer){SmoD12 payload;payload.digits[0]=integer;
            result.push_back({"Fixture constructor F12 uint "+std::to_string(integer),6u,0u,payload,smo_f12_zero()});};
        const auto literal_fixture=[&](std::uint32_t whole,std::uint32_t fraction,std::uint32_t count,std::uint32_t negative){
            SmoD12 payload;payload.digits={whole,fraction,count,negative};
            result.push_back({"Fixture constructor F12 literal W="+std::to_string(whole)+" F="+std::to_string(fraction)+
                " count="+std::to_string(count)+" sign="+std::to_string(negative),7u,0u,payload,smo_f12_zero()});};
        for(const auto integer:{0u,1u,12u,255u,20735u,20736u,429981695u,429981696u,4294967295u})integer_fixture(integer);
        for(unsigned i=0;i<(exhaustive?64u:16u);++i)integer_fixture(random());
        const auto packed=[&](unsigned count){std::uint32_t value=0;for(unsigned digit=0;digit<count;++digit)value|=(random()%12u)<<(digit*4u);return value;};
        for(unsigned count=0;count<=8;++count){
            const auto all_eleven=count==8?0xbbbbbbbbu:0xbbbbbbbbu&((1u<<(count*4u))-1u);
            for(const auto negative:{0u,1u}){
                literal_fixture(0u,0u,count,negative);literal_fixture(1u,all_eleven,count,negative);
                literal_fixture(0x10u,packed(count),count,negative);literal_fixture(0xbbbbbbbbu,all_eleven,count,negative);
            }
        }
        // With an eight-digit integral part only four fractional digits fit
        // the p12 significand. The fifth fraction digit is the RNE guard.
        for(const auto fraction:{0x00006000u,0x00016000u,0xbbbb6000u,0x00006001u,0x00005bbbu})
            for(const auto negative:{0u,1u})literal_fixture(0xbbbbbbbbu,fraction,8u,negative);
        for(unsigned i=0;i<(exhaustive?64u:16u);++i){const auto count=random()%9u;
            literal_fixture(packed(8),packed(count),count,random()&1u);}
        literal_fixture(0xcu,0u,0u,0u);literal_fixture(0xffffffffu,0u,0u,0u);
        literal_fixture(0u,0xcu,1u,0u);literal_fixture(0u,0xffffffffu,8u,0u);
        literal_fixture(0u,1u,0u,0u);literal_fixture(0u,0x10u,1u,0u);
        literal_fixture(0u,0u,9u,0u);literal_fixture(0u,0u,0xffffffffu,0u);
        literal_fixture(1u,0u,0u,2u);literal_fixture(1u,0u,0u,0xffffffffu);
    }
    append(99,"1","1",0);auto invalid=result.front();invalid.name="limb radix12 fuera de rango";invalid.left.digits[0]=smo_d12_radix;result.push_back(invalid);
    invalid=result.front();invalid.name="signo fuera de dominio";invalid.left.negative=2;result.push_back(invalid);if(floating12){
        invalid=result.front();invalid.name="paquete fijo sin etiqueta F12";invalid.left.digits[3]=0;result.push_back(invalid);
        invalid=result.front();invalid.name="cero F12 con exponente no canónico";invalid.left=smo_f12_zero();invalid.left.digits[3]+=1;result.push_back(invalid);
        invalid=result.front();invalid.name="coeficiente F12 no normalizado";invalid.left=smo_f12_zero();invalid.left.digits[0]=1;result.push_back(invalid);
    }return result;
}
SmoD12Validation validate_smo_duodecimal(SmoComputeApi api,const SmoComputeOptions& device,bool exhaustive,std::stop_token stop,std::function<void(unsigned,bool)> preparation,bool floating12){cancelled(stop);auto gpu=create_smo_compute(api,device,stop);
    const auto cases=smo_duodecimal_cases(exhaustive,floating12);std::vector<SmoD12Result> expected;std::array<std::vector<std::size_t>,11> groups;std::set<std::uint32_t> profiles;SmoD12Validation report;report.floating12=floating12;report.device=gpu->info();report.cases=cases.size()*2;report.outputs.resize(cases.size()*2);report.identity=state::sha256(source("smo_shader_glsl.inc")+source(floating12?"smo_float12.inc":"smo_duodecimal.inc")+source("smo_duodecimal_rules.inc"));
    for(std::size_t i=0;i<cases.size();++i){const auto& test=cases[i];cancelled(stop);
        if(floating12&&(test.operation==6u||test.operation==7u)){
            // Independent CPU constructor oracle for TEST transport slots;
            // never reinterpret raw constructor words as a numeric packet.
            SmoD12Result fixture;fixture.primary=test.operation==6u?smo_f12_uint(test.left.digits[0]):
                smo_f12_literal(test.left.digits[0],test.left.digits[1],test.left.digits[2],test.left.digits[3]);
            fixture.alternative=smo_f12_zero();fixture.alternative.diagnostic=smo_d12_domain;
            fixture.flags=smo_f12_valid(fixture.primary)?smo_rule_has_primary:0u;expected.push_back(fixture);
        }else expected.push_back(floating12?smo_f12_evaluate(test.operation,test.left,test.right,test.profile):smo_d12_evaluate(test.operation,test.left,test.right,test.profile));
        if(test.operation<10)++report.coverage[test.operation];if(!(test.profile&~63u)&&(test.profile&12u)!=12u)profiles.insert(test.profile);groups[test.operation<10?test.operation:10].push_back(i);}
    report.profiles.assign(profiles.begin(),profiles.end());
    for(unsigned operation=0;operation<groups.size();++operation){if(groups[operation].empty())continue;cancelled(stop);if(preparation)preparation(operation,false);const auto began=Clock::now();const auto kernel=gpu->kernel(smo_duodecimal_rules_kernel(operation,floating12));report.shader_preparation_ms[operation]=std::chrono::duration<double,std::milli>(Clock::now()-began).count();if(preparation)preparation(operation,true);std::vector<GpuCase> inputs;
        for(const auto i:groups[operation])inputs.push_back({cases[i].operation,cases[i].profile,cases[i].left,cases[i].right});const auto input=gpu->buffer(inputs.size()*sizeof(GpuCase),bytes(inputs)),output=gpu->buffer(inputs.size()*sizeof(SmoD12Result));SmoDispatch call{kernel,{input,output},{},{(static_cast<std::uint32_t>(inputs.size())+63)/64,1,1}};call.constants[0]=static_cast<std::uint32_t>(inputs.size());
        for(unsigned variant=0;variant<2;++variant){call.constants[1]=variant;(void)gpu->execute(std::span(&call,1),false);const auto observed=read<SmoD12Result>(*gpu,output,inputs.size());
            for(std::size_t j=0;j<inputs.size();++j){const auto i=groups[operation][j];const auto& actual=observed[j];const auto& reference=expected[i];report.outputs[variant*cases.size()+i]=actual;
                if(actual.primary!=reference.primary||actual.alternative!=reference.alternative||actual.flags!=reference.flags)report.failures.push_back({cases[i].name+(variant?" / helper especializado":" / evaluador general"),"Diferencia exacta de cifras, signo, diagnóstico o alternativa",cases[i].operation,cases[i].profile,reference,actual});}}
    }
    return report;
}
SmoD12SceneReport run_smo_duodecimal_scene(const SmoD12SceneOptions& options,std::function<void(SmoSceneProgress)> progress,std::stop_token stop){cancelled(stop);
    if(options.bodies<2||options.bodies>16||!options.width||!options.height||static_cast<std::uint64_t>(options.width)*options.height>2304||!std::isfinite(options.seconds)||!(options.seconds>0)||options.seconds>120||!std::isfinite(options.warmup_seconds)||options.warmup_seconds<0||options.warmup_seconds>5||options.minimum_frames<2||options.minimum_frames>256)throw std::invalid_argument("Carga duodecimal fuera del presupuesto declarado.");
    auto gpu=create_smo_compute(options.api,options.device,stop);SmoD12SceneReport report;report.device=gpu->info();report.options=options;report.scene_identity=state::sha256(source("smo_duodecimal_scene.inc"));
    if(report.device.software&&options.seconds>1)throw std::invalid_argument("WARP solo acredita corrección breve, no estrés físico.");std::array<SmoKernel,5> kernels{};
    for(unsigned stage=0;stage<5;++stage){cancelled(stop);if(progress)progress({report.device.api,"Preparando etapa "+std::to_string(stage),0,0,0,0,{}});const auto shader=smo_duodecimal_scene_kernel(stage,options.fp32_control,options.floating12);if(!options.fp32_control)require_integer_shader(shader);kernels[stage]=gpu->kernel(shader);cancelled(stop);if(progress)progress({report.device.api,"Etapa preparada "+std::to_string(stage),0,0,0,0,{}});if(!stage)report.identity=state::sha256(shader.hlsl+shader.glsl);}
    auto current=gpu->buffer(options.bodies*sizeof(SmoD12SceneBody));auto integrated=gpu->buffer(options.bodies*sizeof(SmoD12SceneBody));auto next=gpu->buffer(options.bodies*sizeof(SmoD12SceneBody));const auto pixels=gpu->buffer(static_cast<std::size_t>(options.width)*options.height*sizeof(SmoD12ScenePixel));const SmoD12SceneWorkload work{options.bodies,options.width,options.height};
    const auto initialize=[&]{SmoDispatch call{kernels[0],{0,current},smo_duodecimal_scene_constants(work),{(options.bodies+63)/64,1,1}};(void)gpu->execute(std::span(&call,1),false);};initialize();
    const auto initial=read<SmoD12SceneBody>(*gpu,current,options.bodies);const auto expected_initial=smo_duodecimal_scene_initial(work,options.floating12);bool initial_valid=initial==expected_initial;
    if(options.fp32_control){initial_valid=true;for(std::size_t i=0;i<initial.size();++i)initial_valid=control_body(expected_initial[i],initial[i],options.floating12)&&initial_valid;}
    if(options.fp32_control)for(std::size_t i=0;i<initial.size();++i)if(!control_encoding(initial[i],options.bodies))report.failures.push_back("Defecto del control FP32: codificación inicial inválida en cuerpo "+std::to_string(i));
    if(!initial_valid)report.failures.push_back(options.fp32_control?"Sin equivalencia numérica (2e-4) entre la inicialización FP32 y las cantidades D12; no es por sí sola un defecto de API.":"La inicialización GPU no coincide con las cantidades duodecimales de referencia.");
    const auto frame=[&](std::uint32_t index,bool timed){const auto constants=smo_duodecimal_scene_constants(work,index);const auto body_groups=(options.bodies+63)/64,pixel_groups=(options.width*options.height+63)/64;
        const std::array<SmoDispatch,4> calls{{{kernels[1],{current,integrated},constants,{body_groups,1,1}},{kernels[2],{integrated,next},constants,{body_groups,1,1}},{kernels[3],{next,pixels},constants,{pixel_groups,1,1}},{kernels[4],{next,pixels},constants,{pixel_groups,1,1}}}};const auto ms=gpu->execute(calls,timed);std::swap(current,next);return ms;};
    const auto warm_begin=Clock::now();while(std::chrono::duration<double>(Clock::now()-warm_begin).count()<options.warmup_seconds){cancelled(stop);(void)frame(1,false);}initialize();
    const auto began=Clock::now();double last_preview=-5;std::vector<std::uint32_t> previous_image;auto previous_bodies=initial;SmoD12Checkpoint latest;bool saved_fatal=false;
    do{cancelled(stop);const auto number=static_cast<std::uint32_t>(report.frames+1);report.gpu_ms.push_back(frame(number,true));latest=capture(*gpu,current,pixels,options,number);auto& actual=latest;actual.previous_bodies=previous_bodies;std::vector<std::uint32_t> image_values;
        bool fatal_frame=false;for(std::size_t i=0;i<actual.bodies.size();++i){const auto& body=actual.bodies[i];report.contacts+=body.contacts;if(body.diagnostic&smo_d12_fatal_mask){++report.fatal_values;if(!saved_fatal&&!fatal_frame)report.failures.push_back("Primer diagnóstico físico: frame="+std::to_string(number)+" cuerpo="+std::to_string(i)+" bits="+std::to_string(body.diagnostic));fatal_frame=true;}if(body.diagnostic&smo_d12_inexact)++report.inexact_values;}
        for(std::size_t i=0;i<actual.pixels.size();++i){const auto& pixel=actual.pixels[i];image_values.push_back(pixel.bgra);report.ray_tests+=pixel.tests;if(pixel.diagnostic&smo_d12_fatal_mask){++report.fatal_values;if(!saved_fatal&&!fatal_frame)report.failures.push_back("Primer diagnóstico de rayos: frame="+std::to_string(number)+" píxel="+std::to_string(i)+" bits="+std::to_string(pixel.diagnostic));fatal_frame=true;}if(pixel.diagnostic&smo_d12_inexact)++report.inexact_values;}
        if(!previous_image.empty()&&image_values!=previous_image)++report.dynamic_frames;previous_image=std::move(image_values);if(actual.bodies!=previous_bodies)++report.changed_state_frames;previous_bodies=actual.bodies;++report.frames;
        const auto elapsed=std::chrono::duration<double>(Clock::now()-began).count();if(progress&&elapsed-last_preview>=5){progress(image(actual,options,report.device.api,elapsed));last_preview=elapsed;}
        // Fixed power-of-two checkpoints remain comparable across APIs even
        // when their one-minute runs complete different total frame counts.
        if(number==1||(number>=16&&(number&(number-1))==0)||number==options.minimum_frames||(fatal_frame&&!saved_fatal))report.checkpoints.push_back(actual);
        if(fatal_frame){saved_fatal=true;break;}
    }while(report.frames<std::max(2u,options.minimum_frames)||std::chrono::duration<double>(Clock::now()-began).count()<options.seconds);
    report.seconds=std::chrono::duration<double>(Clock::now()-began).count();report.median_ms=percentile(report.gpu_ms,.5);report.p95_ms=percentile(report.gpu_ms,.95);report.p99_ms=percentile(report.gpu_ms,.99);
    report.completed=report.frames>=options.minimum_frames&&report.seconds>=options.seconds&&!report.fatal_values;
    if(!report.completed)report.failures.push_back("La prueba se detuvo al detectar un valor fuera de rango; no acredita completar la duración solicitada.");
    if(report.checkpoints.empty()||report.checkpoints.back().frame!=report.frames)report.checkpoints.push_back(std::move(latest));
    report.timing_valid=std::isfinite(report.median_ms)&&std::isfinite(report.p95_ms)&&std::isfinite(report.p99_ms)&&report.gpu_ms.size()==report.frames;
    if(!report.timing_valid)report.failures.push_back("Las marcas de tiempo GPU no son todas finitas y positivas; no se acredita rendimiento.");
    if(!report.changed_state_frames)report.failures.push_back("El estado físico no cambió durante la prueba; no se acredita una escena dinámica.");
    if(options.seconds>=60&&(!report.dynamic_frames||!report.contacts))report.failures.push_back("El minuto no acreditó cambios de imagen y contactos reales de la escena.");
    if(!report.ray_tests)report.failures.push_back("La escena no acreditó pruebas de intersección de rayos.");
    // CPU checks are outside the sustained interval and use the independent
    // integer128 implementation. Each saved transition uses its GPU input as a
    // common input; it is not a claim of a CPU replay of unsaved trajectories.
    // Binary32 control stays a separate numerical check at the existing2e-4.
    for(const auto& point:report.checkpoints){cancelled(stop);
        const auto input=options.fp32_control?control_reference(point.previous_bodies,options.floating12):point.previous_bodies;
        const auto expected=smo_duodecimal_scene_step_reference(input,options.floating12);++report.validated_checkpoints;
        for(std::size_t i=0;i<expected.size();++i){cancelled(stop);++report.validated_body_steps;const bool valid=options.fp32_control?control_body(expected[i],point.bodies[i],options.floating12):expected[i]==point.bodies[i];
            if(options.fp32_control&&!control_encoding(point.bodies[i],options.bodies))report.failures.push_back("Defecto del control FP32: codificación física inválida: frame="+std::to_string(point.frame)+" cuerpo="+std::to_string(i));
            if(!valid)report.failures.push_back(std::string(options.fp32_control?"Sin equivalencia numérica FP32/D12 (2e-4), no acredita defecto de API: frame=":"Paso físico GPU distinto del oráculo independiente: frame=")+std::to_string(point.frame)+" cuerpo="+std::to_string(i));}
        const auto ray_bodies=options.fp32_control?control_reference(point.bodies,options.floating12):point.bodies;
        for(std::uint32_t pixel=0;pixel<options.width*options.height;++pixel){cancelled(stop);++report.validated_rays;const auto reference=smo_duodecimal_scene_ray_reference(ray_bodies,pixel,work,options.floating12);const bool valid=options.fp32_control?control_pixel(reference,point.pixels[pixel],options.floating12):reference==point.pixels[pixel];
            if(options.fp32_control&&!control_encoding(point.pixels[pixel],options.bodies))report.failures.push_back("Defecto del control FP32: codificación o BGRA propio inválido: frame="+std::to_string(point.frame)+" píxel="+std::to_string(pixel));
            if(!valid)report.failures.push_back(std::string(options.fp32_control?"Sin equivalencia numérica de rayo FP32/D12 (2e-4), no acredita defecto de API: frame=":"Rayo GPU distinto del oráculo duodecimal: frame=")+std::to_string(point.frame)+" píxel="+std::to_string(pixel));}
    }
    if(report.fatal_values)report.failures.push_back("Se detectaron valores fuera del dominio/rango duodecimal; la imagen no se acredita como válida.");
    if(progress)progress(image(report.checkpoints.back(),options,report.device.api,report.seconds));return report;
}
}
