#pragma once

#include "core/lesson_state.hpp"
#include <filesystem>
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
struct CatalogProgress { std::size_t completed = 0, total = 0; };
void validate_catalog_location(const CatalogLocation&);
[[nodiscard]] CatalogLevel catalog_level(const CatalogLocation&);
[[nodiscard]] CatalogLocation catalog_parent(CatalogLocation);
[[nodiscard]] CatalogLocation catalog_child(CatalogLocation, std::string_view id);
[[nodiscard]] std::filesystem::path catalog_index_path(const CatalogLocation&);
[[nodiscard]] std::filesystem::path catalog_lesson_path(const CatalogLocation&, std::string_view id);
[[nodiscard]] std::string catalog_position_key(const CatalogLocation&);
[[nodiscard]] bool catalog_lesson_completed(const state::StateData&, std::string_view lesson_id);
[[nodiscard]] bool catalog_lesson_started(const state::StateData&, std::string_view lesson_id);
[[nodiscard]] CatalogProgress catalog_progress(const state::StateData&,const CatalogScope&);
// Explicit user-confirmed reset only. Keeps draft source/stdin, installed
// resources, cache, preferences and unrelated courses/routes byte-for-byte.
std::size_t reset_catalog_progress(state::StateData&,const CatalogScope&);
} // namespace lgaed::curriculum
