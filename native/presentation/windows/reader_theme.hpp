#pragma once

#include <windows.h>

namespace lgaed::presentation::reader_theme {
enum class Surface { canvas, header, card, field };
struct Palette {
    COLORREF canvas, header, card, field, text, muted, accent, outline;
    bool high_contrast;
};

[[nodiscard]] Palette palette();
[[nodiscard]] HBRUSH brush(Surface surface);
// Only the supplied HWND is themed. Existing characters, spans and editing
// state are untouched; apply before formatting rich text or code.
void apply(HWND window, Surface surface);
[[nodiscard]] bool control_color(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);
[[nodiscard]] bool draw_button(const DRAWITEMSTRUCT& item);
void primary_button(HWND button, bool primary = true);

// RichEdit formatting uses its local surface; untouched controls retain the
// original Windows system colors.
[[nodiscard]] bool applied(HWND window);
[[nodiscard]] COLORREF background(HWND window);
[[nodiscard]] COLORREF foreground(HWND window);
} // namespace lgaed::presentation::reader_theme
