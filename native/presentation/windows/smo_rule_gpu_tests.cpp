#include "presentation/windows/smo_rule_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "core/smo.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action>void rejects(Action action){bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Se aceptó una configuración de benchmark inválida.");}
void representation_properties(){
    for(std::uint32_t i=0;i<10000;++i)require(smo_rule_unpack12(smo_rule_pack12(i))==i,"El empaquetado base12 perdió una cantidad exacta.");
    std::uint32_t power=1;
    for(unsigned place=1;place<8;++place){power*=12;SmoRuleInput item;item.operation=static_cast<std::uint32_t>(SmoRuleOperation::base12_add);
        item.packed_left=smo_rule_pack12(power-1);item.packed_right=smo_rule_pack12(1);const auto carry=smo_rule_reference(item);
        require(carry.diagnostic==0&&carry.packed_primary==smo_rule_pack12(power),"El acarreo jerárquico no es una suma radix12 exacta.");
        std::swap(item.packed_left,item.packed_right);require(smo_rule_reference(item).packed_primary==carry.packed_primary,"La suma packed perdió conmutatividad en su dominio exacto.");
        item.operation=static_cast<std::uint32_t>(SmoRuleOperation::base12_rational);item.packed_left=smo_rule_pack12(power);item.packed_right=smo_rule_pack12(power/12);
        const auto ratio=smo_rule_reference(item);require(ratio.diagnostic==0&&ratio.numerator==smo_rule_pack12(12)&&ratio.denominator==smo_rule_pack12(1),"La razón entre órdenes no conserva numerador y denominador base12 exactos.");
    }
    require(smo_rule_format12(smo_rule_pack12(12))=="10"&&smo_rule_format12(smo_rule_pack12(10))=="#"&&smo_rule_format12(smo_rule_pack12(11))=="~", "Se confundieron dígitos SMO con etiquetas decimales.");
    for(auto profile:{0u,2u}){SmoRuleInput scale;scale.operation=static_cast<std::uint32_t>(SmoRuleOperation::base12_scale);scale.profile=profile;scale.packed_left=smo_rule_pack12(144);scale.factor=0;
        const auto first=smo_rule_reference(scale);scale.selection=1;const auto second=smo_rule_reference(scale);
        require(first.packed_primary==(profile?smo_rule_pack12(144):0)&&second.packed_primary==(profile?0:smo_rule_pack12(144)),"La escala cero perdió su política o alternativa exacta.");}
    for(auto profile:{0u,4u,8u}){const auto input=smo_rule_input(SmoRuleOperation::divide,3,0,profile);const auto output=smo_rule_reference(input);
        if(profile==8)require((output.flags&smo_rule_unresolved)&&!(output.flags&smo_rule_has_primary)&&output.diagnostic==smo_rule_division_unresolved,"Una división sin resolver inventó una cantidad.");
        else require(output.diagnostic==0&&(output.flags&smo_rule_has_alternative)&&smo_rule_primary(output)==(profile==4?0.f:3.f),"Cambió una política explícita de división cero.");}
    require(lgaed::smo::multiply(-2,-3).value==-6&&lgaed::smo::divide(3,0).value==3,"Los perfiles GPU modificaron las reglas del intérprete existente.");
}
void verify(const SmoRuleGpuReport& report,const SmoRuleGpuOptions& options){
    require(!report.device.api.empty()&&!report.device.adapter.empty()&&!report.source_identity.empty(),"Falta identidad real de GPU, API o reglas.");
    require(report.device.software==options.device.software,"Se confundió WARP con una GPU física.");
    require(report.cases>=10000&&report.profiles==48&&report.packed_cases>0&&report.rational_cases>0,"Falta cobertura CPU/GPU de reglas, perfiles y representaciones.");
    require(report.equivalent&&report.failures.empty(),"Alguna regla, diagnóstico, representación o estado GPU no coincide con el oráculo.");
    for(unsigned mode=0;mode<3;++mode)require(report.mismatches[mode]==0&&report.unresolved_states_preserved[mode]>0,"Una variante cambió reglas o actualizó una división sin resolver.");
    require(options.benchmark?!report.benchmarks.empty():report.benchmarks.empty(),"Se ignoró la petición de microbenchmarks.");
    for(const auto& row:report.benchmarks){require(row.cases==options.benchmark_cases&&row.equivalent,"Un lote uniforme alteró sus entradas o resultados.");bool valid=true;
        for(unsigned mode=0;mode<3;++mode){require(row.mismatches[mode]==0&&row.gpu_ms[mode].size()==options.rounds,"Faltan rondas verificadas de una variante.");
            const bool measured=std::ranges::all_of(row.gpu_ms[mode],[](double value){return std::isfinite(value)&&value>0;});valid=valid&&measured;
            require(measured?(std::isfinite(row.median_ms[mode])&&row.median_ms[mode]>0):std::isnan(row.median_ms[mode]),"Una consulta inválida se sustituyó por una medida inventada.");}
        require(row.timing_valid==valid,"El estado de los timestamps GPU es incorrecto.");
        if(row.profile==smo_rule_divide_zero_unresolved)for(auto count:row.unresolved_states_preserved)require(count==row.cases,"El microbenchmark actualizó una división no resuelta.");
    }
}
void print(const SmoRuleGpuReport& report){
    std::cout<<std::setprecision(9)<<"api="<<report.device.api<<" adapter="<<std::quoted(report.device.adapter)<<" driver="<<std::quoted(report.device.driver)
        <<" software="<<report.device.software<<" cases="<<report.cases<<" profiles="<<report.profiles<<" packed_cases="<<report.packed_cases<<" rational_cases="<<report.rational_cases
        <<" CPU_reference_ms="<<report.reference_ms<<" max_support_absolute_error="<<report.maximum_error<<" equivalent="<<report.equivalent<<'\n';
    for(unsigned mode=0;mode<3;++mode)std::cout<<"variant="<<mode<<" mismatches="<<report.mismatches[mode]<<" unresolved_GPU_states_preserved="<<report.unresolved_states_preserved[mode]<<'\n';
    for(const auto& failure:report.failures){std::cout<<"FAIL variant="<<failure.variant<<" case="<<std::quoted(failure.name)<<" operation="<<failure.input.operation<<" profile="<<failure.input.profile<<" selection="<<failure.input.selection
        <<" input_left_bits="<<failure.input.left_bits<<" input_right_bits="<<failure.input.right_bits<<" input_packed_left="<<failure.input.packed_left<<" input_packed_right="<<failure.input.packed_right<<" factor="<<failure.input.factor
        <<" expected_flags="<<failure.expected.flags<<" actual_flags="<<failure.actual.flags<<" expected_diagnostic="<<smo_rule_diagnostic_name(failure.expected.diagnostic)
        <<" actual_diagnostic="<<smo_rule_diagnostic_name(failure.actual.diagnostic)<<" expected_primary="<<smo_rule_primary(failure.expected)<<" actual_primary="<<smo_rule_primary(failure.actual)
        <<" expected_alternative="<<smo_rule_alternative(failure.expected)<<" actual_alternative="<<smo_rule_alternative(failure.actual)
        <<" expected_packed="<<failure.expected.packed_primary<<" actual_packed="<<failure.actual.packed_primary<<" expected_num="<<failure.expected.numerator<<" actual_num="<<failure.actual.numerator
        <<" expected_packed_alternative="<<failure.expected.packed_alternative<<" actual_packed_alternative="<<failure.actual.packed_alternative
        <<" expected_den="<<failure.expected.denominator<<" actual_den="<<failure.actual.denominator<<" expected_state_bits="<<failure.expected_state<<" actual_state_bits="<<failure.actual_state<<'\n';}
    for(const auto& row:report.benchmarks){std::cout<<"benchmark="<<std::quoted(row.name)<<" cases="<<row.cases<<" equivalent="<<row.equivalent<<" valid_GPU_clock="<<row.timing_valid
        <<" ordinary_optimized_ms="<<row.median_ms[0]<<" SMO_generic_ms="<<row.median_ms[1]<<" SMO_uniform_specialization_ms="<<row.median_ms[2]<<'\n';
        for(unsigned mode=0;mode<3;++mode){std::cout<<"samples["<<mode<<"]=";for(const auto value:row.gpu_ms[mode])std::cout<<' '<<value;std::cout<<'\n';}}
    std::cout<<"source_identity="<<report.source_identity<<'\n'<<report.conclusion<<'\n';
}
std::string key(std::string_view text){std::string value;for(const unsigned char c:text)if(std::isalnum(c))value.push_back(static_cast<char>(std::tolower(c)));return value;}
bool same_gpu(const SmoComputeInfo& first,const SmoComputeInfo& other){const auto a=key(first.adapter),b=key(other.adapter);
    return first.software==other.software&&(!first.vendor_id||!other.vendor_id||first.vendor_id==other.vendor_id)&&
        (!first.device_id||!other.device_id||first.device_id==other.device_id)&&a.size()>4&&b.size()>4&&(a.find(b)!=a.npos||b.find(a)!=b.npos);}
}

int main(int argc,char** argv){try{
    representation_properties();SmoRuleGpuOptions invalid;invalid.benchmark_cases=0;rejects([&]{(void)run_smo_rule_gpu(invalid);});invalid.benchmark_cases=64;invalid.rounds=2;rejects([&]{(void)run_smo_rule_gpu(invalid);});
    std::stop_source stop;stop.request_stop();bool canceled=false;try{(void)run_smo_rule_gpu({},stop.get_token());}catch(const std::runtime_error&){canceled=true;}require(canceled,"La cancelación previa alcanzó el dispositivo GPU.");
    if(argc==1){SmoRuleGpuOptions options;options.device.software=true;options.benchmark_cases=256;const auto report=run_smo_rule_gpu(options);print(report);verify(report,options);
        std::cout<<"PASS WARP: reglas/perfiles, radix12 exacto, racionales, diagnósticos y omisión real de estados; no rendimiento físico.\n";return 0;}
    require(argc<=3&&std::string_view(argv[1])=="hardware","Uso: lgaed_smo_rule_gpu_tests [hardware [d3d11|d3d12|vulkan|opengl|all]]");
    const auto selector=argc==3?std::string_view(argv[2]):std::string_view("all");
    constexpr std::array<SmoComputeApi,4> apis{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl};
    std::optional<SmoComputeInfo> baseline;std::string identity;bool failed=false;
    for(const auto api:apis){if(selector!="all"&&api!=smo_compute_api(selector))continue;
        try{SmoRuleGpuOptions options;options.api=api;options.benchmark_cases=4096;if(baseline)options.device.adapter_name=baseline->adapter;
            auto report=run_smo_rule_gpu(options);print(report);verify(report,options);
            if(baseline)require(same_gpu(*baseline,report.device)&&identity==report.source_identity,"La comparación cambió GPU o código de las reglas.");else{baseline=report.device;identity=report.source_identity;}
        }catch(const std::exception& error){failed=true;std::cerr<<"API FAIL "<<smo_compute_api_name(api)<<": "<<error.what()<<'\n';}
    }
    return failed?1:0;
}catch(const std::exception& error){std::cerr<<"SMO rule GPU failure: "<<error.what()<<'\n';return 1;}}
