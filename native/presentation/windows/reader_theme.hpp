#pragma once

#include <windows.h>
#include <optional>
#include <string_view>

namespace lgaed::state { struct StateData; }

namespace lgaed::presentation::reader_theme {
// These values mirror the Android XML surfaces; menu screens deliberately use
// a different canvas and card from lesson pages.
enum class Surface { canvas, header, card, field, screen, catalog_card, card_header, inset, low, note, popup,
    compiler_header, compiler_editor, settings_defaults, compiler_output };
struct Palette {
    COLORREF canvas, header, card, field, text, muted, accent, outline;
    bool high_contrast;
};
// Register already-present Android-equivalent fonts privately for this process.
// This does not install fonts or change the user's Windows font registration.
void prepare_fonts();
// The same unmodified OFL Noto Serif 1.07 family used by Android's serif face.
// Its normal/italic resources are private to this process, never OS-installed.
[[nodiscard]] const wchar_t* math_font_face();
// Byte-identical Apache-2.0 Droid Sans Mono from Android's monospace family.
[[nodiscard]] const wchar_t* code_font_face();

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
// Exact Android LessonReadingColor policy: opaque neutral gray, >=4.5:1.
[[nodiscard]] COLORREF unread_color(COLORREF background);
[[nodiscard]] HBRUSH brush(Surface surface);
// Only the supplied HWND is themed. Existing characters, spans and editing
// state are untouched; apply before formatting rich text or code.
// RichEdit keeps native scroll/input semantics; its visual-style scrollbar
// animation yields to our nonclient skin outside Windows high contrast.
void apply(HWND window, Surface surface);
// Repaint only the actual system scrollbar rectangles in window coordinates.
// Native hit testing, scroll positions, messages and accessibility stay owned
// by Windows. In system high contrast the native renderer remains in charge.
void paint_scrollbars(HWND window,HDC window_dc);
// SetScrollInfo can redraw the native frame without sending WM_NCPAINT.
// Call after changing a custom viewport's range; no scroll state is changed.
void refresh_scrollbars(HWND window);
// A custom viewport can keep USER's scroll range for keyboard/accessibility
// while drawing no native scrollbar frame. Suppress its frame before native
// nonclient layout/painting; hiding it after SetScrollInfo is visibly too late.
void set_scrollbar_frame_hidden(HWND window,bool vertical,bool horizontal = false);
[[nodiscard]] bool control_color(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);
[[nodiscard]] bool draw_button(const DRAWITEMSTRUCT& item);
// Source-over completion tint, calibrated to the existing Android catalogue.
[[nodiscard]] COLORREF completion_background(COLORREF surface);
void completed_control(HWND window, bool completed);
void primary_button(HWND button, bool primary = true);
// Native BUTTON semantics, focus, accessible name and click handling remain
// unchanged. Only the fill/frame are removed for Android-style icon actions.
void icon_button(HWND button, bool icon = true);
void premium_button(HWND button, bool premium = true);
// Paint an existing checkbox as the Android Options switch. Its BUTTON role,
// checked state, accessible name, notifications and keyboard remain native.
// System high contrast retains the ordinary checkbox presentation.
void switch_button(HWND button, bool is_switch = true);
void set_text_color(HWND window,std::optional<COLORREF> color);
// A mouse click must not leave a dotted Win32 selection rectangle. Keyboard
// navigation still receives a restrained, solid focus outline.
[[nodiscard]] bool keyboard_focus_visible(HWND window);
void paint_keyboard_focus(HWND window,HDC dc,RECT bounds,COLORREF ink);

// RichEdit formatting uses its local surface; untouched controls retain the
// original Windows system colors.
[[nodiscard]] bool applied(HWND window);
[[nodiscard]] COLORREF background(HWND window);
[[nodiscard]] COLORREF foreground(HWND window);
} // namespace lgaed::presentation::reader_theme
