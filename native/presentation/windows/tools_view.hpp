#pragma once

#include "platform/windows/environment.hpp"
#include <windows.h>
#include <functional>
#include <string>
#include <cstdint>

namespace lgaed::presentation {
// Native counterparts of the imported Android calculator/graphics entry points;
// these are tools, not another educational content or answer source.
[[nodiscard]] HWND create_calculator_view(HWND parent,const WindowsEnvironment&,std::function<void()> on_back);
[[nodiscard]] HWND create_graphics_view(HWND parent,std::function<void()> on_back);
[[nodiscard]] HWND create_graphics_view(HWND parent,const WindowsEnvironment&,std::function<void()> on_back);
struct GraphicsFrame {
    std::uint64_t presented_frames = 0;
    std::size_t colored_pixels = 0;
    double rotation = 0;
    bool wireframe = false;
    std::string backend;
};
// Explicit readback of the real Direct3D back buffer, never used by its frame
// loop. Allows native verification without sampling another app's screen.
[[nodiscard]] GraphicsFrame inspect_graphics_frame(HWND);
} // namespace lgaed::presentation
