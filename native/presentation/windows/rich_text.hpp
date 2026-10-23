#pragma once

#include "core/lesson.hpp"

#include <windows.h>

namespace lgaed::presentation {
// Android sp at font scale 1 are logical pixels, not Windows points.
// A role is applied before syntax/emphasis/math formatting, never over it.
enum class RichTextRole { body, prompt, heading, lesson_title };
[[nodiscard]] constexpr LONG rich_text_height(RichTextRole role = RichTextRole::body) {
    return 15 * (role == RichTextRole::lesson_title ? 32 : role == RichTextRole::heading ? 23 : 18);
}
[[nodiscard]] std::wstring wide(std::string_view utf8);
[[nodiscard]] std::string utf8(std::wstring_view text);
// Observes actual character geometry and zoom without changing selection,
// scroll, text, focus or Undo. Whitespace remains available for lesson gestures.
[[nodiscard]] bool rich_text_hit_test(HWND edit,POINT point);
// Plain editing preserves the original characters and exposes paragraph breaks as LF.
[[nodiscard]] std::string edit_text(HWND edit);
void set_edit_text(HWND edit, std::string_view text);
// Uses the HWND's optional reader_theme surface; untouched controls keep their
// system palette. Never changes text, selection, scroll or the undo history.
void style_cpp(HWND edit);
// Display-only inline delimiters are hidden; original lesson strings remain untouched.
void set_rich_text(HWND edit, std::string_view text,
    std::span<const curriculum::Emphasis> emphasis = {}, bool code = false,
    RichTextRole role = RichTextRole::body);
} // namespace lgaed::presentation
