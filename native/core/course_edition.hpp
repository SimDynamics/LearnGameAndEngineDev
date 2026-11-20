#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lgaed::curriculum {
// The descriptor belongs to an authored course publication. Its new IDs keep
// progress, drafts and reader bookmarks separate without deleting old data.
struct CourseEdition {
    std::string course_id, locale, id;
    std::size_t generation = 0;
    bool operator==(const CourseEdition&) const = default;
};
inline bool course_edition_identifier(std::string_view value, std::size_t maximum) {
    return !value.empty() && value.size() <= maximum &&
        std::ranges::all_of(value, [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}
inline bool uses_course_edition_namespace(std::string_view value) {
    return value.find("__edition_") != value.npos;
}
inline void validate_course_edition(const CourseEdition& edition) {
    if (!course_edition_identifier(edition.course_id, 256) ||
        uses_course_edition_namespace(edition.course_id) ||
        !course_edition_identifier(edition.id, 64) || edition.id == "legacy" ||
        (edition.locale != "es" && edition.locale != "en" && edition.locale != "fr") ||
        !edition.generation || edition.generation > 1000000)
        throw std::invalid_argument("La edición curricular no tiene un descriptor válido.");
    // The shortest publishable lesson needs "u__l" after its edition prefix.
    // Reject a valid-looking descriptor that could never publish any lesson ID.
    const std::size_t shortest_lesson_id =
        edition.course_id.size() + std::string_view("__edition_").size() +
        edition.locale.size() + 1 + std::to_string(edition.generation).size() +
        std::string_view("__u__l").size();
    if(shortest_lesson_id > 256)
        throw std::invalid_argument("La edición curricular excede el límite de ID de lección.");
}
inline std::string course_edition_prefix(const CourseEdition& edition) {
    validate_course_edition(edition);
    return edition.course_id + "__edition_" + edition.locale + '_' +
        std::to_string(edition.generation) + "__";
}
inline bool course_edition_slug(std::string_view value) {
    const auto alphanumeric = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9');
    };
    return course_edition_identifier(value, 128) && value.find("__") == value.npos &&
        alphanumeric(static_cast<unsigned char>(value.front())) &&
        alphanumeric(static_cast<unsigned char>(value.back()));
}
inline bool course_edition_unit_id(const CourseEdition& edition, std::string_view id) {
    const auto prefix = course_edition_prefix(edition);
    return id.size() <= 256 && id.starts_with(prefix) &&
        course_edition_slug(id.substr(prefix.size()));
}
inline bool course_edition_lesson_id(const CourseEdition& edition, std::string_view id) {
    const auto prefix = course_edition_prefix(edition);
    if (id.size() > 256 || !id.starts_with(prefix)) return false;
    const auto remainder = id.substr(prefix.size());
    const auto separator = remainder.find("__");
    return separator != remainder.npos && course_edition_slug(remainder.substr(0, separator)) &&
        course_edition_slug(remainder.substr(separator + 2));
}
inline std::string course_edition_frame(const CourseEdition& edition) {
    validate_course_edition(edition);
    return "course-edition/v1/" + edition.course_id + '/' + edition.locale + '/' +
        std::to_string(edition.generation) + '/' + edition.id + '/';
}
} // namespace lgaed::curriculum
