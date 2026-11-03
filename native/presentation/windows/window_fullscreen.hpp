#pragma once
#include <windows.h>

namespace lgaed::presentation {
// Changes only the native frame/placement of this root HWND. The same child
// pages, documents and playback remain alive. A second toggle restores it.
[[nodiscard]] bool toggle_window_fullscreen(HWND root) noexcept;
[[nodiscard]] bool window_fullscreen(HWND root) noexcept;
// Call before dialog/editor dispatch, including nested internal modal loops.
// Key repeat is consumed without repeatedly toggling the native frame.
[[nodiscard]] bool dispatch_window_fullscreen_key(HWND root,const MSG& message) noexcept;
}
