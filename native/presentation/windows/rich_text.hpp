#pragma once

#include "core/lesson.hpp"

#include <windows.h>

namespace lgaed::presentation {
[[nodiscard]] std::wstring wide(std::string_view utf8);
[[nodiscard]] std::string utf8(std::wstring_view text);
// Plain editing preserves the original characters and exposes paragraph breaks as LF.
[[nodiscard]] std::string edit_text(HWND edit);
void set_edit_text(HWND edit, std::string_view text);
// Applies syntax colors without changing text, selection, scroll or the undo history.
void style_cpp(HWND edit);
// Display-only inline delimiters are hidden; original lesson strings remain untouched.
void set_rich_text(HWND edit, std::string_view text,
    std::span<const curriculum::Emphasis> emphasis = {}, bool code = false);
} // namespace lgaed::presentation
