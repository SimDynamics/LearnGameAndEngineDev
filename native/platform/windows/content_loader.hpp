#pragma once

#include "core/lesson.hpp"
#include <optional>

namespace lgaed::windows {

// Same explicit aliases/normalization as Android NarrationLanguage.fromTag.
// Content loading, preparation and the visible reader share this one contract.
[[nodiscard]] std::optional<std::string> narration_language_tag(std::string_view authored);

// Reads the shared descriptor of a course, unit or lesson. Absence is legacy;
// malformed descriptors fail before any current-course state is opened.
[[nodiscard]] std::optional<curriculum::CourseEdition> parse_course_edition_metadata(
    std::string_view utf8, std::string_view expected_locale = "es");

// Windows' complete JSON parser; malformed or unsupported contracts throw.
[[nodiscard]] curriculum::Lesson load_lesson(
    const std::filesystem::path& path, std::string_view expected_locale = "es");
[[nodiscard]] curriculum::Lesson parse_lesson_json(
    std::string_view utf8, std::string_view expected_locale = "es");

// Read-only catalog projection: validates the same JSON/state identities and
// preserves exact response payloads without opening editors or executing any
// CMake/multi-file compiler activity. It cannot be used to open an editor.
[[nodiscard]] curriculum::Lesson parse_lesson_progress_metadata(
    std::string_view utf8, std::string_view expected_locale = "es");
[[nodiscard]] curriculum::Lesson load_lesson_progress_metadata(
    const std::filesystem::path& path, std::string_view expected_locale = "es");

} // namespace lgaed::windows
