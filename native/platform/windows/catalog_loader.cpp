#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
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
std::shared_ptr<const Lesson> progress_metadata(const CatalogEntry& entry) {
    struct Cached {
        std::filesystem::file_time_type stamp;
        std::uintmax_t size = 0;
        std::shared_ptr<const Lesson> lesson;
    };
    static std::mutex lock;
    static std::map<std::pair<std::filesystem::path,std::string>,Cached> entries;
    const auto path = state::native_io_path(entry.asset);
    const auto stamp = std::filesystem::last_write_time(path);
    const auto size = std::filesystem::file_size(path);
    const auto key = std::make_pair(entry.asset,entry.asset_locale);
    {
        const std::lock_guard guard(lock);
        const auto found = entries.find(key);
        if (found != entries.end() && found->second.stamp == stamp && found->second.size == size) {
            require(found->second.lesson->id == entry.id,"el recurso de progreso pertenece a otra lección.");
            return found->second.lesson;
        }
    }
    auto lesson = std::make_shared<const Lesson>(load_lesson_progress_metadata(path,entry.asset_locale));
    require(lesson->id == entry.id,"el recurso de progreso pertenece a otra lección.");
    require(std::filesystem::last_write_time(path) == stamp && std::filesystem::file_size(path) == size,
        "el contenido cambió durante la lectura del progreso.");
    {
        const std::lock_guard guard(lock);
        if (entries.size() >= 2048) entries.erase(entries.begin());
        entries[key] = Cached{stamp,size,lesson};
    }
    return lesson;
}
CatalogProgress lesson_progress(const CatalogEntry& entry,const state::StateData& data,
                               const CatalogMathCompletion& math_correct) {
    if (!entry.available) return {};
    const auto lesson = progress_metadata(entry);
    // Android initializes immutable state-layout ownership when loading the
    // lesson. Do the same on a private read-only projection; catalog rendering
    // never changes preferences or migrates the student's actual stored data.
    state::StateData metadata;
    if (const auto domain = data.preferences.find("learning_progress"); domain != data.preferences.end()) {
        const auto prefix = "lesson_" + lesson->id + '_';
        for (const auto& [key,value] : domain->second)
            if (key.starts_with(prefix)) metadata.preferences["learning_progress"].emplace(key,value);
    }
    state::prepare_lesson_state(metadata,*lesson);
    bool pending = false;
    auto result = catalog_lesson_page_progress(metadata,*lesson,[&](const Block& block) {
        if(block.type==BlockType::cpp_compiler)return compiler_puzzle_completed(data,*lesson,block);
        const auto saved = state::get_response(data,exercise_key(*lesson,block));
        if (!saved || !saved->correct) return false;
        if (block.type != BlockType::math_puzzle) return true;
        if (!math_correct) { pending = true; return false; }
        const auto verified = math_correct(block,*saved,lesson->locale);
        if (!verified) { pending = true; return false; }
        return *verified;
    });
    result.pending_validation = pending;
    return result;
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
curriculum::CatalogProgress catalog_entry_progress(const std::filesystem::path& root,
    const curriculum::CatalogLocation& parent,const curriculum::CatalogEntry& entry,
    const state::StateData& data,const CatalogMathCompletion& math_correct) {
    if (!entry.available) return {};
    const auto page = load_catalog(root,parent);
    const auto published = std::ranges::find_if(page.entries,[&](const auto& item) {
        return item.id == entry.id && item.asset == entry.asset && item.asset_locale == entry.asset_locale;
    });
    require(published != page.entries.end(),"el progreso no pertenece a la entrada publicada.");
    switch (catalog_level(parent)) {
    case CatalogLevel::courses:
        return catalog_progress(data,catalog_scope(root,catalog_child(parent,entry.id)));
    case CatalogLevel::units: {
        const auto unit = catalog_child(parent,entry.id);
        const auto lessons = load_catalog(root,unit);
        const auto scope = catalog_scope(root,unit);
        std::vector<CatalogProgress> progress;
        for (const auto& lesson : lessons.entries) progress.push_back(lesson_progress(lesson,data,math_correct));
        return catalog_unit_learning_progress(data,scope,progress);
    }
    case CatalogLevel::lessons:
        return lesson_progress(entry,data,math_correct);
    default:
        return {};
    }
}
std::optional<curriculum::CatalogLocation> next_catalog_lesson(const std::filesystem::path& root,
    const curriculum::CatalogLocation& current,bool between_units,bool between_courses) {
    validate_catalog_location(current); require(!current.lesson.empty(),"la continuación necesita una lección publicada.");
    auto unit = current; unit.lesson.clear(); const auto lessons = load_catalog(root,unit);
    const auto selected = std::ranges::find_if(lessons.entries,[&](const auto& entry) { return entry.id == current.lesson; });
    require(selected != lessons.entries.end() && selected->available,"la lección actual no está disponible en su índice.");
    for (auto entry = std::next(selected); entry != lessons.entries.end(); ++entry)
        if (entry->available) { auto result = unit; result.lesson = entry->id; return result; }
    auto course = unit; course.unit.clear(); const auto units = load_catalog(root,course);
    const auto selected_unit = std::ranges::find_if(units.entries,[&](const auto& entry) { return entry.id == current.unit; });
    require(selected_unit != units.entries.end(),"la unidad actual no pertenece a su curso.");
    const auto first_lesson = [&](const curriculum::CatalogLocation& target_course,const curriculum::CatalogEntry& entry)
        -> std::optional<curriculum::CatalogLocation> {
        if (!entry.available) return {};
        const auto target_unit = catalog_child(target_course,entry.id); const auto page = load_catalog(root,target_unit);
        for (const auto& lesson : page.entries) if (lesson.available) { auto result = target_unit; result.lesson = lesson.id; return result; }
        return {};
    };
    for (auto entry = std::next(selected_unit); entry != units.entries.end(); ++entry) {
        if (!entry->available) continue;
        const auto next = first_lesson(course,*entry);
        if (next) return between_units ? next : std::nullopt;
    }
    if (!between_courses) return {};
    auto route = course; route.course.clear(); const auto courses = load_catalog(root,route);
    const auto selected_course = std::ranges::find_if(courses.entries,[&](const auto& entry) { return entry.id == current.course; });
    require(selected_course != courses.entries.end(),"el curso actual no pertenece a su ruta.");
    for (auto entry = std::next(selected_course); entry != courses.entries.end(); ++entry) {
        if (!entry->available) continue;
        const auto target_course = catalog_child(route,entry->id); const auto target_units = load_catalog(root,target_course);
        for (const auto& target_unit : target_units.entries) if (const auto next = first_lesson(target_course,target_unit)) return next;
    }
    return {};
}
std::optional<curriculum::CatalogEntry> active_catalog_lesson(const std::filesystem::path& root,
    const curriculum::CatalogLocation& current,std::string_view active_lesson_id) {
    auto listing = current; listing.lesson.clear();
    validate_catalog_location(listing);
    if (catalog_level(listing) != curriculum::CatalogLevel::lessons || active_lesson_id.empty()) return {};
    const auto lessons = load_catalog(root,listing);
    const auto selected = std::ranges::find_if(lessons.entries,[&](const auto& entry) {
        return entry.id == active_lesson_id && entry.available;
    });
    if (selected == lessons.entries.end()) return {};
    return *selected;
}
} // namespace lgaed::windows
