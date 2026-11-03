#pragma once
#include <windows.h>

namespace lgaed::presentation {
struct WindowFrameColors { COLORREF caption,text,border; };
// High contrast returns the native DWM defaults for every attribute. The
// ordinary caption uses the global application accent only while active.
[[nodiscard]] WindowFrameColors window_frame_colors(COLORREF accent,bool active,bool high_contrast) noexcept;
// UI-thread only, idempotent. Owns a small subclass until WM_NCDESTROY. Does
// not replace nonclient handling, activate/disable windows or write settings.
// Unsupported DWM attributes retain Windows' standard frame without errors.
void update_window_frame(HWND window,COLORREF global_accent) noexcept;
}
