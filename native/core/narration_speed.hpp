#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace lgaed::narration {
// Playback changes the existing recording's clock, never Kokoro synthesis or
// its cache identity. Keep all selectors and native players on the same range.
inline constexpr std::array playback_rates{.5,1.,1.5,2.,2.5,3.};
[[nodiscard]] inline double clamp_playback_rate(double value) {
    return std::isfinite(value)?std::clamp(value,playback_rates.front(),playback_rates.back()):1.;
}
}
