#pragma once
#include "presentation/windows/icons.hpp"
#include <algorithm>
#include <cmath>

namespace lgaed::presentation::attempt_indicator {
// Attach the recovery time to the glyph, not to the bottom of its hit target.
// The existing top labels, font, horizontal centering and button size stay put.
inline RECT recovery_bounds(RECT button, const RECT& glyph) {
    button.top = std::clamp(glyph.bottom, button.top, button.bottom);
    return button;
}

// Both colors use the very same vector rectangle. Complementary clips prevent
// drawing accent over a white antialiased edge (which leaves a pale fringe).
inline void draw(HDC dc, const RECT& bounds, Icon icon,
                 COLORREF accent, COLORREF pending, double progress) {
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
    const double fraction = std::isfinite(progress) ? std::clamp(progress, 0., 1.) : 0.;
    const LONG split = bounds.left + static_cast<LONG>(std::lround((bounds.right - bounds.left) * fraction));
    const auto layer = [&](LONG left, LONG right, COLORREF color) {
        if (left >= right) return;
        const int saved = SaveDC(dc);
        if (!saved) return;
        IntersectClipRect(dc, left, bounds.top, right, bounds.bottom);
        draw_icon(dc, bounds, icon, color);
        RestoreDC(dc, saved);
    };
    layer(split, bounds.right, pending);
    layer(bounds.left, split, accent);
}
} // namespace lgaed::presentation::attempt_indicator
