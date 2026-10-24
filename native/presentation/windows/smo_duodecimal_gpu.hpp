#pragma once
#include "presentation/windows/smo_duodecimal_scene.hpp"
#include "presentation/windows/smo_scene.hpp"
#include <functional>

namespace lgaed::presentation {
struct SmoD12Case {std::string name;std::uint32_t operation=0,profile=0;SmoD12 left,right;};
struct SmoD12Failure {std::string name,reason;std::uint32_t operation=0,profile=0;SmoD12Result expected,actual;};
struct SmoD12Validation {
    SmoComputeInfo device;std::string identity;
    bool floating12=false;
    std::uint64_t cases=0;std::array<std::uint64_t,10> coverage{};
    std::array<double,11> shader_preparation_ms{};
    std::vector<std::uint32_t> profiles;
    std::vector<SmoD12Failure> failures;
    // Exact raw values/diagnostics for cross-API checks, never FP tolerances.
    std::vector<SmoD12Result> outputs;
};
struct SmoD12SceneOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;SmoComputeOptions device;
    std::uint32_t bodies=16,width=64,height=36,minimum_frames=2;
    double seconds=.08,warmup_seconds=0;
    bool fp32_control=false,floating12=false;
};
struct SmoD12Checkpoint {
    std::uint32_t frame=0;std::vector<SmoD12SceneBody> bodies;std::vector<SmoD12ScenePixel> pixels;
    // The immediately preceding GPU state, retained from the previous readback.
    // It permits an independent CPU check of every saved transition without
    // CPU physics or ray work interrupting the sustained GPU interval.
    std::vector<SmoD12SceneBody> previous_bodies;
};
struct SmoD12SceneReport {
    SmoComputeInfo device;SmoD12SceneOptions options;std::string identity,scene_identity;
    std::uint64_t frames=0,dynamic_frames=0,contacts=0,fatal_values=0,inexact_values=0,ray_tests=0;
    std::uint64_t changed_state_frames=0,validated_checkpoints=0,validated_body_steps=0,validated_rays=0;
    double seconds=0,median_ms=0,p95_ms=0,p99_ms=0;
    bool timing_valid=false,completed=false;
    std::vector<double> gpu_ms;
    std::vector<SmoD12Checkpoint> checkpoints;
    std::vector<std::string> failures;
};
[[nodiscard]] SmoKernelSource smo_duodecimal_rules_kernel(unsigned operation,bool floating12=false);
[[nodiscard]] std::vector<SmoD12Case> smo_duodecimal_cases(bool exhaustive=true,bool floating12=false);
[[nodiscard]] SmoD12Validation validate_smo_duodecimal(SmoComputeApi,const SmoComputeOptions& = {},bool exhaustive=true,std::stop_token={},std::function<void(unsigned,bool)> preparation={},bool floating12=false);
[[nodiscard]] SmoD12SceneReport run_smo_duodecimal_scene(const SmoD12SceneOptions&,
    std::function<void(SmoSceneProgress)> progress={},std::stop_token={});
}
