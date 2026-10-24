#pragma once
#include "presentation/windows/smo_compute.hpp"
#include <array>
#include <limits>

namespace lgaed::presentation {
inline constexpr std::uint32_t smo_radix_ablation_limit=429981695u; //12^8-1
inline constexpr unsigned smo_radix_ablation_stages=6;
enum class SmoRadixVariant:unsigned {binary_per_item,binary_grouped,packed12_per_item,packed12_grouped};
// Identical32-byte storage/stride for BOTH numerical representations. group
// and power are addressing/control metadata, never encoded quantities.
struct SmoRadixRecord {
    std::array<std::uint32_t,3> value{};
    std::uint32_t group=0;
    std::array<std::uint32_t,4> reserved{};
    bool operator==(const SmoRadixRecord&)const=default;
};
struct SmoRadixTerm {
    std::array<std::uint32_t,3> value{};
    std::uint32_t power=2;
    std::array<std::uint32_t,4> reserved{};
    bool operator==(const SmoRadixTerm&)const=default;
};
static_assert(sizeof(SmoRadixRecord)==32&&sizeof(SmoRadixTerm)==32);
struct SmoRadixWorkload {
    std::uint32_t groups=0,terms=0;
    // Index0: exact native uint. Index1: eight positional radix12 nibbles.
    // Prepared ONCE before any GPU timing; no per-operation round trips.
    std::array<std::vector<SmoRadixRecord>,2> items;
    std::array<std::vector<SmoRadixTerm>,2> coefficients;
    std::vector<SmoRadixRecord> expected;
};
[[nodiscard]] std::uint32_t smo_radix_pack12(std::uint32_t);
[[nodiscard]] std::uint32_t smo_radix_unpack12(std::uint32_t);
[[nodiscard]] SmoRadixWorkload smo_radix_workload(std::uint32_t items,std::uint32_t groups,std::uint32_t terms,std::uint32_t seed=0x13579bdu);
struct SmoRadixMismatch {
    unsigned variant=0,round=0;
    std::uint32_t item=0;
    SmoRadixRecord expected,raw_actual,decoded_actual;
    std::string reason;
};
struct SmoRadixVariantResult {
    std::string name;
    std::vector<double> gpu_ms;
    double median_ms=std::numeric_limits<double>::quiet_NaN();
    std::uint64_t item_term_evaluations=0,scalar_powers=0,mismatches=0;
    std::uint64_t raw_hash=0,canonical_hash=0;
    std::uint64_t gpu_mismatches=0,gpu_verified_records=0,timing_samples_discarded=0;
};
// Reserved extension point for separately specified rule contracts. The clean
// representation experiment currently accepts ordinary_exact ONLY: differing
// SMO semantics must never be relabelled as a representation speedup.
enum class SmoRadixRuleContract {ordinary_exact};
struct SmoRadixAblationOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device{true,{}};
    std::uint32_t items=1024,groups=16,terms=16,seed=0x13579bdu;
    unsigned warmups=2,rounds=9;
    double sustained_seconds=0; //0 micro;60 explicitly requests a real minute
    SmoRadixRuleContract rule=SmoRadixRuleContract::ordinary_exact;
};
struct SmoRadixAblationReport {
    SmoComputeInfo device;
    std::string source_identity,conclusion;
    std::uint32_t items=0,groups=0,terms=0;
    unsigned warmups=0,rounds=0;
    double elapsed_seconds=0;
    std::uint64_t rounds_actual=0;
    std::array<SmoRadixVariantResult,4> variants;
    std::vector<SmoRadixMismatch> failures;
    bool equivalent=false,timing_valid=false;
    // Baseline/alternative: A/B,C/D,A/C,B/D respectively. Values>1 mean
    // grouping/packed is faster; every ratio stays NaN if any result differs
    // or any real GPU timestamp is invalid. These are NOT application FPS.
    double binary_grouping_ratio=std::numeric_limits<double>::quiet_NaN();
    double packed_grouping_ratio=std::numeric_limits<double>::quiet_NaN();
    double per_item_representation_ratio=std::numeric_limits<double>::quiet_NaN();
    double grouped_representation_ratio=std::numeric_limits<double>::quiet_NaN();
};
// Root shader factory:0 binary per-item,1 binary group sum,2 binary expand;
// 3 packed12 per-item,4 packed12 group sum,5 packed12 expand.
[[nodiscard]] SmoKernelSource smo_radix_ablation_kernel(unsigned stage);
[[nodiscard]] SmoRadixAblationReport run_smo_radix_ablation(const SmoRadixAblationOptions& = {},std::stop_token={});
// b0 item32,b1 term32(group-major),b2 common32,b3 output32,b4 expected32,
// b7 four uint64 mismatch counters. Constants: item_count0,group_count1,
// term_count2,verification_enabled3,variant4. Sustained GPU timings INCLUDE
// the same raw eight-word comparison in ALL variants; atomics only on error.
// Micro disables that comparison. No FP instructions/views in either path.
} //namespace lgaed::presentation
