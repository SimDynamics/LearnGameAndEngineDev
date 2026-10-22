#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lgaed::presentation {
namespace {
using curriculum::BlockType;
using windows::CodeEditor;
constexpr wchar_t view_class[] = L"LearnGameAndEngineDev.LessonView";
constexpr wchar_t pane_class[] = L"LearnGameAndEngineDev.LessonPane";
constexpr UINT result_message = WM_APP + 71;
constexpr UINT flush_message = WM_APP + 72;
constexpr UINT close_import_message = WM_APP + 74;
constexpr UINT_PTR save_timer = 73;
constexpr int previous_id = 101, page_id = 102, next_id = 103, import_id = 104;
constexpr int import_prompt_id = 200, import_path_id = 201, import_browse_id = 202;
constexpr int import_review_id = 203, import_summary_id = 204, import_commit_id = 205, import_cancel_id = 206;
constexpr int import_label_id = 207;
constexpr std::string_view route = "programming";

enum class ActionType { check, choose, move_up, move_down, run, reset, tab };
struct Action { int control; ActionType type; std::size_t block; std::size_t option = 0; };
struct Row {
    std::vector<HWND> cells;
    // Positive widths are logical pixels; zero takes the remaining width.
    std::vector<int> widths;
    int logical_height = 30;
    bool size_to_text = false;
    int requested_height = 0;
};
struct BlockControls {
    HWND answer = nullptr, source = nullptr, input = nullptr, feedback = nullptr;
    HWND console = nullptr, compile = nullptr;
    std::unique_ptr<CodeEditor> answer_editor, source_editor, input_editor;
    std::vector<HWND> choices;
    state::Response response;
    std::vector<std::size_t> order;
    std::uint64_t revision = 0;
    std::string input_original, input_visual;
};
struct LessonView;
LRESULT CALLBACK view_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK pane_proc(HWND, UINT, WPARAM, LPARAM);

int control_id(std::size_t block, int part) { return 1000 + static_cast<int>(block) * 100 + part; }
bool exercise(BlockType type) {
    return type == BlockType::multiple_choice || type == BlockType::output_prediction
        || type == BlockType::fill_code || type == BlockType::order_code;
}

// Text controls fit their contents. When they have no scroll of their own,
// the ordinary mouse wheel belongs to the containing lesson.
LRESULT CALLBACK wheel_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                            UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL) {
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE};
        if ((GetWindowLongPtrW(window, GWL_STYLE) & ES_READONLY)
            || !GetScrollInfo(window, SB_VERT, &info)
            || info.nMax - info.nMin + 1 <= static_cast<int>(info.nPage)) {
            return SendMessageW(GetParent(window), message, wparam, lparam);
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, wheel_proc, 1);
    return DefSubclassProc(window, message, wparam, lparam);
}

std::string diagnostic_text(std::string_view bytes) {
    try { (void)wide(bytes); return std::string(bytes); }
    catch (...) {
        const int length = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring converted(static_cast<std::size_t>(std::max(0, length)), L'\0');
        if (length) MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), converted.data(), length);
        return utf8(converted);
    }
}
std::string control_text(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    const auto copied = GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(copied));
    return utf8(value);
}

struct LessonView {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    state::StateStore store;
    state::StateData data;
    HWND window = nullptr, pane = nullptr, title = nullptr, previous = nullptr;
    HWND selector = nullptr, next = nullptr, importer = nullptr;
    HWND import_prompt = nullptr, import_path = nullptr, import_browse = nullptr;
    HWND import_review = nullptr, import_summary = nullptr, import_commit = nullptr, import_cancel = nullptr;
    HWND import_label = nullptr;
    std::optional<state::AndroidImportPreview> import_preview;
    bool import_open = false, import_done = false;
    std::vector<Row> rows;
    std::vector<Action> actions;
    std::vector<BlockControls> controls;
    std::size_t page = 0;
    int scroll = 0, extent = 0;
    bool updating = false, laying_out = false, dirty = false, ready = false;
    HFONT button_font = nullptr;
    HMODULE rich_module = nullptr;
    std::jthread worker;
    std::mutex result_mutex;
    std::optional<windows::CppRunResult> completed;
    std::uint64_t request_id = 0;
    std::optional<std::size_t> running_block;
    std::string running_input;

    LessonView(const WindowsEnvironment& env, curriculum::Lesson model)
        : environment(env), lesson(std::move(model)),
          store(env.paths.root), data(store.load()) {
        curriculum::validate_reference_relationships(lesson);
        state::validate_lesson_state(data, lesson);
        state::prepare_lesson_state(data, lesson);
        page = state::current_page(data, lesson);
        if (store.empty()) {
            // Initial UI bookkeeping is not a student edit. Closing an untouched
            // first page must not make an otherwise empty profile unimportable.
            state::save_current_page(data, lesson, page);
            data.preferences["native_windows"][scroll_key()] = std::int64_t{0};
        }
        rich_module = LoadLibraryW(L"Msftedit.dll");
        if (!rich_module) throw std::runtime_error("Windows Rich Edit is unavailable");
    }
    ~LessonView() {
        cancel_run();
        controls.clear();
        if (button_font) DeleteObject(button_font);
        if (rich_module) FreeLibrary(rich_module);
    }
    int px(int value) const { return MulDiv(value, static_cast<int>(GetDpiForWindow(window)), 96); }
    const curriculum::Block& block(std::size_t index) const { return lesson.pages.at(page).blocks.at(index); }
    std::string key(std::size_t index) const { return curriculum::exercise_key(lesson, block(index)); }
    std::string draft_key(std::size_t index) const { return curriculum::draft_key(route, lesson, block(index)); }
    std::string scroll_key() const { return "scroll/" + lesson.id + "/" + lesson.pages.at(page).stable_id; }

    bool has_references(const curriculum::Block& activity) const {
        return std::ranges::any_of(lesson.reference_after,[&](const auto& relation) { return relation.activity == activity.stable_id; });
    }
    std::string reference_key(const curriculum::Block& activity) const {
        return "reference-proof/v1/" + lesson.locale + '/' + curriculum::draft_key(route,lesson,activity);
    }
    std::string reference_seal(const curriculum::Block& activity) const {
        const auto draft = state::get_draft(data,curriculum::draft_key(route,lesson,activity),
                                            activity.initial_code,activity.standard_input.value_or(""));
        const auto input = activity.standard_input ? draft.input : std::string{};
        auto objective = curriculum::reference_objective_frame(lesson,activity);
        objective += "/source/" + std::to_string(draft.source.size()) + ':' + draft.source;
        objective += "/input/" + std::to_string(input.size()) + ':' + input;
        return state::sha256(objective);
    }
    bool reference_available(const curriculum::Block& reference) const {
        const auto dependency = curriculum::reference_activity(lesson,reference);
        if (!dependency) return true;
        const auto& activity = lesson.pages[dependency->page].blocks[dependency->block];
        const auto receipt = state::string_value(data,"native_windows",reference_key(activity));
        return !receipt.empty() && receipt == reference_seal(activity);
    }
    static std::size_t row_block(const Row& row) {
        return static_cast<std::size_t>((GetDlgCtrlID(row.cells.front()) - 1000) / 100);
    }
    void reference_row(std::size_t index) {
        const auto& current = block(index);
        if (current.type == BlockType::code) text_row(current.code,control_id(index,1),true);
        else text_row(current.text,control_id(index,0),false,current.emphasis,current.type == BlockType::heading);
    }
    void refresh_references() {
        if (!pane || import_open) return;
        const auto was_updating = updating; updating = true;
        bool changed = false;
        for (std::size_t index = 0; index < controls.size(); ++index) {
            const auto& current = block(index);
            if (!curriculum::reference_activity(lesson,current)) continue;
            const bool present = std::ranges::any_of(rows,[&](const auto& row) { return row_block(row) == index; });
            const bool available = reference_available(current);
            if (present == available) continue;
            changed = true;
            if (available) reference_row(index);
            else {
                // Read the block identity while HWNDs are still valid, and
                // detach each row before destruction sends window messages.
                // A destroyed HWND no longer has a usable control ID.
                for (auto row = rows.begin(); row != rows.end();) {
                    if (row_block(*row) != index) { ++row; continue; }
                    auto cells = std::move(row->cells);
                    row = rows.erase(row);
                    for (const auto item : cells) DestroyWindow(item);
                }
            }
        }
        if (changed) {
            std::stable_sort(rows.begin(),rows.end(),[](const auto& left, const auto& right) { return row_block(left) < row_block(right); });
            update_fonts(); reflow();
        }
        updating = was_updating;
    }
    void clear_reference_completion(std::size_t index) {
        if (!has_references(block(index))) return;
        if (auto domain = data.preferences.find("native_windows"); domain != data.preferences.end())
            if (domain->second.erase(reference_key(block(index)))) mark_dirty();
        refresh_references();
    }

    void show_error(std::string_view message) {
        MessageBoxW(window, wide(message).c_str(), L"LearnGameAndEngineDev", MB_OK | MB_ICONERROR);
    }
    HWND control(const wchar_t* type, std::string_view text, DWORD style, int id, HWND parent = nullptr) {
        const auto value = wide(text);
        auto result = CreateWindowExW(0, type, value.c_str(), WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, parent ? parent : pane, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (!result) throw std::runtime_error("Cannot create a lesson control");
        return result;
    }
    HWND rich(std::string_view text, int id, bool code = false,
              std::span<const curriculum::Emphasis> emphasis = {}, bool editable = false) {
        auto result = control(MSFTEDIT_CLASS, "", ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP
            | (editable ? WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL : ES_READONLY), id);
        SendMessageW(result, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
        SendMessageW(result, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE | ENM_CHANGE | ENM_SELCHANGE);
        SendMessageW(result, EM_EXLIMITTEXT, 0, 2 * 1024 * 1024);
        set_rich_text(result, text, emphasis, code);
        if (editable) SendMessageW(result, EM_SETTARGETDEVICE, 0, 1);
        SetWindowSubclass(result, wheel_proc, 1, 0);
        return result;
    }
    void text_row(std::string_view text, int id, bool code = false,
                  std::span<const curriculum::Emphasis> emphasis = {}, bool heading = false) {
        const auto item = rich(text, id, code, emphasis);
        if (heading) {
            CHARFORMAT2W format{}; format.cbSize = sizeof(format);
            format.dwMask = CFM_SIZE | CFM_BOLD; format.yHeight = 16 * 20; format.dwEffects = CFE_BOLD;
            SendMessageW(item, EM_SETSEL, 0, -1);
            SendMessageW(item, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            SendMessageW(item, EM_SETSEL, 0, 0);
        }
        rows.push_back({{item}, {0}, 26, true});
    }
    HWND button(std::string_view text, int id, Action action) {
        auto result = control(L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, id);
        action.control = id; actions.push_back(action);
        return result;
    }
    void update_fonts() {
        auto font = CreateFontW(-MulDiv(11, static_cast<int>(GetDpiForWindow(window)), 72), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) return;
        for (auto item : {previous, selector, next, importer}) if (item) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (auto item : {import_label, import_path, import_browse, import_review, import_commit, import_cancel})
            if (item) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (const auto& row : rows) for (auto item : row.cells) {
            wchar_t name[32]{}; GetClassNameW(item, name, 32);
            if (std::wstring_view(name) != MSFTEDIT_CLASS) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        if (button_font) DeleteObject(button_font);
        button_font = font;
    }
    void initialize(HWND target) {
        window = target;
        title = control(MSFTEDIT_CLASS, "", ES_READONLY | ES_MULTILINE, 105, window);
        set_rich_text(title, lesson.title);
        previous = control(L"BUTTON", "Anterior", WS_TABSTOP | BS_PUSHBUTTON, previous_id, window);
        selector = control(L"COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, page_id, window);
        next = control(L"BUTTON", "Siguiente", WS_TABSTOP | BS_PUSHBUTTON, next_id, window);
        importer = control(L"BUTTON", "Importar estado Android…", WS_TABSTOP | BS_PUSHBUTTON, import_id, window);
        for (std::size_t index = 0; index < lesson.pages.size(); ++index) {
            auto label = L"Página " + std::to_wstring(index + 1) + L" de " + std::to_wstring(lesson.pages.size());
            SendMessageW(selector, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        pane = CreateWindowExW(WS_EX_CONTROLPARENT, pane_class, L"Contenido de la lección",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 0, 0,
            window, nullptr, GetModuleHandleW(nullptr), this);
        if (!pane) throw std::runtime_error("Cannot create the lesson viewport");
        render_page(); ready = true; resize();
    }
    void mark_dirty() { dirty = true; SetTimer(window, save_timer, 300, nullptr); }
    void remember_scroll() {
        data.preferences["native_windows"][scroll_key()] = static_cast<std::int64_t>(MulDiv(scroll, 96, static_cast<int>(GetDpiForWindow(window))));
    }
    void capture_draft(std::size_t index) {
        auto& vm = controls.at(index);
        if (!vm.source_editor) return;
        auto document = vm.source_editor->document();
        state::Draft value;
        value.source = document.text;
        const auto preserved = state::get_draft(data, draft_key(index), block(index).initial_code, block(index).standard_input.value_or(""));
        value.input = preserved.input;
        if (vm.input) {
            const auto visible = edit_text(vm.input);
            value.input = visible == vm.input_visual ? vm.input_original : visible;
        }
        value.indentation_json = state::make_indentation_metadata(value.source, document.indentation_starts);
        value.selection_start = document.selection_start; value.selection_end = document.selection_end;
        state::put_draft(data, draft_key(index), value);
    }
    void capture_page() {
        for (std::size_t i = 0; i < controls.size(); ++i) if (controls[i].source_editor) capture_draft(i);
        remember_scroll();
        state::save_current_page(data, lesson, page);
    }
    void flush() {
        KillTimer(window, save_timer);
        if (!dirty) return;
        capture_page(); store.save(data); dirty = false;
    }
    void flush_selection() {
        if (import_open) return;
        const auto before = data;
        capture_page();
        dirty = dirty || before != data;
        flush();
    }
    void cancel_run() {
        if (worker.joinable()) { worker.request_stop(); worker.join(); }
        if (running_block && *running_block < controls.size()) {
            const auto compile = controls[*running_block].compile;
            if (compile && IsWindow(compile)) EnableWindow(compile, TRUE);
        }
        running_block.reset();
        std::lock_guard guard(result_mutex); completed.reset();
    }
    void feedback(std::size_t index, std::string_view text, bool correct) {
        const auto previous_update = updating; updating = true;
        set_rich_text(controls.at(index).feedback, text);
        CHARFORMAT2W format{}; format.cbSize = sizeof(format); format.dwMask = CFM_COLOR;
        const auto bg = GetSysColor(COLOR_WINDOW);
        const bool dark = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) < 384;
        format.crTextColor = correct ? (dark ? RGB(122, 195, 141) : RGB(20, 112, 48))
                                    : (dark ? RGB(245, 150, 150) : RGB(160, 35, 35));
        const auto label_end = text.find('\n');
        SendMessageW(controls[index].feedback, EM_SETSEL, 0,
            static_cast<LPARAM>(wide(text.substr(0, label_end)).size()));
        SendMessageW(controls[index].feedback, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        SendMessageW(controls[index].feedback, EM_SETSEL, 0, 0);
        updating = previous_update; update_navigation(); reflow();
    }
    bool can_advance(std::size_t index) const {
        const auto& candidate = lesson.pages.at(index);
        for (const auto& item : candidate.blocks) {
            if (!exercise(item.type)) continue;
            const auto response = state::get_response(data, curriculum::exercise_key(lesson, item));
            if (!response || !response->correct) return false;
        }
        return true;
    }
    void update_navigation() {
        if (next) EnableWindow(next, page + 1 < lesson.pages.size() && can_advance(page));
    }
    void sync_quiz_controls(std::size_t index) {
        auto& vm = controls.at(index);
        if (!exercise(block(index).type)) return;
        const bool editable = !vm.response.correct;
        if (vm.answer) EnableWindow(vm.answer, editable);
        for (auto choice : vm.choices) EnableWindow(choice, editable);
        for (std::size_t position = 0; position < vm.order.size(); ++position) {
            EnableWindow(GetDlgItem(pane, control_id(index, 20 + static_cast<int>(position) * 3)), editable && position > 0);
            EnableWindow(GetDlgItem(pane, control_id(index, 21 + static_cast<int>(position) * 3)), editable && position + 1 < vm.order.size());
        }
        SetWindowTextW(GetDlgItem(pane, control_id(index, 3)), editable ? L"Comprobar" : L"Reiniciar respuesta");
    }
    void refresh_order(std::size_t index) {
        const auto previous_update = updating; updating = true;
        const auto& vm = controls.at(index);
        for (std::size_t position = 0; position < vm.order.size(); ++position)
            set_rich_text(GetDlgItem(pane, control_id(index, 22 + static_cast<int>(position) * 3)),
                block(index).fragments.at(vm.order[position]), {}, true);
        updating = previous_update; sync_quiz_controls(index); reflow();
    }
    void create_feedback(std::size_t index) {
        auto& vm = controls[index];
        vm.feedback = rich("", control_id(index, 4));
        rows.push_back({{vm.feedback}, {0}, 0, true});
        if (vm.response.correct) {
            auto explanation = block(index).explanation;
            feedback(index, "Correcto" + (explanation.empty() ? "" : "\n" + explanation), true);
        } else if (exercise(block(index).type) && state::response_reviewed(vm.response, block(index))) {
            const auto& current = block(index);
            // Restoring a reviewed attempt is not a new submission. Never
            // attach a success explanation to an imported incorrect attempt.
            feedback(index, "Incorrecto\n" + (current.review_feedback.empty()
                ? "Revisa tu respuesta e inténtalo de nuevo." : current.review_feedback), false);
        }
        sync_quiz_controls(index);
    }
    void render_page() {
        updating = true;
        controls.clear();
        for (const auto& row : rows) for (auto item : row.cells) DestroyWindow(item);
        rows.clear(); actions.clear();
        controls.resize(lesson.pages.at(page).blocks.size());
        SendMessageW(selector, CB_SETCURSEL, page, 0);
        EnableWindow(previous, page > 0); EnableWindow(next, page + 1 < lesson.pages.size());
        for (std::size_t index = 0; index < controls.size(); ++index) {
            const auto& current = block(index); auto& vm = controls[index];
            // Locked references have no HWND, text provider or scrollable row.
            if (!reference_available(current)) continue;
            if (exercise(current.type))
                if (const auto saved = state::get_response(data, key(index))) vm.response = *saved;
            if (current.type == BlockType::heading) { text_row(current.text, control_id(index, 0), false, current.emphasis, true); continue; }
            if (current.type == BlockType::paragraph || current.type == BlockType::note) { text_row(current.text, control_id(index, 0), false, current.emphasis); continue; }
            if (current.type == BlockType::code) { text_row(current.code, control_id(index, 1), true); continue; }
            if (!current.question.empty()) text_row(current.question, control_id(index, 0), false, current.emphasis);
            if (!current.code.empty()) text_row(current.code, control_id(index, 1), true);
            if (current.type == BlockType::cpp_compiler) {
                auto caption = rich("C++", control_id(index, 16));
                auto reset = button("Reiniciar código", control_id(index, 12), {0, ActionType::reset, index});
                auto tab = button("TAB", control_id(index, 13), {0, ActionType::tab, index});
                rows.push_back({{caption, reset, tab}, {0, 150, 64}, 36, false});
                const auto saved = state::get_draft(data, draft_key(index), current.initial_code, current.standard_input.value_or(""));
                auto document = saved.has_saved
                    ? windows::EditorDocument{saved.source, state::indentation_units(saved.source, saved.indentation_json), saved.selection_start, saved.selection_end}
                    : windows::predefined_code_document(current.initial_code);
                vm.source = rich("", control_id(index, 10), true, {}, true);
                vm.source_editor = std::make_unique<CodeEditor>(vm.source, std::move(document), true);
                style_cpp(vm.source);
                const int lines = 1 + static_cast<int>(std::count(saved.source.begin(), saved.source.end(), '\n'));
                rows.push_back({{vm.source}, {0}, std::clamp(lines * 19 + 16, 128, 320), false});
                if (current.standard_input) {
                    text_row("Entrada", control_id(index, 17));
                    vm.input = rich("", control_id(index, 15), false, {}, true);
                    set_edit_text(vm.input, saved.input);
                    vm.input_original = saved.input; vm.input_visual = edit_text(vm.input);
                    rows.push_back({{vm.input}, {0}, 74, false});
                }
                vm.compile = button(current.output_check ? "Compilar, ejecutar y comprobar" : "Compilar y ejecutar", control_id(index, 11), {0, ActionType::run, index});
                rows.push_back({{vm.compile}, {280}, 36, false});
                vm.console = rich("", control_id(index, 14), true);
                rows.push_back({{vm.console}, {0}, 0, true});
                create_feedback(index); continue;
            }
            if (current.type == BlockType::multiple_choice) {
                for (std::size_t option = 0; option < current.options.size(); ++option) {
                    const auto radio = control(L"BUTTON", current.options[option], BS_RADIOBUTTON | WS_TABSTOP,
                        control_id(index, 20 + static_cast<int>(option) * 2));
                    actions.push_back({control_id(index, 20 + static_cast<int>(option) * 2), ActionType::choose, index, option});
                    SendMessageW(radio, BM_SETCHECK, vm.response.selected == static_cast<std::int64_t>(option) ? BST_CHECKED : BST_UNCHECKED, 0);
                    vm.choices.push_back(radio);
                    auto label = rich(current.options[option], control_id(index, 21 + static_cast<int>(option) * 2));
                    rows.push_back({{radio, label}, {28, 0}, 28, true});
                }
            } else if (current.type == BlockType::order_code) {
                vm.order.resize(current.fragments.size()); std::iota(vm.order.begin(), vm.order.end(), 0);
                if (vm.response.order.size() == vm.order.size()) {
                    auto candidate = vm.response.order; std::ranges::sort(candidate);
                    bool valid = true;
                    for (std::size_t i = 0; i < candidate.size(); ++i) valid = valid && candidate[i] == static_cast<std::int64_t>(i);
                    if (valid) for (std::size_t i = 0; i < vm.order.size(); ++i) vm.order[i] = static_cast<std::size_t>(vm.response.order[i]);
                }
                for (std::size_t position = 0; position < vm.order.size(); ++position) {
                    auto up = button("↑", control_id(index, 20 + static_cast<int>(position) * 3), {0, ActionType::move_up, index, position});
                    auto down = button("↓", control_id(index, 21 + static_cast<int>(position) * 3), {0, ActionType::move_down, index, position});
                    EnableWindow(up, position > 0); EnableWindow(down, position + 1 < vm.order.size());
                    auto fragment = rich(current.fragments.at(vm.order[position]), control_id(index, 22 + static_cast<int>(position) * 3), true);
                    rows.push_back({{up, down, fragment}, {40, 40, 0}, 32, true});
                }
            } else {
                vm.answer = rich("", control_id(index, 2), false, {}, true);
                const bool multiline = !curriculum::uses_single_line_answer(current);
                vm.answer_editor = std::make_unique<CodeEditor>(vm.answer, windows::EditorDocument{vm.response.answer}, multiline,
                    [this, index] { check_answer(index); }, current.type == BlockType::fill_code);
                rows.push_back({{vm.answer}, {0}, multiline ? 82 : 36, false});
            }
            auto check = button("Comprobar", control_id(index, 3), {0, ActionType::check, index});
            rows.push_back({{check}, {150}, 34, false}); create_feedback(index);
        }
        scroll = px(static_cast<int>(state::integer_value(data, "native_windows", scroll_key())));
        updating = false; update_fonts(); update_navigation(); reflow();
    }
    void resize() {
        if (!ready) return;
        RECT area{}; GetClientRect(window, &area);
        const int margin = px(18), available = static_cast<int>(area.right) - margin * 2;
        MoveWindow(title, margin, px(10), available, px(34), TRUE);
        MoveWindow(previous, margin, px(53), px(94), px(34), TRUE);
        MoveWindow(selector, margin + px(104), px(53), px(144), px(250), TRUE);
        MoveWindow(next, margin + px(258), px(53), px(94), px(34), TRUE);
        MoveWindow(importer, std::max(margin + px(362), static_cast<int>(area.right) - margin - px(235)), px(53), px(235), px(34), TRUE);
        MoveWindow(pane, 0, px(100), static_cast<int>(area.right), std::max(px(60), static_cast<int>(area.bottom) - px(100)), TRUE);
        if (import_open) layout_import();
        reflow();
    }
    void reflow() {
        if (!pane || laying_out) return;
        laying_out = true;
        RECT area{}; GetClientRect(pane, &area);
        const int margin = px(20), gap = px(8);
        const int width = std::max(px(100), std::min(px(1050), static_cast<int>(area.right) - margin * 2));
        const int left = std::max(margin, (static_cast<int>(area.right) - width) / 2);
        int top = px(12);
        for (auto& row : rows) {
            int fixed = 0; for (auto value : row.widths) if (value) fixed += px(value);
            const int remaining = std::max(px(30), width - fixed - gap * (static_cast<int>(row.cells.size()) - 1));
            int x = left;
            const auto provisional = std::max(px(row.logical_height), row.requested_height);
            for (std::size_t i = 0; i < row.cells.size(); ++i) {
                const int cell_width = row.widths[i] ? px(row.widths[i]) : remaining;
                MoveWindow(row.cells[i], x, top - scroll, cell_width, std::max(px(1), provisional), TRUE);
                if (row.size_to_text) SendMessageW(row.cells[i], EM_REQUESTRESIZE, 0, 0);
                x += cell_width + gap;
            }
            const int height = row.size_to_text ? std::max(px(row.logical_height), row.requested_height) : px(row.logical_height);
            x = left;
            for (std::size_t i = 0; i < row.cells.size(); ++i) {
                const int cell_width = row.widths[i] ? px(row.widths[i]) : remaining;
                MoveWindow(row.cells[i], x, top - scroll, cell_width, std::max(0, height), TRUE);
                x += cell_width + gap;
            }
            top += height + (height ? px(12) : 0);
        }
        extent = top + px(16);
        const int old_scroll = scroll;
        scroll = std::clamp(scroll, 0, std::max(0, extent - static_cast<int>(area.bottom)));
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0; info.nMax = std::max(0, extent - 1); info.nPage = static_cast<UINT>(std::max<LONG>(0, area.bottom)); info.nPos = scroll;
        SetScrollInfo(pane, SB_VERT, &info, TRUE);
        laying_out = false;
        if (old_scroll != scroll) reflow();
    }
    void scroll_to(int position) {
        RECT area{}; GetClientRect(pane, &area);
        const int new_scroll = std::clamp(position, 0, std::max(0, extent - static_cast<int>(area.bottom)));
        if (new_scroll == scroll) return;
        scroll = new_scroll; remember_scroll(); mark_dirty(); reflow();
    }
    void change_page(std::size_t value) {
        if (value == page || value >= lesson.pages.size()) return;
        for (std::size_t required = page; required < value; ++required) if (!can_advance(required)) {
            SendMessageW(selector, CB_SETCURSEL, page, 0);
            MessageBoxW(window, L"Completa los ejercicios pendientes antes de continuar.", L"Continuar", MB_OK | MB_ICONINFORMATION);
            return;
        }
        capture_page(); dirty = true; flush(); cancel_run();
        page = value; scroll = 0;
        state::save_current_page(data, lesson, page); mark_dirty(); render_page();
    }
    void check_answer(std::size_t index) {
        auto& vm = controls.at(index); const auto& current = block(index);
        if (vm.response.correct) {
            // Explicit reset affects only this quiz; compiler drafts and jobs stay intact.
            updating = true; vm.response = {};
            if (vm.answer_editor) vm.answer_editor->reset(windows::EditorDocument{});
            for (auto choice : vm.choices) SendMessageW(choice, BM_SETCHECK, BST_UNCHECKED, 0);
            if (!vm.order.empty()) std::iota(vm.order.begin(), vm.order.end(), 0);
            updating = false;
            if (!vm.order.empty()) refresh_order(index);
            state::put_response(data, key(index), vm.response); mark_dirty(); flush();
            feedback(index, "", false); sync_quiz_controls(index); return;
        }
        curriculum::AnswerEvaluation result;
        if (current.type == BlockType::multiple_choice) {
            if (vm.response.selected < 0) { feedback(index, "Selecciona una opción antes de comprobar.", false); return; }
            result = curriculum::evaluate_choice(current, static_cast<std::size_t>(vm.response.selected));
        } else if (current.type == BlockType::order_code) {
            result = curriculum::evaluate_order(current, vm.order);
            vm.response.order.assign(vm.order.begin(), vm.order.end());
        } else {
            vm.response.answer = vm.answer_editor->document().text;
            result = curriculum::evaluate_text_answer(current, vm.response.answer);
        }
        vm.response.correct = result.correct;
        state::mark_response_reviewed(vm.response, current);
        state::put_response(data, key(index), vm.response); mark_dirty(); flush();
        auto text = result.correct ? std::string("Correcto") : std::string("Incorrecto");
        if (!result.feedback.empty()) text += "\n" + result.feedback;
        else if (!result.correct) text += "\nRevisa tu respuesta e inténtalo de nuevo.";
        feedback(index, text, result.correct);
        sync_quiz_controls(index);
        // Enter checks this activity only. Navigation always requires its own action.
    }
    void edit_changed(HWND sender) {
        if (updating) return;
        for (std::size_t index = 0; index < controls.size(); ++index) {
            auto& vm = controls[index];
            if (sender == vm.answer && vm.answer_editor) {
                const auto before = vm.response.answer;
                vm.answer_editor->capture_change(); vm.response.answer = vm.answer_editor->document().text;
                if (before == vm.response.answer) return;
                vm.response.correct = false; state::clear_response_review(vm.response);
                state::put_response(data, key(index), vm.response);
                feedback(index, "", false); mark_dirty(); return;
            }
            if (sender == vm.source || sender == vm.input) {
                const auto before = state::get_draft(data, draft_key(index), block(index).initial_code,
                    block(index).standard_input.value_or(""));
                if (sender == vm.source) vm.source_editor->capture_change();
                auto input = before.input;
                if (vm.input) {
                    const auto visible = edit_text(vm.input);
                    input = visible == vm.input_visual ? vm.input_original : visible;
                }
                if (before.source == vm.source_editor->document().text && before.input == input) return;
                ++vm.revision;
                clear_reference_completion(index);
                if (running_block == index) { worker.request_stop(); }
                capture_draft(index); feedback(index, "", false);
                updating = true; set_rich_text(vm.console, "El programa cambió. Ejecuta de nuevo para obtener una salida actual.");
                if (sender == vm.source) style_cpp(vm.source);
                updating = false; mark_dirty(); return;
            }
        }
    }
    void run(std::size_t index) {
        cancel_run(); capture_draft(index); clear_reference_completion(index); mark_dirty(); flush();
        auto& vm = controls.at(index);
        windows::CppRunRequest request;
        request.request_id = ++request_id; request.revision = vm.revision;
        request.source_utf8 = vm.source_editor->document().text;
        request.stdin_utf8 = block(index).standard_input.value_or("");
        if (vm.input) {
            auto visible = edit_text(vm.input);
            request.stdin_utf8 = visible == vm.input_visual ? vm.input_original : visible;
        }
        request.projects_directory = environment.paths.projects;
        running_block = index; running_input = request.stdin_utf8;
        EnableWindow(vm.compile, FALSE); feedback(index, "", false);
        updating = true; set_rich_text(vm.console, "Compilando…"); updating = false;
        worker = std::jthread([this, request = std::move(request)](std::stop_token stop) {
            auto result = windows::run_cpp(request, stop);
            { std::lock_guard guard(result_mutex); completed = std::move(result); }
            PostMessageW(window, result_message, 0, 0);
        });
    }
    void finish_run() {
        std::optional<windows::CppRunResult> result;
        { std::lock_guard guard(result_mutex); result = std::move(completed); completed.reset(); }
        if (!result || !running_block || result->request_id != request_id) return;
        auto index = *running_block; running_block.reset();
        if (worker.joinable()) worker.join();
        auto& vm = controls.at(index); EnableWindow(vm.compile, TRUE);
        if (result->revision != vm.revision) return;
        std::string output;
        if (!result->compilation.succeeded()) {
            output = "Diagnósticos de compilación\n" + diagnostic_text(result->compilation.stdout_bytes + result->compilation.stderr_bytes);
            if (!result->error_utf8.empty()) output += "\n" + result->error_utf8;
            if (result->compilation.status == windows::CppPhaseStatus::timed_out) output += "\nLa compilación superó el tiempo permitido.";
        } else if (!result->execution.succeeded()) {
            output = "La ejecución no terminó correctamente.\n" + diagnostic_text(result->execution.stdout_bytes);
            output += diagnostic_text(result->execution.stderr_bytes);
            if (!result->error_utf8.empty()) output += "\n" + result->error_utf8;
            if (result->execution.status == windows::CppPhaseStatus::timed_out) output += "\nSe detuvo por el límite de tiempo.";
            if (result->execution.status == windows::CppPhaseStatus::output_limit) output += "\nSe detuvo por el límite de salida.";
        } else {
            output = diagnostic_text(result->execution.stdout_bytes);
            if (!result->execution.stderr_bytes.empty()) output += "\nErrores del programa\n" + diagnostic_text(result->execution.stderr_bytes);
        }
        updating = true; set_rich_text(vm.console, output, {}, true); updating = false;
        if (block(index).output_check) {
            curriculum::FreshExecution fresh;
            fresh.source_revision = result->revision;
            fresh.compilation_succeeded = result->compilation.succeeded(); fresh.execution_succeeded = result->execution.succeeded();
            fresh.exit_code = result->execution.exit_code ? static_cast<int>(*result->execution.exit_code) : -1;
            fresh.standard_input = running_input; fresh.standard_output = result->execution.stdout_bytes;
            auto evaluation = curriculum::evaluate_output_check(*block(index).output_check, fresh, vm.revision);
            if (evaluation.verdict != curriculum::OutputVerdict::unavailable) {
                const bool correct = evaluation.verdict == curriculum::OutputVerdict::correct;
                auto text = correct ? std::string("Correcto") : std::string("Incorrecto");
                if (!evaluation.feedback.empty()) text += "\n" + evaluation.feedback;
                feedback(index, text, correct);
                if (correct && has_references(block(index))) {
                    data.preferences["native_windows"][reference_key(block(index))] = reference_seal(block(index));
                    mark_dirty(); flush(); refresh_references();
                }
            }
        }
        reflow();
    }
    HWND import_text(int id, std::string_view text, bool scrollable = true) {
        auto item = control(MSFTEDIT_CLASS, "", ES_MULTILINE | ES_READONLY | (scrollable ? WS_VSCROLL : 0), id, window);
        SendMessageW(item, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
        set_rich_text(item, text);
        return item;
    }
    void layout_import() {
        RECT area{}; GetClientRect(window, &area);
        const int margin = px(20), width = std::max(px(100), static_cast<int>(area.right) - margin * 2);
        MoveWindow(import_prompt, margin, px(110), width, px(48), TRUE);
        MoveWindow(import_label, margin, px(160), width, px(20), TRUE);
        MoveWindow(import_path, margin, px(182), std::max(px(30), width - px(128)), px(34), TRUE);
        MoveWindow(import_browse, margin + width - px(118), px(182), px(118), px(34), TRUE);
        MoveWindow(import_review, margin, px(224), px(150), px(30), TRUE);
        const int buttons_top = std::max(px(310), static_cast<int>(area.bottom) - px(52));
        MoveWindow(import_summary, margin, px(258), width, std::max(px(40), buttons_top - px(270)), TRUE);
        MoveWindow(import_commit, margin, buttons_top, px(230), px(34), TRUE);
        MoveWindow(import_cancel, margin + px(240), buttons_top, px(180), px(34), TRUE);
    }
    void invalidate_import() {
        import_preview.reset(); EnableWindow(import_commit, FALSE);
        set_rich_text(import_summary, "Pulsa Revisar para verificar la carpeta antes de importar. Todavía no se ha cambiado ningún dato.");
    }
    void import_state() {
        flush_selection();
        if (!store.empty()) { show_error("Este perfil ya contiene estado. La importación requiere un perfil Windows vacío para no reemplazar tus datos."); return; }
        import_done = false;
        import_prompt = import_text(import_prompt_id, "Importar estado Android\nSelecciona o escribe la carpeta exportada que contiene shared_prefs y, si corresponde, files.", false);
        import_label = control(L"STATIC", "Carpeta exportada:", SS_LEFT, import_label_id, window);
        import_path = control(L"EDIT", "", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, import_path_id, window);
        import_browse = control(L"BUTTON", "Examinar…", BS_PUSHBUTTON | WS_TABSTOP, import_browse_id, window);
        import_review = control(L"BUTTON", "Revisar", BS_PUSHBUTTON | WS_TABSTOP, import_review_id, window);
        import_summary = import_text(import_summary_id, "");
        import_commit = control(L"BUTTON", "Confirmar importación", BS_PUSHBUTTON | WS_TABSTOP, import_commit_id, window);
        import_cancel = control(L"BUTTON", "Cancelar", BS_PUSHBUTTON | WS_TABSTOP, import_cancel_id, window);
        import_open = true;
        invalidate_import();
        ShowWindow(pane, SW_HIDE);
        for (auto item : {previous, selector, next, importer}) EnableWindow(item, FALSE);
        update_fonts(); layout_import(); SetFocus(import_path);
    }
    void browse_import() {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Cannot open the Windows folder selector");
        DWORD options{}; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Selecciona una carpeta exportada de Android (shared_prefs y files)");
        const auto shown = dialog->Show(window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        if (FAILED(shown)) throw std::runtime_error("No se pudo abrir el selector de carpetas.");
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("No export folder was selected");
        PWSTR raw_path = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path))) throw std::runtime_error("Cannot resolve the export path");
        std::filesystem::path path(raw_path); CoTaskMemFree(raw_path);
        SetWindowTextW(import_path, path.c_str());
    }
    void review_import() {
        import_preview.reset(); EnableWindow(import_commit, FALSE);
        try {
            const auto text = control_text(import_path);
            if (text.empty()) throw std::runtime_error("Selecciona o escribe la carpeta exportada.");
            auto preview = state::inspect_android_export(std::filesystem::path(wide(text)));
            // Validate against the original lesson before publishing active state.
            // The preview itself stays byte-faithful to the exported preferences.
            state::validate_lesson_state(preview.data, lesson);
            auto summary = "Se verificaron " + std::to_string(preview.preference_count) + " preferencias, "
                + std::to_string(preview.files.size()) + " archivos y " + std::to_string(preview.project_revisions)
                + " revisiones de proyectos.\n\nEl origen se conservará intacto. La importación se hará en este perfil vacío."
                  "\n\nLos resultados antiguos de compilación necesitarán una nueva ejecución.";
            for (const auto& note : preview.notes) summary += "\n\n" + note;
            set_rich_text(import_summary, summary);
            import_preview = std::move(preview); EnableWindow(import_commit, TRUE);
        } catch (const std::exception& error) {
            set_rich_text(import_summary, std::string("No se puede importar:\n") + error.what());
        }
    }
    void confirm_import() {
        if (!import_preview || import_done) return;
        try {
            state::validate_lesson_state(import_preview->data, lesson);
            state::commit_android_import(*import_preview, environment.paths.root);
            data = store.load(); state::prepare_lesson_state(data, lesson);
            page = state::current_page(data, lesson); dirty = false; import_done = true;
            import_preview.reset();
            SendMessageW(import_path, EM_SETREADONLY, TRUE, 0);
            for (auto item : {import_browse, import_review, import_commit}) EnableWindow(item, FALSE);
            SetWindowTextW(import_cancel, L"Volver a la lección");
            set_rich_text(import_summary, "Importación completada y verificada.\n\nEl origen se conserva intacto. Vuelve a la lección para continuar desde el estado importado.");
            SetFocus(import_cancel);
        } catch (const std::exception& error) {
            import_preview.reset(); EnableWindow(import_commit, FALSE);
            set_rich_text(import_summary, std::string("No se ha completado la importación:\n") + error.what()
                + "\n\nRevisa la carpeta de nuevo antes de reintentar.");
        }
    }
    void close_import() {
        import_open = false; import_preview.reset();
        for (auto* item : {&import_prompt, &import_label, &import_path, &import_browse, &import_review, &import_summary, &import_commit, &import_cancel}) {
            DestroyWindow(*item); *item = nullptr;
        }
        EnableWindow(selector, TRUE); EnableWindow(importer, TRUE);
        if (import_done) { scroll = 0; render_page(); }
        else { EnableWindow(previous, page > 0); update_navigation(); }
        ShowWindow(pane, SW_SHOW); SetFocus(selector);
    }
    void command(int id, int notification, HWND sender) {
        if (import_open) {
            if (id == import_path_id && notification == EN_CHANGE && !import_done) invalidate_import();
            if (notification != BN_CLICKED) return;
            try {
                if (id == import_browse_id && !import_done) browse_import();
                else if (id == import_review_id && !import_done) review_import();
                else if (id == import_commit_id) confirm_import();
                else if (id == import_cancel_id) {
                    // BN_CLICKED is synchronous inside the button's BM_CLICK.
                    // Keep its HWND alive until that native callback returns.
                    if (!PostMessageW(window, close_import_message, 0, 0))
                        throw std::runtime_error("No se pudo cerrar el panel de importacion; vuelve a intentarlo.");
                }
            } catch (const std::exception& error) {
                set_rich_text(import_summary, error.what());
            }
            return;
        }
        if (notification == EN_CHANGE) { edit_changed(sender); return; }
        if (notification == EN_KILLFOCUS) {
            for (std::size_t i = 0; i < controls.size(); ++i) if (sender == controls[i].source && !updating) { capture_draft(i); mark_dirty(); }
        }
        if (id == page_id && notification == CBN_SELCHANGE) { change_page(static_cast<std::size_t>(SendMessageW(selector, CB_GETCURSEL, 0, 0))); return; }
        if (notification != BN_CLICKED) return;
        if (id == previous_id) { if (page) change_page(page - 1); return; }
        if (id == next_id) { change_page(page + 1); return; }
        if (id == import_id) { import_state(); return; }
        auto found = std::ranges::find_if(actions, [id](const auto& action) { return action.control == id; });
        if (found == actions.end()) return;
        const auto action = *found; auto& vm = controls.at(action.block);
        switch (action.type) {
        case ActionType::check: check_answer(action.block); break;
        case ActionType::choose:
            if (vm.response.correct) break;
            vm.response.selected = static_cast<std::int64_t>(action.option); vm.response.correct = false;
            state::clear_response_review(vm.response);
            for (std::size_t i = 0; i < vm.choices.size(); ++i) SendMessageW(vm.choices[i], BM_SETCHECK, i == action.option ? BST_CHECKED : BST_UNCHECKED, 0);
            state::put_response(data, key(action.block), vm.response); feedback(action.block, "", false); mark_dirty(); break;
        case ActionType::move_up: case ActionType::move_down: {
            if (vm.response.correct) break;
            const auto destination = action.type == ActionType::move_up ? action.option - 1 : action.option + 1;
            if (destination >= vm.order.size()) break;
            std::swap(vm.order[action.option], vm.order[destination]);
            vm.response.order.assign(vm.order.begin(), vm.order.end()); vm.response.correct = false;
            state::clear_response_review(vm.response);
            state::put_response(data, key(action.block), vm.response); mark_dirty();
            feedback(action.block, "", false); refresh_order(action.block); break;
        }
        case ActionType::run: run(action.block); break;
        case ActionType::reset:
            if (running_block == action.block) cancel_run();
            updating = true;
            vm.source_editor->reset(windows::predefined_code_document(block(action.block).initial_code));
            updating = false;
            ++vm.revision; capture_draft(action.block); feedback(action.block, "", false);
            clear_reference_completion(action.block);
            updating = true; set_rich_text(vm.console, ""); style_cpp(vm.source); updating = false;
            EnableWindow(vm.compile, TRUE); mark_dirty(); break;
        case ActionType::tab: SetFocus(vm.source); vm.source_editor->insert_tab(); break;
        }
    }
};

LRESULT CALLBACK pane_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* view = reinterpret_cast<LessonView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        view = static_cast<LessonView*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }
    if (view && (message == WM_COMMAND || message == WM_NOTIFY || message == WM_VSCROLL || message == WM_MOUSEWHEEL))
        return SendMessageW(view->window, message, wparam, lparam);
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK view_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* view = reinterpret_cast<LessonView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        view = static_cast<LessonView*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }
    if (!view) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case WM_CREATE: view->initialize(window); return 0;
        case WM_SIZE: view->resize(); return 0;
        case WM_DPICHANGED_AFTERPARENT: view->update_fonts(); view->resize(); return 0;
        case WM_COMMAND: view->command(LOWORD(wparam), HIWORD(wparam), reinterpret_cast<HWND>(lparam)); return 0;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header->code == EN_REQUESTRESIZE) {
                const auto* request = reinterpret_cast<REQRESIZE*>(lparam);
                for (auto& row : view->rows) if (std::ranges::find(row.cells, header->hwndFrom) != row.cells.end()) {
                    row.requested_height = std::max(view->px(2), static_cast<int>(request->rc.bottom - request->rc.top) + view->px(4));
                    break;
                }
            }
            return 0;
        }
        case WM_VSCROLL: {
            if (view->import_open) return 0;
            SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(view->pane, SB_VERT, &info);
            int value = view->scroll;
            switch (LOWORD(wparam)) {
            case SB_LINEUP: value -= view->px(35); break;
            case SB_LINEDOWN: value += view->px(35); break;
            case SB_PAGEUP: value -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: value += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: value = info.nTrackPos; break;
            case SB_TOP: value = 0; break;
            case SB_BOTTOM: value = info.nMax; break;
            }
            view->scroll_to(value); return 0;
        }
        case WM_MOUSEWHEEL:
            if (!view->import_open) view->scroll_to(view->scroll - MulDiv(GET_WHEEL_DELTA_WPARAM(wparam), view->px(100), WHEEL_DELTA));
            return 0;
        case WM_TIMER: if (wparam == save_timer) { view->flush(); return 0; } break;
        case result_message: view->finish_run(); return 0;
        case close_import_message: if (view->import_open) view->close_import(); return 0;
        case flush_message: view->flush_selection(); return 1;
        case WM_DESTROY: view->cancel_run(); KillTimer(window, save_timer); return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0); delete view; return DefWindowProcW(window, message, wparam, lparam);
        }
    } catch (const std::exception& error) {
        if (message == WM_CREATE) { OutputDebugStringW(wide(error.what()).c_str()); return -1; }
        view->show_error(error.what());
        if (message == flush_message) return 0;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment,
                        const std::filesystem::path& content_root) {
    return create_lesson_view(parent,environment,windows::load_lesson(content_root /
        L"es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"));
}

HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment, curriculum::Lesson lesson) {
    static const bool registered = [] {
        for (const auto& entry : std::array<std::pair<const wchar_t*, WNDPROC>, 2>{{{view_class, view_proc}, {pane_class, pane_proc}}}) {
            WNDCLASSEXW definition{}; definition.cbSize = sizeof(definition);
            definition.hInstance = GetModuleHandleW(nullptr); definition.lpfnWndProc = entry.second;
            definition.hCursor = LoadCursorW(nullptr, IDC_ARROW); definition.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            definition.lpszClassName = entry.first;
            if (!RegisterClassExW(&definition)) throw std::runtime_error("Cannot register the native lesson window");
        }
        return true;
    }();
    (void)registered;
    auto view = std::make_unique<LessonView>(environment,std::move(lesson));
    auto window = CreateWindowExW(WS_EX_CONTROLPARENT, view_class, L"Lección", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), view.get());
    if (!window) { view.release(); throw std::runtime_error("Cannot open the lesson; see its content contract or saved-state diagnostic"); }
    view.release(); return window;
}

bool flush_lesson_view(HWND window) {
    return !window || SendMessageW(window, flush_message, 0, 0) != 0;
}

} // namespace lgaed::presentation
