#pragma once

#include "platform/windows/environment.hpp"
#include "core/lesson_state.hpp"
#include <windows.h>
#include <functional>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

namespace lgaed::windows { class NarrationWorker; }

namespace lgaed::presentation {
struct CalculatorPreferences {
    std::function<state::StateData()> read;
    std::function<void(const std::function<void(state::StateData&)>&)> update;
};
// Native counterparts of the imported Android calculator/graphics entry points;
// these are tools, not another educational content or answer source.
[[nodiscard]] HWND create_calculator_view(HWND parent,const WindowsEnvironment&,std::function<void()> on_back,
    std::shared_ptr<windows::NarrationWorker> backend={},std::function<void(std::string_view)> paste_result={},
    CalculatorPreferences preferences={});
// Owned modal interaction without a nested message loop or another engine.
[[nodiscard]] HWND open_calculator_window(HWND owner,const WindowsEnvironment&,
    std::shared_ptr<windows::NarrationWorker>,std::function<void(std::string_view)> paste_result,
    CalculatorPreferences preferences={});
struct CalculatorSnapshot {
    std::string input,draft,result,error;
    bool degrees=false,advanced=false,editing_keys=false,evaluating=false;
    std::vector<std::string> key_order;
};
[[nodiscard]] CalculatorSnapshot inspect_calculator(HWND);
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
