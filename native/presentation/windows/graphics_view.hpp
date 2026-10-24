#pragma once
#include "platform/windows/environment.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace lgaed::smo { struct Frame; }

namespace lgaed::presentation {
enum class GraphicsBackend { automatic, hardware, warp };
struct GraphicsBodySnapshot {float x{},y{},z{},velocity_x{},velocity_y{},velocity_z{},radius{},u{},v{};};
struct GraphicsLabSnapshot {
    std::uint64_t presented_frames=0;
    std::size_t colored_pixels=0;
    bool benchmark=false,software=false;
    unsigned benchmark_side=8,objects=14,width=0,height=0;
    int selected=-1;
    double frames_per_second=0,milliseconds_per_frame=0;
    float simulation_time=0,recent_impact=0;
    std::array<GraphicsBodySnapshot,14> bodies{};
    std::string backend,error;
};
struct GraphicsFrame;
[[nodiscard]] HWND create_physics_graphics_view(HWND parent,std::optional<WindowsEnvironment>,
    std::function<void()> on_back,GraphicsBackend initial=GraphicsBackend::automatic);
// Readback runs on the renderer's own thread. It never advances physics or the
// displayed frame, and is used only by explicit verification requests.
[[nodiscard]] GraphicsLabSnapshot inspect_graphics_lab(HWND,bool read_pixels=true);
[[nodiscard]] GraphicsFrame inspect_physics_graphics_frame(HWND);

struct SmoSurfaceSnapshot {
    std::uint64_t frames=0,pixel_hash=0;
    std::size_t colored_pixels=0;
    unsigned width=0,height=0;
    bool software=false;
    std::string backend,error;
};
// This surface has no simulation clock: every vertex, transform and draw
// request comes from the SMO interpreter. D3D is only its graphics support.
[[nodiscard]] HWND create_smo_surface(HWND parent,GraphicsBackend initial=GraphicsBackend::automatic);
void draw_smo_surface(HWND,const smo::Frame&);
// Explicit real back-buffer readback; does not run SMO or advance the frame.
[[nodiscard]] SmoSurfaceSnapshot inspect_smo_surface(HWND,bool read_pixels=true);
}
