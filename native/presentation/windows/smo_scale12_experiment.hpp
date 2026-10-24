#pragma once
#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/smo_float12.hpp"
#include <array>
#include <limits>

namespace lgaed::presentation {
// Scope: the PRIMARY value of the existing F12 multiplication convenience
// helper (profile0), including its numeric diagnostics. This is not complete
// SMO Number equivalence: alternatives/result flags/pdir are not transported.
inline constexpr unsigned smo_scale12_experiment_stages=4;
// Identical input/output record sizes in every kernel. Native uint32 control
// consumes ONLY the explicitly eligible integer subset, not arbitrary F12.
struct SmoScale12Input {
    SmoF12 value,factor;
    std::int32_t exponent_hint=0; //evidence only; fast GPU derives k from factor
    std::uint32_t binary_value=0,binary_factor=0,binary_eligible=0;
};
static_assert(sizeof(SmoScale12Input)==64);
struct SmoScale12Output {
    SmoF12 value;
    std::array<std::uint32_t,2> reserved{};
    bool operator==(const SmoScale12Output&)const=default;
};
static_assert(sizeof(SmoScale12Output)==32);
struct SmoScale12Options {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device{true,{}};
    std::uint32_t items=1024,warmups=2,rounds=9,sustained_seconds=0;
};
struct SmoScale12Timing {
    std::vector<double> gpu_ms;
    double median_ms=std::numeric_limits<double>::quiet_NaN();
    bool valid=false;
    std::uint64_t output_hash=0,discarded_samples=0;
};
struct SmoScale12Report {
    SmoComputeInfo device;
    std::uint32_t items=0,binary_items=0,validation_cases=0,fast_cases=0,verifier_fault_cases=0;
    std::uint64_t input_hash=0,verified_words=0,rounds_actual=0;
    // In sustained mode every output word is checked on the GPU before reuse.
    // Coverage is derived from submitted verifier ranges; flags retain errors
    // permanently until the experiment ends, without a wrapping error count.
    std::uint64_t gpu_verified_words=0,verifier_polls=0;
    std::array<std::uint32_t,2> gpu_mismatch_flags{};
    bool resident_verification=false;
    double elapsed_seconds=0;
    bool equivalent=false; //primary multiplication only; see scope above
    SmoScale12Timing general,exponent,binary;
    double general_over_exponent=std::numeric_limits<double>::quiet_NaN();
};
// Positive 12^k is representable exactly for -181<=k<=203 in this profile.
[[nodiscard]] SmoF12 smo_scale12_power(std::int32_t k);
[[nodiscard]] bool smo_scale12_fast_eligible(const SmoScale12Input&);
[[nodiscard]] std::vector<SmoScale12Input> smo_scale12_validation_inputs();
[[nodiscard]] std::vector<SmoScale12Input> smo_scale12_inputs(std::uint32_t count);
// Independent CPU F12 oracle. All reference generation occurs before timing.
[[nodiscard]] SmoScale12Output smo_scale12_reference(const SmoScale12Input&);
[[nodiscard]] SmoScale12Report run_smo_scale12_experiment(const SmoScale12Options& = {},std::stop_token = {});
// Root supplies author-time assets: prefix+smo_float12.inc+experiment.inc,
// #define SMO_SCALE12_STAGE0..3; names scale12_stage0..3. Entry CSMain/main.
// Stage0=general F12 product;1=exact exponent specialization with fallback;
// 2=native uint32 fixed scale;3=all8word verifier (b1 actual,b2 expected,
// b7 two sticky uint flags; Params.x=count,.y=variant). No float instructions.
[[nodiscard]] SmoKernelSource smo_scale12_experiment_kernel(unsigned stage);
} //namespace lgaed::presentation
