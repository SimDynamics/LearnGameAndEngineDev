#pragma once

#include <string>
#include <string_view>

namespace lgaed::math {
enum class AngleUnit { radians, degrees };
// Parses the supplied expression directly; no services, guessed results or
// modifications to educational formula/content are involved.
[[nodiscard]] double calculate(std::string_view expression,AngleUnit unit = AngleUnit::radians);
[[nodiscard]] std::string format_calculation(double value);
} // namespace lgaed::math
