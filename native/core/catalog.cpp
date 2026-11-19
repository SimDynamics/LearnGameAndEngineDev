#include "core/catalog.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace lgaed::curriculum {
namespace {
bool identifier(std::string_view id) {
    return !id.empty() && id.size() <= 256 && std::ranges::all_of(id,[](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
std::string slug(std::string_view id, std::string_view owner) {
    const auto prefix = std::string(owner) + "__";
    if (!identifier(id) || !id.starts_with(prefix) || id.size() == prefix.size())
        throw std::invalid_argument("El identificador del catálogo no pertenece a su índice padre.");
    return std::string(id.substr(prefix.size()));
}
// Scoped progress resets may target one lesson, one course or a whole route.
// Check every explicit parent-child relation before changing any stored key.
// A malformed mixed-course scope must not erase another course's progress.
// A new edition has distinct unit and lesson IDs but reuses its catalog course
// ID. Its historical course-level completion receipt must not be interpreted
// as a result achieved by the new edition or erased by an edition-only reset.
bool edition_unit_of_course(std::string_view unit, std::string_view course) {
    const auto prefix = std::string(course) + "__edition_";
    if(!unit.starts_with(prefix))return false;
    auto suffix=unit.substr(prefix.size());
    if(!(suffix.starts_with("es_")||suffix.starts_with("en_")||suffix.starts_with("fr_")))
        return false;
    suffix.remove_prefix(3);
    const auto delimiter=suffix.find("__");
    if(delimiter==suffix.npos||delimiter==0||delimiter+2==suffix.size())return false;
    const auto generation=suffix.substr(0,delimiter);
    return generation.front()!='0' &&
        std::ranges::all_of(generation,[](char c){return c>='0'&&c<='9';});
}
void validate_reset_scope(const CatalogScope& scope) {
    const auto unique_safe = [](const std::vector<std::string>& ids) {
        std::set<std::string_view> seen;
        for (const auto& id : ids)
            if (!identifier(id) || !seen.insert(id).second)
                throw std::invalid_argument(
                    "El reinicio contiene identidades inválidas o repetidas.");
    };
    unique_safe(scope.courses);
    unique_safe(scope.units);
    unique_safe(scope.lessons);
    const auto belongs_to = [](std::string_view id,
                               const std::vector<std::string>& parents) {
        return std::ranges::any_of(parents, [&](const std::string& parent) {
            const auto prefix = std::string_view(parent);
            return id.size() > prefix.size() + 2 && id.starts_with(prefix) &&
                id.substr(prefix.size(), 2) == "__";
        });
    };
    if (!scope.courses.empty())
        for (const auto& unit : scope.units)
            if (!belongs_to(unit, scope.courses))
                throw std::invalid_argument(
                    "Una unidad del reinicio no pertenece al curso autorizado.");
    for (const auto& lesson : scope.lessons) {
        if (!scope.units.empty() && !belongs_to(lesson, scope.units))
            throw std::invalid_argument(
                "Una lección del reinicio no pertenece a las unidades autorizadas.");
        if (!scope.courses.empty() && !belongs_to(lesson, scope.courses))
            throw std::invalid_argument(
                "Una lección del reinicio no pertenece al curso autorizado.");
    }
}
}
void validate_catalog_location(const CatalogLocation& location) {
    if (location.locale != "es" && location.locale != "en" && location.locale != "fr")
        throw std::invalid_argument("Idioma curricular no compatible.");
    if (!location.route.empty() && location.route != "programming" && location.route != "math_physics")
        throw std::invalid_argument("Ruta curricular desconocida.");
    if (!location.course.empty() && (location.route.empty() || !identifier(location.course)))
        throw std::invalid_argument("Curso curricular inválido.");
    if (!location.unit.empty()) {
        if (location.course.empty()) throw std::invalid_argument("La unidad necesita su curso padre.");
        (void)slug(location.unit,location.course);
    }
    if (!location.lesson.empty()) {
        if (location.unit.empty()) throw std::invalid_argument("La lección necesita su unidad padre.");
        (void)slug(location.lesson,location.unit);
    }
}
CatalogLevel catalog_level(const CatalogLocation& location) {
    validate_catalog_location(location);
    if (!location.unit.empty()) return CatalogLevel::lessons;
    if (!location.course.empty()) return CatalogLevel::units;
    return location.route.empty() ? CatalogLevel::routes : CatalogLevel::courses;
}
CatalogLocation catalog_parent(CatalogLocation location) {
    validate_catalog_location(location);
    if (!location.lesson.empty()) location.lesson.clear();
    else if (!location.unit.empty()) location.unit.clear();
    else if (!location.course.empty()) location.course.clear();
    else location.route.clear();
    return location;
}
CatalogLocation catalog_recover_location(
        CatalogLocation saved, const std::function<bool(const CatalogLocation&)>& available) {
    // A selected unit in a retired edition no longer belongs to the current
    // course index. Keep the surviving course, route, and requested language.
    // Missing indexes or a malformed saved location must not cause a new
    // profile, progress reset, or an unrelated course to be opened.
    for (int level=0; level<6; ++level) {
        try {
            validate_catalog_location(saved);
            if (available(saved)) return saved;
        } catch (const std::exception&) {
            // A damaged/stale descendant is not a reason to discard parents.
        }
        if (!saved.lesson.empty()) saved.lesson.clear();
        else if (!saved.unit.empty()) saved.unit.clear();
        else if (!saved.course.empty()) saved.course.clear();
        else if (!saved.route.empty()) saved.route.clear();
        else if (saved.locale != "es") saved.locale = "es";
        else return {};
    }
    return {};
}
CatalogLocation catalog_child(CatalogLocation location, std::string_view id) {
    if (!identifier(id)) throw std::invalid_argument("Identificador curricular vacío o no seguro.");
    switch (catalog_level(location)) {
    case CatalogLevel::routes: location.route = id; break;
    case CatalogLevel::courses: location.course = id; break;
    case CatalogLevel::units: location.unit = id; break;
    case CatalogLevel::lessons: throw std::invalid_argument("Una lección se abre, no crea otro nivel de catálogo.");
    }
    validate_catalog_location(location); return location;
}
std::filesystem::path catalog_index_path(const CatalogLocation& location) {
    switch (catalog_level(location)) {
    case CatalogLevel::courses: return std::filesystem::path(location.route) / "courses.json";
    case CatalogLevel::units: return std::filesystem::path(location.route) / location.course / ("course_" + location.course + ".json");
    case CatalogLevel::lessons: {
        const auto unit = slug(location.unit,location.course);
        return std::filesystem::path(location.route) / location.course / unit / ("unit_" + unit + ".json");
    }
    default: return {};
    }
}
std::filesystem::path catalog_lesson_path(const CatalogLocation& location, std::string_view id) {
    if (catalog_level(location) != CatalogLevel::lessons) throw std::invalid_argument("La lección necesita su índice de unidad.");
    const auto lesson = slug(id,location.unit);
    return catalog_index_path(location).parent_path() / ("lesson_" + lesson + ".json");
}
std::string catalog_position_key(const CatalogLocation& location) {
    validate_catalog_location(location);
    return "catalog/v1/" + location.locale + '/' + location.route + '/' + location.course + '/' + location.unit + '/' + location.lesson;
}
std::string catalog_position_key(const CatalogPage& page) {
    auto key=catalog_position_key(page.location);
    if(catalog_level(page.location)!=CatalogLevel::units||page.entries.empty())return key;
    // load_catalog already authenticates the edition identity and the unit
    // index. Its asset language may differ from the requested UI language.
    const auto& entry=page.entries.front();
    const auto prefix=page.location.course+"__edition_"+entry.asset_locale+"_";
    const auto id=std::string_view(entry.id);
    if(!id.starts_with(prefix))return key;
    const auto tail=id.substr(prefix.size());
    const auto separator=tail.find("__");
    if(separator==tail.npos||separator==0||separator+2==tail.size())return key;
    const auto generation=tail.substr(0,separator);
    if(!std::ranges::all_of(generation,[](char c){return c>='0'&&c<='9';}))
        return key;
    // Do not reuse the numeric list position from another course generation.
    // Keep the same key if the author reorders units within one generation.
    return key+"/edition/"+entry.asset_locale+"_"+std::string(generation);
}
std::string catalog_saved_entry_id(const CatalogPage& page, std::int64_t index) {
    if(index<0||static_cast<std::size_t>(index)>=page.entries.size())return {};
    return page.entries[static_cast<std::size_t>(index)].id;
}
std::size_t catalog_restore_entry_index(const CatalogPage& page,
        std::string_view saved_id, std::int64_t saved_index, std::size_t total_count) {
    if(total_count==0)return 0;
    if(!saved_id.empty()) {
        const auto entry=std::ranges::find(page.entries,saved_id,&CatalogEntry::id);
        // A retired entry has no successor with its identity: start at the
        // first real item instead of reusing its former numerical position.
        if(entry==page.entries.end())return 0;
        return static_cast<std::size_t>(entry-page.entries.begin());
    }
    // Older profiles and auxiliary home actions still use their stored
    // index. No existing saved key is deleted or interpreted differently.
    return saved_index<0?0:
        std::min(static_cast<std::size_t>(saved_index),total_count-1);
}
bool catalog_lesson_completed(const state::StateData& data, std::string_view id) {
    const auto value = state::find(data,"learning_progress","lesson_" + std::string(id) + "_completed");
    const auto completed = value ? std::get_if<bool>(value) : nullptr; return completed && *completed;
}
bool catalog_lesson_started(const state::StateData& data, std::string_view id) {
    if (catalog_lesson_completed(data,id)) return true;
    const auto value = state::find(data,"learning_progress","lesson_" + std::string(id) + "_started");
    const auto started = value ? std::get_if<bool>(value) : nullptr;
    return (started && *started) || state::saved_page(data,id) != 0;
}
CatalogProgress catalog_progress(const state::StateData& data,const CatalogScope& scope) {
    // Use the same authenticated parent/child and uniqueness contract as a
    // scoped reset. Without it, a duplicate new-edition lesson can be counted
    // twice as completed, even though there is only one saved response owner.
    validate_reset_scope(scope);
    CatalogProgress result{0,scope.lessons.size()};
    for (const auto& id : scope.lessons) if (catalog_lesson_completed(data,id)) ++result.completed;
    return result;
}
CatalogProgress catalog_lesson_page_progress(const state::StateData& data,const Lesson& lesson,
                                           const CatalogExerciseCompletion& correct) {
    CatalogProgress result{0,lesson.pages.size()};
    if (lesson.pages.empty()) return result;
    const auto base = "lesson_" + lesson.id;
    const auto started_value = state::find(data,"learning_progress",base + "_started");
    if (started_value && !std::holds_alternative<bool>(*started_value))
        throw std::runtime_error("El estado de inicio de la lección no es válido.");
    const bool started = started_value && std::get<bool>(*started_value);
    const auto is_exercise = [&](const Block& block) {
        const auto type=block.type;
        return type == BlockType::multiple_choice || type == BlockType::fill_code ||
            type == BlockType::output_prediction || type == BlockType::order_code ||
            type == BlockType::drag_fill_code || type == BlockType::inline_fill_code || type == BlockType::math_puzzle
            || type == BlockType::cpp_compiler;
    };
    struct Group { std::string id; std::vector<const Block*> blocks; };
    std::vector<Group> groups;
    const auto& layout = lesson.state_layout;
    std::set<std::string> read_pages, read_blocks;
    if (layout.version) {
        std::map<std::string,const Block*> displayed;
        for (const auto& page : lesson.pages) for (const auto& block : page.blocks)
            if (!displayed.emplace(block.stable_id,&block).second)
                throw std::runtime_error("Una identidad de bloque aparece repetida en el progreso.");
        for (const auto& original : layout.origin) {
            Group group{original.id,{}};
            auto ids = original.blocks;
            for (const auto& addition : layout.additions)
                if (addition.progress_page == original.id) ids.push_back(addition.id);
            for (const auto& id : ids) {
                const auto found = displayed.find(id);
                if (found == displayed.end()) {
                    if(retired_block(lesson,id))continue;
                    throw std::runtime_error("Falta un bloque del grupo histórico de progreso.");
                }
                group.blocks.push_back(found->second);
            }
            groups.push_back(std::move(group));
        }
        const auto suffix = layout.version == 2 ? "v2_" + layout.locale : std::string("v1");
        const auto coverage = [&](std::string_view name, std::set<std::string>& target) {
            const auto value = state::find(data,"learning_progress",base + std::string(name) + suffix);
            if (!value) return;
            const auto items = std::get_if<std::vector<std::string>>(value);
            if (!items) throw std::runtime_error("La cobertura de lectura guardada no es válida.");
            target.insert(items->begin(),items->end());
        };
        coverage("_read_pages_",read_pages); coverage("_read_blocks_",read_blocks);
    } else {
        for (const auto& page : lesson.pages) {
            Group group;
            for (const auto& block : page.blocks) group.blocks.push_back(&block);
            groups.push_back(std::move(group));
        }
    }
    const auto current_page = std::min(state::saved_page(data,lesson.id),lesson.pages.size()-1);
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const auto& group = groups[index];
        const bool has_exercise = std::ranges::any_of(group.blocks,[&](const auto* block) { return is_exercise(*block); });
        bool completed = false;
        if (has_exercise) {
            completed = std::ranges::all_of(group.blocks,[&](const auto* block) {
                return !is_exercise(*block) || (correct && correct(*block));
            });
        } else if (layout.version) {
            completed = started && (group.blocks.empty() ? read_pages.contains(group.id) :
                std::ranges::all_of(group.blocks,[&](const auto* block) { return read_blocks.contains(block->stable_id); }));
        } else {
            // This is Android's historical theory-only rule, gated by the
            // explicit started flag. It never credits an unanswered puzzle.
            completed = started && index <= current_page;
        }
        if (completed) ++result.completed;
    }
    return result;
}
CatalogProgress catalog_unit_learning_progress(const state::StateData& data,const CatalogScope& scope,
                                              std::span<const CatalogProgress> lessons) {
    if (scope.lessons.size() != lessons.size()) throw std::invalid_argument("El progreso no coincide con las lecciones publicadas.");
    auto result = catalog_progress(data,scope);
    result.learning_total = scope.lessons.size() * 100;
    for (const auto& lesson : lessons) {
        result.learning_completed += lesson.percentage();
        result.pending_validation = result.pending_validation || lesson.pending_validation;
    }
    return result;
}
std::size_t reset_catalog_progress(state::StateData& data,const CatalogScope& scope) {
    validate_reset_scope(scope);
    // Preflight every selected read-coverage receipt before erasing any
    // response, page frontier, native bookmark or coverage from the state.
    // A corrupt historical receipt must reject the entire scoped reset,
    // rather than leaving the first lessons partially erased.
    if (const auto found = data.preferences.find("learning_progress");
            found != data.preferences.end()) {
        const auto& values = found->second;
        for (const auto& id : scope.lessons) {
            const auto base = "lesson_" + id;
            for (const auto suffix : {"v1", "v2_es", "v2_en", "v2_fr"})
                for (const auto field : {"_read_pages_", "_read_blocks_"}) {
                    const auto coverage = values.find(base + field + suffix);
                    if (coverage != values.end() &&
                            !std::holds_alternative<std::vector<std::string>>(coverage->second))
                        throw std::runtime_error("Cobertura de lectura no válida; no se reinicia el estado.");
                }
        }
    }
    // If every indexed unit of a selected course belongs to a new edition,
    // the old course_<id>_completed flag is a retired-edition receipt. The
    // new progress is computed from the new lesson IDs, not from that flag.
    // A mixed/legacy course reset still removes its original course receipt.
    std::set<std::string> preserved_legacy_course_completion;
    for(const auto& course:scope.courses) {
        const auto prefix=course+"__";
        bool selected_units=false, edition_only=true;
        for(const auto& unit:scope.units) if(unit.starts_with(prefix)) {
            selected_units=true;
            if(!edition_unit_of_course(unit,course))edition_only=false;
        }
        if(selected_units&&edition_only)
            preserved_legacy_course_completion.insert("course_"+course+"_completed");
    }
    std::size_t erased = 0;
    for (auto& [domain,values] : data.preferences) {
        for (auto item = values.begin(); item != values.end();) {
            const auto& key = item->first; bool matches = false;
            if (domain == "learning_progress") {
                auto response_key=std::string_view(key);
                if(response_key.starts_with("order-lines/v1/")||response_key.starts_with("choice-options/v1/")) {
                    response_key.remove_prefix(response_key.starts_with("order-lines/v1/")?15:18);
                    const auto locale_end=response_key.find('/');
                    const auto locale=response_key.substr(0,locale_end);
                    if(locale_end==response_key.npos||(locale!="es"&&locale!="en"&&locale!="fr"))response_key={};
                    else {
                        response_key.remove_prefix(locale_end+1);
                        if(response_key.size()<65||response_key[64]!='/'||
                            !std::ranges::all_of(response_key.substr(0,64),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))response_key={};
                        else response_key.remove_prefix(65);
                    }
                }
                for (const auto& id : scope.lessons) {
                    const auto base = "lesson_" + id;
                    if (key == base + "_page" || key == base + "_started" || key == base + "_completed") matches = true;
                    for (const auto locale : {"es","en","fr"})
                        if (key == base + "_reached_page_v1_" + locale) matches = true;
                    for (const auto suffix : {"v1","v2_es","v2_en","v2_fr"})
                        if (key == base + "_read_pages_" + suffix || key == base + "_read_blocks_" + suffix) {
                            auto* coverage = std::get_if<std::vector<std::string>>(&item->second);
                            if (!coverage) throw std::runtime_error("Cobertura de lectura no válida; no se reinicia el estado.");
                            if (!coverage->empty()) { coverage->clear(); ++erased; }
                        }
                    const auto prefix = "exercise_" + id;
                    if (!response_key.starts_with(prefix)) continue;
                    auto tail = response_key.substr(prefix.size());
                    if (tail.starts_with("_state_v2_")) {
                        tail.remove_prefix(10);
                        if (tail.size() < 64 || !std::ranges::all_of(tail.substr(0,64),[](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) continue;
                        tail.remove_prefix(64);
                    }
                    if (!tail.starts_with("_page_")) continue;
                    tail.remove_prefix(6);
                    const auto split = tail.find("_block_");
                    const auto digits = [](std::string_view text) { return !text.empty() && std::ranges::all_of(text,[](char c) { return c >= '0' && c <= '9'; }); };
                    if (split != tail.npos && digits(tail.substr(0,split)) && digits(tail.substr(split + 7))) matches = true;
                }
                for (const auto& id : scope.units) if (key == "unit_" + id + "_completed") matches = true;
                for (const auto& id : scope.courses) if (key == "course_" + id + "_completed" &&
                    !preserved_legacy_course_completion.contains(key)) matches = true;
            } else if (domain == "native_windows") {
                for (const auto& id : scope.lessons) {
                    if (key.starts_with("scroll/" + id + '/')) matches = true;
                    for(const auto locale:{"es","en","fr"})
                        if(key.starts_with(std::string("compiler-puzzle/v1/")+locale+'/'+id+'/'))matches=true;
                    if (key.starts_with("reference-proof/v1/")) for (const auto route : {"programming","math_physics"})
                        for (const auto locale : {"es","en","fr"})
                            if (key.starts_with(std::string("reference-proof/v1/") + locale + '/' + route + '/' + id + '/')) matches = true;
                }
            }
            if (matches) { item = values.erase(item); ++erased; } else ++item;
        }
    }
    return erased;
}
} // namespace lgaed::curriculum
