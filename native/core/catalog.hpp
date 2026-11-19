#pragma once

#include "core/lesson_state.hpp"
#include "core/lesson.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {
enum class CatalogLevel { routes, courses, units, lessons };
struct CatalogLocation {
    std::string locale = "es", route, course, unit, lesson;
    bool operator==(const CatalogLocation&) const = default;
};
struct CatalogEntry {
    std::string id, title;
    std::size_t number = 0;
    bool available = false;
    std::filesystem::path asset;
    std::string asset_locale;
};
struct CatalogPage { CatalogLocation location; std::string title; std::vector<CatalogEntry> entries; };
struct CatalogScope { std::vector<std::string> courses, units, lessons; };
struct CatalogProgress {
    // Counter text: completed lessons for course/unit, completed pages for a lesson.
    std::size_t completed = 0, total = 0;
    // Android units weight every lesson equally (its truncated percentage),
    // while retaining completed-lesson counts in the text beside the bar.
    std::size_t learning_completed = 0, learning_total = 0;
    bool pending_validation = false;
    [[nodiscard]] std::size_t percentage() const {
        const auto count = learning_total ? learning_completed : completed;
        const auto maximum = learning_total ? learning_total : total;
        return maximum ? std::min<std::size_t>(100, count * 100 / maximum) : 0;
    }
};
using CatalogExerciseCompletion = std::function<bool(const Block&)>;
void validate_catalog_location(const CatalogLocation&);
[[nodiscard]] CatalogLevel catalog_level(const CatalogLocation&);
[[nodiscard]] CatalogLocation catalog_parent(CatalogLocation);
[[nodiscard]] CatalogLocation catalog_child(CatalogLocation, std::string_view id);
[[nodiscard]] std::filesystem::path catalog_index_path(const CatalogLocation&);
[[nodiscard]] std::filesystem::path catalog_lesson_path(const CatalogLocation&, std::string_view id);
[[nodiscard]] std::string catalog_position_key(const CatalogLocation&);
// New editions cannot reuse the old unit-list ordinal. Existing historical
// catalog keys remain unchanged for courses without an edition.
[[nodiscard]] std::string catalog_position_key(const CatalogPage&);
// Persist stable entry identity alongside legacy list indices. An authored
// reorder must not silently select a different unit in the same edition.
[[nodiscard]] std::string catalog_saved_entry_id(const CatalogPage&, std::int64_t index);
[[nodiscard]] std::size_t catalog_restore_entry_index(const CatalogPage&,
    std::string_view saved_id, std::int64_t saved_index, std::size_t total_count);
[[nodiscard]] bool catalog_lesson_completed(const state::StateData&, std::string_view lesson_id);
[[nodiscard]] bool catalog_lesson_started(const state::StateData&, std::string_view lesson_id);
[[nodiscard]] CatalogProgress catalog_progress(const state::StateData&,const CatalogScope&);
// Membership follows historical progress groups, never the displayed page that
// happens to contain a moved exercise. The callback checks the current response.
[[nodiscard]] CatalogProgress catalog_lesson_page_progress(const state::StateData&,const Lesson&,
                                                          const CatalogExerciseCompletion&);
[[nodiscard]] CatalogProgress catalog_unit_learning_progress(const state::StateData&,const CatalogScope&,
                                                             std::span<const CatalogProgress> lessons);
// Explicit user-confirmed reset only. Keeps draft source/stdin, installed
// resources, cache, preferences and unrelated courses/routes byte-for-byte.
std::size_t reset_catalog_progress(state::StateData&,const CatalogScope&);
} // namespace lgaed::curriculum
