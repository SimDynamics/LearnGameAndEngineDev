#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace lgaed::windows {
namespace {
using namespace winrt::Windows::Data::Json;
using namespace curriculum;
void require(bool condition, std::string_view message) { if (!condition) throw std::runtime_error("Catálogo: " + std::string(message)); }
std::string text(const JsonObject& object, const wchar_t* name) {
    const auto value = object.GetNamedValue(name); require(value.ValueType() == JsonValueType::String,"se esperaba texto.");
    return winrt::to_string(value.GetString());
}
struct Resolved { std::filesystem::path path; std::string locale; };
Resolved resolve(const std::filesystem::path& root, std::string_view locale, const std::filesystem::path& relative) {
    for (const auto language : {std::string(locale),std::string("es")}) {
        const auto path = root / language / relative;
        state::require_safe_path(path);
        if (std::filesystem::is_regular_file(state::native_io_path(path))) return {path,language};
    }
    return {};
}
IJsonValue read(const std::filesystem::path& path) {
    auto bytes = state::read_bytes(path,8 * 1024 * 1024);
    std::string_view text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    return JsonValue::Parse(winrt::to_hstring(text));
}
std::string route_title(std::string_view route, std::string_view locale) {
    if (route == "programming") return locale == "fr" ? "Programmation avancée et architecture des moteurs" : locale == "en" ? "Advanced Programming and Engine Architecture" : "Programación Avanzada y Arquitectura de Motores";
    return locale == "fr" ? "Mathématiques et physique" : locale == "en" ? "Mathematics and Physics" : "Matemáticas y Física";
}
}
curriculum::CatalogPage load_catalog(const std::filesystem::path& content_root, const curriculum::CatalogLocation& location) {
    validate_catalog_location(location); ensure_winrt_apartment(); state::require_safe_path(content_root);
    require(std::filesystem::is_directory(state::native_io_path(content_root)),"no existe la carpeta curricular.");
    try {
        CatalogPage result{location,{},{}};
        const auto level = catalog_level(location);
        if (level == CatalogLevel::routes) {
            result.title = location.locale == "fr" ? "Parcours" : location.locale == "en" ? "Learning paths" : "Rutas de aprendizaje";
            for (const auto id : {"programming","math_physics"}) {
                const auto asset = resolve(content_root,location.locale,catalog_index_path(catalog_child(location,id)));
                result.entries.push_back({id,route_title(id,location.locale),0,!asset.path.empty(),asset.path,asset.locale});
            }
            return result;
        }
        if (level == CatalogLevel::units || level == CatalogLevel::lessons) {
            const auto parent = load_catalog(content_root,catalog_parent(location));
            const auto& owner = level == CatalogLevel::units ? location.course : location.unit;
            require(std::ranges::any_of(parent.entries,[&](const auto& entry) { return entry.id == owner; }),
                "la ubicación no está publicada en su índice padre.");
        }
        const auto asset = resolve(content_root,location.locale,catalog_index_path(location));
        require(!asset.path.empty(),"este índice todavía no contiene material instalado.");
        const auto json = read(asset.path);
        JsonArray entries;
        if (level == CatalogLevel::courses) {
            entries = json.GetArray(); result.title = route_title(location.route,location.locale);
        } else {
            const auto object = json.GetObject(); result.title = text(object,L"title");
            if (level == CatalogLevel::units) {
                require(text(object,L"id") == location.course,"el índice pertenece a otro curso.");
                entries = object.GetNamedArray(L"units");
            } else {
                require(text(object,L"courseId") == location.course && text(object,L"unitId") == location.unit,"el índice pertenece a otra unidad.");
                entries = object.GetNamedArray(L"lessons");
            }
        }
        require(entries.Size() <= 10000,"demasiadas entradas.");
        std::set<std::string> ids;
        for (const auto& value : entries) {
            const auto item = value.GetObject(); CatalogEntry entry;
            entry.id = text(item,L"id"); entry.title = text(item,L"title");
            require(!entry.title.empty() && ids.insert(entry.id).second,"título vacío o identificador duplicado.");
            const auto number = item.GetNamedValue(L"number");
            require(number.ValueType() == JsonValueType::Number,"número de entrada inválido.");
            const auto ordinal = number.GetNumber();
            require(std::isfinite(ordinal) && ordinal >= 1 && ordinal <= 1000000 && std::trunc(ordinal) == ordinal,"número de entrada inválido.");
            entry.number = static_cast<std::size_t>(ordinal);
            const auto relative = level == CatalogLevel::lessons ? catalog_lesson_path(location,entry.id)
                : catalog_index_path(catalog_child(location,entry.id));
            const auto installed = resolve(content_root,location.locale,relative);
            entry.asset = installed.path; entry.asset_locale = installed.locale; entry.available = !entry.asset.empty();
            result.entries.push_back(std::move(entry));
        }
        return result;
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Índice curricular inválido: " + winrt::to_string(error.message()));
    }
}
curriculum::CatalogScope catalog_scope(const std::filesystem::path& root,const curriculum::CatalogLocation& location) {
    auto list_location = location; list_location.lesson.clear();
    const auto page = load_catalog(root,list_location); CatalogScope result;
    if (!location.course.empty()) result.courses.push_back(location.course);
    if (!location.unit.empty()) result.units.push_back(location.unit);
    if (!location.lesson.empty()) {
        require(std::ranges::any_of(page.entries,[&](const auto& entry) { return entry.id == location.lesson; }),"la lección no pertenece al índice publicado.");
        result.lessons.push_back(location.lesson); return result;
    }
    for (const auto& entry : page.entries) {
        if (catalog_level(location) == CatalogLevel::lessons) { result.lessons.push_back(entry.id); continue; }
        if (!entry.available) continue;
        const auto child = catalog_scope(root,catalog_child(location,entry.id));
        for (const auto& id : child.courses) if (std::ranges::find(result.courses,id) == result.courses.end()) result.courses.push_back(id);
        for (const auto& id : child.units) if (std::ranges::find(result.units,id) == result.units.end()) result.units.push_back(id);
        result.lessons.insert(result.lessons.end(),child.lessons.begin(),child.lessons.end());
    }
    return result;
}
} // namespace lgaed::windows
