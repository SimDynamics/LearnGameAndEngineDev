#pragma once
#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/smo_duodecimal.hpp"
#include <array>
#include <limits>
#include <span>

namespace lgaed::presentation {
// Separate integer-radix12 experiment; these layouts never replace the FP32
// scene. Every scalar is the six-word SmoD12 ABI, including its diagnostics.
inline constexpr std::uint32_t smo_d12_scene_body_words=48;
inline constexpr std::uint32_t smo_d12_scene_pixel_words=32;
inline constexpr std::uint32_t smo_d12_scene_body_contacts_word=42;
inline constexpr std::uint32_t smo_d12_scene_body_diagnostic_word=43;
inline constexpr std::uint32_t smo_d12_scene_pixel_hit_word=24;
inline constexpr std::uint32_t smo_d12_scene_pixel_diagnostic_word=25;
inline constexpr std::uint32_t smo_d12_scene_pixel_shadow_word=26;
inline constexpr std::uint32_t smo_d12_scene_pixel_bgra_word=27;
inline constexpr std::uint32_t smo_d12_scene_pixel_tests_word=28;
struct SmoD12SceneBody {
    std::array<SmoD12,3> position{};
    SmoD12 radius;
    std::array<SmoD12,3> velocity{};
    std::uint32_t contacts=0,diagnostic=0;
    std::array<std::uint32_t,4> reserved{};
    bool operator==(const SmoD12SceneBody&)const=default;
};
struct SmoD12ScenePixel {
    std::array<SmoD12,3> color{};
    SmoD12 distance;
    std::uint32_t hit=std::numeric_limits<std::uint32_t>::max(),diagnostic=0,shadow=0,bgra=0,tests=0;
    std::array<std::uint32_t,3> reserved{};
    bool operator==(const SmoD12ScenePixel&)const=default;
};
static_assert(sizeof(SmoD12SceneBody)==smo_d12_scene_body_words*4&&sizeof(SmoD12ScenePixel)==smo_d12_scene_pixel_words*4);
static_assert(offsetof(SmoD12SceneBody,radius)==18*4&&offsetof(SmoD12SceneBody,velocity)==24*4&&
    offsetof(SmoD12SceneBody,contacts)==42*4&&offsetof(SmoD12SceneBody,diagnostic)==43*4);
static_assert(offsetof(SmoD12ScenePixel,distance)==18*4&&offsetof(SmoD12ScenePixel,hit)==24*4&&
    offsetof(SmoD12ScenePixel,diagnostic)==25*4&&offsetof(SmoD12ScenePixel,bgra)==27*4);
struct SmoD12SceneWorkload {
    std::uint32_t bodies=16,width=64,height=36;
    bool operator==(const SmoD12SceneWorkload&)const=default;
};
// All constants describing quantities are duodecimal literals. Parameters are
// only integer counts/indices: [0] bodies, [1] width, [2] height, [3] frame.
// Stage0 init: b1 output; stage1 integrate: b0 input/b1 output;
// stage2 collisions: b0 integrated/b1 corrected; stage3 primary rays: b0 state,
// b1 pixels. A valid hit temporarily stores its ray origin in the RGB slots
// and BGRA0 means pending. Stage4 shades those hits in b1 in place (b0 bodies);
// misses/fatal records are already final. All reserved words stay zero.
[[nodiscard]] std::array<std::uint32_t,16> smo_duodecimal_scene_constants(const SmoD12SceneWorkload&,std::uint32_t frame=0);
// fp32_control=true deliberately selects the separately labelled FP32 backend
// for these same equations. The default remains actual integer-radix12 D12.
[[nodiscard]] SmoKernelSource smo_duodecimal_scene_kernel(unsigned stage,bool fp32_control=false,bool floating12=false);
// Independent CPU control flow over the checked 128-bit CPU radix12 core.
// No float/double is used by initialization, physics, ray tests or RGB packing.
// floating12 selects the version-tagged adaptive profile. False retains the
// original fixed-point experiment and its exact reference behavior.
[[nodiscard]] std::vector<SmoD12SceneBody> smo_duodecimal_scene_initial(const SmoD12SceneWorkload& = {},bool floating12=false);
[[nodiscard]] std::vector<SmoD12SceneBody> smo_duodecimal_scene_step_reference(std::span<const SmoD12SceneBody>,bool floating12=false);
[[nodiscard]] SmoD12ScenePixel smo_duodecimal_scene_ray_reference(std::span<const SmoD12SceneBody>,std::uint32_t pixel,const SmoD12SceneWorkload& = {},bool floating12=false);
}
