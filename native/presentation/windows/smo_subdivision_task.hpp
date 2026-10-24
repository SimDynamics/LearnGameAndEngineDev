#pragma once
#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/smo_float12.hpp"
#include "core/smo.hpp"
#include <functional>

namespace lgaed::presentation {
// A bounded, exact affine-subdivision application. The ordinary control uses
// the SAME rational grid/domain, not a lower precision FP32 approximation.
inline constexpr unsigned smo_subdivision_stages=7,smo_subdivision_variants=4;
struct SmoSubdivisionProgram {
    std::string source;
    smo::ProgramIr ir;
    std::vector<std::uint32_t> operations;
    std::vector<SmoF12> literals;
    // Each row computes (a*x+b*y+c*scale)/denominator in one exact grid.
    std::array<std::array<std::uint32_t,4>,2> affine{};
};
[[nodiscard]] std::string smo_subdivision_source();
[[nodiscard]] SmoSubdivisionProgram compile_smo_subdivision(std::string_view);
struct SmoSubdivisionOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device{true,{}};
    std::string source; //empty selects the visible built-in SMO program
    std::uint32_t items=1024,steps=4,rounds=9,warmups=2;
};
struct SmoSubdivisionTiming {
    std::vector<double> task_gpu_ms,conversion_gpu_ms,frame_gpu_ms,frame_wall_ms;
    double task_median_ms=0,conversion_median_ms=0,frame_median_ms=0,wall_median_ms=0;
    std::uint64_t state_hash=0,point_hash=0,image_hash=0;
    bool timing_valid=false;
};
struct SmoSubdivisionReport {
    SmoComputeInfo device;
    std::string source,contract;
    std::uint32_t items=0,steps=0,grid_scale=0,width=256,height=192,operation_count=0;
    // The fourth path removes the VM only after matching the complete parsed
    // instruction/literal stream. Other valid affine sources explicitly use
    // the existing exponent-VM fallback; they are never run as a fixed sample.
    bool direct_f12=false;
    std::array<SmoSubdivisionTiming,smo_subdivision_variants> variants;
    std::array<double,smo_subdivision_stages> shader_asset_ms{},shader_creation_ms{};
    std::vector<SmoComputePreparationEvent> preparation_events;
    double preparation_ms=0,source_compile_ms=0,shader_prepare_ms=0,upload_ms=0;
    double preflight_ms=0,warmup_ms=0,readback_ms=0,validation_ms=0;
    std::uint64_t gpu_buffer_bytes=0,verified_values=0,process_peak_working_set=0;
    bool equivalent=false;
    std::vector<std::uint32_t> preview_bgra;
};
[[nodiscard]] SmoKernelSource smo_subdivision_kernel(unsigned);
[[nodiscard]] SmoSubdivisionReport run_smo_subdivision_task(const SmoSubdivisionOptions& = {},std::stop_token = {},
    std::function<void(std::string_view)> progress = {});
} //namespace lgaed::presentation
