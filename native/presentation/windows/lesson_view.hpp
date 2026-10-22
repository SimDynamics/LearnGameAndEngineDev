#pragma once

#include "platform/windows/environment.hpp"
#include <windows.h>
#include <filesystem>

namespace lgaed::curriculum { struct Lesson; }

namespace lgaed::presentation {

// A native child window owns the lesson, controls and its cancellable execution worker.
// Content is read directly from the existing JSON; no second curriculum is generated.
[[nodiscard]] HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment,
                                     const std::filesystem::path& content_root);
// A loaded model can also be supplied by a catalog or another trusted content adapter.
[[nodiscard]] HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment,
                                     curriculum::Lesson lesson);
[[nodiscard]] bool flush_lesson_view(HWND window);

} // namespace lgaed::presentation
