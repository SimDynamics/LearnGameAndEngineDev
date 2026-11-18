// Integración Windows de una edición autoral aislada. Los datos y los recibos
// históricos son sintéticos y pertenecen exclusivamente a perfiles QA nuevos.
// El quiz sí usa controles/evaluación reales; no se ejecuta ningún compilador
// ni se acredita voz, PCM o una pausa real mediante estos fixtures.
#include "core/attempts.hpp"
#include "core/catalog.hpp"
#include "core/compiler_flow.hpp"
#include "core/course_edition.hpp"
#include "core/narration_state.hpp"
#include "core/view_preferences.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_project_history.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
namespace fs = std::filesystem;
constexpr std::string_view old_id = "qa_edition__original__primera";
constexpr std::string_view new_id = "qa_edition__edition_es_1__inicio__primera";
constexpr std::string_view new_unit = "qa_edition__edition_es_1__inicio";
constexpr std::string_view next_id = "qa_edition__edition_es_1__inicio__segunda";
constexpr std::string_view other_id = "qa_other__independiente__primera";
constexpr std::string_view marker = "LearnGameAndEngineDev.course-edition-fixture.v1";

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}
template<class Action> void rejects(Action&& action, std::string_view message) {
    bool rejected = false;
    try { action(); }
    catch (const std::exception&) { rejected = true; }
    catch (const winrt::hresult_error&) { rejected = true; }
    require(rejected, message);
}
std::string bytes(const fs::path& path) {
    const auto value = state::read_bytes(path);
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
void write(const fs::path& path, std::string_view text) {
    state::require_safe_path(path);
    fs::create_directories(state::native_io_path(path.parent_path()));
    std::ofstream output(state::native_io_path(path), std::ios::binary | std::ios::trunc);
    require(output.good(), "No se pudo abrir un archivo QA.");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    require(output.good(), "No se pudo guardar el archivo QA completo.");
}
fs::path relative_path(std::string_view name) {
    const fs::path result(winrt::to_hstring(name).c_str());
    require(!result.empty() && !result.is_absolute() && !result.has_root_name(),
        "El fixture debe usar rutas relativas.");
    for (const auto& part : result)
        require(part != L".." && part != L".", "Ruta no admitida en el fixture QA.");
    return result;
}
std::string stringify(const JsonObject& value) { return winrt::to_string(value.Stringify()); }
JsonObject object(std::string_view value) { return JsonObject::Parse(winrt::to_hstring(value)); }
template<class Change> std::string changed(std::string_view source, Change&& change) {
    auto value = object(source); change(value); return stringify(value);
}
struct RestoreFile {
    fs::path path;
    std::string original;
    explicit RestoreFile(fs::path target) : path(std::move(target)), original(bytes(path)) {}
    ~RestoreFile() { try { write(path, original); } catch (...) {} }
};

struct Profile {
    WindowsEnvironment environment;
    explicit Profile(std::wstring_view suffix)
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"course-edition-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L"-" + std::wstring(suffix), false})) {
        require(environment.options.qa_instance.has_value(), "Falta el aislamiento QA.");
        require(!fs::exists(environment.paths.root), "El perfil QA debe ser nuevo.");
        initialize_profile_directories(environment.paths);
    }
    state::StateData load() const {
        state::StateStore store(environment.paths.root); return store.load();
    }
    void save(const state::StateData& value) const {
        state::StateStore store(environment.paths.root); (void)store.load(); store.save(value);
    }
};
struct Fixture {
    fs::path source, root, content;
    std::map<std::string, std::string> originals, paths;
    Fixture(const Profile& profile, const fs::path& input)
        : source(input), root(profile.environment.paths.root / "course-edition-fixture"),
          content(root / "content") {
        const auto manifest = object(bytes(source / "manifest.json"));
        require(winrt::to_string(manifest.GetNamedString(L"fixture")) == marker,
            "No es un fixture de edición generado por la operación de autoría.");
        for (const auto& item : manifest.GetNamedObject(L"files")) {
            const auto name = winrt::to_string(item.Key());
            const auto file = relative_path(name);
            const auto original = bytes(source / file);
            require(state::sha256(original) == winrt::to_string(item.Value().GetString()),
                "El fixture cambió después de generarse.");
            originals.emplace(name, original); write(root / file, original);
        }
        for (const auto& item : manifest.GetNamedObject(L"paths")) {
            const auto name = winrt::to_string(item.Key());
            const auto file = winrt::to_string(item.Value().GetString());
            require(originals.contains(file), "El manifiesto señala un recurso ausente.");
            paths.emplace(name, file);
        }
        require(originals.size() == 11, "El fixture tiene un alcance distinto del esperado.");
    }
    fs::path path(std::string_view key) const { return root / relative_path(paths.at(std::string(key))); }
    void old_catalog() { write(path("course"), bytes(path("legacyCourse"))); }
    void new_catalog() { write(path("course"), originals.at(paths.at("course"))); }
    void preserved() const {
        for (const auto& [name, original] : originals) {
            require(bytes(root / relative_path(name)) == original, "Se alteró un recurso QA conservado.");
            require(bytes(source / relative_path(name)) == original, "Se alteró el fixture fuente del build.");
        }
    }
};

void preserved_values(const state::StateData& expected, const state::StateData& actual) {
    for (const auto& [domain, preferences] : expected.preferences)
        for (const auto& [key, value] : preferences) {
            const auto found = state::find(actual, domain, key);
            require(found && *found == value, "La nueva edición cambió una clave histórica o ajena.");
        }
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
HWND descendant(HWND parent, int id) {
    struct Search { int id; HWND found = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND child, LPARAM raw) -> BOOL {
        auto& item = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(child) != item.id) return TRUE;
        item.found = child; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
struct View {
    const Profile& profile;
    curriculum::Lesson lesson;
    HWND host = nullptr, view = nullptr;
    std::size_t completions = 0;
    View(const Profile& owner, curriculum::Lesson model) : profile(owner), lesson(std::move(model)) {
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC",
            L"Prueba aislada de edición", WS_OVERLAPPEDWINDOW,
            0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "No se pudo crear el contenedor oculto.");
        open();
    }
    ~View() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        LessonViewHooks hooks;
        hooks.lesson_completed = [this](HWND) { ++completions; };
        view = create_lesson_view(host, profile.environment, lesson, std::move(hooks));
        require(view != nullptr, "No se pudo abrir la lección QA.");
        MoveWindow(view, 0, 0, 1100, 850, TRUE); pump();
        require(!IsWindowVisible(host), "La prueba expuso una ventana en el escritorio.");
    }
    state::StateData saved() {
        require(flush_lesson_view(view), "No se pudo guardar el estado QA.");
        return read_lesson_state(view);
    }
    void reopen() {
        (void)saved(); DestroyWindow(view); view = nullptr; open();
    }
    HWND control(std::size_t block, int part) const {
        const auto result = descendant(view, 1000 + static_cast<int>(block) * 100 + part);
        require(result != nullptr, "Falta un control real del ejercicio."); return result;
    }
    void answer(std::size_t block, int option) {
        const auto choose = control(block, 20 + option * 2), check = control(block, 3);
        require(IsWindowEnabled(choose), "El quiz nuevo está bloqueado.");
        SendMessageW(choose, BM_CLICK, 0, 0);
        require(IsWindowEnabled(check), "La selección no habilitó Comprobar.");
        SendMessageW(check, BM_CLICK, 0, 0); pump();
    }
    void replace_source(std::size_t block, std::string_view text) {
        const auto editor = control(block, 10);
        require(IsWindowEnabled(editor), "El editor nuevo está bloqueado.");
        CHARRANGE all{0, -1}; SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto wide = winrt::to_hstring(text);
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(wide.c_str())); pump();
        require(edit_text(editor) == text, "El editor alteró el borrador suministrado.");
    }
};

curriculum::CatalogLocation course_location() { return {"es", "programming", "qa_edition"}; }
curriculum::CatalogLocation unit_location() {
    return {"es", "programming", "qa_edition", std::string(new_unit)};
}
const curriculum::CatalogEntry& entry(const curriculum::CatalogPage& page, std::string_view id) {
    const auto found = std::ranges::find(page.entries, id, &curriculum::CatalogEntry::id);
    require(found != page.entries.end(), "Falta una entrada del catálogo QA."); return *found;
}
bool reference_available(const state::StateData& data, const curriculum::Lesson& lesson,
                         std::size_t page, std::size_t block_index = 0) {
    const auto completed = [&](const auto& block) {
        return windows::compiler_puzzle_completed(data, lesson, block);
    };
    const auto proofs = curriculum::compiler_completion_snapshot(lesson, completed);
    return curriculum::compiler_reference_available(lesson, page, lesson.pages.at(page).blocks.at(block_index),
        proofs, completed);
}
void seed_legacy(state::StateData& data, const curriculum::Lesson& old,
                 const curriculum::Lesson& other) {
    state::prepare_lesson_state(data, old);
    state::Response answer; answer.correct = true; answer.selected = 0;
    state::put_response(data, old, old.pages[0].blocks[0], answer);
    const auto& compiler = old.pages[1].blocks[0];
    // Captura HISTÓRICA SINTÉTICA: sirve para detectar la reutilización indebida
    // de un recibo. No es una compilación ni una ejecución de esta prueba.
    windows::CppRunRequest request; request.revision = 7; request.source_utf8 = compiler.initial_code;
    curriculum::FreshExecution capture{7, true, true, 0, "", "42\n"};
    require(windows::record_compiler_puzzle_case(data, old, compiler, request, capture,
        "Recibo histórico sintético QA"), "No se pudo preparar el recibo antiguo.");
    state::Draft draft;
    draft.source = "// Borrador anterior conservado\n" + compiler.initial_code;
    draft.input = "entrada anterior\n"; draft.selection_start = 3; draft.selection_end = 9;
    draft.indentation_json = state::make_indentation_metadata(draft.source, {});
    state::put_draft(data, curriculum::draft_key("programming", old, compiler), draft);
    for (std::size_t page = 0; page < old.pages.size(); ++page)
        (void)state::mark_page_reached(data, old, page);
    state::save_current_page(data, old, old.pages.size() - 1);
    auto& progress = data.preferences["learning_progress"];
    for (const auto& lesson : {old.id, other.id}) {
        progress["lesson_" + lesson + "_started"] = true;
        progress["lesson_" + lesson + "_completed"] = true;
    }
    // Esta bandera histórica del curso NO debe acreditar las nuevas unidades.
    progress["course_qa_edition_completed"] = true;
    progress["unit_qa_edition__original_completed"] = true;
    progress["course_qa_other_completed"] = true;
    state::save_current_page(data, other, 0);
    data.preferences["native_windows"]["scroll/" + old.id + "/" + old.pages.back().stable_id] = std::int64_t{137};
    data.preferences["native_windows"]["navigation/lesson"] = old.id;
    appearance::save_global_text_scale(data, 110);
    appearance::save_course_text_scale(data, "qa_edition", false, 120);
    appearance::save_course_text_scale(data, "qa_other", false, 115);
    appearance::save_course_wrap(data, "qa_edition", true);
    auto& reserve = data.preferences["attempt_reserve_seconds_test"];
    reserve["reserve_programming"] = 3.25;
    reserve["updated_at_programming"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    reserve["recovery_interval_programming"] = attempts::recovery_millis;
    data.preferences["qa_unrelated"]["opaque"] = std::string("Conservar exactamente & < >.");
}

void pending(const state::StateData& data, const curriculum::Lesson& current) {
    require(state::current_page(data, current) == 0, "La nueva edición heredó una página antigua.");
    require(!curriculum::catalog_lesson_completed(data, current.id), "La nueva lección heredó el cierre.");
    require(!state::get_response(data, current, current.pages[0].blocks[0]), "Se heredó la respuesta antigua.");
    const auto& compiler = current.pages[1].blocks[0];
    const auto draft = state::get_draft(data, curriculum::draft_key("programming", current, compiler),
        compiler.initial_code);
    require(!draft.has_saved && draft.source == compiler.initial_code && draft.input.empty(),
        "La nueva edición heredó un borrador o una entrada.");
    require(!windows::compiler_puzzle_completed(data, current, compiler)
        && windows::compiler_puzzle_case_count(data, current, compiler) == 0,
        "Un recibo antiguo acreditó el nuevo editor.");
    require(!reference_available(data, current, 2), "La referencia nueva heredó una acreditación.");
}

void catalog_pending(const Fixture& fixture, const state::StateData& data) {
    const auto course = windows::load_catalog(fixture.content, course_location());
    require(course.entries.size() == 1 && course.entries.front().id == new_unit,
        "El catálogo mezcló unidades de ediciones diferentes.");
    const auto unit = windows::load_catalog(fixture.content, unit_location());
    require(unit.entries.size() == 2 && unit.entries[0].id == new_id && unit.entries[1].id == next_id,
        "El índice activo reutilizó o perdió lecciones.");
    const auto scope = windows::catalog_scope(fixture.content, course_location());
    require(scope.lessons == std::vector<std::string>{std::string(new_id), std::string(next_id)},
        "El alcance del curso conserva lecciones retiradas.");
    const auto routes = windows::load_catalog(fixture.content, {"es", "programming"});
    const auto course_progress = windows::catalog_entry_progress(fixture.content, {"es", "programming"},
        entry(routes, "qa_edition"), data);
    require(course_progress.completed == 0 && course_progress.total == 2 && course_progress.percentage() == 0,
        "Una bandera antigua de curso acreditó la edición nueva.");
    const auto other = windows::catalog_entry_progress(fixture.content, {"es", "programming"},
        entry(routes, "qa_other"), data);
    require(other.completed == 1 && other.total == 1, "Cambió el avance del curso ajeno.");
    const auto first = windows::catalog_entry_progress(fixture.content, unit_location(), unit.entries[0], data);
    require(first.completed == 0 && first.total == 3, "El catálogo reutilizó páginas o caché de la edición antigua.");
    require(!windows::active_catalog_lesson(fixture.content, unit_location(), old_id),
        "Continuar resolvió una lección que ya no pertenece al índice.");
    const curriculum::CatalogLocation current{"es", "programming", "qa_edition", std::string(new_unit), std::string(new_id)};
    const auto following = windows::next_catalog_lesson(fixture.content, current, false, false);
    require(following && following->lesson == next_id && following->unit == new_unit,
        "La continuación no sigue el índice de la edición activa.");
    rejects([&] { (void)windows::load_catalog(fixture.content, {"es", "programming", "qa_edition", "qa_edition__original"}); },
        "Una unidad antigua aún se puede abrir desde el catálogo activo.");
}

std::string xml_escape(std::string_view value) {
    std::string result;
    for (char character : value) switch (character) {
    case '&': result += "&amp;"; break; case '<': result += "&lt;"; break;
    case '>': result += "&gt;"; break; case '"': result += "&quot;"; break;
    case '\'': result += "&apos;"; break; case '\r': result += "&#13;"; break;
    default: result += character; break;
    }
    return result;
}
void export_preferences(const fs::path& root, const state::StateData& data) {
    for (const auto& [domain, preferences] : data.preferences) {
        std::ostringstream xml; xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<map>\n";
        for (const auto& [key, value] : preferences) {
            const auto name = xml_escape(key);
            std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, std::string>)
                    xml << "<string name=\"" << name << "\">" << xml_escape(item) << "</string>\n";
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    xml << "<long name=\"" << name << "\" value=\"" << item << "\"/>\n";
                else if constexpr (std::is_same_v<T, double>)
                    xml << "<float name=\"" << name << "\" value=\"" << std::setprecision(17) << item << "\"/>\n";
                else if constexpr (std::is_same_v<T, bool>)
                    xml << "<boolean name=\"" << name << "\" value=\"" << (item ? "true" : "false") << "\"/>\n";
                else {
                    xml << "<set name=\"" << name << "\">";
                    for (const auto& text : item) xml << "<string>" << xml_escape(text) << "</string>";
                    xml << "</set>\n";
                }
            }, value);
        }
        xml << "</map>\n"; write(root / "shared_prefs" / (domain + ".xml"), xml.str());
    }
}

void import_isolated(const Fixture& fixture, const state::StateData& historic,
                     const curriculum::Lesson& current, const fs::path& revision_file,
                     const windows::CppHistoryRevision& revision) {
    auto exported = historic;
    // El importador Android rechaza native_windows deliberadamente. Sus
    // recibos nativos se conservan en la prueba de reapertura, no se falsifican
    // como una exportación Android. El resto usa su formato real y tipado.
    exported.preferences.erase("native_windows");
    const auto source = fixture.root / "export";
    export_preferences(source, exported);
    const auto relative_revision = fs::path("files/cpp_project_history") / revision.project_key / (revision.id + ".revision");
    write(source / relative_revision, bytes(revision_file));
    const auto preview = state::inspect_android_export(source);
    require(preview.data == exported && preview.project_revisions == 1,
        "La previsualización de importación cambió preferencias o perdió el historial.");
    state::validate_lesson_state(preview.data, current);
    Profile target(L"import");
    state::StateData native;
    native.preferences["native_windows"]["ui_language"] = std::string("es");
    native.preferences["native_windows"]["content_language"] = std::string("es");
    native.preferences["native_windows"]["catalog/route"] = std::string("programming");
    native.preferences["native_windows"]["catalog/course"] = std::string("qa_edition");
    native.preferences["native_windows"]["navigation/lesson"] = std::string(new_id);
    target.save(native);
    require(state::can_import_android_profile(target.load()), "El perfil QA de importación no está vacío de progreso.");
    state::commit_android_import(preview, target.environment.paths.root);
    const auto imported = target.load();
    preserved_values(exported, imported); preserved_values(native, imported);
    pending(imported, current); catalog_pending(fixture, imported);
    require(bytes(source / relative_revision) == bytes(revision_file), "La importación alteró el historial origen.");
    const auto copied = target.environment.paths.root / "state/active/android" / relative_revision;
    require(bytes(copied) == bytes(revision_file), "La importación cambió los bytes del historial conservado.");
    require(state::inspect_android_export(source).data == exported, "La importación reescribió sus preferencias origen.");
    {
        View opened(target, current);
        require(lesson_page_state(opened.view).index == 0 && !lesson_page_state(opened.view).can_next,
            "La importación reabrió la edición nueva acreditada o adelantada.");
        require(!navigate_lesson_view(opened.view, 1) && !navigate_lesson_view(opened.view, 2),
            "La importación eludió la secuencia de actividades nuevas.");
        preserved_values(imported, opened.saved());
    }
    const auto occupied = target.load();
    rejects([&] { state::commit_android_import(preview, target.environment.paths.root); },
        "La importación reemplazó un perfil con datos.");
    require(target.load() == occupied, "Una importación rechazada alteró el destino.");
    write(source / "shared_prefs/native_windows.xml", "<map><string name=\"ui_language\">es</string></map>");
    rejects([&] { (void)state::inspect_android_export(source); },
        "La importación aceptó recibos/preferencias nativas como exportación Android.");
}

void physical_edition_long_paths(const Fixture& fixture) {
    // Keep the original fixture and state untouched. Reproduce a complete
    // indexed edition beyond MAX_PATH and read it with the real native
    // lesson, progress and catalog loaders; no student profile is opened.
    auto folder = fixture.root / "edition-long-path-isolation";
    while (folder.native().size() < 295)
        folder /= L"segmento_de_comprobacion_de_rutas_largas_0123456789";
    const auto content = folder / "content";
    for (const auto& [relative, original] : fixture.originals)
        write(folder / relative_path(relative), original);
    for (const auto role : {"course", "unit", "lesson"}) {
        const auto target = folder / relative_path(fixture.paths.at(role));
        require(target.native().size() > MAX_PATH,
            "The long-path regression did not exceed the Win32 legacy limit");
        require(fs::is_regular_file(state::native_io_path(target)),
            "Cannot create an isolated long-path curriculum resource");
        windows::validate_course_edition_physical_path(content, target);
    }
    const auto lesson_file = folder / relative_path(fixture.paths.at("lesson"));
    const auto lesson = windows::load_lesson(lesson_file);
    const auto metadata = windows::load_lesson_progress_metadata(lesson_file);
    require(lesson.id == new_id && metadata.id == new_id &&
            lesson.course_edition == metadata.course_edition,
        "Extended Win32 path accepted metadata but not the complete edition lesson");
    const auto course = windows::load_catalog(content, course_location());
    const auto unit = windows::load_catalog(content, unit_location());
    require(course.entries.size() == 1 && course.entries.front().id == new_unit &&
            unit.entries.size() == 2 && unit.entries[0].id == new_id,
        "The native edition catalog could not open a valid long-path index");
    const state::StateData empty_profile;
    const auto progress = windows::catalog_entry_progress(content, unit_location(),
        unit.entries.front(), empty_profile);
    require(progress.completed == 0 && progress.total == 3,
        "The catalog progress projection could not read the long-path edition lesson");
    const auto scope = windows::catalog_scope(content, course_location());
    require(scope.lessons == std::vector<std::string>{std::string(new_id), std::string(next_id)},
        "The long-path catalog lost a lesson while calculating the active edition scope");
    const curriculum::CatalogLocation current{"es", "programming", "qa_edition",
        std::string(new_unit), std::string(new_id)};
    const auto following = windows::next_catalog_lesson(content, current, false, false);
    require(following && following->lesson == next_id && following->unit == new_unit,
        "The long-path catalog could not continue to the next authored lesson");
    // A missing file at the same depth must still fail closed.
    const auto missing = folder / relative_path(fixture.paths.at("lesson"));
    std::error_code ignored;
    const auto missing_target = missing.parent_path() / "lesson_inexistente.json";
    require(!fs::exists(state::native_io_path(missing_target), ignored),
        "The missing long-path QA target unexpectedly exists");
    bool missing_rejected = false;
    try { windows::validate_course_edition_physical_path(content, missing_target); }
    catch (const std::exception& failure) {
        const std::string_view reason(failure.what());
        missing_rejected =
            reason.find("A course-edition resource or directory is unavailable") != reason.npos &&
            reason.find("Win32 ") != reason.npos &&
            reason.find("component lesson_inexistente.json") != reason.npos &&
            reason.find(folder.filename().string()) == reason.npos;
    }
    require(missing_rejected,
        "The long-path guard accepted a missing resource or disclosed the full QA path");
}

void physical_edition_catalog_alias_rejected(const Fixture& fixture) {
    // The original fixture and student profile remain untouched. For every
    // target, warm the normal catalog/identity caches, then replace one path
    // with a byte-identical symbolic link. Cached metadata cannot authorize
    // a path which became a reparse point after the initial catalog load.
    for (const int kind : {0, 1, 2, 3, 4}) {
        // Case 4 aliases the entire fixture, above the content root. This
        // must fail even when all content-relative paths are ordinary files.
        const auto mirror = fixture.root / ("edition-alias-isolation-" + std::to_string(kind));
        for (const auto& [relative, unused] : fixture.originals) {
            const auto file = relative_path(relative);
            write(mirror / file, bytes(fixture.root / file));
        }
        const auto content = mirror / "content";
        const auto lesson = mirror / relative_path(fixture.paths.at("lesson"));
        const auto course = mirror / relative_path(fixture.paths.at("course"));
        const auto unit = mirror / relative_path(fixture.paths.at("unit"));
        require(windows::load_lesson(lesson).course_edition.has_value(),
            "The isolated native edition was invalid before creating an alias");
        const auto old_course = windows::load_catalog(content, course_location());
        const auto old_unit = windows::load_catalog(content, unit_location());
        require(old_course.entries.size() == 1 && old_unit.entries.size() == 2,
            "The isolated edition catalog was invalid before alias injection");
        const state::StateData empty_profile;
        const auto old_progress = windows::catalog_entry_progress(content, unit_location(),
            old_unit.entries.front(), empty_profile);
        require(old_progress.completed == 0 && old_progress.total == 3,
            "Cannot warm the native edition progress cache before the alias regression");

        const bool directory = kind >= 3;
        const auto linked = kind == 0 ? course :
            kind == 1 ? unit : kind == 2 ? lesson :
            kind == 3 ? unit.parent_path() : mirror;
        const auto physical = fs::path(linked.wstring() + L".physical-qa");
        require(!fs::exists(physical), "A previous QA alias target was present");
        fs::rename(linked, physical);
        struct Restore {
            fs::path link, original;
            ~Restore() {
                std::error_code error;
                if (fs::is_symlink(link, error)) fs::remove(link, error);
                if (fs::exists(original, error)) fs::rename(original, link, error);
            }
        } restore{linked, physical};
        constexpr DWORD permit_unprivileged = 0x2; // Windows 10+ developer-mode flag.
        const DWORD flags = permit_unprivileged | (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
        if (!CreateSymbolicLinkW(linked.c_str(), physical.c_str(), flags)) {
            // OS policy can prohibit links even inside an isolated QA profile.
            // Do not report this particular case as verified when it was skipped.
            std::cout << "SKIP native course-edition alias type " << kind
                      << ": Win32 error " << GetLastError() << "\n";
            continue;
        }
        const auto attributes = GetFileAttributesW(linked.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
            "The QA alias was not reported as a Win32 reparse point");
        const auto rejects_alias = [&](const auto& action) {
            bool rejected = false;
            try { action(); }
            catch (const std::exception& failure) {
                // The catalog resolves through the general state I/O guard
                // before its edition-specific guard. Both diagnostics prove
                // that the real reparse-point path was rejected.
                const std::string_view message(failure.what());
                rejected = message.find("symbolic link or junction") != message.npos ||
                    message.find("El estado no admite enlaces ni puntos de redireccion") != message.npos;
            }
            require(rejected, "An edition consumer accepted a published path via a reparse point");
        };
        rejects_alias([&] { (void)windows::load_lesson(lesson); });
        rejects_alias([&] { (void)windows::load_lesson_progress_metadata(lesson); });
        if (kind == 4) {
            // Catalog operations also refuse this ancestor through their
            // existing path-safety guard, whose diagnostic differs from the
            // course-edition loader's explicit alias error.
            rejects([&] { (void)windows::load_catalog(content, course_location()); },
                "The catalog accepted a symbolic alias above the content root");
            rejects([&] { (void)windows::load_catalog(content, unit_location()); },
                "The unit catalog accepted a symbolic alias above the content root");
        } else {
            if (kind != 2) {
                // A course index reads units, but does not yet read lessons.
                rejects_alias([&] { (void)windows::load_catalog(content, course_location()); });
            } else require(windows::load_catalog(content, course_location()).entries.size() == 1,
                "A lesson-file alias incorrectly invalidated its course index");
            rejects_alias([&] { (void)windows::load_catalog(content, unit_location()); });
        }
        // This public progress API must not reuse a cached read-only lesson
        // or silently continue with an alias installed after the first read.
        rejects([&] { (void)windows::catalog_entry_progress(content, unit_location(),
            old_unit.entries.front(), empty_profile); },
            "The catalog credited progress after an edition resource became an alias");
        require(fs::exists(physical), "The QA link check destroyed the original bytes");
    }
}


void negative_contracts(const Fixture& fixture, const curriculum::Lesson& current,
                        const state::StateData& current_data) {
    const auto original = bytes(fixture.path("lesson"));
    const auto both_reject = [&](std::string_view candidate) {
        rejects([&] { (void)windows::parse_lesson_json(candidate); }, "El cargador completo aceptó una edición inválida.");
        rejects([&] { (void)windows::parse_lesson_progress_metadata(candidate); }, "La proyección de progreso aceptó una edición inválida.");
    };
    both_reject(changed(original, [](auto& value) { value.Remove(L"courseEdition"); }));
    both_reject(changed(original, [](auto& value) { value.Remove(L"stateLayout"); }));
    both_reject(changed(original, [](auto& value) {
        value.Insert(L"id", JsonValue::CreateStringValue(winrt::to_hstring(old_id)));
    }));
    for (const auto& field : {L"version", L"courseId", L"locale", L"id", L"generation"})
        both_reject(changed(original, [&](auto& value) { value.GetNamedObject(L"courseEdition").Remove(field); }));
    both_reject(changed(original, [](auto& value) {
        value.GetNamedObject(L"courseEdition").Insert(L"version", JsonValue::CreateNumberValue(2));
    }));
    both_reject(changed(original, [](auto& value) {
        value.GetNamedObject(L"courseEdition").Insert(L"generation", JsonValue::CreateNumberValue(2));
    }));
    both_reject(changed(original, [](auto& value) {
        value.GetNamedObject(L"courseEdition").Insert(L"locale", JsonValue::CreateStringValue(L"fr"));
    }));
    both_reject(changed(original, [](auto& value) {
        value.GetNamedObject(L"courseEdition").Insert(L"courseId", JsonValue::CreateStringValue(L"qa_other"));
    }));
    both_reject(changed(original, [](auto& value) {
        value.GetNamedObject(L"courseEdition").Insert(L"extra", JsonValue::CreateBooleanValue(true));
    }));
    both_reject(changed(original, [](auto& value) {
        auto family = value.GetNamedObject(L"stateLayout").GetNamedObject(L"family");
        auto another = object(stringify(family.GetNamedObject(L"es")));
        family.Insert(L"fr", another);
    }));
    rejects([&] { (void)windows::parse_lesson_json(original, "fr"); }, "Una edición española se interpretó como otra locale.");
    const auto renamed = windows::parse_lesson_json(changed(original, [](auto& value) {
        value.GetNamedObject(L"courseEdition").Insert(L"id", JsonValue::CreateStringValue(L"otra-edicion"));
    }));
    require(renamed.state_layout.lineage != current.state_layout.lineage, "El nombre de edición no participa en su linaje.");
    rejects([&] { state::validate_lesson_state(current_data, renamed); },
        "Cambiar sólo el descriptor reutilizó los mismos datos acreditados.");

    {
        RestoreFile restore(fixture.path("lesson"));
        write(restore.path, changed(original, [](auto& value) {
            value.GetNamedObject(L"courseEdition").Insert(L"id", JsonValue::CreateStringValue(L"otra-edicion"));
        }));
        rejects([&] { (void)windows::load_lesson(restore.path); }, "Se aceptó una lección con descriptor distinto del índice.");
        rejects([&] { (void)windows::load_lesson_progress_metadata(restore.path); }, "El catálogo aceptó metadata de otra edición.");
    }
    {
        RestoreFile restore(fixture.path("unit"));
        write(restore.path, changed(restore.original, [](auto& value) {
            value.GetNamedObject(L"courseEdition").Insert(L"id", JsonValue::CreateStringValue(L"otra-edicion"));
        }));
        rejects([&] { (void)windows::load_catalog(fixture.content, unit_location()); }, "Se aceptó una unidad de otra edición.");
        rejects([&] { (void)windows::load_lesson(fixture.path("lesson")); }, "El lector ignoró el descriptor de unidad.");
    }
    {
        RestoreFile restore(fixture.path("course"));
        write(restore.path, changed(restore.original, [](auto& value) {
            value.GetNamedArray(L"units").Append(object(R"({"id":"qa_edition__original","number":2,"title":"Mezcla inválida"})"));
        }));
        rejects([&] { (void)windows::load_catalog(fixture.content, course_location()); }, "El catálogo mezcló la unidad antigua.");
        rejects([&] { (void)windows::load_lesson(fixture.path("lesson")); }, "El lector aceptó un índice mezclado.");
    }
    {
        RestoreFile restore(fixture.path("unit"));
        write(restore.path, changed(restore.original, [](auto& value) {
            auto lessons = value.GetNamedArray(L"lessons"); lessons.Append(lessons.GetAt(0));
        }));
        rejects([&] { (void)windows::load_catalog(fixture.content, unit_location()); }, "El índice reutilizó una ID de lección.");
    }
    for (const auto name : {"course", "unit"}) {
        RestoreFile restore(fixture.path(name));
        write(restore.path, changed(restore.original, [](auto& value) { value.Remove(L"courseEdition"); }));
        rejects([&] { (void)windows::load_lesson(fixture.path("lesson")); }, "El lector aceptó un padre sin descriptor.");
        rejects([&] { (void)windows::load_catalog(fixture.content,
            std::string_view(name) == "course" ? course_location() : unit_location()); },
            "Un índice reservado sin descriptor cayó al modo histórico.");
    }
    // Los recursos antiguos siguen cargándose directamente, pero quedan fuera
    // de los índices activos. Conservar archivos no vuelve a acreditarlos.
    require(windows::load_lesson(fixture.path("legacyLesson")).id == old_id, "La edición impidió conservar el recurso antiguo.");
    fixture.preserved();
}

void run(const fs::path& fixture_source) {
    Profile profile(L"native");
    Fixture fixture(profile, fixture_source);
    // The real Windows environment can supply an extended \\?\C:\... QA
    // root. Verify the whole physical ancestor chain without mistaking the
    // drive root-name for an unavailable directory before loading JSON.
    require(fixture.content.is_absolute(), "The isolated QA content root is not absolute");
    for (const auto role : {"course", "unit", "lesson"})
        windows::validate_course_edition_physical_path(fixture.content, fixture.path(role));
    std::cout << "PASS QA course-edition roots: course/unit/lesson are physical assets.\n";
    fixture.old_catalog();
    const auto old = windows::load_lesson(fixture.path("legacyLesson"));
    const auto other = windows::load_lesson(fixture.path("otherLesson"));
    require(old.id == old_id && other.id == other_id, "El fixture cambió sus propietarios históricos.");
    state::StateData initial; seed_legacy(initial, old, other); profile.save(initial);
    const curriculum::CatalogLocation old_unit{"es", "programming", "qa_edition", "qa_edition__original"};
    const auto old_catalog = windows::load_catalog(fixture.content, old_unit);
    const auto old_progress = windows::catalog_entry_progress(fixture.content, old_unit,
        entry(old_catalog, old_id), initial);
    require(old_progress.completed == 2 && old_progress.total == 2,
        "El escenario antiguo no estaba completado antes de sustituir el índice.");
    std::string old_page_identity;
    {
        View opened(profile, old);
        require(lesson_page_state(opened.view).index == 1, "La edición antigua no recuperó su página final.");
        require(reference_available(opened.saved(), old, 1, 1), "El recibo antiguo no acredita su propia referencia.");
        old_page_identity = lesson_page_state(opened.view).narration_page_identity;
        update_lesson_preferences(opened.view, [&](auto& data) {
            // Marcadores de preservación, sin HeardPrefix ni prueba de escucha.
            auto history = narration::begin({}, "qa-old-recording", "qa-owner", {1, 137, 0, false},
                "qa-old-script", old_page_identity);
            history = narration::begin(history, "qa-other-recording", "qa-owner", {0, 29, 0, false},
                "qa-other-script", "programming/" + std::string(other_id) + "/0");
            narration::write_history(data, history);
        });
        (void)opened.saved();
    }
    auto historic = profile.load();
    // Conserva también un desplazamiento anterior que no pueda confundirse
    // con el comienzo de la página nueva, aunque el host QA sea más alto.
    historic.preferences["native_windows"]["scroll/" + old.id + "/" + old.pages.back().stable_id] = std::int64_t{137};
    profile.save(historic);
    const auto history = narration::read_history(historic);
    const auto old_key = curriculum::draft_key("programming", old, old.pages[1].blocks[0]);
    const auto history_root = profile.environment.paths.root / "state/active/files/cpp_project_history";
    windows::CppProjectHistory old_history(history_root, old_key);
    const auto revision = old_history.record({{"main.cpp", old.pages[1].blocks[0].initial_code}},
        {"main.cpp"}, "Revisión antigua QA");
    const auto revision_file = history_root / revision.project_key / (revision.id + ".revision");
    const auto revision_bytes = bytes(revision_file);
    const auto audio_sentinel = profile.environment.paths.cache / "qa-retained.opus";
    const std::string audio_bytes = "Fixture de conservación; no contiene audio reproducible.";
    write(audio_sentinel, audio_bytes);
    fixture.new_catalog();
    const auto current = windows::load_lesson(fixture.path("lesson"));
    require(current.id == new_id && current.pages.size() == 3 && current.course_edition,
        "No se cargó la reorganización como edición explícita.");
    const curriculum::CourseEdition descriptor{"qa_edition", "es", "qa-edition-1", 1};
    require(*current.course_edition == descriptor && current.state_layout.version == 2,
        "La autoría y el cargador no comparten el descriptor/mapa.");
    const auto metadata = windows::load_lesson_progress_metadata(fixture.path("lesson"));
    require(metadata.course_edition == current.course_edition
        && metadata.state_layout.lineage == current.state_layout.lineage,
        "El catálogo y el lector interpretan ediciones diferentes.");
    pending(historic, current); catalog_pending(fixture, historic);
    state::validate_lesson_state(historic, current);
    require(profile.load() == historic, "La comprobación de compatibilidad mutó el perfil.");
    const auto new_key = curriculum::draft_key("programming", current, current.pages[1].blocks[0]);
    require(old.pages[1].blocks[0].address.page == current.pages[1].blocks[0].address.page
        && old.pages[1].blocks[0].address.block == current.pages[1].blocks[0].address.block
        && old.pages[1].blocks[0].address.addition_namespace == current.pages[1].blocks[0].address.addition_namespace
        && old.pages[1].blocks[0].initial_code == current.pages[1].blocks[0].initial_code,
        "El escenario ya no prueba editores idénticos en la misma dirección relativa.");
    require(old_key != new_key && curriculum::exercise_key(old, old.pages[0].blocks[0])
        != curriculum::exercise_key(current, current.pages[0].blocks[0]),
        "La nueva edición reutilizó claves históricas.");
    windows::CppProjectHistory new_history(history_root, new_key);
    require(new_history.summaries().empty(), "El editor nuevo heredó revisiones del proyecto anterior.");
    rejects([&] { (void)new_history.load(revision.id); }, "Una revisión antigua se abrió bajo la nueva identidad.");
    state::StateData after;
    auto expected_after_interaction = historic;
    {
        View opened(profile, current);
        auto page = lesson_page_state(opened.view);
        require(page.index == 0 && !page.can_previous && !page.can_next, "La nueva vista no empezó pendiente en la página cero.");
        require(page.narration_page_identity != old_page_identity
            && page.narration_page_identity.find(current.id) != std::string::npos,
            "La nueva página heredó la identidad de narración anterior.");
        for (const auto& archive : editorial_lesson_reading_archives(opened.view))
            require(archive.page_identity.find(old.id) == std::string::npos, "El lector importó un archivo editorial de otra edición.");
        for (const auto& archive : retired_lesson_reading_archives(opened.view))
            require(archive.page_identity.find(old.id) == std::string::npos, "El lector importó una pausa de una edición retirada.");
        require(!navigate_lesson_view(opened.view, 2) && !navigate_lesson_view(opened.view, 1),
            "La navegación saltó actividades de la nueva edición.");
        const auto options = lesson_options_snapshot(opened.view);
        require(options.available_pages == std::vector<std::size_t>{0} && !options.finish_enabled,
            "El menú de páginas heredó acceso al final.");
        require(state::integer_value(opened.saved(), "native_windows",
            "scroll/" + current.id + "/" + current.pages[0].stable_id) == 0,
            "La nueva página heredó el desplazamiento antiguo.");
        require(narration::read_history(opened.saved()) == history,
            "Abrir la edición nueva borró o reclamó los marcadores anteriores.");
        preserved_values(historic, opened.saved());
        opened.answer(0, 0);
        const auto incorrect = opened.saved();
        const auto& previous_reserve = historic.preferences.at("attempt_reserve_seconds_test");
        const auto& current_reserve = incorrect.preferences.at("attempt_reserve_seconds_test");
        if (attempts::premium(historic)) {
            require(current_reserve == previous_reserve, "Una evaluación Premium consumió vidas.");
        } else {
            const auto before = attempts::number(historic, "reserve_programming", 0);
            const auto remaining = attempts::number(incorrect, "reserve_programming", 0);
            require(remaining <= before - .5 && remaining >= before - 1.001
                && state::integer_value(incorrect, "attempt_reserve_seconds_test", "updated_at_programming")
                    >= state::integer_value(historic, "attempt_reserve_seconds_test", "updated_at_programming"),
                "La respuesta incorrecta no descontó una vida de la reserva vigente.");
        }
        expected_after_interaction.preferences["attempt_reserve_seconds_test"] = current_reserve;
        auto response = state::get_response(incorrect, current, current.pages[0].blocks[0]);
        require(response && response->selected == 0 && !response->correct
            && !lesson_page_state(opened.view).can_next, "Se acreditó la respuesta correcta de la edición anterior.");
        opened.answer(0, 1);
        response = state::get_response(opened.saved(), current, current.pages[0].blocks[0]);
        require(response && response->selected == 1 && response->correct && lesson_page_state(opened.view).can_next,
            "La evaluación real no acreditó la respuesta actual.");
        require(!navigate_lesson_view(opened.view, 2), "Resolver el quiz permitió saltarse el editor.");
        require(navigate_lesson_view(opened.view, 1), "La evaluación actual no abrió la siguiente página.");
        pump();
        require(!navigate_lesson_view(opened.view, 2) && !lesson_page_state(opened.view).can_next,
            "El recibo antiguo abrió la referencia nueva.");
        bool compiler_region = false;
        for (const auto& region : lesson_reading_regions(opened.view))
            if (region.block_index == 0 && region.is_exercise && !region.feedback) {
                compiler_region = true;
                require(region.required_compiler && !region.solved,
                    "El lector presentó el editor nuevo como resuelto.");
            }
        require(compiler_region, "El lector omitió el editor obligatorio.");
        require(edit_text(opened.control(0, 10)) == current.pages[1].blocks[0].initial_code,
            "El editor mostró la fuente del borrador antiguo.");
        const auto source = "// Borrador propio de la nueva edición\n" + current.pages[1].blocks[0].initial_code;
        opened.replace_source(0, source);
        CHARRANGE selection{3, 9}; SendMessageW(opened.control(0, 10), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        opened.reopen();
        require(lesson_page_state(opened.view).index == 1 && edit_text(opened.control(0, 10)) == source,
            "Reabrir perdió la página o el borrador nuevo.");
        after = opened.saved();
        const auto draft = state::get_draft(after, new_key, "");
        require(draft.has_saved && draft.source == source && draft.selection_start == 3 && draft.selection_end == 9,
            "No se conservaron el borrador y selección propios de la edición nueva.");
        require(!windows::compiler_puzzle_completed(after, current, current.pages[1].blocks[0])
            && !reference_available(after, current, 2) && !navigate_lesson_view(opened.view, 2),
            "Reabrir o editar fabricó una acreditación de compilación.");
        require(!invoke_lesson_option(opened.view, LessonOptionAction::finish) && opened.completions == 0,
            "Se cerró una lección con editor pendiente.");
        preserved_values(expected_after_interaction, after);
        require(narration::read_history(after) == history, "La edición nueva alteró el historial de lectura.");
        require(appearance::text_scale(after, "qa_edition") == 120
            && appearance::text_scale(after, "qa_other") == 115, "Cambió la preferencia de tamaño del curso.");
        const auto lesson_catalog = windows::load_catalog(fixture.content, unit_location());
        const auto progress = windows::catalog_entry_progress(fixture.content, unit_location(),
            entry(lesson_catalog, new_id), after);
        require(progress.completed == 1 && progress.total == 3, "El catálogo no refleja únicamente el quiz nuevo evaluado.");
    }
    after = profile.load(); preserved_values(expected_after_interaction, after);
    require(bytes(revision_file) == revision_bytes && old_history.load(revision.id).files == revision.files
        && old_history.summaries().size() == 1 && new_history.summaries().empty(),
        "Cambió el historial antiguo o apareció bajo el nuevo proyecto.");
    require(bytes(audio_sentinel) == audio_bytes, "La edición borró o cambió un archivo de caché conservado.");
    import_isolated(fixture, historic, current, revision_file, revision);
    negative_contracts(fixture, current, after);
    physical_edition_long_paths(fixture);
    physical_edition_catalog_alias_rejected(fixture);
    fixture.preserved();
    std::cout << "PASS course_edition: índices activos, curso ajeno, estado/drafts/recibos separados, "
        "quiz nativo real, secuencia de páginas, reapertura, identidades de lectura, historial e importación.\n"
        "Alcance: recibo y bookmarks históricos sintéticos; sin compilación, voz ni escucha real.\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Uso: lgaed_course_edition_tests <directorio-fixtures>\n"; return 2; }
    const auto apartment = OleInitialize(nullptr);
    if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common); int result = 0;
    try { run(fs::path(argv[1])); }
    catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; result = 1; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    OleUninitialize(); return result;
}
