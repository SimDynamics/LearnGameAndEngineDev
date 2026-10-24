#pragma once

#include "core/catalog.hpp"
#include "platform/windows/environment.hpp"
#include <windows.h>
#include <functional>
#include <optional>

namespace lgaed::presentation {
enum class CatalogAction { settings, audio, calculator, compiler, graphics, smo };
using CatalogActionHandler = std::function<void(CatalogAction,const curriculum::CatalogLocation&)>;
using CatalogActionAvailability = std::function<bool(CatalogAction,const curriculum::CatalogLocation&)>;
using CatalogMathValidator = std::function<std::optional<bool>(const curriculum::Block&,const state::Response&,std::string_view)>;
[[nodiscard]] HWND create_catalog_view(HWND parent, const WindowsEnvironment& environment,
    const std::filesystem::path& content_root,
    std::function<void(const std::filesystem::path&)> on_open_lesson,
    CatalogActionHandler on_action = {}, CatalogActionAvailability can_action = {},
    CatalogMathValidator validate_math = {});
[[nodiscard]] curriculum::CatalogLocation catalog_location(HWND);
void navigate_catalog(HWND,const curriculum::CatalogLocation&);
// Reload progress after returning from a lesson without altering its page,
// responses, drafts, narration positions or the catalog's own list position.
void refresh_catalog_view(HWND);
void navigate_catalog_home(HWND);
// Returns false at the routes root without changing focus, selection or scroll.
[[nodiscard]] bool navigate_catalog_back(HWND);
} // namespace lgaed::presentation
