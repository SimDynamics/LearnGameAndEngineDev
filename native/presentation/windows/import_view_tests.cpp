#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using curriculum::BlockType;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

int block_id(std::size_t block, int part) {
    return 1000 + static_cast<int>(block) * 100 + part;
}

HWND descendant(HWND parent, int id) {
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND child, LPARAM parameter) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(parameter);
        if (GetDlgCtrlID(child) == value.id) { value.result = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

std::string window_text(HWND control) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return utf8(text);
}

void pump(HWND host) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        if (!IsDialogMessageW(host, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}

void wait_for(HWND host, const std::function<bool()>& ready, std::string_view message) {
    const auto deadline = GetTickCount64() + 90000;
    do {
        pump(host);
        if (ready()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(std::string(message));
}
thread_local bool menu_observed = false;
void CALLBACK cancel_menu_timer(HWND,UINT,UINT_PTR timer,DWORD) {
    GUITHREADINFO info{sizeof(info)};
    menu_observed = GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags & GUI_INMENUMODE);
    KillTimer(nullptr,timer); EndMenu();
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

void write_file(const std::filesystem::path& file, std::string_view value) {
    std::filesystem::create_directories(state::native_io_path(file.parent_path()));
    std::ofstream output(state::native_io_path(file), std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
    require(static_cast<bool>(output), "Cannot write isolated Android export fixture");
}

// Numeric character references preserve CR, LF and TAB exactly through XML
// parsing, including raw legacy strings; escaping is not limited to JSON-v1.
std::string xml_text(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        case '\r': result += "&#13;"; break;
        case '\n': result += "&#10;"; break;
        case '\t': result += "&#9;"; break;
        default: result += character; break;
        }
    }
    return result;
}

void export_preferences(const std::filesystem::path& folder, const state::StateData& data) {
    for (const auto& [domain, values] : data.preferences) {
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<map>";
        for (const auto& [key, value] : values) std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            const auto attribute = " name=\"" + xml_text(key) + "\"";
            if constexpr (std::is_same_v<T, std::string>)
                xml += "<string" + attribute + ">" + xml_text(item) + "</string>";
            else if constexpr (std::is_same_v<T, bool>)
                xml += "<boolean" + attribute + " value=\"" + (item ? "true" : "false") + "\"/>";
            else if constexpr (std::is_same_v<T, std::int64_t>)
                xml += "<long" + attribute + " value=\"" + std::to_string(item) + "\"/>";
            else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream number; number.imbue(std::locale::classic());
                number << std::setprecision(17) << item;
                xml += "<float" + attribute + " value=\"" + number.str() + "\"/>";
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                xml += "<set" + attribute + ">";
                for (const auto& entry : item) xml += "<string>" + xml_text(entry) + "</string>";
                xml += "</set>";
            }
        }, value);
        xml += "</map>";
        write_file(folder / L"shared_prefs" / (domain + ".xml"), xml);
    }
}

using FileHashes = std::map<std::filesystem::path, std::string>;
FileHashes hashes(const std::filesystem::path& folder) {
    FileHashes result;
    for (const auto& file : std::filesystem::recursive_directory_iterator(state::native_io_path(folder))) {
        if (!file.is_regular_file()) continue;
        result[file.path().lexically_relative(state::native_io_path(folder))] =
            state::sha256(state::read_bytes(file.path()));
    }
    return result;
}

std::string crlf(std::string_view source) {
    std::string result;
    for (const auto character : source) { if (character == '\n') result += '\r'; result += character; }
    return result;
}
std::string visual_lf(std::string_view source) {
    std::string result;
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\r') {
            result += '\n';
            if (index + 1 < source.size() && source[index + 1] == '\n') ++index;
        } else result += source[index];
    }
    return result;
}

std::string byte_difference(std::string_view expected, std::string_view actual) {
    std::size_t first = 0;
    while (first < expected.size() && first < actual.size() && expected[first] == actual[first]) ++first;
    return " expected bytes=" + std::to_string(expected.size()) + " actual bytes=" + std::to_string(actual.size()) +
        " first difference=" + std::to_string(first) + " CR=" +
        std::to_string(std::count(expected.begin(), expected.end(), '\r')) + "->" +
        std::to_string(std::count(actual.begin(), actual.end(), '\r')) + " LF=" +
        std::to_string(std::count(expected.begin(), expected.end(), '\n')) + "->" +
        std::to_string(std::count(actual.begin(), actual.end(), '\n'));
}

struct ExportFixture {
    std::filesystem::path folder;
    state::StateData data;
    std::size_t prediction = 0, conceptual = 0;
    std::vector<std::size_t> editors;
    std::vector<state::Draft> drafts;

    ExportFixture(const std::filesystem::path& path, const curriculum::Lesson& lesson) : folder(path) {
        require(lesson.state_layout.version == 2 && lesson.pages.size() == 6, "Expected original U1L2 stateLayout v2");
        state::save_current_page(data, lesson, 4);
        // These are already-submitted synthetic previous-page responses. The
        // page-five prediction below deliberately remains unsubmitted.
        for (std::size_t page = 0; page != 4; ++page) for (const auto& block : lesson.pages[page].blocks) {
            state::Response answer;
            if (block.type == BlockType::multiple_choice) answer.selected = static_cast<std::int64_t>(*block.correct_index);
            else if (block.type == BlockType::order_code) answer.order.assign(block.correct_order.begin(), block.correct_order.end());
            else if (block.type == BlockType::output_prediction || block.type == BlockType::fill_code) answer.answer = block.answers.at(0);
            else continue;
            answer.correct = true;
            state::put_response(data, curriculum::exercise_key(lesson, block), answer);
        }
        for (std::size_t index = 0; index < lesson.pages[4].blocks.size(); ++index) {
            const auto& block = lesson.pages[4].blocks[index];
            if (block.type == BlockType::output_prediction) prediction = index;
            else if (block.type == BlockType::multiple_choice) conceptual = index;
            else if (block.type == BlockType::cpp_compiler) editors.push_back(index);
        }
        require(editors.size() == 2, "Expected two independent page-five editors");
        state::Response partial;
        partial.answer = lesson.pages[4].blocks[prediction].answers.at(0);
        state::put_response(data, curriculum::exercise_key(lesson, lesson.pages[4].blocks[prediction]), partial);
        for (std::size_t position = 0; position != editors.size(); ++position) {
            const auto& block = lesson.pages[4].blocks[editors[position]];
            auto source = block.initial_code;
            if (position) {
                const auto literal = source.find("Zona inicial");
                require(literal != source.npos, "Missing original U1L2 literal");
                source.replace(literal, std::string("Zona inicial").size(), "Caja recibida");
            }
            state::Draft draft;
            draft.source = position ? "// versión JSON-v1 😀\r" + crlf(source) + "\t// segundo borrador\r\n"
                                    : "// versión original 😀\r\n" + crlf(source) + "// conserva espacios \t\r\n    ";
            draft.input = position ? "\tLeo\r2\r\n\t \n" : " \tRita 4\r\n \n    ";
            const auto document = windows::predefined_code_document(draft.source);
            draft.indentation_json = state::make_indentation_metadata(draft.source, document.indentation_starts);
            const auto token = draft.source.find("std::cout");
            require(token != draft.source.npos, "Missing known source token for UTF16 caret");
            draft.selection_start = static_cast<std::int64_t>(wide(std::string_view(draft.source).substr(0, token)).size());
            draft.selection_end = draft.selection_start + 9;
            const auto key = curriculum::draft_key("programming", lesson, block);
            state::put_draft(data, key, draft);
            if (!position) {
                // Genuine historical non-JSON storage, with independent SHA
                // and indentation sidecar. No invented positional keys.
                auto& preferences = data.preferences["cpp_lesson_drafts"];
                preferences[key + "/source"] = draft.source;
                preferences[key + "/input"] = draft.input;
                preferences.erase(key + "/sourceEncoding");
                preferences.erase(key + "/inputEncoding");
            }
            drafts.push_back(std::move(draft));
        }
        const auto first_key = curriculum::draft_key("programming", lesson, lesson.pages[4].blocks[editors[0]]);
        const auto second_key = curriculum::draft_key("programming", lesson, lesson.pages[4].blocks[editors[1]]);
        require(first_key != second_key && first_key.find("state-layout-v2/") != first_key.npos,
            "Added editor must not steal the historical modification slot");
        // Old cached compiler correctness is retained as opaque source state,
        // but the UI must never turn it into a fresh execution verdict.
        state::Response stale; stale.answer = "Resultado antiguo"; stale.correct = true;
        state::put_response(data, curriculum::exercise_key(lesson, lesson.pages[4].blocks[editors[1]]), stale);
        state::validate_lesson_state(data, lesson);
        publish();
        write_file(folder / L"files/cpp_projects/main.cpp", "\tint dato = 4;\r\n// archivo del estudiante 😀\r\n");
        write_file(folder / L"files/cpp_projects/entrada.txt", " \tRita 4\r\n    ");
    }

    void publish() const { export_preferences(folder, data); }
};

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::filesystem::path content;
    HWND host = nullptr, view = nullptr;

    View() : environment(resolve_windows_environment({ProfileId::Codex,
            L"native-import-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false})),
        content(std::filesystem::path(build::source_root) / L"app/src/main/assets/content") {
        require(!std::filesystem::exists(environment.paths.root), "An import UI test cannot reuse a real profile");
        initialize_profile_directories(environment.paths);
        lesson = windows::load_lesson(content / L"es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json");
        host = CreateWindowExW(WS_EX_CONTROLPARENT, L"LgaedImportViewTests", L"Native Android import QA",
            WS_OVERLAPPEDWINDOW, 30, 30, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create native import host");
        open();
    }
    ~View() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        view = create_lesson_view(host, environment, content);
        require(view != nullptr, "Cannot create the real lesson/import view");
        RECT bounds{}; GetClientRect(host, &bounds);
        MoveWindow(view, 0, 0, bounds.right, bounds.bottom, TRUE);
        pump(host);
    }
    void reopen() {
        require(flush_lesson_view(view), "Cannot flush before reopening");
        DestroyWindow(view); view = nullptr; pump(host); open();
    }
    HWND control(int id) const {
        const auto result = descendant(view, id);
        require(result != nullptr, "Missing native control " + std::to_string(id));
        return result;
    }
    HWND control(std::size_t block, int part) const { return control(block_id(block, part)); }
    void click(int id) {
        if (id == 104) {
            require(IsWindowEnabled(control(107)),"A disabled lesson menu cannot open import");
            // Import is now a menu command, rather than an invisible button.
            SendMessageW(view,WM_COMMAND,MAKEWPARAM(104,0),0); pump(host); return;
        }
        const auto button = control(id);
        require(IsWindowEnabled(button), "A disabled import control cannot be activated");
        SendMessageW(button, BM_CLICK, 0, 0);
        if (id == 206)
            require(IsWindow(button), "Import navigation destroyed its button during the native BM_CLICK callback");
        pump(host);
    }
    void cancel_menu() {
        const bool shown = IsWindowVisible(host); if (!shown) { ShowWindow(host,SW_SHOWNOACTIVATE); pump(host); }
        menu_observed = false;
        const auto timer = SetTimer(nullptr,0,50,cancel_menu_timer);
        require(timer != 0,"Cannot schedule native menu cancellation");
        click(107); KillTimer(nullptr,timer);
        require(menu_observed && !descendant(view,205),"The actual native menu did not open/cancel without choosing import");
        if (!shown) { ShowWindow(host,SW_HIDE); pump(host); }
    }
    void path(const std::filesystem::path& folder) {
        require(SetWindowTextW(control(201), folder.c_str()) != FALSE, "Cannot enter the exported folder");
        pump(host);
        require(window_text(control(201)) == utf8(folder.wstring()), "The real path EDIT changed Unicode or symbols");
    }
    std::string summary() const { return edit_text(control(204)); }
    bool empty() const { return state::StateStore(environment.paths.root).empty(); }
    std::size_t page() const { return lesson_page_state(view).index; }
    state::StateData saved() const {
        require(flush_lesson_view(view), "Cannot save the restored lesson");
        state::StateStore reader(environment.paths.root); return reader.load();
    }
};

void no_pending_transaction(const View& view) {
    const auto root = view.environment.paths.root / L"state";
    if (!std::filesystem::exists(root)) return;
    for (const auto& file : std::filesystem::directory_iterator(root))
        require(!file.path().filename().wstring().ends_with(L".pending"), "Failed import left an unpublished transaction");
}

void assert_restored(View& view, const ExportFixture& exported) {
    require(view.page() == 4, "The lesson did not restore the historical page-five bookmark");
    const auto data = view.saved();
    const auto& prediction = view.lesson.pages[4].blocks[exported.prediction];
    const auto answer = state::get_response(data, curriculum::exercise_key(view.lesson, prediction));
    require(answer && answer->answer == prediction.answers.at(0) && !answer->correct &&
        !state::response_reviewed(*answer, prediction), "A partial correct-looking answer was silently submitted/evaluated");
    require(edit_text(view.control(exported.prediction, 2)) == prediction.answers.at(0) &&
        edit_text(view.control(exported.prediction, 4)).empty() && IsWindowEnabled(view.control(exported.prediction, 2)),
        "Partial prediction did not remain visible, editable and unreviewed");
    for (std::size_t position = 0; position != exported.editors.size(); ++position) {
        const auto index = exported.editors[position];
        const auto& block = view.lesson.pages[4].blocks[index];
        const auto restored = state::get_draft(data, curriculum::draft_key("programming", view.lesson, block), block.initial_code);
        const auto& expected = exported.drafts[position];
        require(restored.source == expected.source,
            "Opening/restoring changed editor " + std::to_string(position) + " source:" +
                byte_difference(expected.source, restored.source));
        require(restored.input == expected.input,
            "Opening/restoring changed editor " + std::to_string(position) + " hidden stdin:" +
                byte_difference(expected.input, restored.input));
        require(state::indentation_units(restored.source, restored.indentation_json) ==
            state::indentation_units(expected.source, expected.indentation_json), "Imported indentation origin was not retained");
        require(restored.selection_start == expected.selection_start && restored.selection_end == expected.selection_end,
            "Imported UTF16 source selection changed during display/save");
        require(edit_text(view.control(index, 10)) == visual_lf(expected.source), "Source displayed the wrong independent editor draft");
        require(!descendant(view.view, block_id(index, 15)), "An imported hidden input invented a curricular stdin field");
        require(edit_text(view.control(index, 4)).empty() && edit_text(view.control(index, 14)).empty(),
            "An imported compiler state revived a stale verdict or console output");
    }
}

} // namespace

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr);
    host.lpszClassName = L"LgaedImportViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        View view;
        ExportFixture exported(view.environment.paths.projects / L"exportación & revisión", view.lesson);
        const auto initial_hashes = hashes(exported.folder);
        view.cancel_menu();
        view.click(104);
        require(!IsWindowEnabled(view.control(205)) && (GetWindowLongPtrW(view.control(204), GWL_STYLE) & ES_READONLY),
            "Import must start with a readonly summary and disabled confirmation");
        require(view.empty(), "Opening the import panel published active state");
        view.click(206); require(!descendant(view.view, 201) && view.empty(), "Cancel did not leave an untouched profile");
        view.reopen(); require(view.empty(), "Closing/reopening after Cancel made the empty profile ineligible");
        view.click(104); view.path(exported.folder / L"inexistente"); view.click(203);
        require(view.summary().starts_with("No se puede importar") && !IsWindowEnabled(view.control(205)) && view.empty(),
            "An invalid folder enabled confirmation or published state");
        require(hashes(exported.folder) == initial_hashes, "Opening/canceling/invalid review changed exported files");
        std::cout << "PASS Real import panel opening, cancel, invalid path and untouched-profile reopen\n" << std::flush;

        auto incompatible = exported.data;
        const auto& conceptual = view.lesson.pages[4].blocks[exported.conceptual];
        state::Response wrong; wrong.selected = 999;
        state::mark_response_reviewed(wrong, conceptual);
        state::put_response(incompatible, curriculum::exercise_key(view.lesson, conceptual), wrong);
        const auto incompatible_folder = view.environment.paths.projects / L"export-incompatible";
        export_preferences(incompatible_folder, incompatible);
        const auto incompatible_hashes = hashes(incompatible_folder);
        view.path(incompatible_folder); view.click(203);
        require(view.summary().starts_with("No se puede importar") && !IsWindowEnabled(view.control(205)) && view.empty(),
            "An incompatible reviewed choice was accepted or silently repaired");
        require(hashes(incompatible_folder) == incompatible_hashes, "Rejected reviewed state changed its source XML");
        no_pending_transaction(view);
        std::cout << "PASS Actual U1L2 stateLayout validation rejects reviewed out-of-range choices without publishing\n" << std::flush;

        view.path(exported.folder); view.click(203);
        require(IsWindowEnabled(view.control(205)) && view.summary().starts_with("Se verificaron") && view.empty(),
            "Valid preview failed, or review imported before confirmation");
        require(hashes(exported.folder) == initial_hashes, "Preview changed original export bytes");
        view.path(exported.folder / L"otra ruta");
        require(!IsWindowEnabled(view.control(205)) && view.empty(), "Changing the path retained a stale confirmed preview");
        view.path(exported.folder); view.click(203);
        require(IsWindowEnabled(view.control(205)) && view.empty(), "A new review did not restore an actionable preview");
        std::cout << "PASS Valid review is read-only and changing the path invalidates confirmation\n" << std::flush;

        write_file(exported.folder / L"files/cpp_projects/entrada.txt", " \tLeo\r\n2\r\n    ");
        const auto changed_hashes = hashes(exported.folder);
        view.click(205);
        require(view.summary().starts_with("No se ha completado") && !IsWindowEnabled(view.control(205)) && view.empty(),
            "A changed source was imported using a stale preview");
        require(hashes(exported.folder) == changed_hashes, "Refused confirmation changed source files");
        no_pending_transaction(view);
        view.click(203); require(IsWindowEnabled(view.control(205)), "Retry review did not recover after a source change");
        view.click(205);
        require(!view.empty() && view.summary().starts_with("Importación completada") &&
            !IsWindowEnabled(view.control(205)) && IsWindowEnabled(view.control(201)) &&
            (GetWindowLongPtrW(view.control(201), GWL_STYLE) & ES_READONLY) &&
            window_text(view.control(206)) == "Volver a la lección", "Successful confirmation did not complete the actual import flow");
        require(hashes(exported.folder) == changed_hashes, "Successful import changed the original export");
        for (const auto& [relative, hash] : changed_hashes)
            require(state::sha256(state::read_bytes(view.environment.paths.root / L"state/active/android" / relative)) == hash,
                "The transactional archive did not preserve an exported file exactly");
        no_pending_transaction(view);
        std::cout << "PASS Source mutation is refused, then a new review and explicit confirmation import atomically\n" << std::flush;

        view.click(206);
        require(!descendant(view.view, 201), "Returning to the lesson left the import controls active");
        assert_restored(view, exported);
        require(navigate_lesson_view(view.view,3),"Backward page navigation after import was blocked");
        pump(view.host); require(view.page() == 3, "Backward page navigation after import failed");
        require(navigate_lesson_view(view.view,4),"Forward page navigation after import was blocked");
        pump(view.host); assert_restored(view, exported);
        view.reopen(); assert_restored(view, exported);
        require(hashes(exported.folder) == changed_hashes, "Native lesson reopen changed the Android source export");
        std::cout << "PASS Real page-five restoration, independent legacy/JSON-v1 drafts, exact whitespace and unsubmitted answers\n" << std::flush;

        // A cached correct compiler response is still preserved as source
        // state, while its visible verdict above remained empty on reopen.
        const auto before_run = view.saved();
        const auto cached = state::get_response(before_run, curriculum::exercise_key(view.lesson,
            view.lesson.pages[4].blocks[exported.editors[1]]));
        require(cached && cached->correct && cached->answer == "Resultado antiguo", "Opaque historical compiler state was discarded");
        const auto original = exported.editors[0];
        const auto compile = view.control(original, 11);
        view.click(block_id(original, 11));
        wait_for(view.host, [&] { return IsWindowEnabled(compile) && edit_text(view.control(original, 14)) != "Compilando…"; },
            "A freshly imported real source could not compile and execute");
        require(edit_text(view.control(original, 14)) == "Zona inicial" && edit_text(view.control(original, 4)) == "Correcto",
            "A fresh imported source did not produce/check its real output");
        view.reopen(); assert_restored(view, exported);
        require(hashes(exported.folder) == changed_hashes, "Compiling the imported source changed the original export");
        std::cout << "PASS Imported C++ compiles freshly; cached correctness never becomes a persisted execution verdict\n"
            << "Native import UI integration: 6 groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Native import UI integration: " << error.what() << '\n';
        return 1;
    }
}
