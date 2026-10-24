#include "presentation/windows/smo_radix_ablation.hpp"
#include "presentation/windows/smo_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::presentation;
std::size_t checks=0;
void check(bool value,std::string_view reason){++checks;if(!value)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& action,std::string_view reason){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,reason);}
// Independent integer preparation/checking only. These helpers never run
// inside GPU query intervals and never create a floating result view.
std::uint32_t pack_reference(std::uint32_t value){std::uint32_t result=0;
    for(unsigned digit=0;digit<8;++digit){result|=(value%12u)<<(digit*4u);value/=12u;}check(value==0,"Test oracle exceeded eight radix12 digits.");return result;}
std::uint32_t unpack_reference(std::uint32_t packed){std::uint64_t result=0;
    for(unsigned digit=8;digit>0;--digit){const auto value=(packed>>((digit-1)*4u))&15u;check(value<12,"GPU/prepared packed data contains a non-duodecimal digit.");result=result*12u+value;}
    check(result<=smo_radix_ablation_limit,"Packed data escaped the shared exact integer domain.");return static_cast<std::uint32_t>(result);}
template<class T>bool zero_padding(const T& value){return std::ranges::all_of(value.reserved,[](std::uint32_t word){return word==0;});}
std::uint64_t exact_power(std::uint32_t base,std::uint32_t power){std::uint64_t result=1;
    for(std::uint32_t exponent=0;exponent<power;++exponent)result*=base;return result;}

void packing_contract(){
    static_assert(sizeof(SmoRadixRecord)==32&&sizeof(SmoRadixTerm)==32);
    static_assert(offsetof(SmoRadixRecord,group)==12&&offsetof(SmoRadixRecord,reserved)==16);
    static_assert(offsetof(SmoRadixTerm,power)==12&&offsetof(SmoRadixTerm,reserved)==16);
    check(smo_radix_ablation_limit==429981695u,"The exact shared domain is no longer12^8-1.");
    for(const auto value:{0u,1u,9u,10u,11u,12u,13u,143u,144u,1727u,1728u,20735u,20736u,429981694u,429981695u}){
        const auto actual=smo_radix_pack12(value);check(actual==pack_reference(value),"CPU packing differs from positional base12 digits.");
        check(smo_radix_unpack12(actual)==value&&unpack_reference(actual)==value,"Exact base12 packing lost an integer magnitude.");}
    std::uint32_t random=0x5a12d00du;
    for(unsigned i=0;i<257;++i){random=random*1664525u+1013904223u;const auto value=random%(smo_radix_ablation_limit+1u);
        check(smo_radix_pack12(value)==pack_reference(value)&&smo_radix_unpack12(pack_reference(value))==value,"Random exact integer round trip changed precision.");}
    rejects([]{(void)smo_radix_pack12(smo_radix_ablation_limit+1u);},"The binary path accepted magnitudes unavailable to eight radix12 digits.");
    rejects([]{(void)smo_radix_pack12(std::numeric_limits<std::uint32_t>::max());},"UINT32 full range was silently confused with the shared radix12 range.");
    for(unsigned place=0;place<8;++place)for(const auto invalid:{12u,13u,14u,15u})
        rejects([&]{(void)smo_radix_unpack12(invalid<<(place*4u));},"Invalid positional digit was silently interpreted as a quantity.");
}

void workload_contract(std::uint32_t count,std::uint32_t groups,std::uint32_t terms,std::uint32_t seed){
    const auto work=smo_radix_workload(count,groups,terms,seed),again=smo_radix_workload(count,groups,terms,seed);
    check(work.groups==groups&&work.terms==terms&&work.items[0].size()==count&&work.items[1].size()==count&&
        work.coefficients[0].size()==static_cast<std::size_t>(groups)*terms&&work.coefficients[1].size()==work.coefficients[0].size()&&work.expected.size()==count,
        "Prepared exact workload changed its declared dimensions.");
    check(work.items==again.items&&work.coefficients==again.coefficients&&work.expected==again.expected,"The same seed did not prepare the identical exact workload.");
    std::vector<std::array<std::uint64_t,3>> sums(groups);
    for(std::uint32_t group=0;group<groups;++group)for(std::uint32_t term=0;term<terms;++term){const auto at=static_cast<std::size_t>(group)*terms+term;
        const auto& binary=work.coefficients[0][at];const auto& duodecimal=work.coefficients[1][at];
        check(zero_padding(binary)&&zero_padding(duodecimal)&&binary.power==duodecimal.power&&(binary.power==2||binary.power==3),
            "A coefficient changed policy, stride or padding between representations.");
        for(unsigned axis=0;axis<3;++axis){check(binary.value[axis]>=1&&binary.value[axis]<=7,"A coefficient escaped the declared exact2/3-power domain.");
            check(duodecimal.value[axis]==pack_reference(binary.value[axis])&&unpack_reference(duodecimal.value[axis])==binary.value[axis],
                "Prepared coefficients perform a different calculation or precision in the two representations.");
            sums[group][axis]+=exact_power(binary.value[axis],binary.power);}
    }
    for(std::uint32_t item=0;item<count;++item){const auto& binary=work.items[0][item];const auto& duodecimal=work.items[1][item];const auto& expected=work.expected[item];
        check(binary.group==item%groups&&duodecimal.group==binary.group&&expected.group==binary.group,"Grouping changed an item's actual emitter membership.");
        check(zero_padding(binary)&&zero_padding(duodecimal)&&zero_padding(expected),"Preparation left uninitialized padding in a timed input or expected output.");
        for(unsigned axis=0;axis<3;++axis){check(binary.value[axis]<=smo_radix_ablation_limit&&duodecimal.value[axis]==pack_reference(binary.value[axis])&&
                unpack_reference(duodecimal.value[axis])==binary.value[axis],"Prepared item values do not have identical exact magnitudes.");
            const auto result=static_cast<std::uint64_t>(binary.value[axis])+sums[binary.group][axis];
            check(result<=smo_radix_ablation_limit&&expected.value[axis]==result,"The workload oracle differs from an independent exact uint64 polynomial sum.");}
    }
}

void cpu_contract(){packing_contract();
    workload_contract(1,1,1,1);workload_contract(65,65,3,2);workload_contract(129,1,9,3);workload_contract(257,7,17,0x13579bdu);
    workload_contract(17,1,4096,0x12u);
    for(const auto dimensions:{std::array{0u,1u,1u},std::array{1u,0u,1u},std::array{1u,1u,0u},std::array{1u,2u,1u},
        std::array{1048577u,1u,1u},std::array{1025u,1025u,1u},std::array{1u,1u,4097u},std::array{1048576u,1024u,4096u}})
        rejects([&]{(void)smo_radix_workload(dimensions[0],dimensions[1],dimensions[2]);},"The workload accepted an out-of-budget preparation.");
    // A wrong policy is rejected, not silently interpreted as the clean
    // ordinary arithmetic baseline. Alternative semantics need new contracts.
    auto invalid=SmoRadixAblationOptions{};invalid.rule=static_cast<SmoRadixRuleContract>(1);
    rejects([&]{(void)run_smo_radix_ablation(invalid);},"An unimplemented semantic profile was silently benchmarked as base12-only.");
    invalid={};invalid.warmups=1;rejects([&]{(void)run_smo_radix_ablation(invalid);},"The ablation silently removed a required warmup.");
    for(const auto rounds:{0u,33u}){invalid={};invalid.rounds=rounds;
        rejects([&]{(void)run_smo_radix_ablation(invalid);},"The ablation accepted an out-of-budget timing sample count.");}
    for(const auto seconds:{-1.,61.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        invalid={};invalid.sustained_seconds=seconds;
        rejects([&]{(void)run_smo_radix_ablation(invalid);},"The ablation accepted an invalid sustained duration.");}
    std::stop_source cancelled;cancelled.request_stop();rejects([&]{(void)run_smo_radix_ablation({},cancelled.get_token());},"Pre-cancelled GPU work was still performed.");
}

void counter_integrity(SmoComputeApi target,const SmoComputeOptions& options){
    const auto work=smo_radix_workload(5,2,3,0x12u);auto gpu=create_smo_compute(target,options);
    const auto kernel=gpu->kernel(smo_radix_ablation_kernel(0));
    const auto bytes=[](const auto& values){return std::as_bytes(std::span(values));};
    const auto input=gpu->buffer(work.items[0].size()*sizeof(SmoRadixRecord),bytes(work.items[0]));
    const auto terms=gpu->buffer(work.coefficients[0].size()*sizeof(SmoRadixTerm),bytes(work.coefficients[0]));
    const auto common=gpu->buffer(work.groups*sizeof(SmoRadixRecord)),output=gpu->buffer(work.expected.size()*sizeof(SmoRadixRecord));
    auto damaged=work.expected;damaged[2].value[0]^=1u;
    const auto expected=gpu->buffer(damaged.size()*sizeof(SmoRadixRecord),bytes(damaged));
    std::array<std::uint32_t,8> initial{};const auto counters=gpu->buffer(sizeof(initial),bytes(initial));
    SmoDispatch call{kernel,{input,terms,common,output,expected,0,0,counters},{5,2,3,1,0},{1,1,1}};
    const auto read_counters=[&]{const auto raw=gpu->read(counters);check(raw.size()==sizeof(initial),"Injected GPU counter read is truncated.");
        std::array<std::uint32_t,8> values;std::memcpy(values.data(),raw.data(),raw.size());return values;};
    const auto check_output=[&]{const auto raw=gpu->read(output);check(raw.size()==work.expected.size()*sizeof(SmoRadixRecord),"Injected test output is truncated.");
        std::vector<SmoRadixRecord> actual(work.expected.size());std::memcpy(actual.data(),raw.data(),raw.size());
        check(actual==work.expected,"Counter fault injection changed the calculated quantity instead of only the expected reference.");};
    const auto expect=[&](std::uint32_t low,std::uint32_t high){const auto actual=read_counters();
        check(actual[0]==low&&actual[1]==high,"The real GPU mismatch counter missed, duplicated or overflowed an injected error.");
        check(std::ranges::all_of(actual.begin()+2,actual.end(),[](std::uint32_t value){return value==0;}),"A mismatch was charged to an unrelated variant.");};
    // Only the RAM-prepared reference has ONE wrong word. The actual kernel
    // inputs/formula remain valid and its output must match the original gold.
    (void)gpu->execute(std::span(&call,1),false);expect(1,0);check_output();
    (void)gpu->execute(std::span(&call,1),false);expect(2,0);check_output();
    initial[0]=0xffffffffu;gpu->write(counters,bytes(initial));
    (void)gpu->execute(std::span(&call,1),false);expect(0,1);check_output();
    // Prove that the final padding word is compared too, not just value.x.
    damaged=work.expected;damaged[2].reserved[3]=1;gpu->write(expected,bytes(damaged));initial={};gpu->write(counters,bytes(initial));
    (void)gpu->execute(std::span(&call,1),false);expect(1,0);check_output();
    gpu->write(expected,bytes(work.expected));initial={};gpu->write(counters,bytes(initial));
    (void)gpu->execute(std::span(&call,1),false);expect(0,0);check_output();
    // Micro mode intentionally disables the extra comparison, but still
    // computes exactly the same output. No sustained minute is run by CTest.
    gpu->write(expected,bytes(damaged));call.constants[3]=0;
    (void)gpu->execute(std::span(&call,1),false);expect(0,0);check_output();
    std::cout<<"PASS real GPU mismatch counter "<<gpu->info().api<<": injected1, cumulative2, uint64 carry, padding, valid gold and disabled micro verification; arithmetic output preserved.\n";
}

void report_contract(const SmoRadixAblationReport& report,const SmoRadixAblationOptions& options){
    check(report.items==options.items&&report.groups==options.groups&&report.terms==options.terms&&report.rounds==options.rounds&&report.warmups==options.warmups,
        "Ablation report changed the requested exact workload or sample count.");
    check(report.equivalent&&report.failures.empty(),"An exact scalar/metadata/padding mismatch was hidden by the ablation report.");
    check(report.rounds_actual>0&&std::isfinite(report.elapsed_seconds)&&report.elapsed_seconds>=0,
        "The ablation omitted its actual completed-round count or wall interval.");
    if(options.sustained_seconds>0)check(!report.device.software&&report.elapsed_seconds>=options.sustained_seconds&&
        report.rounds_actual>=std::max(9u,options.rounds),
        "The sustained hardware test ended before its requested interval or ran on software.");
    else check(report.rounds_actual==options.rounds,"The microbenchmark changed its requested sample count.");
    const auto per_item=static_cast<std::uint64_t>(options.items)*options.terms,grouped=static_cast<std::uint64_t>(options.groups)*options.terms;
    for(unsigned variant=0;variant<4;++variant){const auto& row=report.variants[variant];const auto expected=(variant%2==0)?per_item:grouped;
        check(!row.name.empty()&&row.mismatches==0&&row.item_term_evaluations==expected&&row.scalar_powers==expected*3,
            "The reported reduction changed arithmetic policy or invented an instruction count.");
        const auto retained=std::min<std::uint64_t>(1048576u,report.rounds_actual);
        check(row.gpu_ms.size()==retained&&row.timing_samples_discarded==report.rounds_actual-retained,
            "The ablation silently lost timestamp samples instead of declaring its memory cap.");
        if(row.timing_samples_discarded)check(!report.timing_valid&&std::isnan(row.median_ms),"A truncated timing distribution fabricated valid performance statistics.");
        check(row.gpu_mismatches==0,"The sustained GPU comparison detected mismatches in an unsaved iteration.");
        check(row.gpu_verified_records==(options.sustained_seconds>0?static_cast<std::uint64_t>(options.items)*report.rounds_actual:0u),
            "The sustained test did not compare every written record under its declared dispatch coverage.");
        check(row.canonical_hash==report.variants[0].canonical_hash,"Decoded GPU results differ between representation/grouping variants.");
        if(report.timing_valid)check(std::isfinite(row.median_ms)&&row.median_ms>0&&
            std::ranges::all_of(row.gpu_ms,[](double ms){return std::isfinite(ms)&&ms>0;}),"A valid GPU timing report contains a missing/invalid sample.");
    }
    check(report.variants[0].raw_hash==report.variants[1].raw_hash&&report.variants[2].raw_hash==report.variants[3].raw_hash,
        "Grouping changed the raw representation or left different padding in the output.");
    if(options.groups==options.items)check(per_item==grouped,"The no-sharing control falsely reports fewer term evaluations.");
    else check(grouped<per_item,"The declared shared-emitter case did not reduce duplicated term evaluations.");
    // Floating arithmetic here concerns external timestamp metadata ONLY.
    // It never serves as an oracle for a numerical engine result.
    if(report.timing_valid){const auto& v=report.variants;
        const std::array expected{v[0].median_ms/v[1].median_ms,v[2].median_ms/v[3].median_ms,v[0].median_ms/v[2].median_ms,v[1].median_ms/v[3].median_ms};
        const std::array actual{report.binary_grouping_ratio,report.packed_grouping_ratio,report.per_item_representation_ratio,report.grouped_representation_ratio};
        for(unsigned i=0;i<actual.size();++i)check(std::isfinite(actual[i])&&std::abs(actual[i]-expected[i])<=1e-12*std::max(1.,std::abs(expected[i])),
            "A reported ratio reverses its declared baseline/alternative orientation.");
    }else check(std::isnan(report.binary_grouping_ratio)&&std::isnan(report.packed_grouping_ratio)&&
        std::isnan(report.per_item_representation_ratio)&&std::isnan(report.grouped_representation_ratio),"Invalid clocks fabricated an acceleration ratio.");
}

void print(const SmoRadixAblationReport& report){std::cout<<std::setprecision(12)<<"Exact radix ablation api="<<report.device.api<<" adapter="<<std::quoted(report.device.adapter)
    <<" software="<<report.device.software<<" items="<<report.items<<" groups="<<report.groups<<" terms="<<report.terms
    <<" equivalent="<<report.equivalent<<" timing_valid="<<report.timing_valid<<" elapsed_seconds="<<report.elapsed_seconds
    <<" rounds_actual="<<report.rounds_actual<<" source="<<report.source_identity<<'\n';
    for(unsigned variant=0;variant<report.variants.size();++variant){const auto& row=report.variants[variant];
        std::cout<<char('A'+variant)<<' '<<row.name<<" median_GPU_ms="<<row.median_ms<<" term_evaluations="<<row.item_term_evaluations
            <<" scalar_powers="<<row.scalar_powers<<" mismatches="<<row.mismatches<<" accumulated_GPU_mismatches="<<row.gpu_mismatches
            <<" GPU_records_checked_dispatch_derived="<<row.gpu_verified_records<<" raw_hash="<<row.raw_hash<<" canonical_hash="<<row.canonical_hash
            <<" sample_count="<<row.gpu_ms.size()<<" declared_samples_discarded="<<row.timing_samples_discarded<<" samples_ms=";
        const auto shown=std::min<std::size_t>(16,row.gpu_ms.size());for(std::size_t i=0;i<shown;++i)std::cout<<row.gpu_ms[i]<<',';
        if(shown<row.gpu_ms.size())std::cout<<"...(first16 shown; all samples remain in the result)";std::cout<<'\n';}
    std::cout<<"Ratios A/B="<<report.binary_grouping_ratio<<" C/D="<<report.packed_grouping_ratio<<" A/C="<<report.per_item_representation_ratio
        <<" B/D="<<report.grouped_representation_ratio<<"; representation and algorithm are separate comparisons.\n"<<report.conclusion<<'\n';
}
} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;
    const bool cpu=argc==2&&std::string_view(argv[1])=="cpu",hardware=argc>=2&&std::string_view(argv[1])=="hardware",
        sustained=argc>=2&&std::string_view(argv[1])=="sustained",physical=hardware||sustained;
    check(argc==1||cpu||(physical&&argc<=3),"Usage: smo_radix_ablation_tests [cpu|hardware|sustained [d3d11|d3d12|vulkan|opengl|all]]");
    cpu_contract();if(cpu){std::cout<<"PASS: "<<checks<<" exact CPU preparation/policy contract checks; GPU not executed.\n";return 0;}
    const std::string_view selector=argc==3?argv[2]:physical?"all":"d3d11";const bool all=selector=="all";
    std::optional<std::vector<SmoRadixAblationReport>> baseline;std::string adapter;
    for(const auto target:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){
        if(!all&&target!=smo_compute_api(selector))continue;std::vector<SmoRadixAblationReport> reports;
        if(!sustained){SmoComputeOptions counter_options;counter_options.software=!physical;counter_options.adapter_name=adapter;counter_integrity(target,counter_options);}
        const auto run=[&](std::uint32_t items,std::uint32_t groups,std::uint32_t terms,std::uint32_t seed){
            SmoRadixAblationOptions options;options.api=target;options.device.software=!physical;options.device.adapter_name=adapter;
            options.items=items;options.groups=groups;options.terms=terms;options.seed=seed;options.rounds=physical?9u:3u;options.warmups=2;
            options.sustained_seconds=sustained?60.:0.;
            if(sustained)std::cout<<"BEGIN sustained60s hardware ablation: exact per-record GPU comparison is embedded in all four timed variants; not a pure-ALU time or application FPS.\n";
            auto report=run_smo_radix_ablation(options);if(physical){check(!report.device.software,"A physical-GPU request was silently tested on software.");if(adapter.empty())adapter=report.device.adapter;}
            report_contract(report,options);print(report);reports.push_back(std::move(report));};
        if(sustained)run(65536,256,64,0x2468aceu);
        else{run(65,65,3,2);run(129,1,9,3);run(257,7,17,0x13579bdu);if(hardware)run(65536,256,64,0x2468aceu);}
        if(baseline){check(reports.size()==baseline->size(),"Cross-API ablation coverage differs.");for(std::size_t row=0;row<reports.size();++row){
            check(reports[row].source_identity==(*baseline)[row].source_identity,"Cross-API ablation compiled different numerical sources.");
            for(unsigned variant=0;variant<4;++variant)check(reports[row].variants[variant].raw_hash==(*baseline)[row].variants[variant].raw_hash&&
                reports[row].variants[variant].canonical_hash==(*baseline)[row].variants[variant].canonical_hash,"Cross-API exact ablation results differ; no floating tolerance is permitted.");}}
        else baseline=std::move(reports);
    }
    check(baseline.has_value(),"No requested ablation GPU API was executed.");
    std::cout<<"PASS: "<<checks<<" exact preparation/GPU ablation checks. No fixed speed or base12 superiority is asserted.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL radix ablation: "<<error.what()<<'\n';return 1;}}
