#pragma once

#include "core/catalog.hpp"
#include <optional>

namespace lgaed::windows {
// Follows existing indices only. Missing translations fall back per asset to
// Spanish, matching the Android curriculum adapter; malformed files do not.
[[nodiscard]] curriculum::CatalogPage load_catalog(const std::filesystem::path& content_root,
                                                   const curriculum::CatalogLocation& location);
[[nodiscard]] curriculum::CatalogScope catalog_scope(const std::filesystem::path& content_root,
                                                      const curriculum::CatalogLocation& location);
// nullopt is a temporarily unavailable validator, not an incorrect answer.
using CatalogMathCompletion = std::function<std::optional<bool>(const curriculum::Block&,const state::Response&,std::string_view)>;
// Exact Android card metrics: courses count completed lessons, units retain
// those counts but weight partial lessons equally, lessons count completed pages.
[[nodiscard]] curriculum::CatalogProgress catalog_entry_progress(
    const std::filesystem::path& content_root,const curriculum::CatalogLocation& parent,
    const curriculum::CatalogEntry&,const state::StateData&,const CatalogMathCompletion& = {});
[[nodiscard]] std::optional<curriculum::CatalogLocation> next_catalog_lesson(
    const std::filesystem::path& content_root,const curriculum::CatalogLocation& current,
    bool between_units,bool between_courses);
// Restores only an available lesson listed under these exact current parents.
// The saved ID is never interpreted as a filename or searched across routes.
[[nodiscard]] std::optional<curriculum::CatalogEntry> active_catalog_lesson(
    const std::filesystem::path& content_root,const curriculum::CatalogLocation& current,
    std::string_view active_lesson_id);
} // namespace lgaed::windows
