#pragma once
#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/smo_rule_contract.hpp"
#include <array>

namespace lgaed::presentation {
struct SmoRuleGpuMismatch {
    std::string name;
    std::uint32_t variant=0;
    SmoRuleInput input;
    SmoRuleResult expected,actual;
    std::uint32_t expected_state=0,actual_state=0;
};
struct SmoRuleGpuBenchmark {
    std::string name;
    SmoRuleOperation operation=SmoRuleOperation::add;
    std::uint32_t profile=0,selection=0,cases=0;
    std::array<std::vector<double>,3> gpu_ms;
    std::array<double,3> median_ms{};
    std::array<std::uint64_t,3> mismatches{},unresolved_states_preserved{};
    double maximum_error=0;
    bool equivalent=false,timing_valid=false;
};
struct SmoRuleGpuReport {
    SmoComputeInfo device;
    std::string source_identity,conclusion;
    std::size_t cases=0,profiles=0,packed_cases=0,rational_cases=0;
    std::array<std::uint64_t,3> mismatches{},unresolved_states_preserved{};
    std::vector<SmoRuleGpuMismatch> failures;
    std::vector<SmoRuleGpuBenchmark> benchmarks;
    double maximum_error=0,reference_ms=0;
    bool equivalent=false;
};
struct SmoRuleGpuOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device;
    std::uint32_t benchmark_cases=1024;
    unsigned rounds=3;
    bool benchmark=true;
};
// Variants: optimized ordinary implementation of the SAME rule contract,
// generic SMO evaluator, and SMO with uniform operation/profile specialization.
// All diagnostic, exact packed/rational and skipped-update fields are checked;
// only usable FP32 support views use the existing 2e-4 tolerance.
[[nodiscard]] SmoKernelSource smo_rule_gpu_kernel(unsigned variant);
[[nodiscard]] SmoRuleGpuReport run_smo_rule_gpu(const SmoRuleGpuOptions& = {},std::stop_token={});
}
