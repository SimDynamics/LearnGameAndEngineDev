#include "presentation/windows/smo_scale12_experiment.hpp"
#include "presentation/windows/smo_scene.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <windows.h>
#include <psapi.h>

namespace {
using namespace lgaed::presentation;
std::size_t checks=0;
void check(bool condition,std::string_view reason){++checks;if(!condition)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& call,std::string_view reason){bool failed=false;try{call();}catch(const std::exception&){failed=true;}check(failed,reason);}
SmoF12 positional_reference(const SmoScale12Input& input){const auto diagnostic=input.value.diagnostic|input.factor.diagnostic;auto result=smo_f12_zero();result.diagnostic=diagnostic;
    if((input.value.digits[0]|input.value.digits[1]|input.value.digits[2])==0u)return result;
    const auto exponent=smo_f12_exponent(input.value)+smo_f12_exponent(input.factor)+11;
    if(exponent< -192)result.diagnostic|=smo_d12_underflow;else if(exponent>192)result.diagnostic|=smo_d12_overflow;
    else{result=input.value;result.diagnostic=diagnostic;result.digits[3]=smo_f12_tag|static_cast<std::uint32_t>(exponent+192);}return result;}
void cpu_contract(){check(sizeof(SmoScale12Input)==64u&&sizeof(SmoScale12Output)==32u,"The scale experiment silently changed padded transport sizes.");
    for(const auto exponent:{-181,-180,-1,0,1,180,203}){const auto literal=exponent==0?std::string("1"):exponent>0?"1"+std::string(static_cast<std::size_t>(exponent),'0'):"0."+std::string(static_cast<std::size_t>(-exponent-1),'0')+"1";
        const auto factor=smo_scale12_power(exponent);check(factor==smo_f12_parse(literal)&&smo_f12_valid(factor),"The factor is not exactly12^k in the declared F12 domain.");}
    rejects([]{(void)smo_scale12_power(-182);},"An unrepresentable factor was silently clamped.");rejects([]{(void)smo_scale12_power(204);},"An overflowing power factor was silently accepted.");
    const auto edges=smo_scale12_validation_inputs();bool overflow=false,underflow=false,encoding=false,fallback=false,warning=false;
    for(const auto& input:edges){const auto result=smo_scale12_reference(input);check(result.reserved==std::array<std::uint32_t,2>{},"A CPU reference changed output padding.");
        overflow=overflow||(result.value.diagnostic&smo_d12_overflow);underflow=underflow||(result.value.diagnostic&smo_d12_underflow);encoding=encoding||(result.value.diagnostic&smo_d12_encoding);warning=warning||(result.value.diagnostic&smo_d12_inexact);
        if(smo_scale12_fast_eligible(input))check(result.value==positional_reference(input),"Exact exponent scaling changed coefficient/sign/range/zero/diagnostics relative to general F12.");else fallback=true;}
    check(overflow&&underflow&&encoding&&fallback&&warning,"The edge corpus omitted a declared numeric/error case.");
    for(const auto& input:smo_scale12_inputs(1025)){check(smo_scale12_fast_eligible(input),"A timed factor unexpectedly requires a general-product fallback.");const auto expected=smo_scale12_reference(input);check(smo_f12_valid(expected.value)&&expected.value==positional_reference(input),"Timed scaling changed arithmetic or escaped the measured finite domain.");
        if(input.binary_eligible){const auto product=static_cast<std::uint64_t>(input.binary_value)*input.binary_factor;check(product<=0xffffffffull&&expected.value==smo_f12_uint(static_cast<std::uint32_t>(product)),"The native integer control changed the mathematical value or overflowed.");}}
    rejects([]{(void)smo_scale12_inputs(0);},"An empty scale workload was accepted.");SmoScale12Options invalid;invalid.rounds=0;rejects([&]{(void)run_smo_scale12_experiment(invalid);},"Zero measured rounds were accepted.");
    invalid={};invalid.sustained_seconds=59;rejects([&]{(void)run_smo_scale12_experiment(invalid);},"An unsupported sustained duration was accepted.");
    std::stop_source stop;stop.request_stop();rejects([&]{(void)run_smo_scale12_experiment({},stop.get_token());},"A pre-cancelled experiment started GPU work.");}
void artifact_contract(){for(unsigned stage=0;stage<smo_scale12_experiment_stages;++stage){const auto shader=smo_scale12_experiment_kernel(stage);check(!shader.hlsl.empty()&&!shader.glsl.empty(),"An experiment kernel has no real API source.");
    if(!shader.spirv.empty()){for(std::size_t at=5;at<shader.spirv.size();){const auto words=shader.spirv[at]>>16u,opcode=shader.spirv[at]&0xffffu;check(words>0&&words<=shader.spirv.size()-at&&opcode!=22u,"The scale experiment inserted floating arithmetic or malformed SPIR-V.");at+=words;}}}}
double quantile(std::vector<double> values,double q){if(values.empty()||std::ranges::any_of(values,[](double value){return !std::isfinite(value);}))return std::numeric_limits<double>::quiet_NaN();std::ranges::sort(values);return values[static_cast<std::size_t>(q*static_cast<double>(values.size()-1u))];}
void print(std::string_view name,const SmoScale12Timing& value){std::cout<<name<<" samples="<<value.gpu_ms.size()<<" discarded="<<value.discarded_samples<<" valid="<<value.valid<<" GPU_median_ms="<<value.median_ms<<" GPU_p95_ms="<<quantile(value.gpu_ms,.95)<<" GPU_min_ms="<<quantile(value.gpu_ms,0)<<" GPU_max_ms="<<quantile(value.gpu_ms,1)<<" raw_output_hash="<<value.output_hash<<'\n';}
void print_pairs(const SmoScale12Report& result){if(!result.general.valid||!result.exponent.valid)return;
    const auto& general=result.general.gpu_ms;const auto& exponent=result.exponent.gpu_ms;
    check(general.size()==exponent.size(),"The paired timing series have different lengths.");
    std::vector<double> ratios;ratios.reserve(general.size());std::uint64_t exponent_wins=0,general_wins=0,ties=0;
    for(std::size_t pair=0;pair<general.size();++pair){ratios.push_back(general[pair]/exponent[pair]);
        if(exponent[pair]<general[pair])++exponent_wins;else if(general[pair]<exponent[pair])++general_wins;else ++ties;}
    check(exponent_wins+general_wins+ties==result.rounds_actual,"The paired comparison omitted a measured round.");
    std::cout<<"paired_general_over_exponent_median="<<quantile(ratios,.5)<<" exponent_faster_pairs="<<exponent_wins
        <<" general_faster_pairs="<<general_wins<<" equal_GPU_tick_pairs="<<ties<<" (exact recorded clocks, no significance threshold).\n";
    const auto middle=general.size()/2u;
    const auto half=[&](std::string_view label,std::size_t begin,std::size_t end){if(begin==end)return;
        std::cout<<label<<" pairs="<<(end-begin)<<" general_median_ms="<<quantile(std::vector<double>(general.begin()+begin,general.begin()+end),.5)
            <<" exponent_median_ms="<<quantile(std::vector<double>(exponent.begin()+begin,exponent.begin()+end),.5)
            <<" paired_ratio_median="<<quantile(std::vector<double>(ratios.begin()+begin,ratios.begin()+end),.5)<<'\n';};
    half("first_half",0,middle);half("second_half",middle,general.size());
}
} //namespace
int main(int argc,char** argv){try{std::cout<<std::unitbuf;bool hardware=false,cpu=false,sustained=false,large=false,selected=false;std::string selector="d3d11";
    for(int at=1;at<argc;++at){const std::string_view arg=argv[at];if(arg=="hardware")hardware=true;else if(arg=="cpu")cpu=true;else if(arg=="sustained")sustained=true;else if(arg=="large")large=true;else if(arg=="d3d11"||arg=="d3d12"||arg=="vulkan"||arg=="opengl"||arg=="all"){check(!selected,"Choose one API selector.");selector=arg;selected=true;}else throw std::invalid_argument("Usage: smo_scale12_experiment_tests [cpu|hardware] [d3d11|d3d12|vulkan|opengl|all] [large] [sustained]");}
    check(!(cpu&&(hardware||sustained||large||selected)),"CPU-only cannot also request GPU work.");check((!sustained&&!large)||hardware,"The larger/real-minute experiment requires explicit hardware selection.");cpu_contract();if(cpu){std::cout<<"PASS scale12 CPU proof/transport checks="<<checks<<"; no GPU timing executed.\n";return 0;}artifact_contract();if(hardware&&!selected)selector="all";
    std::string adapter;unsigned exercised=0;std::optional<std::uint64_t> baseline_hash;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){if(selector!="all"&&api!=smo_compute_api(selector))continue;SmoScale12Options options;options.api=api;options.device={ !hardware,adapter };options.items=large?1048576u:hardware?65536u:129u;options.sustained_seconds=sustained?60u:0u;
        auto probe=create_smo_compute(api,options.device);if(hardware){check(!probe->info().software,"The scale experiment silently substituted software for hardware.");if(adapter.empty())adapter=probe->info().adapter;options.device.adapter_name=adapter;}probe.reset();
        const auto result=run_smo_scale12_experiment(options);check(result.equivalent&&result.items==options.items&&result.fast_cases==result.items&&result.validation_cases>=70u&&result.binary_items>0,"The real GPU scaling coverage is incomplete.");
        check(result.verifier_fault_cases==8u&&result.gpu_mismatch_flags==std::array<std::uint32_t,2>{},"The verifier missed a raw field or retained an output discrepancy.");
        check(result.resident_verification==sustained,"The output verification mode does not match the requested run.");
        if(sustained){check(result.gpu_verified_words==result.rounds_actual*static_cast<std::uint64_t>(result.items)*16u,
                "The resident verifier did not cover all eight output words of both variants in every round.");
            check(result.verifier_polls==result.rounds_actual/128u+1u,"A periodic or final sticky discrepancy check was omitted.");}
        else check(result.gpu_verified_words==0u&&result.verifier_polls==0u,"The quick run unexpectedly changed its CPU-per-round verification method.");
        const auto cpu_expected=16u*(static_cast<std::uint64_t>(result.validation_cases)+2u*result.items+result.binary_items
            +(sustained?0u:result.rounds_actual*result.items));
        check(result.verified_words==cpu_expected,"A complete CPU pre/final or quick per-round output check was omitted.");
        check(result.general.output_hash==result.exponent.output_hash&&result.general.gpu_ms.size()+result.general.discarded_samples==result.rounds_actual&&result.exponent.gpu_ms.size()+result.exponent.discarded_samples==result.rounds_actual,"The same-precision variants changed output bytes or omitted measured rounds.");
        check(result.binary.gpu_ms.size()==options.rounds&&result.rounds_actual>=options.rounds,"The separate integer control or paired run omitted rounds.");if(sustained)check(result.elapsed_seconds>=60.0,"The sustained experiment did not execute its real minute.");
        if(baseline_hash)check(*baseline_hash==result.general.output_hash,"Integer-only F12 scale outputs differ across APIs.");else baseline_hash=result.general.output_hash;
        std::cout<<"scale12 api="<<result.device.api<<" adapter="<<result.device.adapter<<" software="<<result.device.software<<" items="<<result.items<<" fast_eligible_items="<<result.fast_cases<<" input_stride=64 output_stride=32 input_hash="<<result.input_hash<<" edge_cases="<<result.validation_cases<<" paired_rounds="<<result.rounds_actual<<" elapsed_wall_seconds="<<result.elapsed_seconds<<" CPU_verified_words="<<result.verified_words
            <<" GPU_verified_words_derived="<<result.gpu_verified_words<<" verifier_fault_fields="<<result.verifier_fault_cases<<" sticky_polls="<<result.verifier_polls
            <<" sticky_general="<<result.gpu_mismatch_flags[0]<<" sticky_exponent="<<result.gpu_mismatch_flags[1]<<'\n';print("general_F12",result.general);print("exponent_F12",result.exponent);print("native_uint32_SUBSET",result.binary);print_pairs(result);
        if(result.general.valid&&result.exponent.valid){check(std::isfinite(result.general_over_exponent)&&result.general_over_exponent>0,"A valid timing pair produced an invalid ratio.");std::cout<<"Measured general/exponent GPU ratio="<<result.general_over_exponent<<"; same F12 values/RNE/errors, NOT app FPS or a promised gain.\n";}else check(std::isnan(result.general_over_exponent),"Missing GPU clocks were replaced by a fabricated timing ratio.");
        std::cout<<"Scope: existing F12 multiplication PRIMARY (profile0) and numeric diagnostics only. No Number alternative/result flags or pdir equivalence is claimed, including for zero.\n";
        std::cout<<"Timed factors are ALL eligible positive powers of12. Mixed fallback traffic is functionally tested in the edge corpus, not represented by this specialized timing.\n";
        std::cout<<(sustained?"Verification: resident GPU checks all eight output words of BOTH variants after every pair and before reuse; sticky flags are polled every 128 pairs and finally. GPU coverage is derived from submitted ranges, not a success atomic. Complete CPU checks precede and follow timing. ":"Verification: complete CPU checks after every paired round, as well as before and after timing. ")
            <<"Producer GPU timestamps exclude verification/readbacks; wall duration includes them. Native uint32 control count="<<result.binary_items<<" uses only exact nonnegative integers with no overflow, measured separately with pre/post verification; no cross-domain ratio. Signed zero is not an F12 encoding: raw-0 is tested as an encoding error, not silently normalized.\n";++exercised;}
    check(exercised>0,"No requested GPU backend was selected.");
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    check(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=0,
        "The process memory counters could not be read.");
    std::cout<<"process_peak_working_set_bytes="<<memory.PeakWorkingSetSize<<" current_working_set_bytes="<<memory.WorkingSetSize
        <<" private_commit_bytes="<<memory.PrivateUsage<<" (process lifetime, NOT GPU VRAM).\n";
    std::cout<<"PASS scale12 checks="<<checks<<". No audio, curriculum or existing renderer path changed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL scale12 experiment: "<<error.what()<<'\n';return 1;}}
