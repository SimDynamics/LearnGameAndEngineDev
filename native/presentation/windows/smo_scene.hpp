#pragma once
#include "presentation/windows/smo_compute.hpp"
#include <array>
#include <functional>
#include <string_view>

namespace lgaed::presentation {
struct SmoSceneBody {std::array<float,3> position{};float radius=0;std::array<float,3> velocity{};float contacts=0;};
static_assert(sizeof(SmoSceneBody)==32);
struct SmoSceneWorkload {std::uint32_t width=640,height=360,bodies=256,group_size=16;bool operator==(const SmoSceneWorkload&)const=default;};
struct SmoSustainedOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device;
    SmoSceneWorkload workload;
    double seconds=60,warmup_seconds=2;
    bool calibrate=true;
};
struct SmoSceneProgress {std::string api,phase;double elapsed=0;std::uint64_t frames=0;std::uint32_t width=0,height=0;std::vector<std::uint32_t> preview_bgra;};
struct SmoSceneMetric {std::vector<double> gpu_ms,cpu_ms;double median_ms=0,p95_ms=0,p99_ms=0;bool valid=false;};
struct SmoSceneCheckpoint {std::uint64_t frame=0;std::vector<SmoSceneBody> bodies;std::vector<std::array<float,4>> pixels;};
struct SmoSustainedReport {
    SmoComputeInfo device;SmoSceneWorkload workload;
    std::string identity,conclusion;
    double elapsed_seconds=0,max_error=0;std::uint64_t frames=0,contacts=0,dynamic_frames=0,state_mismatches=0,pixel_mismatches=0,invalid_values=0;
    std::array<std::uint64_t,2> state_pair_mismatches{},pixel_pair_mismatches{}; // A/B, A/C
    std::array<std::uint64_t,3> ray_sphere_tests{};std::array<std::uint64_t,2> ray_box_tests{};
    std::array<SmoSceneMetric,3> metrics;
    std::vector<SmoSceneCheckpoint> checkpoints;
    bool equivalent=false,target_reached=false;
};
struct SmoSceneDifference {
    std::string phase;
    std::uint32_t api=0,frame=0,element=0,component=0,expected_bits=0,actual_bits=0;
    float expected=0,actual=0;
    std::uint64_t ulps=0;
    bool outside_tolerance=false;
};
struct SmoSceneDiagnosticReport {
    SmoSceneWorkload workload;
    std::uint32_t frames=0;
    std::vector<SmoComputeInfo> devices;
    std::vector<SmoSceneDifference> first_exact,first_outside_tolerance;
    std::vector<SmoSceneDifference> contact_trace;
    bool contact_trace_reproduced=true;
};
[[nodiscard]] std::string smo_compute_api_name(SmoComputeApi);
[[nodiscard]] SmoComputeApi smo_compute_api(std::string_view);
[[nodiscard]] std::unique_ptr<SmoCompute> create_smo_compute(SmoComputeApi,const SmoComputeOptions&,std::stop_token={});
[[nodiscard]] SmoKernelSource smo_scene_kernel(unsigned stage,unsigned mode);
[[nodiscard]] std::vector<SmoSceneBody> smo_scene_initial(std::uint32_t count);
// Independent ordinary FP32 oracles. Their domain uses magnitudes and
// nonnegative coefficients; generic SMO multiplication is not redefined.
[[nodiscard]] std::vector<SmoSceneBody> smo_scene_step_reference(std::span<const SmoSceneBody>,std::uint32_t frame);
[[nodiscard]] std::array<float,4> smo_scene_ray_reference(std::span<const SmoSceneBody>,std::uint32_t pixel,const SmoSceneWorkload&);
void smo_compute_smoke(SmoComputeApi,const SmoComputeOptions& = {},std::stop_token={});
// Untimed diagnostic, independent of the sustained benchmark: each phase gets
// identical raw input bytes across APIs. Native trajectory accumulation is
// checked separately so an earlier integration rounding difference cannot be
// misdiagnosed as a collision or ray-tracing implementation defect.
[[nodiscard]] SmoSceneDiagnosticReport diagnose_smo_scene(std::span<const SmoComputeApi>,
    const SmoSceneWorkload&,std::uint32_t frames=1,const SmoComputeOptions& = {},std::stop_token={});
[[nodiscard]] SmoSustainedReport run_smo_sustained(const SmoSustainedOptions&,
    std::function<void(SmoSceneProgress)> progress={},std::stop_token={});
}
