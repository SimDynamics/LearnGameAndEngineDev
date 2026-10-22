#pragma once

#include <windows.h>
#include <optional>
#include <string_view>

namespace lgaed::state { struct StateData; }

namespace lgaed::presentation::reader_theme {
enum class Surface { canvas, header, card, field };
struct Palette {
    COLORREF canvas, header, card, field, text, muted, accent, outline;
    bool high_contrast;
};

[[nodiscard]] Palette palette();
// Inherits an accent from the closest parent with an explicit local value.
// High contrast always retains the actual Windows system highlight.
[[nodiscard]] Palette palette(HWND context);
void set_accent(HWND,std::optional<COLORREF> accent);
// Reads the original Android appearance_settings keys without persisting or
// changing any preference. Unknown routes use the configured global accent.
[[nodiscard]] COLORREF appearance_accent(const state::StateData&,std::string_view route = {});
[[nodiscard]] COLORREF accent_foreground(HWND context);
// Ink over an existing surface, not a replacement accent/fill. Keep the
// configured color when it has 3:1 contrast; otherwise use readable theme ink.
[[nodiscard]] COLORREF accent_ink(HWND context,COLORREF surface);
[[nodiscard]] HBRUSH brush(Surface surface);
// Only the supplied HWND is themed. Existing characters, spans and editing
// state are untouched; apply before formatting rich text or code.
void apply(HWND window, Surface surface);
// Repaint only the actual system scrollbar rectangles in window coordinates.
// Native hit testing, scroll positions, messages and accessibility stay owned
// by Windows. In system high contrast the native renderer remains in charge.
void paint_scrollbars(HWND window,HDC window_dc);
// SetScrollInfo can redraw the native frame without sending WM_NCPAINT.
// Call after changing a custom viewport's range; no scroll state is changed.
void refresh_scrollbars(HWND window);
[[nodiscard]] bool control_color(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);
[[nodiscard]] bool draw_button(const DRAWITEMSTRUCT& item);
void primary_button(HWND button, bool primary = true);

// RichEdit formatting uses its local surface; untouched controls retain the
// original Windows system colors.
[[nodiscard]] bool applied(HWND window);
[[nodiscard]] COLORREF background(HWND window);
[[nodiscard]] COLORREF foreground(HWND window);
} // namespace lgaed::presentation::reader_theme
