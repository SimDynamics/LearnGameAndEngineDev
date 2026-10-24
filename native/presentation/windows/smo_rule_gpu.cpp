#include "presentation/windows/smo_rule_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using Clock=std::chrono::steady_clock;
using Op=SmoRuleOperation;
constexpr float tolerance=2e-4f;
constexpr std::uint32_t old_state=0x42f68000u; //123.25, an actual prior GPU state.
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Prueba de reglas GPU cancelada.");}
std::string source(std::string_view name){const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,512*1024);
    std::string text;for(std::size_t i=0;i<raw.size();++i){if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;text+=static_cast<char>(raw[i]);}return text;}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& value){return std::as_bytes(std::span(value));}
template<class T>std::vector<T> read(SmoCompute& gpu,SmoBuffer id,std::size_t count){const auto raw=gpu.read(id);
    if(raw.size()!=count*sizeof(T))throw std::runtime_error("La lectura GPU de reglas está incompleta.");std::vector<T> result(count);std::memcpy(result.data(),raw.data(),raw.size());return result;}
double median(const std::vector<double>& values){if(values.empty()||std::ranges::any_of(values,[](double v){return !std::isfinite(v)||v<=0;}))return std::numeric_limits<double>::quiet_NaN();
    auto ordered=values;std::ranges::sort(ordered);const auto half=ordered.size()/2;return ordered.size()%2?ordered[half]:(ordered[half-1]+ordered[half])*.5;}
bool view(std::uint32_t expected,std::uint32_t actual,double& maximum){const float a=std::bit_cast<float>(expected),b=std::bit_cast<float>(actual);
    if(!std::isfinite(a)||!std::isfinite(b))return false;const double error=std::abs(static_cast<double>(a)-b);maximum=std::max(maximum,error);
    return error<=static_cast<double>(tolerance)*std::max(1.,std::abs(static_cast<double>(a)));}
bool result_equal(const SmoRuleResult& expected,const SmoRuleResult& actual,double& maximum){
    if(expected.flags!=actual.flags||expected.diagnostic!=actual.diagnostic||expected.packed_primary!=actual.packed_primary||
        expected.packed_alternative!=actual.packed_alternative||expected.numerator!=actual.numerator||expected.denominator!=actual.denominator)return false;
    const bool usable=expected.diagnostic==0;
    const bool first=usable&&(expected.flags&smo_rule_has_primary)?view(expected.primary_bits,actual.primary_bits,maximum):expected.primary_bits==actual.primary_bits;
    const bool second=usable&&(expected.flags&smo_rule_has_alternative)?view(expected.alternative_bits,actual.alternative_bits,maximum):expected.alternative_bits==actual.alternative_bits;
    return first&&second;
}
bool state_equal(const SmoRuleResult& expected,const SmoRuleResult& actual,std::uint32_t state,double& maximum){
    if(expected.diagnostic!=0||!(expected.flags&smo_rule_has_primary))return state==old_state;
    // Updating must use the actual primary result, not a stale view, while the
    // independent oracle's FP32 support view retains its declared tolerance.
    return state==actual.primary_bits&&view(expected.primary_bits,state,maximum);
}
struct Batch {
    SmoCompute& gpu;std::size_t count;SmoBuffer input;
    std::array<SmoBuffer,3> output{},state{};std::array<SmoKernel,3> kernel;
    std::array<std::uint32_t,16> constants{};
    Batch(SmoCompute& compute,const std::array<SmoKernel,3>& shaders,const std::vector<SmoRuleInput>& data,bool uniform):gpu(compute),count(data.size()),kernel(shaders){
        if(data.empty()||data.size()>262144)throw std::invalid_argument("Lote de reglas GPU vacío o excesivo.");
        input=gpu.buffer(data.size()*sizeof(SmoRuleInput),bytes(data));constants[0]=static_cast<std::uint32_t>(count);
        if(uniform){const auto& first=data.front();for(const auto& item:data)if(item.operation!=first.operation||item.profile!=first.profile||item.selection!=first.selection)
                throw std::invalid_argument("La especialización debe conservar la operación/perfil/selección de todas las entradas.");
            constants[1]=first.operation;constants[2]=first.profile;constants[3]=first.selection;constants[4]=1;}
        const std::vector<std::uint32_t> previous(count,old_state);
        for(unsigned mode=0;mode<3;++mode){output[mode]=gpu.buffer(count*sizeof(SmoRuleResult));state[mode]=gpu.buffer(count*4,bytes(previous));}
    }
    double execute(unsigned mode,bool timed){const SmoDispatch dispatch{kernel[mode],{input,output[mode],state[mode]},constants,{(static_cast<std::uint32_t>(count)+63)/64,1,1}};
        return gpu.execute(std::span(&dispatch,1),timed);}
};
void verify(Batch& batch,const std::vector<SmoRuleInput>& input,const std::vector<SmoRuleResult>& expected,
    const std::vector<std::string>& names,SmoRuleGpuReport& report,SmoRuleGpuBenchmark* benchmark,std::stop_token stop){
    for(unsigned mode=0;mode<3;++mode){const auto results=read<SmoRuleResult>(batch.gpu,batch.output[mode],input.size());const auto states=read<std::uint32_t>(batch.gpu,batch.state[mode],input.size());
        for(std::size_t i=0;i<input.size();++i){if((i&255)==0)canceled(stop);double error=0;const bool correct=result_equal(expected[i],results[i],error)&&state_equal(expected[i],results[i],states[i],error);
            report.maximum_error=std::max(report.maximum_error,error);if(benchmark)benchmark->maximum_error=std::max(benchmark->maximum_error,error);
            if((expected[i].flags&smo_rule_unresolved)&&states[i]==old_state){++report.unresolved_states_preserved[mode];if(benchmark)++benchmark->unresolved_states_preserved[mode];}
            if(!correct){++report.mismatches[mode];if(benchmark)++benchmark->mismatches[mode];
                const auto expected_state=expected[i].diagnostic==0&&(expected[i].flags&smo_rule_has_primary)?expected[i].primary_bits:old_state;
                report.failures.push_back({names[i],mode,input[i],expected[i],results[i],expected_state,states[i]});}
        }
    }
}
std::vector<SmoRuleCase> cases(){auto result=smo_rule_cases();
    // Actual radix12 carry/scale/rational hierarchies, not a decimal renaming.
    // Their eight packed digits remain exact even above FP32's integer range.
    std::uint32_t power=1;
    for(unsigned place=1;place<8;++place){power*=12;SmoRuleInput input;input.operation=static_cast<std::uint32_t>(Op::base12_add);
        input.packed_left=smo_rule_pack12(power-1);input.packed_right=smo_rule_pack12(1);result.push_back({"jerarquía12: acarreo a posición "+std::to_string(place),input});
        input.operation=static_cast<std::uint32_t>(Op::base12_scale);input.packed_left=smo_rule_pack12(power/12);input.factor=11;result.push_back({"jerarquía12: once bloques de orden "+std::to_string(place-1),input});
        input.operation=static_cast<std::uint32_t>(Op::base12_rational);input.packed_left=smo_rule_pack12(power);input.packed_right=smo_rule_pack12(power/12);result.push_back({"jerarquía12: razón exacta entre órdenes",input});
    }
    if(result.size()<10000)throw std::runtime_error("La cobertura de reglas perdió los diez mil casos CPU/GPU previstos.");return result;
}
struct Scenario {std::string name;Op operation;std::uint32_t profile=0,selection=0;unsigned pattern=0;};
std::vector<Scenario> scenarios(){std::vector<Scenario> result;
    for(auto p:{0u,1u})result.push_back({"signos",Op::multiply,p,0,0});
    for(auto p:{0u,2u})for(auto s:{0u,1u})result.push_back({"producto cero",Op::multiply,p,s,1});
    for(auto p:{0u,4u,8u}){result.push_back({"división cero",Op::divide,p,0,2});if(p!=8)result.push_back({"división cero alternativa",Op::divide,p,1,2});}
    for(auto op:{Op::direct_power,Op::inverted_power}){
        for(auto p:{0u,16u})for(auto s:{0u,1u})result.push_back({"potencia cero",op,p,s,3});
        for(auto p:{0u,32u})result.push_back({"exponentes ±1",op,p,0,4});
        result.push_back({"potencias de magnitudes base12",op,0,0,5});}
    result.push_back({"suma radix12 con acarreo",Op::base12_add,0,0,6});result.push_back({"escala radix12",Op::base12_scale,0,0,7});
    result.push_back({"escala radix12 cero conserva",Op::base12_scale,2,0,7});result.push_back({"racionales radix12 reducidos",Op::base12_rational,0,0,8});return result;
}
std::vector<SmoRuleInput> workload(const Scenario& scenario,std::uint32_t count){std::vector<SmoRuleInput> result;result.reserve(count);
    constexpr std::array<float,6> signed_values{-3.f,-1.f,0.f,1.f,2.f,3.f};constexpr std::array<float,5> bases{.5f,1.f,2.f,3.f,12.f};
    constexpr std::array<int,6> exponents{-3,-2,-1,1,2,3};constexpr std::array<std::uint32_t,8> packed{0,1,11,12,143,144,1728,35831808};
    for(std::uint32_t i=0;i<count;++i){float a=signed_values[i%signed_values.size()],b=signed_values[(i/3+1)%signed_values.size()];
        if(scenario.pattern==1){if(i%2)b=0;else{b=a;a=0;}}
        if(scenario.pattern==2)b=0;
        if(scenario.pattern>=3&&scenario.pattern<=5){a=bases[i%bases.size()];b=scenario.pattern==3?0.f:scenario.pattern==4?(i%2?-1.f:1.f):static_cast<float>(exponents[i%exponents.size()]);}
        auto input=smo_rule_input(scenario.operation,a,b,scenario.profile,scenario.selection);
        if(scenario.pattern>=6){input.packed_left=smo_rule_pack12(packed[i%packed.size()]);input.packed_right=smo_rule_pack12(packed[(i/5+1)%packed.size()]);input.factor=i%12;}
        result.push_back(input);
    }
    return result;
}
} // namespace

SmoKernelSource smo_rule_gpu_kernel(unsigned variant){if(variant>2)throw std::invalid_argument("Variante de reglas GPU inválida.");
    const auto hlsl=source("smo_shader_hlsl.inc"),glsl=source("smo_shader_glsl.inc"),arithmetic=source("smo_arithmetic.inc"),body=source("smo_rules_common.inc");
    const auto defines="#define MODE "+std::to_string(variant)+"\n";SmoKernelSource result;result.name="rule_mode"+std::to_string(variant);
    result.hlsl=defines+hlsl+arithmetic+body;result.glsl="#version 430\n"+defines+glsl+arithmetic+body;
#if defined(LGAED_HAS_VULKAN) && defined(LGAED_SMO_RULE_SHADER_ROOT)
    const auto root=std::filesystem::path(LGAED_SMO_RULE_SHADER_ROOT);const auto raw_hash=state::read_bytes(root/"source.sha256",256);std::string hash(raw_hash.begin(),raw_hash.end());
    while(!hash.empty()&&(hash.back()=='\r'||hash.back()=='\n'||hash.back()==' '))hash.pop_back();
    if(hash!=state::sha256(glsl+arithmetic+body))throw std::runtime_error("Las reglas Vulkan cambiaron: recompila el preset Windows existente.");
    const auto raw=state::read_bytes(root/"shaders"/(result.name+".spv"),2*1024*1024);if(raw.size()<20||raw.size()%4)throw std::runtime_error("SPIR-V de reglas incompleto.");
    result.spirv.resize(raw.size()/4);std::memcpy(result.spirv.data(),raw.data(),raw.size());if(result.spirv.front()!=0x07230203u)throw std::runtime_error("SPIR-V de reglas inválido.");
#endif
    return result;
}
SmoRuleGpuReport run_smo_rule_gpu(const SmoRuleGpuOptions& options,std::stop_token stop){canceled(stop);
    if(options.benchmark_cases<64||options.benchmark_cases>262144||options.rounds<3||options.rounds>9)throw std::invalid_argument("Carga de reglas fuera del límite de verificación breve.");
    const auto suite=cases();std::vector<SmoRuleInput> inputs;std::vector<SmoRuleResult> expected;std::vector<std::string> names;std::set<std::uint32_t> profiles;
    SmoRuleGpuReport report;report.cases=suite.size();const auto reference_begin=Clock::now();
    for(const auto& item:suite){canceled(stop);inputs.push_back(item.input);expected.push_back(smo_rule_reference(item.input));names.push_back(item.name);
        if((item.input.profile&~smo_rule_profile_mask)==0&&(item.input.profile&smo_rule_divide_zero_mask)!=smo_rule_divide_zero_mask)profiles.insert(item.input.profile);
        if(item.input.operation==static_cast<std::uint32_t>(Op::base12_add)||item.input.operation==static_cast<std::uint32_t>(Op::base12_scale))++report.packed_cases;
        if(item.input.operation==static_cast<std::uint32_t>(Op::base12_rational))++report.rational_cases;}
    report.reference_ms=std::chrono::duration<double,std::milli>(Clock::now()-reference_begin).count();report.profiles=profiles.size();
    auto gpu=create_smo_compute(options.api,options.device,stop);report.device=gpu->info();std::array<SmoKernel,3> kernels{};
    for(unsigned mode=0;mode<3;++mode){canceled(stop);kernels[mode]=gpu->kernel(smo_rule_gpu_kernel(mode));}
    report.source_identity=state::sha256(source("smo_shader_hlsl.inc")+source("smo_shader_glsl.inc")+source("smo_arithmetic.inc")+source("smo_rules_common.inc"));
    Batch conformance(*gpu,kernels,inputs,false);for(unsigned mode=0;mode<3;++mode)(void)conformance.execute(mode,false);
    verify(conformance,inputs,expected,names,report,nullptr,stop);
    if(options.benchmark)for(const auto& scenario:scenarios()){canceled(stop);SmoRuleGpuBenchmark row;row.operation=scenario.operation;row.profile=scenario.profile;row.selection=scenario.selection;row.cases=options.benchmark_cases;
        row.name=scenario.name+" · "+smo_rule_operation_name(scenario.operation)+" · "+smo_rule_profile_name(scenario.profile)+(scenario.selection?" · alternativa":"");
        const auto data=workload(scenario,options.benchmark_cases);std::vector<SmoRuleResult> answers;std::vector<std::string> labels;answers.reserve(data.size());labels.reserve(data.size());
        for(std::size_t i=0;i<data.size();++i){answers.push_back(smo_rule_reference(data[i]));labels.push_back(row.name+"/"+std::to_string(i));}
        Batch batch(*gpu,kernels,data,true);
        for(unsigned warm=0;warm<2;++warm)for(unsigned mode=0;mode<3;++mode)(void)batch.execute(mode,false);
        for(unsigned round=0;round<options.rounds;++round)for(unsigned position=0;position<3;++position){const auto mode=(round+position)%3;canceled(stop);row.gpu_ms[mode].push_back(batch.execute(mode,true));}
        verify(batch,data,answers,labels,report,&row,stop);
        row.timing_valid=true;for(unsigned mode=0;mode<3;++mode){row.median_ms[mode]=median(row.gpu_ms[mode]);row.timing_valid=row.timing_valid&&std::isfinite(row.median_ms[mode]);}
        row.equivalent=std::ranges::all_of(row.mismatches,[](auto count){return count==0;});report.benchmarks.push_back(std::move(row));
    }
    report.equivalent=std::ranges::all_of(report.mismatches,[](auto count){return count==0;});
    report.conclusion="Contrato experimental GPU de todas las reglas/perfiles SMO: control ordinario optimizado que implementa las MISMAS reglas, SMO genérico y SMO especializado por operación/perfil uniforme. Las políticas diferentes no son el mismo cálculo y no acreditan velocidad entre sí. ";
    report.conclusion+="Se verifican flags, diagnósticos, valores ausentes, cifras packed base12 y racionales exactamente; los campos FP32 utilizables conservan la tolerancia 2e-4. Una división sin resolver no escribe el estado GPU previo. ";
    report.conclusion+="Cada muestra temporal es un único Dispatch de un lote independiente: incluye validación y escritura, sin lotes artificiales repetidos ni optimizaciones deshabilitadas. CPU/oráculos/readback/compilación quedan fuera de los timestamps. ";
    report.conclusion+="Los dígitos y acarreos radix12 y los racionales son reales y exactos en su dominio acotado; sus vistas FP32 siguen siendo soporte binario, no hardware nativo base12 ni un sistema SMO completo. Resultados distintos o clocks inválidos impiden acreditar una aceleración; resultados/tiempos iguales también son un resultado válido. ";
    if(report.device.software)report.conclusion+="WARP/software sólo acredita corrección y tiempos de ese backend, no rendimiento de una GPU física.";
    return report;
}
}
