#pragma once
#include <windows.h>

namespace lgaed::presentation {
enum class Icon { back, home, settings, speaker, language, terminal, copy, reset,
    undo, redo, expand, play, pause, rewind3, rewind5, forward3, forward5 };
// Uses the original Android vector paths when an equivalent drawable exists.
// The requested tint is independent of the glyph, including high contrast.
void draw_icon(HDC, RECT, Icon, COLORREF);
}
