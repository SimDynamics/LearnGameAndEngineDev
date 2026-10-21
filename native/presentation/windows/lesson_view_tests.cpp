#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using curriculum::BlockType;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
int id(std::size_t block, int part) { return 1000 + static_cast<int>(block) * 100 + part; }
std::string window_text(HWND window) {
    const auto size = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(size) + 1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));
    return utf8(value);
}
HWND descendant(HWND parent, int control) {
    struct Search { int id; HWND result = nullptr; } search{control};
    EnumChildWindows(parent, [](HWND window, LPARAM value) -> BOOL {
        auto& current = *reinterpret_cast<Search*>(value);
        if (GetDlgCtrlID(window) == current.id) { current.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
void pump(HWND host) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        if (!IsDialogMessageW(host, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
void wait_for(HWND host, const std::function<bool()>& completed, std::string_view message,
              ULONGLONG timeout = 90000) {
    const auto deadline = GetTickCount64() + timeout;
    do {
        pump(host);
        if (completed()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(std::string(message));
}
LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}
struct Fixture {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::filesystem::path content;
    HWND host = nullptr, view = nullptr;
    explicit Fixture(const std::function<void(state::StateData&, const curriculum::Lesson&)>& initialize = {})
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"native-lesson-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false})),
        content(std::filesystem::path(build::source_root) / "app/src/main/assets/content") {
        require(!std::filesystem::exists(environment.paths.root), "QA instance unexpectedly already exists");
        initialize_profile_directories(environment.paths);
        lesson = windows::load_lesson(content / "es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json");
        if (initialize) {
            state::StateStore seed_store(environment.paths.root); auto seed = seed_store.load();
            initialize(seed, lesson); seed_store.save(seed);
        }
        host = CreateWindowExW(WS_EX_CONTROLPARENT, L"LgaedLessonViewTests", L"Native lesson integration QA",
            WS_OVERLAPPEDWINDOW, 30, 30, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create native QA host");
        open();
    }
    ~Fixture() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        require(!view, "Lesson view already open");
        view = create_lesson_view(host, environment, content);
        require(view != nullptr, "Cannot create real lesson view");
        RECT area{}; GetClientRect(host, &area);
        MoveWindow(view, 0, 0, area.right, area.bottom, TRUE); pump(host);
    }
    void close() {
        require(flush_lesson_view(view), "Lesson could not persist state");
        DestroyWindow(view); view = nullptr; pump(host);
    }
    void reopen() { close(); open(); }
    HWND control(int value) const {
        const auto result = descendant(view, value);
        require(result != nullptr, "Expected lesson control is missing: " + std::to_string(value)); return result;
    }
    HWND control(std::size_t block, int part) const { return control(id(block,part)); }
    std::string text(std::size_t block, int part) const { return edit_text(control(block,part)); }
    std::size_t page() const { return static_cast<std::size_t>(SendMessageW(control(102), CB_GETCURSEL, 0, 0)); }
    void click(int value) {
        const auto item = control(value); require(IsWindowEnabled(item), "Attempted a disabled control");
        SendMessageW(item, BM_CLICK, 0, 0); pump(host);
    }
    void click(std::size_t block, int part) { click(id(block,part)); }
    void replace(std::size_t block, int part, std::string_view value) {
        const auto item = control(block,part); require(IsWindowEnabled(item), "Cannot edit a locked answer");
        const auto source = wide(value);
        CHARRANGE range{0,-1}; SendMessageW(item, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        SendMessageW(item, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(source.c_str())); pump(host);
        require(edit_text(item) == value, "Native edit changed supplied answer/source characters");
    }
    void enter(std::size_t block) {
        const auto field = control(block,2); SetFocus(field);
        MSG key{field, WM_KEYDOWN, VK_RETURN, 1};
        if (!IsDialogMessageW(host, &key)) { TranslateMessage(&key); DispatchMessageW(&key); }
        pump(host);
        SendMessageW(field, WM_KEYUP, VK_RETURN, 1); pump(host);
    }
    void next() {
        const auto previous = page(); click(103); require(page() == previous + 1, "Next did not advance one page");
    }
    void select_page(std::size_t value) {
        const auto selector = control(102);
        SendMessageW(selector, CB_SETCURSEL, value, 0);
        SendMessageW(view, WM_COMMAND, MAKEWPARAM(102,CBN_SELCHANGE), reinterpret_cast<LPARAM>(selector));
        pump(host); require(page() == value, "Page selector did not restore the requested unlocked page");
    }
    void solve(std::size_t block) {
        const auto& current = lesson.pages.at(page()).blocks.at(block);
        if (current.type == BlockType::multiple_choice) {
            click(block,20 + static_cast<int>(*current.correct_index) * 2); click(block,3);
        } else if (current.type == BlockType::order_code) {
            std::vector<std::size_t> order(current.fragments.size()); std::iota(order.begin(), order.end(), 0);
            for (std::size_t position = 0; position < order.size(); ++position) {
                auto found = static_cast<std::size_t>(std::ranges::find(order,current.correct_order[position]) - order.begin());
                while (found > position) { click(block,20 + static_cast<int>(found) * 3); std::swap(order[found],order[found-1]); --found; }
            }
            click(block,3);
        } else {
            replace(block,2,current.answers.at(0)); const auto current_page = page(); enter(block);
            require(page() == current_page, "Enter checked and unexpectedly changed page");
        }
        require(text(block,4).starts_with("Correcto"), "Authored response was not accepted through actual controls");
    }
    void run(std::size_t block) {
        const auto compile = control(block,11);
        require(window_text(compile) == "Compilar, ejecutar y comprobar", "Combined compiler action label differs from authored outputCheck");
        click(block,11);
        wait_for(host,[&] { return IsWindowEnabled(compile) && text(block,14) != "Compilando…"; },
                 "Real C++ compile/execute operation did not complete");
    }
    state::StateData saved() {
        require(flush_lesson_view(view), "Explicit save failed"); state::StateStore reader(environment.paths.root); return reader.load();
    }
    void non_overlapping_rows() {
        HWND pane = nullptr;
        EnumChildWindows(view, [](HWND item, LPARAM output) -> BOOL {
            wchar_t type[96]{}; GetClassNameW(item,type,96);
            if (std::wstring_view(type) == L"LearnGameAndEngineDev.LessonPane") { *reinterpret_cast<HWND*>(output) = item; return FALSE; }
            return TRUE;
        },reinterpret_cast<LPARAM>(&pane));
        require(pane != nullptr, "Lesson pane missing");
        std::vector<RECT> rectangles;
        for (auto child = GetWindow(pane,GW_CHILD); child; child = GetWindow(child,GW_HWNDNEXT)) {
            RECT rect{}; GetWindowRect(child,&rect); if (rect.bottom > rect.top && rect.right > rect.left) rectangles.push_back(rect);
        }
        std::ranges::sort(rectangles,[](const RECT& left, const RECT& right) { return left.top < right.top; });
        LONG row_top = std::numeric_limits<LONG>::min(), row_bottom = row_top;
        for (const auto& rectangle : rectangles) {
            if (rectangle.top != row_top) {
                require(rectangle.top >= row_bottom, "Lesson rows overlap after native layout"); row_top = rectangle.top;
            }
            row_bottom = std::max(row_bottom,rectangle.bottom);
        }
    }
};
std::string message_code(const curriculum::Block& block, std::string_view old_message, std::string_view new_message) {
    auto source = block.initial_code;
    const auto position = source.find(old_message); require(position != source.npos, "Authored literal fixture missing");
    source.replace(position,old_message.size(),new_message); return source;
}
}

int main() {
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr); host.lpszClassName = L"LgaedLessonViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        Fixture view;
        require(view.page() == 0 && !IsWindowEnabled(view.control(103)), "A fresh profile bypassed initial page exercises");
        view.close();
        {
            state::StateStore untouched(view.environment.paths.root);
            require(untouched.empty(), "Closing an untouched first page made a fresh profile ineligible for import");
        }
        view.open();
        require(view.page() == 0 && !IsWindowEnabled(view.control(103)), "An untouched reopen changed the initial bookmark or exercise gate");
        {
            state::StateStore untouched(view.environment.paths.root);
            require(untouched.empty(), "Reopening an untouched profile published personal state");
        }
        view.non_overlapping_rows(); view.solve(4); view.next();
        std::cout << "PASS Untouched profile remains importable after close/reopen; initial choice controls and forward gate\n" << std::flush;

        const auto& first_prediction = view.lesson.pages[1].blocks[3];
        view.replace(3,2,first_prediction.answers.at(0) + ' '); view.enter(3);
        require(view.page() == 1 && view.text(3,4).starts_with("Incorrecto") && !IsWindowEnabled(view.control(103)), "Wrong exact prediction did not stay on its page");
        view.reopen();
        require(view.page() == 1 && view.text(3,4).starts_with("Incorrecto"), "Reviewed incorrect feedback did not reopen");
        view.replace(3,2,first_prediction.answers.at(0));
        require(view.text(3,4).empty(), "Editing retained an obsolete incorrect verdict");
        view.reopen(); require(view.text(3,4).empty(), "Unreviewed edited response reopened as Incorrecto");
        view.enter(3);
        require(view.page() == 1 && view.text(3,4).starts_with("Correcto") && !IsWindowEnabled(view.control(3,2)), "Correct response was not locked or Enter navigated");
        require(window_text(view.control(3,3)) == "Reiniciar respuesta", "Correct quiz lacks explicit reset action");
        view.click(3,3);
        require(IsWindowEnabled(view.control(3,2)) && view.text(3,2).empty() && view.text(3,4).empty(), "Reset did not restore an editable unanswered quiz");
        view.solve(3); view.solve(4);
        require(view.page() == 1 && IsWindowEnabled(view.control(103)), "Last Enter auto-advanced instead of enabling Next");
        view.next(); std::cout << "PASS Exact prediction, reviewed feedback, single-line Enter and explicit reset\n" << std::flush;

        view.solve(3); view.solve(4); view.solve(5); view.non_overlapping_rows(); view.next();
        view.solve(2); view.solve(3); view.non_overlapping_rows(); view.next();
        require(view.page() == 4, "Six-page sequence did not reach the authored compiler activities");
        view.solve(1);
        require(!IsWindowEnabled(view.control(103)), "Page 5 concept question was bypassed");
        require(!descendant(view.view,id(3,15)) && !descendant(view.view,id(6,15)), "Unrequested stdin fields are visible");
        RECT caption{}, reset{}, source{};
        GetWindowRect(view.control(3,16), &caption); GetWindowRect(view.control(3,12), &reset);
        GetWindowRect(view.control(3,10), &source);
        require(caption.top == reset.top && reset.left >= caption.right && source.top > caption.bottom,
                "Reset control is not beside C++ as specified by the existing lesson");
        SCROLLINFO wheel_before{sizeof(wheel_before), SIF_POS}, wheel_after = wheel_before;
        const auto pane = GetParent(view.control(3,10)); GetScrollInfo(pane, SB_VERT, &wheel_before);
        SendMessageW(view.control(2,0), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        GetScrollInfo(pane, SB_VERT, &wheel_after);
        require(wheel_after.nPos > wheel_before.nPos, "Wheel over read-only prose did not scroll the lesson");
        const auto& original = view.lesson.pages[4].blocks[3];
        const auto& modified = view.lesson.pages[4].blocks[6];
        require(view.text(3,10) == original.initial_code && view.text(6,10) == modified.initial_code, "Original sources lost indentation or final LF");
        view.run(3);
        require(view.text(3,14) == "Zona inicial" && view.text(3,4) == "Correcto", "Original program did not produce/check its fresh exact output");
        const auto changed_source = message_code(modified,"Zona inicial","Caja recibida");
        view.replace(6,10,changed_source);
        require(view.text(3,10) == original.initial_code, "Modification editor changed the independent original draft");
        view.run(6);
        require(view.text(6,14) == "Caja recibida" && view.text(6,4).starts_with("Correcto\n"), "Modified compiler lost its explanation or checked old output");
        std::cout << "PASS Real original and modified C++ executions, integrated checking and independent editors\n" << std::flush;

        view.replace(6,10,message_code(modified,"Zona inicial","Zona segura"));
        require(view.text(6,4).empty(), "Editing retained the preceding correct verdict");
        view.run(6);
        require(view.text(6,4).starts_with("Incorrecto\nRevisa el mensaje solicitado"), "Authored targeted output review was not selected");
        view.replace(6,10,message_code(modified,"Zona inicial","Salida desconocida")); view.run(6);
        require(view.text(6,4) == "Incorrecto", "An unknown output invented a pedagogical explanation");
        auto broken = changed_source; const auto terminator = broken.find("\"Caja recibida\";");
        require(terminator != broken.npos, "Missing authored statement fixture"); broken.erase(terminator + std::string("\"Caja recibida\"").size(),1);
        view.replace(6,10,broken); view.run(6);
        require(view.text(6,14).starts_with("Diagnósticos de compilación") && view.text(6,4).empty(), "Compilation failure became an Incorrecto response");
        view.click(6,12);
        require(view.text(6,10) == modified.initial_code && view.text(6,4).empty() && view.text(6,14).empty(), "Reset did not restore exact authored code and clear stale results");

        const auto original_draft = original.initial_code + "// borrador original QA\n";
        const auto modified_draft = changed_source + "// borrador de modificación QA\n";
        view.replace(3,10,original_draft); view.replace(6,10,modified_draft); view.run(6);
        auto saved = view.saved();
        require(state::get_draft(saved,curriculum::draft_key("programming",view.lesson,original),original.initial_code).source == original_draft &&
                state::get_draft(saved,curriculum::draft_key("programming",view.lesson,modified),modified.initial_code).source == modified_draft,
                "Independent drafts were not persisted in their original/new historical slots");
        CHARRANGE cursor{3,7};
        SendMessageW(view.control(6,10), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cursor));
        saved = view.saved();
        const auto cursor_draft = state::get_draft(saved,curriculum::draft_key("programming",view.lesson,modified),modified.initial_code);
        require(cursor_draft.selection_start == 3 && cursor_draft.selection_end == 7,
                "Closing/saving after only moving the selection lost the current cursor");
        view.reopen();
        require(view.page() == 4 && view.text(3,10) == original_draft && view.text(6,10) == modified_draft, "Compiler drafts or bookmark changed on reopen");
        require(view.text(3,4).empty() && view.text(6,4).empty(), "A persisted execution verdict was reused without a fresh run");
        view.select_page(3); view.next();
        require(view.text(6,10) == modified_draft && view.text(6,4).empty(), "Page transition reused a console verdict or lost a draft");
        view.solve(7); view.non_overlapping_rows(); view.next();
        std::cout << "PASS Compilation diagnostics, edit/reset invalidation, targeted reviews and persistent independent drafts\n" << std::flush;

        require(view.page() == 5 && !IsWindowEnabled(view.control(103)), "Final page navigation escaped the lesson");
        const auto& independent = view.lesson.pages[5].blocks[6];
        view.replace(6,10,message_code(independent,"Caja recibida","Zona segura")); view.run(6);
        require(view.text(6,14) == "Zona segura" && view.text(6,4) == "Correcto", "Page 6 did not keep its independent target");
        view.reopen();
        require(view.page() == 5 && view.text(6,10).find("Zona segura") != std::string::npos && view.text(6,4).empty(), "Final reopen lost position/source or reused a verdict");
        view.non_overlapping_rows();
        view.close();
        std::cout << "PASS Complete six-page native lesson route, final independent output and layout bounds\n" << std::flush;

        // This is already-exported legacy state adoption, not an automated file-dialog import.
        std::string legacy_source, legacy_input = " \tRita 4\r\n \n", partial_answer;
        std::int64_t legacy_start = 0, legacy_end = 0;
        Fixture legacy([&](state::StateData& data, const curriculum::Lesson& lesson) {
            const auto prefix = "lesson_" + lesson.id;
            auto& progress = data.preferences["learning_progress"];
            progress[prefix + "_started"] = true; progress[prefix + "_page"] = std::int64_t{4};
            progress[prefix + "_state_lineage_v1"] = lesson.state_layout.legacy_lineage;
            std::vector<std::string> pages, blocks;
            for (std::size_t p = 0; p <= 4; ++p) {
                pages.push_back(lesson.state_layout.origin[p].id);
                const auto& original_blocks = lesson.state_layout.origin[p].blocks;
                blocks.insert(blocks.end(),original_blocks.begin(),original_blocks.end());
            }
            progress[prefix + "_read_pages_v1"] = pages; progress[prefix + "_read_blocks_v1"] = blocks;
            partial_answer = "{\"answer\":\"Zona \",\"correct\":false,\"qaMetadata\":{\"preserve\":true}}";
            progress[curriculum::exercise_key(lesson, lesson.pages[4].blocks[1])] = partial_answer;
            const auto lf_source = message_code(lesson.pages[4].blocks[6],"Zona inicial","Caja heredada");
            for (const auto c : lf_source) { if (c == '\n') legacy_source += '\r'; legacy_source += c; }
            legacy_source += "// sesión 😀 heredada\r\n  \t ";
            const auto document = windows::predefined_code_document(legacy_source);
            const auto token = legacy_source.find("std::cout");
            require(token != legacy_source.npos,"Historical source fixture token missing");
            legacy_start = static_cast<std::int64_t>(wide(std::string_view(legacy_source).substr(0,token)).size());
            legacy_end = legacy_start + 9;
            const auto draft = curriculum::draft_key("programming",lesson,lesson.pages[4].blocks[6]);
            auto& sources = data.preferences["cpp_lesson_drafts"];
            sources[draft + "/source"] = legacy_source; sources[draft + "/input"] = legacy_input;
            sources[draft + "/sourceIndentation_v2"] = state::make_indentation_metadata(legacy_source,document.indentation_starts);
            sources[draft + "/windowsSelectionStart"] = legacy_start; sources[draft + "/windowsSelectionEnd"] = legacy_end;
        });
        require(legacy.page() == 4 && legacy.text(1,2) == "Zona " && legacy.text(1,4).empty(),
                "A partial historical prediction was lost or treated as a reviewed answer");
        require(legacy.text(3,10) == legacy.lesson.pages[4].blocks[3].initial_code,
                "The added original editor claimed the historical modification draft");
        require(!descendant(legacy.view,id(3,15)) && !descendant(legacy.view,id(6,15)),
                "An old hidden input invented a visible curricular input field");
        legacy.run(6);
        require(legacy.text(6,14) == "Caja heredada" && legacy.text(6,4) == "Incorrecto",
                "The imported historical source was not executed as its own current draft");
        bool found_input = false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(legacy.environment.paths.projects))
            if (entry.path().filename() == L"stdin.bin") {
                found_input = true; require(entry.file_size() == 0,"Unrequested hidden input was fed to the new program");
            }
        require(found_input,"Real compiler run did not expose its isolated standard-input artifact");
        legacy.reopen();
        const auto persisted = legacy.saved();
        const auto restored = state::get_draft(persisted,
            curriculum::draft_key("programming",legacy.lesson,legacy.lesson.pages[4].blocks[6]),legacy.lesson.pages[4].blocks[6].initial_code);
        require(restored.source == legacy_source && restored.input == legacy_input,
                "Opening, executing or closing a legacy draft changed CRLF, whitespace or its hidden input");
        require(restored.selection_start == legacy_start && restored.selection_end == legacy_end,
                "Legacy source selection did not survive its native display mapping");
        require(state::string_value(persisted,"learning_progress",
            curriculum::exercise_key(legacy.lesson,legacy.lesson.pages[4].blocks[1])) == partial_answer,
                "An unchanged partial historical answer lost opaque metadata");
        require(legacy.text(6,4).empty(),"An imported compiler draft reopened with a stale execution verdict");
        std::cout << "PASS Historical v1 state adoption, partial answers, independent added editor, CRLF/Unicode/caret and unrequested input preservation\n"
                  << "Native lesson UI integration: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Native lesson UI integration: " << error.what() << '\n'; return 1;
    }
}
