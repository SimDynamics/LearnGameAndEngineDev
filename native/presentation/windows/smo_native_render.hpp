#pragma once
#include "presentation/windows/smo_native_gpu.hpp"
#include <limits>

namespace lgaed::presentation {
inline constexpr unsigned smo_native_render_stages=5;
enum class SmoNativeRenderMode {cached,reference_uncached};
struct SmoNativeImage {
    std::uint32_t width=0,height=0,diagnostic=0;
    std::uint64_t colored_pixels=0,pixel_hash=0;
    std::vector<std::uint32_t> pixels;
    double gpu_ms=std::numeric_limits<double>::quiet_NaN();
    double wall_ms=std::numeric_limits<double>::quiet_NaN();
    double readback_ms=std::numeric_limits<double>::quiet_NaN();
};
struct SmoNativeRenderGeometry {
    std::vector<std::array<std::uint32_t,3>> vertices;
    std::vector<std::uint32_t> diagnostics;
};
[[nodiscard]] SmoKernelSource smo_native_render_kernel(unsigned stage);
// Uses the program's SAME GPU executor/buffers. All rotation, trigonometry,
// perspective, triangle intersection and depth are native hardware binary32.
// Normal rendering reads BGRA + a diagnostic code, never geometric quantities.
// Preview trigonometric inputs are explicitly limited to |angle|<=1000000
// radians, matching the earlier preview. Excess is diagnosed, never clamped.
class SmoNativeRenderer {
public:
    explicit SmoNativeRenderer(SmoCompute&,std::uint32_t width=256,std::uint32_t height=144);
    ~SmoNativeRenderer();
    SmoNativeRenderer(SmoNativeRenderer&&) noexcept;
    SmoNativeRenderer& operator=(SmoNativeRenderer&&) noexcept;
    SmoNativeRenderer(const SmoNativeRenderer&)=delete;
    SmoNativeRenderer& operator=(const SmoNativeRenderer&)=delete;
    void resize(std::uint32_t width,std::uint32_t height);
    [[nodiscard]] SmoNativeImage render(const SmoNativeGpuFrameView&,bool timed=false,SmoNativeRenderMode mode=SmoNativeRenderMode::cached);
    // Explicit QA evidence only. UI rendering NEVER calls this readback.
    [[nodiscard]] SmoNativeRenderGeometry inspect_geometry()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Author-time assets: SMO_NATIVE_RENDER_STAGE0..4, names native_render0..4.
// Stage4 is the opt-in uncached QA control, loaded lazily, never by the UI.
// Prefixes smo_shader_hlsl/glsl.inc + smo_native_render.inc (no F12 source).
// Roots LGAED_SMO_NATIVE_RENDER_SHADER_ROOT / ..._HLSL_SHADER_ROOT.
} //namespace lgaed::presentation
