#pragma once

#include "core/catalog.hpp"

namespace lgaed::windows {
// Follows existing indices only. Missing translations fall back per asset to
// Spanish, matching the Android curriculum adapter; malformed files do not.
[[nodiscard]] curriculum::CatalogPage load_catalog(const std::filesystem::path& content_root,
                                                   const curriculum::CatalogLocation& location);
[[nodiscard]] curriculum::CatalogScope catalog_scope(const std::filesystem::path& content_root,
                                                      const curriculum::CatalogLocation& location);
} // namespace lgaed::windows
