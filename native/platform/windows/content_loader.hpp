#pragma once

#include "core/lesson.hpp"

namespace lgaed::windows {

// Windows' complete JSON parser; malformed or unsupported contracts throw.
[[nodiscard]] curriculum::Lesson load_lesson(
    const std::filesystem::path& path, std::string_view expected_locale = "es");
[[nodiscard]] curriculum::Lesson parse_lesson_json(
    std::string_view utf8, std::string_view expected_locale = "es");

} // namespace lgaed::windows
