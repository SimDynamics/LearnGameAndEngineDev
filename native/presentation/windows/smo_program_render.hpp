#pragma once
#include "presentation/windows/smo_program_gpu.hpp"
#include <array>
#include <limits>
#include <memory>
#include <span>

namespace lgaed::presentation {
inline constexpr std::uint32_t smo_program_render_stages=5;
enum class SmoProgramRenderMode {cached,reference_uncached};
inline constexpr std::uint32_t smo_program_render_vertex_words=20,smo_program_render_pixel_words=9;
inline constexpr std::uint32_t smo_program_render_face_words=80;
struct SmoProgramRenderPixel {
    SmoF12 depth;
    std::uint32_t bgra=0,face=0xffffffffu,diagnostic=0;
    bool operator==(const SmoProgramRenderPixel&)const=default;
};
static_assert(sizeof(SmoProgramRenderPixel)==smo_program_render_pixel_words*4);
struct SmoProgramImage {
    std::uint32_t width=0,height=0,diagnostic=0;
    std::uint64_t colored_pixels=0,pixel_hash=0;
    std::vector<std::uint32_t> pixels;
    // Optional exact F12 depth/face evidence; never converted to host FP.
    std::vector<SmoProgramRenderPixel> exact_pixels;
    // Opt-in component profiling only. Normal UI rendering performs no query.
    double gpu_ms=std::numeric_limits<double>::quiet_NaN();
    double wall_ms=std::numeric_limits<double>::quiet_NaN();
    double readback_ms=std::numeric_limits<double>::quiet_NaN();
};
[[nodiscard]] SmoKernelSource smo_program_render_kernel(unsigned stage);
class SmoProgramRenderer {
public:
    explicit SmoProgramRenderer(SmoCompute&,std::uint32_t width=256,std::uint32_t height=144);
    ~SmoProgramRenderer();
    SmoProgramRenderer(SmoProgramRenderer&&) noexcept;
    SmoProgramRenderer& operator=(SmoProgramRenderer&&) noexcept;
    SmoProgramRenderer(const SmoProgramRenderer&)=delete;
    SmoProgramRenderer& operator=(const SmoProgramRenderer&)=delete;
    void resize(std::uint32_t width,std::uint32_t height);
    [[nodiscard]] SmoProgramImage render(const SmoGpuFrame&,bool capture_exact=false,bool timed=false,SmoProgramRenderMode mode=SmoProgramRenderMode::cached);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
using SmoProgramRenderVector=std::array<SmoF12,3>;
struct SmoProgramSinCos {SmoF12 sine,cosine;std::uint32_t diagnostic=0;};
// Independent CPU-F12 acceptance oracles, explicitly NOT UI/render fallbacks.
[[nodiscard]] SmoProgramSinCos smo_program_sincos_reference(SmoF12 angle);
[[nodiscard]] SmoProgramImage smo_program_render_reference(std::span<const SmoProgramRenderVector> vertices,
    std::span<const std::array<std::uint32_t,3>> faces,const SmoProgramRenderVector& rotation,
    const SmoProgramRenderVector& color,std::uint32_t width,std::uint32_t height,bool draw_requested=true,std::uint32_t diagnostic=0);
// GPU bindings: b0 VM vertices, b1 VM faces, b2 VM frame40words;
// b3 transformed camera-space vertices20words; b4 sin/cos+color scratch40words;
// b5 pixel records9words; b6 per-face cache80words; b7 unused. Stage0 setup,
// stage1 transform, stage3 prepares e1/e2/t/q/numerator once per face, stage2
// perspective per-pixel triangle intersection/depth, all integer F12.
// Cached q/numerator diagnostics are consumed ONLY in their original branches.
// Stage4 is a lazily loaded QA control: identical F12 operations recomputed per
// ray, without the face prepass. UI/default rendering keeps the cached path.
// Constants: width0,height1,vertex_count2,face_count3,vertex_offset4,face_offset5,
// frame_offset6,setup_phase7 (phase1 aggregates transformed diagnostics).
// Offsets/counts are addressing metadata, never host coordinates.
} //namespace lgaed::presentation
