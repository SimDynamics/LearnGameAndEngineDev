#pragma once

#include "core/lesson.hpp"

#include <windows.h>
#include <cstddef>
#include <utility>

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
// Keep the RichEdit renderer/syntax colors, but accept only text from paste/drop.
// Idempotent; the HWND owns the callback and releases it when destroyed.
void use_plain_text_input(HWND edit);
// Empty editable RichEdit hint: never inserts text, participates in Undo or
// exposes a false value to narration. Only its synthetic empty-paragraph
// selection is collapsed; real document selection remains native.
void set_edit_hint(HWND edit, std::string_view text, COLORREF color = RGB(140,145,155), bool centered = false);
// Android's display-only compactCodeSpaces: narrow U+0020 visually, without
// replacing characters, syntax spans, selection, scrolling or Undo.
void compact_code_spaces(HWND edit);
// Uses the HWND's optional reader_theme surface; untouched controls keep their
// system palette. Never changes text, selection, scroll or the undo history.
void style_cpp(HWND edit);
// Display-only inline delimiters are hidden; original lesson strings remain untouched.
void set_rich_text(HWND edit, std::string_view text,
    std::span<const curriculum::Emphasis> emphasis = {}, bool code = false,
    RichTextRole role = RichTextRole::body, std::string_view locale = "es",
    std::span<const std::pair<std::size_t,std::size_t>> mathematics = {});
} // namespace lgaed::presentation
