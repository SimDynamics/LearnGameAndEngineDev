#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/editing_bar.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "core/learning_flow.hpp"
#include "core/catalog.hpp"
#include "core/view_preferences.hpp"
#include "core/attempts.hpp"
#include "core/attempt_ledger.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <initguid.h>
#include <oleacc.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
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
constexpr UINT header_action_message = WM_APP + 75;
constexpr UINT math_result_message = WM_APP + 76;
constexpr UINT finish_lesson_message = WM_APP + 77;
constexpr UINT math_restore_message = WM_APP + 78;
constexpr UINT_PTR save_timer = 73;
constexpr UINT_PTR attempts_timer = 94;
constexpr int import_id = 104;
constexpr int page_status_id = 106, menu_id = 107, page_top_id = 108, page_bottom_id = 109;
constexpr int back_id = 110, reader_id = 111, language_id = 112, attempts_id = 113, settings_id = 114;
constexpr int finish_id=115;
constexpr int fullscreen_close_id=116;
constexpr int continue_id=117;
constexpr UINT attempts_state_message=WM_APP+187;
struct AttemptsState { int available=8, maximum=8; double progress=1; bool premium=false; };
constexpr int pages_menu_base = 40000;
constexpr int lesson_heading_id = 990;
constexpr int lesson_header_height = 80;
constexpr int import_prompt_id = 200, import_path_id = 201, import_browse_id = 202;
constexpr int import_review_id = 203, import_summary_id = 204, import_commit_id = 205, import_cancel_id = 206;
constexpr int import_label_id = 207;

enum class ActionType { check, choose, move_up, move_down, run, reset, tab, editor_menu, console_menu,
    copy_source, copy_console, clear_console, undo, redo, fullscreen_source, fullscreen_console,
    select_stdout, select_stderr, select_diagnostics, copy_simulated, reset_answer, simulated_menu,
    toggle_wrap, compiler_info };
struct Action { int control; ActionType type; std::size_t block; std::size_t option = 0; };
struct PreferenceUpdateRequest {
    const std::function<void(state::StateData&)>* update;
    std::exception_ptr error;
};
struct Row {
    std::vector<HWND> cells;
    // Positive widths are logical pixels; zero takes the remaining width.
    std::vector<int> widths;
    int logical_height = 30;
    bool size_to_text = false;
    int requested_height = 0;
    int max_logical_height = 0;
    std::optional<std::size_t> simulated_card;
    bool card_header = false, card_end = false;
    int padding_before=0,padding_after=12;
    bool toolbar=false;
    int horizontal_inset=0;
};
struct BlockControls {
    HWND answer = nullptr, source = nullptr, input = nullptr, feedback = nullptr;
    HWND console = nullptr, compile = nullptr, status = nullptr;
    HWND simulated_code = nullptr;
    std::unique_ptr<CodeEditor> answer_editor, source_editor, input_editor;
    std::unique_ptr<CppFillView> fill;
    std::unique_ptr<MathPuzzleView> math;
    std::vector<HWND> choices;
    state::Response response;
    std::vector<std::size_t> order;
    std::uint64_t revision = 0;
    std::string input_original, input_visual;
    std::string feedback_source;
    bool feedback_correct = false;
    bool feedback_generated_verdict = false, feedback_default_review = false;
    std::array<std::string,3> console_channels;
    int console_channel = 0;
    bool rendered = false, locked = false;
};
struct LessonView;
LRESULT CALLBACK view_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK pane_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK gesture_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

int control_id(std::size_t block, int part) { return 1000 + static_cast<int>(block) * 100 + part; }
bool exercise(BlockType type) {
    return type == BlockType::multiple_choice || type == BlockType::output_prediction
        || type == BlockType::fill_code || type == BlockType::order_code
        || type == BlockType::inline_fill_code || type == BlockType::drag_fill_code || type == BlockType::math_puzzle;
}

std::string math_answer_identity(const winrt::Windows::Data::Json::IJsonValue& value) {
    using namespace winrt::Windows::Data::Json;
    using Identity=lgaed::attempts::ExerciseAnswerIdentity;
    if(value.ValueType()==JsonValueType::Object){
        std::map<std::string,std::string> members;
        for(const auto& member:value.GetObject())members.emplace(winrt::to_string(member.Key()),math_answer_identity(member.Value()));
        return Identity::object(members);
    }
    if(value.ValueType()==JsonValueType::Array){
        std::vector<std::string> elements;
        for(const auto& element:value.GetArray())elements.push_back(math_answer_identity(element));
        return Identity::array(elements);
    }
    return Identity::of_canonical(winrt::to_string(value.Stringify()));
}

bool scrolls_internally(HWND window,int axis) {
    const auto style = GetWindowLongPtrW(window,GWL_STYLE);
    const auto scroll_style = axis == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
    if (!(style & scroll_style)) return false;
    SCROLLINFO info{sizeof(info),SIF_RANGE | SIF_PAGE};
    // RichEdit can report the default range 0..100 with page=0 even for
    // autosized prose without a scrollbar. That is not usable overflow.
    return GetScrollInfo(window,axis,&info) && info.nPage > 0
        && info.nMax - info.nMin + 1 > static_cast<int>(info.nPage);
}
// Autosized lesson text hands the wheel to its viewport; overflowing editors
// and capped consoles retain their own real scrolling.
LRESULT CALLBACK wheel_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                            UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL && !scrolls_internally(window,SB_VERT))
        return SendMessageW(GetParent(window), message, wparam, lparam);
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
    std::string route = "programming";
    state::StateStore store;
    state::StateData data;
    HWND window = nullptr, pane = nullptr, title = nullptr;
    HWND menu = nullptr, page_status = nullptr;
    HWND back = nullptr, reader = nullptr, language = nullptr, attempts = nullptr, settings = nullptr, reader_bar = nullptr;
    HWND fullscreen_close = nullptr;
    AttemptsState attempt_state;
    lgaed::attempts::ExerciseAttemptLedger attempt_ledger;
    int reader_height = 0;
    LessonViewHooks hooks;
    bool notifying = false, destroy_notified = false;
    HWND import_prompt = nullptr, import_path = nullptr, import_browse = nullptr;
    HWND import_review = nullptr, import_summary = nullptr, import_commit = nullptr, import_cancel = nullptr;
    HWND import_label = nullptr;
    std::optional<state::AndroidImportPreview> import_preview;
    bool import_open = false, import_done = false;
    std::vector<Row> rows;
    struct SimulatedCardSurface { RECT bounds; int header_bottom; };
    std::vector<SimulatedCardSurface> simulated_card_surfaces;
    std::vector<RECT> toolbar_surfaces;
    std::vector<Action> actions;
    std::vector<BlockControls> controls;
    std::size_t page = 0;
    int scroll = 0, extent = 0;
    bool updating = false, laying_out = false, layout_pending = false, dirty = false, ready = false;
    enum class LaboratoryStatus { ready, running, compilation_failed, finished, failed };
    LaboratoryStatus laboratory_status=LaboratoryStatus::ready;
    int laboratory_exit_code=0;
    HFONT button_font = nullptr,content_button_font = nullptr,laboratory_compiler_font=nullptr;
    HMODULE rich_module = nullptr;
    std::unique_ptr<EditingBar> editing_bar;
    Microsoft::WRL::ComPtr<IAccPropServices> accessibility;
    std::map<HWND,Icon> icon_buttons;
    std::optional<std::size_t> fullscreen_block;
    bool fullscreen_output = false;
    std::map<HWND,int> zoomed;
    std::jthread worker;
    std::mutex result_mutex;
    std::optional<windows::CppRunResult> completed;
    std::uint64_t request_id = 0;
    std::optional<std::size_t> running_block;
    std::string running_input;
    struct MathResult { std::size_t block; std::uint64_t revision; std::string reply,error; };
    std::jthread math_worker;
    std::mutex math_mutex;
    std::optional<MathResult> math_result;
    std::optional<std::size_t> checking_math;
    struct RestoredMath {std::string key,raw,reply;bool valid=false,compatible=false;};
    std::jthread restore_worker;
    std::mutex restore_mutex;
    std::vector<RestoredMath> restored_math;
    std::set<std::string> verified_math;
    enum class DragAxis { pending, lesson_vertical, page_horizontal, editor_vertical, editor_horizontal };
    struct Drag {
        HWND origin = nullptr; POINT start{}, last{}, editor_scroll{};
        WPARAM down_keys = 0; LPARAM down_point = 0;
        ULONGLONG started = 0;
        DragAxis axis = DragAxis::pending;
        bool editor = false, text = false;
    } drag;

    LessonView(const WindowsEnvironment& env, curriculum::Lesson model)
        : environment(env), lesson(std::move(model)),
          store(env.paths.root), data(store.load()) {
        const bool no_student_state = state::can_import_android_profile(data);
        for (const auto& component : lesson.source_path)
            if (component == L"math_physics") { route = "math_physics"; break; }
        curriculum::validate_reference_relationships(lesson);
        curriculum::validate_activity_relationships(lesson);
        state::validate_lesson_state(data, lesson,route);
        state::prepare_lesson_state(data, lesson);
        page = state::current_page(data, lesson);
        if (no_student_state) {
            // Initial UI bookkeeping is not a student edit. Closing an untouched
            // first page must not make an otherwise empty profile unimportable.
            state::save_current_page(data, lesson, page);
            data.preferences["native_windows"][scroll_key()] = std::int64_t{0};
        }
        rich_module = LoadLibraryW(L"Msftedit.dll");
        if (!rich_module) throw std::runtime_error("Windows Rich Edit is unavailable");
    }
    ~LessonView() {
        editing_bar.reset();
        cancel_restore();
        cancel_math();
        cancel_run();
        controls.clear();
        if (button_font) DeleteObject(button_font);
        if (content_button_font) DeleteObject(content_button_font);
        if(laboratory_compiler_font)DeleteObject(laboratory_compiler_font);
        if (rich_module) FreeLibrary(rich_module);
    }
    int px(int value) const { return MulDiv(value, static_cast<int>(GetDpiForWindow(window)), 96); }
    bool laboratory() const { return lesson.id=="native_cpp_laboratory"; }
    windows::EditorDocument predefined_document(std::string_view source)const{
        auto document=windows::predefined_code_document(source);
        if(laboratory())if(const auto indent=document.text.find("\n    \n");indent!=std::string::npos)
            document.selection_start=document.selection_end=static_cast<std::int64_t>(wide(std::string_view(document.text).substr(0,indent+5)).size());
        return document;
    }
    std::string ui(std::string_view es,std::string_view en,std::string_view fr) const {
        const auto locale = interface_language(data);
        return std::string(locale == "fr" ? fr : locale == "en" ? en : es);
    }
    std::string verdict_text(bool correct) const {
        return correct ? ui("Correcto","Correct","Correct") : ui("Incorrecto","Incorrect","Incorrect");
    }
    std::string default_review() const {
        return ui("Revisa tu respuesta e inténtalo de nuevo.","Review your answer and try again.","Vérifie ta réponse et réessaie.");
    }
    std::string laboratory_title()const{return ui("Prototipo del motor C++","C++ Engine Prototype","Prototype du moteur C++");}
    std::string laboratory_description()const{return ui("Compilación local con MSVC y ejecución nativa x64, completamente offline.","Local compilation with MSVC and native x64 execution, completely offline.","Compilation locale avec MSVC et exécution native x64, entièrement hors ligne.");}
    void laboratory_text_style(HWND item,int sp,COLORREF ink,bool bold=false){
        const auto colors=reader_theme::palette(item);CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE|CFM_COLOR|CFM_BOLD;
        format.yHeight=sp*15;format.crTextColor=colors.high_contrast?colors.text:ink;format.dwEffects=bold?CFE_BOLD:0;
        SendMessageW(item,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));SendMessageW(item,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
    }
    void refresh_laboratory_status(){
        if(!laboratory())return;
        auto value=laboratory_status==LaboratoryStatus::running?ui("Compilando y ejecutando el programa x64…","Compiling and running the x64 program…","Compilation et exécution du programme x64…")
            :laboratory_status==LaboratoryStatus::compilation_failed?ui("La compilación falló (código ","Compilation failed (code ","Échec de la compilation (code ")+std::to_string(laboratory_exit_code)+")."
            :laboratory_status==LaboratoryStatus::finished?ui("Ejecución terminada (código ","Execution finished (code ","Exécution terminée (code ")+std::to_string(laboratory_exit_code)+")."
            :laboratory_status==LaboratoryStatus::failed?ui("No se pudo ejecutar el prototipo.","Could not run the prototype.","Impossible d'exécuter le prototype.")
            :ui("Listo para ejecutar.","Ready to Run.","Prêt pour l’exécution.");
        for(auto& vm:controls)if(vm.status){set_rich_text(vm.status,value);laboratory_text_style(vm.status,16,RGB(174,180,190));}
    }
    std::string console_label(int channel) const {
        return channel == 2 ? ui("Diagnósticos de compilación","Compilation diagnostics","Diagnostics de compilation")
            : channel == 1 ? ui("Errores del programa","Program errors","Erreurs du programme")
            : ui("Salida del programa","Program output","Sortie du programme");
    }
    std::string course_id() const { const auto end=lesson.id.find("__");return end==std::string::npos?std::string{}:lesson.id.substr(0,end); }
    int content_text_scale() const { return appearance::text_scale(data,course_id()); }
    const curriculum::Block& block(std::size_t index) const { return lesson.pages.at(page).blocks.at(index); }
    std::string key(std::size_t index) const { return curriculum::exercise_key(lesson, block(index)); }
    std::string draft_key(std::size_t index) const { return curriculum::draft_key(route, lesson, block(index)); }
    static std::int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
    void refresh_attempts() {
        const auto balance=lgaed::attempts::balance(data,route,now_ms());
        const AttemptsState next{balance.available,balance.maximum,balance.progress,balance.unlimited};
        const bool changed=next.available!=attempt_state.available||next.maximum!=attempt_state.maximum||next.premium!=attempt_state.premium
            ||static_cast<int>(next.progress*next.maximum*60)!=static_cast<int>(attempt_state.progress*attempt_state.maximum*60);
        attempt_state=next;
        if(attempts&&changed)InvalidateRect(attempts,nullptr,FALSE);
    }
    bool may_evaluate() {
        lgaed::attempts::initialize(data,route,now_ms());refresh_attempts();
        if(lgaed::attempts::balance(data,route,now_ms()).can_evaluate())return true;
        if(hooks.attempts_requested)PostMessageW(window,header_action_message,attempts_id,0);
        return false;
    }
    void record_attempt(std::size_t index,bool correct) {
        if(correct)return;
        auto& vm=controls[index];std::string identity;
        if(vm.math)identity=math_answer_identity(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(vm.math->answer_identity_json())));
        else if(vm.answer_editor)identity=lgaed::attempts::ExerciseAnswerIdentity::of(vm.answer_editor->document().text);
        else if(block(index).type==BlockType::multiple_choice)identity=lgaed::attempts::ExerciseAnswerIdentity::of(vm.response.selected);
        else {
            std::vector<std::string> values;
            if(vm.fill){if(block(index).type==BlockType::inline_fill_code)for(const auto& answer:vm.fill->answers())values.push_back(lgaed::attempts::ExerciseAnswerIdentity::of(answer));
                else for(const auto slot:vm.fill->slots())values.push_back(lgaed::attempts::ExerciseAnswerIdentity::of(slot));}
            else for(const auto option:vm.order)values.push_back(lgaed::attempts::ExerciseAnswerIdentity::of(option));
            identity=lgaed::attempts::ExerciseAnswerIdentity::array(values);
        }
        attempt_ledger.record_incorrect(key(index),identity,[&]{lgaed::attempts::consume_incorrect(data,route,now_ms());mark_dirty();});
        refresh_attempts();
    }
    std::string scroll_key() const { return "scroll/" + lesson.id + "/" + lesson.pages.at(page).stable_id; }
    void reveal_math_input(HWND field) {
        if(!field||!IsWindow(field)||!IsChild(pane,field))return;
        const auto keyboard=math_keyboard_window(GetAncestor(window,GA_ROOT));
        if(!keyboard||!IsWindowVisible(keyboard))return;
        RECT visible{},toolbar{},input{};GetWindowRect(pane,&visible);GetWindowRect(keyboard,&toolbar);GetWindowRect(field,&input);
        const auto bottom=std::min(visible.bottom,toolbar.top)-px(8);
        auto wanted=input;
        for(std::size_t i=0;i<controls.size();++i)if(controls[i].math&&IsChild(controls[i].math->window(),field)){
            RECT check{};GetWindowRect(GetDlgItem(pane,control_id(i,3)),&check);
            if(check.bottom-input.top<=bottom-visible.top)wanted.bottom=check.bottom;
            break;
        }
        if(wanted.bottom>bottom)scroll_to(scroll+wanted.bottom-bottom);
        else if(wanted.top<visible.top+px(8))scroll_to(scroll+wanted.top-visible.top-px(8));
    }
    void notify_content() {
        if (!hooks.content_changed || notifying || !ready) return;
        notifying = true;
        try { hooks.content_changed(window); } catch (...) { notifying = false; throw; }
        notifying = false;
    }
    static bool own_scroll(HWND item,int axis) {
        return scrolls_internally(item,axis);
    }
    bool hits_text(HWND item,POINT point) const {
        return rich_text_hit_test(item,point);
    }
    void end_drag() {
        const auto owner = drag.origin; drag = {};
        if (owner && GetCapture() == owner) ReleaseCapture();
    }
    std::optional<LRESULT> drag_message(HWND item,UINT message,WPARAM wparam,LPARAM lparam) {
        if (import_open) return {};
        if (message == WM_NCDESTROY || message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
            if (drag.origin == item) end_drag(); return {};
        }
        if (message == WM_LBUTTONDBLCLK) { if (drag.origin == item) end_drag(); return {}; }
        if (message == WM_LBUTTONDOWN) {
            if (!IsWindowEnabled(item) || (wparam & (MK_SHIFT | MK_CONTROL))) return {};
            wchar_t gesture_class[64]{};GetClassNameW(item,gesture_class,64);
            const bool rich_control = std::wstring_view(gesture_class)==MSFTEDIT_CLASS;
            bool editor = false;
            if (rich_control) {
                CHARRANGE selection{}; SendMessageW(item,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
                if (selection.cpMin != selection.cpMax) return {};
                if (!(GetWindowLongPtrW(item,GWL_STYLE) & ES_READONLY)) {
                    editor = std::ranges::any_of(controls,[&](const auto& vm) { return vm.source == item && vm.source_editor; });
                    if (!editor || hits_text(item,{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)})) return {};
                }
            }
            end_drag(); drag.origin = item; drag.start = {GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
            drag.down_keys = wparam; drag.down_point = lparam; drag.started = GetTickCount64();
            drag.editor = editor || (rich_control && (own_scroll(item,SB_VERT) || own_scroll(item,SB_HORZ)));
            drag.text = rich_control && hits_text(item,drag.start);
            ClientToScreen(item,&drag.start); drag.last = drag.start;
            if (drag.editor) SendMessageW(item,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&drag.editor_scroll));
            SetCapture(item); return 0;
        }
        if (drag.origin != item) return {};
        POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ClientToScreen(item,&point);
        const int dx = point.x - drag.start.x, dy = point.y - drag.start.y;
        if (message == WM_MOUSEMOVE) {
            if (!(wparam & MK_LBUTTON)) { end_drag(); return 0; }
            if (drag.axis == DragAxis::pending) {
                if (drag.text && GetTickCount64() - drag.started >= GetDoubleClickTime()) {
                    const auto down_keys = drag.down_keys; const auto down_point = drag.down_point; end_drag();
                    DefSubclassProc(item,WM_LBUTTONDOWN,down_keys,down_point);
                    return DefSubclassProc(item,message,wparam,lparam);
                }
                if(drag.editor&&!drag.text&&std::max(std::abs(dx),std::abs(dy))>=px(2)){
                    const bool horizontal=std::abs(dx)>std::abs(dy);
                    drag.axis=horizontal?(own_scroll(item,SB_HORZ)?DragAxis::editor_horizontal:DragAxis::page_horizontal)
                        :(own_scroll(item,SB_VERT)?DragAxis::editor_vertical:DragAxis::lesson_vertical);
                }else if (std::abs(dy) > px(16) && std::abs(dy) * 5 > std::abs(dx) * 6)
                    drag.axis = drag.editor && own_scroll(item,SB_VERT) ? DragAxis::editor_vertical : DragAxis::lesson_vertical;
                else if (std::abs(dx) > px(16) && std::abs(dx) * 5 > std::abs(dy) * 6)
                    drag.axis = drag.editor && own_scroll(item,SB_HORZ) ? DragAxis::editor_horizontal : DragAxis::page_horizontal;
            }
            if (drag.axis == DragAxis::lesson_vertical) scroll_to(scroll - (point.y - drag.last.y));
            else if (drag.axis == DragAxis::editor_vertical || drag.axis == DragAxis::editor_horizontal) {
                auto position = drag.editor_scroll;
                if (drag.axis == DragAxis::editor_vertical) position.y = std::max<LONG>(0,position.y - dy);
                else position.x = std::max<LONG>(0,position.x - dx);
                SendMessageW(item,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
            }
            drag.last = point; return 0;
        }
        if (message == WM_LBUTTONUP) {
            const auto axis = drag.axis; const auto down_keys = drag.down_keys; const auto down_point = drag.down_point;
            end_drag();
            if (axis == DragAxis::pending) {
                DefSubclassProc(item,WM_LBUTTONDOWN,down_keys,down_point);
                return DefSubclassProc(item,message,wparam,lparam);
            }
            if (axis == DragAxis::page_horizontal && !fullscreen_block && std::abs(dx) > px(64) && std::abs(dx) * 2 > std::abs(dy) * 3) {
                const auto enabled=state::find(data,"app_settings","swipeNavigationEnabled");
                if(enabled&&std::holds_alternative<bool>(*enabled)&&!std::get<bool>(*enabled))return 0;
                const auto destination = dx < 0 ? page + 1 : page ? page - 1 : page;
                // The originating child must finish its callback before any
                // page change can destroy it or its editing adapter.
                PostMessageW(window,lesson_page_navigate_message,destination,0);
            }
            return 0;
        }
        return {};
    }
    void attach_gestures(HWND item) {
        SetWindowSubclass(item,gesture_proc,2,reinterpret_cast<DWORD_PTR>(this));
        GESTURECONFIG pan{GID_PAN,GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY | GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY,
            GC_PAN_WITH_GUTTER | GC_PAN_WITH_INERTIA};
        SetGestureConfig(item,0,1,&pan,sizeof(pan));
    }
    bool double_click_zoom(HWND item) {
        const bool display=std::ranges::any_of(controls,[&](const auto& vm){return vm.simulated_code==item||vm.console==item;});
        if(!display)return false;
        end_drag();
        if(const auto found=zoomed.find(item);found!=zoomed.end()){
            SendMessageW(item,EM_SETZOOM,found->second,100);zoomed.erase(found);
        }else{
            int numerator=0,denominator=0;SendMessageW(item,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            const int previous=denominator?MulDiv(numerator,100,denominator):100;
            zoomed[item]=previous;SendMessageW(item,EM_SETZOOM,std::min(previous*2,6400),100);
        }
        reflow();return true;
    }
    std::vector<LessonReadingRegion> reading_regions() const {
        std::vector<LessonReadingRegion> result;
        if (import_open || !pane) return result;
        if (const auto heading = GetDlgItem(pane,lesson_heading_id); heading && !fullscreen_block)
            result.push_back({lesson.id + '/' + lesson.locale + '/' + lesson.pages[page].stable_id + "/title",
                lesson.title,heading,std::numeric_limits<std::size_t>::max(),false,false,false,true});
        const auto add = [&](std::size_t index,HWND target,std::string_view source,bool is_feedback = false) {
            if (!target || source.empty() || !(GetWindowLongPtrW(target,GWL_STYLE) & WS_VISIBLE)) return;
            const auto& current = block(index); const auto& vm = controls[index];
            result.push_back({lesson.id + '/' + lesson.locale + '/' + lesson.pages[page].stable_id + '/'
                + current.stable_id + '/' + std::to_string(GetDlgCtrlID(target)),std::string(source),target,index,
                is_feedback,is_feedback && vm.feedback_correct,exercise(current.type),vm.response.correct});
        };
        for (std::size_t index = 0; index < controls.size(); ++index) {
            const auto& current = block(index); const auto& vm = controls[index];
            if (!vm.rendered || vm.locked) continue;
            if (current.type == BlockType::note) add(index,GetDlgItem(pane,control_id(index,19)),ui("Nota","Note","Note"));
            if (current.type == BlockType::heading || current.type == BlockType::paragraph || current.type == BlockType::note)
                add(index,GetDlgItem(pane,control_id(index,0)),current.text);
            else add(index,GetDlgItem(pane,control_id(index,0)),current.question);
            for (std::size_t option = 0; option < current.options.size(); ++option)
                add(index,GetDlgItem(pane,control_id(index,21 + static_cast<int>(option) * 2)),current.options[option]);
            for (std::size_t position = 0; position < vm.order.size(); ++position)
                add(index,GetDlgItem(pane,control_id(index,22 + static_cast<int>(position) * 3)),current.fragments.at(vm.order[position]));
            if(vm.math) for(const auto& region:vm.math->reading_regions()) {
                if(!region.target || region.source_text.empty()) continue;
                result.push_back({lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+current.stable_id+'/'+region.key,
                    region.source_text,region.target,index,false,false,true,vm.response.correct});
            }
            add(index,vm.feedback,vm.feedback_source,true);
        }
        return result;
    }

    bool has_references(const curriculum::Block& activity) const {
        return std::ranges::any_of(lesson.reference_after,[&](const auto& relation) { return relation.activity == activity.stable_id; })
            || std::ranges::any_of(lesson.activity_after,[&](const auto& relation) {
                return std::ranges::find(relation.prerequisites,activity.stable_id) != relation.prerequisites.end();
            });
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
    using CompletionSnapshot = std::map<std::string,bool>;
    CompletionSnapshot completion_snapshot() const {
        // Validation guarantees that dependencies precede their targets.
        // Evaluate each node once in that order; recursive DAG walks become
        // exponential when several later editors share earlier prerequisites.
        std::map<std::string,const std::vector<std::string>*> requirements;
        for (const auto& relation : lesson.activity_after) requirements.emplace(relation.activity,&relation.prerequisites);
        CompletionSnapshot result;
        for (const auto& candidate : lesson.pages) for (const auto& current : candidate.blocks) {
            if (current.type != BlockType::cpp_compiler) continue;
            bool available = true;
            if (const auto relation = requirements.find(current.stable_id); relation != requirements.end())
                for (const auto& identity : *relation->second) {
                    const auto found = result.find(identity);
                    if (found == result.end() || !found->second) { available = false; break; }
                }
            const auto receipt = state::string_value(data,"native_windows",reference_key(current));
            result[current.stable_id] = available && current.output_check && !receipt.empty()
                && receipt == reference_seal(current);
        }
        return result;
    }
    bool activity_available(const curriculum::Block& activity,const CompletionSnapshot& proofs) const {
        for (const auto& position : curriculum::activity_prerequisites(lesson,activity)) {
            const auto& prerequisite = lesson.pages[position.page].blocks[position.block];
            const auto found = proofs.find(prerequisite.stable_id);
            if (found == proofs.end() || !found->second) return false;
        }
        return true;
    }
    bool activity_available(const curriculum::Block& activity) const {
        return activity_available(activity,completion_snapshot());
    }
    bool activity_completed(const curriculum::Block& activity) const {
        const auto proofs = completion_snapshot(); const auto found = proofs.find(activity.stable_id);
        return found != proofs.end() && found->second;
    }
    bool reference_available(const curriculum::Block& reference) const {
        const auto dependency = curriculum::reference_activity(lesson,reference);
        return !dependency || activity_completed(lesson.pages[dependency->page].blocks[dependency->block]);
    }
    static std::size_t row_block(const Row& row) {
        if (GetDlgCtrlID(row.cells.front()) < 1000) return std::numeric_limits<std::size_t>::max();
        return static_cast<std::size_t>((GetDlgCtrlID(row.cells.front()) - 1000) / 100);
    }
    void label_radio(HWND item,std::string_view text) {
        if (!accessibility && FAILED(CoCreateInstance(__uuidof(CAccPropServices),nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(accessibility.GetAddressOf()))))
            throw std::runtime_error("Cannot label an accessible lesson option");
        if (FAILED(accessibility->SetHwndPropStr(item,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,wide(text).c_str())))
            throw std::runtime_error("Cannot label an accessible lesson option");
    }
    void decorate_icon(HWND item,Icon icon,std::string_view description) {
        SetWindowTextW(item,L""); icon_buttons[item] = icon; label_radio(item,description);
        reader_theme::icon_button(item);
    }
    bool draw_button(const DRAWITEMSTRUCT& item) {
        const auto icon = icon_buttons.find(item.hwndItem);
        if (icon == icon_buttons.end()) return reader_theme::draw_button(item);
        (void)reader_theme::draw_button(item);
        const auto colors = reader_theme::palette(item.hwndItem);
        auto bounds = item.rcItem;
        if(item.hwndItem==attempts) {
            const auto ink=attempt_state.premium?RGB(243,197,78):colors.accent;
            const int side=px(34);const int x=(bounds.right-side)/2;
            const RECT glyph{x,bounds.bottom-side-px(12),x+side,bounds.bottom-px(12)};
            const auto route_icon=route=="math_physics"?Icon::attempt_math:Icon::terminal;
            draw_icon(item.hDC,glyph,route_icon,colors.outline);
            const auto clipped=SaveDC(item.hDC);IntersectClipRect(item.hDC,glyph.left,glyph.bottom-static_cast<int>(side*attempt_state.progress),glyph.right,glyph.bottom);
            draw_icon(item.hDC,glyph,route_icon,ink);RestoreDC(item.hDC,clipped);
            const auto badge=CreateFontW(-px(12),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
            const auto old=SelectObject(item.hDC,badge);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);
            auto left=bounds;left.right=bounds.right/2;left.bottom=left.top+px(15);
            const auto count=attempt_state.premium?L"∞":std::to_wstring(attempt_state.available);
            DrawTextW(item.hDC,count.c_str(),-1,&left,DT_CENTER|DT_SINGLELINE);
            auto right=bounds;right.left=bounds.right/2;right.bottom=right.top+px(15);
            const auto percent=std::to_wstring(static_cast<int>(std::round(attempt_state.progress*100)))+L"%";
            DrawTextW(item.hDC,percent.c_str(),-1,&right,DT_CENTER|DT_SINGLELINE);
            auto remaining=bounds;remaining.top=remaining.bottom-px(13);
            const auto time=wide(lgaed::attempts::recovery_label(attempt_state.maximum,attempt_state.maximum*attempt_state.progress,attempt_state.premium));
            DrawTextW(item.hDC,time.c_str(),-1,&remaining,DT_CENTER|DT_SINGLELINE);
            SelectObject(item.hDC,old);DeleteObject(badge);return true;
        }
        const bool header=GetParent(item.hwndItem)==window;
        const int inset=header?(item.hwndItem==reader?9:item.hwndItem==language?5:4):10;
        InflateRect(&bounds,-px(inset),-px(inset));
        const auto ink=colors.high_contrast?colors.text:item.hwndItem==reader?colors.accent:
            item.hwndItem==back?RGB(255,255,255):RGB(170,174,183);
        draw_icon(item.hDC,bounds,icon->second,(item.itemState&ODS_DISABLED)?colors.muted:ink);
        return true;
    }
    void clear_accessible_names() {
        if (!accessibility) return;
        const MSAAPROPID property = PROPID_ACC_NAME;
        for (const auto& row : rows) for (const auto item : row.cells)
            if (IsWindow(item)) accessibility->ClearHwndProps(item,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,&property,1);
    }
    void destroy_control(HWND item) {
        zoomed.erase(item);
        icon_buttons.erase(item);
        if (accessibility) {
            const MSAAPROPID property = PROPID_ACC_NAME;
            accessibility->ClearHwndProps(item,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,&property,1);
        }
        DestroyWindow(item);
    }
    void remove_block(std::size_t index) {
        if (fullscreen_block == index) fullscreen_block.reset();
        if (controls[index].source_editor) capture_draft(index);
        if (running_block == index) cancel_run();
        // Detach adapters before their HWNDs. Other editors and their Undo
        // histories are left alive when a prerequisite reveals/hides this one.
        controls[index] = BlockControls{};
        std::erase_if(actions,[&](const auto& action) { return action.block == index; });
        for (auto row = rows.begin(); row != rows.end();) {
            if (row_block(*row) != index) { ++row; continue; }
            auto cells = std::move(row->cells);
            row = rows.erase(row);
            for (const auto item : cells) destroy_control(item);
        }
    }
    void refresh_references() {
        if (!pane || import_open) return;
        const auto was_updating = updating; updating = true;
        bool changed = false;
        for (std::size_t index = 0; index < controls.size(); ++index) {
            const auto& current = block(index);
            const bool available = reference_available(current);
            const bool locked = available && current.type == BlockType::cpp_compiler && !activity_available(current);
            if (controls[index].rendered == available && (!available || controls[index].locked == locked)) continue;
            changed = true;
            remove_block(index);
            if (available) render_block(index);
        }
        if (changed) {
            std::stable_sort(rows.begin(),rows.end(),[](const auto& left, const auto& right) {
                const auto a = row_block(left), b = row_block(right);
                if (a == std::numeric_limits<std::size_t>::max()) return b != a;
                return b != std::numeric_limits<std::size_t>::max() && a < b;
            });
            update_fonts(); reflow();
        }
        updating = was_updating;
        if (editing_bar) editing_bar->refresh();
        update_navigation();
        if (changed) notify_content();
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
        reader_theme::apply(result,laboratory()?reader_theme::Surface::screen:parent == window ? reader_theme::Surface::header : reader_theme::Surface::canvas);
        return result;
    }
    HWND rich(std::string_view text, int id, bool code = false,
              std::span<const curriculum::Emphasis> emphasis = {}, bool editable = false,
              RichTextRole role = RichTextRole::body) {
        auto result = control(MSFTEDIT_CLASS, "", ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP
            | (editable ? WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL : ES_READONLY), id);
        reader_theme::apply(result,laboratory()?reader_theme::Surface::screen:editable ? reader_theme::Surface::field
            : code ? reader_theme::Surface::card : reader_theme::Surface::canvas);
        SendMessageW(result, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE | ENM_CHANGE | ENM_SELCHANGE);
        SendMessageW(result, EM_EXLIMITTEXT, 0, 2 * 1024 * 1024);
        set_rich_text(result, text, emphasis, code, role);
        if(!code&&!editable)(void)set_native_math_inline_text(result,text,emphasis,role);
        SendMessageW(result,EM_SETZOOM,content_text_scale(),100);
        if (editable) SendMessageW(result, EM_SETTARGETDEVICE, 0, 1);
        SetWindowSubclass(result, wheel_proc, 1, 0);
        attach_gestures(result);
        return result;
    }
    void text_row(std::string_view text, int id, bool code = false,
                  std::span<const curriculum::Emphasis> emphasis = {}, RichTextRole role = RichTextRole::body) {
        const auto item = rich(text, id, code, emphasis, false, role);
        rows.push_back({{item}, {0}, 26, true});
        if(!code){rows.back().padding_before=role==RichTextRole::heading?18:0;
            rows.back().padding_after=role==RichTextRole::lesson_title?28:role==RichTextRole::heading?10:18;}
    }
    HWND button(std::string_view text, int id, Action action) {
        auto result = control(L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE, id);
        action.control = id; actions.push_back(action);
        return result;
    }
    HWND icon_button(std::string_view text,int id,Action action,Icon icon) {
        auto result = button("",id,action); decorate_icon(result,icon,text); return result;
    }
    void toolbar_caption(HWND target,std::string_view caption) {
        set_rich_text(target,caption,{},false,RichTextRole::prompt);
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR|CFM_FACE;
        format.crTextColor=reader_theme::palette(target).high_contrast?reader_theme::foreground(target):RGB(225,228,235);
        wcscpy_s(format.szFaceName,L"Roboto");SendMessageW(target,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
    }
    void simulated_header(std::size_t index,bool response) {
        const auto& current=block(index);auto code_language=current.language;
        if(code_language.empty()||code_language=="cpp"||code_language=="c++")code_language="C++";
        else for(auto& letter:code_language)letter=static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
        const auto caption=rich("</> "+code_language,control_id(index,16));
        reader_theme::apply(caption,reader_theme::Surface::card_header);set_rich_text(caption,"</> "+code_language,{},false,RichTextRole::prompt);
        const auto copy=icon_button(response?ui("Copiar ejercicio","Copy exercise","Copier l'exercice"):ui("Copiar código","Copy code","Copier le code"),
            control_id(index,18),{0,ActionType::copy_simulated,index},Icon::copy);
        if(response){
            const auto reset=icon_button(ui("Reiniciar respuesta","Reset answer","Réinitialiser la réponse"),control_id(index,12),{0,ActionType::reset_answer,index},Icon::reset);
            const auto spacer=control(L"STATIC","",0,control_id(index,17));reader_theme::apply(spacer,reader_theme::Surface::card_header);
            if(current.type==BlockType::multiple_choice)rows.push_back({{caption,reset,spacer,copy},{100,44,0,44},48});
            else{
                const auto expand=icon_button(ui("Pantalla completa","Full screen","Plein écran"),control_id(index,13),{0,ActionType::fullscreen_source,index},Icon::expand);
                const auto menu_button=icon_button(ui("Opciones del código","Code options","Options du code"),control_id(index,19),{0,ActionType::simulated_menu,index},Icon::settings);
                rows.push_back({{caption,reset,spacer,copy,expand,menu_button},{100,44,0,44,44,44},48});
            }
        }else{
            const auto expand=icon_button(ui("Pantalla completa","Full screen","Plein écran"),control_id(index,20),{0,ActionType::fullscreen_source,index},Icon::expand);
            const auto menu_button=icon_button(ui("Opciones del código","Code options","Options du code"),control_id(index,19),{0,ActionType::simulated_menu,index},Icon::settings);
            rows.push_back({{caption,copy,expand,menu_button},{0,44,44,44},48});
        }
        rows.back().simulated_card=index;rows.back().card_header=true;
        for(const auto item:rows.back().cells)reader_theme::apply(item,reader_theme::Surface::card_header);
        toolbar_caption(caption,"</> "+code_language);
    }
    void copy_simulated(std::size_t index) {
        const auto& current=block(index);const auto& vm=controls[index];
        if(current.type==BlockType::code){copy_text(current.code);return;}
        if(vm.fill){
            const auto answers=vm.fill->answers();const auto slots=vm.fill->slots();std::string text;std::size_t hole=0,cursor=0;
            while(cursor<current.code_template.size()){
                const auto next=current.code_template.find("[]",cursor);
                if(next==std::string::npos){text+=current.code_template.substr(cursor);break;}
                text+=current.code_template.substr(cursor,next-cursor);
                if(current.type==BlockType::inline_fill_code)text+=hole<answers.size()&&!answers[hole].empty()?answers[hole]:"[]";
                else text+=hole<slots.size()&&slots[hole]<current.choices.size()?current.choices[slots[hole]]:"[]";
                ++hole;cursor=next+2;
            }
            copy_text(text);return;
        }
        if(!vm.order.empty()){
            std::string text;for(const auto option:vm.order){if(!text.empty())text+='\n';text+=current.fragments.at(option);}copy_text(text);return;
        }
        if(vm.answer_editor){copy_text(current.code+"\n\n"+vm.answer_editor->document().text);return;}
        std::string text=current.question+"\n\n";
        for(const auto& option:current.options)text+="• "+option+'\n';
        if(vm.response.selected>=0&&static_cast<std::size_t>(vm.response.selected)<current.options.size())
            text+=ui("\nSeleccionado: ","\nSelected: ","\nSélection : ")+current.options[static_cast<std::size_t>(vm.response.selected)];
        copy_text(text);
    }
    void simulated_menu(std::size_t index) {
        struct Popup { HMENU menu=CreatePopupMenu();~Popup(){if(menu)DestroyMenu(menu);} } popup;
        if(!popup.menu)throw std::runtime_error("Cannot create the native code menu");
        const bool response=exercise(block(index).type);
        AppendMenuW(popup.menu,MF_STRING,control_id(index,18),wide(response?ui("Copiar ejercicio","Copy exercise","Copier l'exercice"):ui("Copiar código","Copy code","Copier le code")).c_str());
        if(response)AppendMenuW(popup.menu,MF_STRING|(IsWindowEnabled(GetDlgItem(pane,control_id(index,12)))?MF_ENABLED:MF_GRAYED),control_id(index,12),
            wide(ui("Reiniciar respuesta","Reset answer","Réinitialiser la réponse")).c_str());
        AppendMenuW(popup.menu,MF_STRING,control_id(index,response?13:20),wide(ui("Pantalla completa","Full screen","Plein écran")).c_str());
        const auto style=prepare_native_menu(popup.menu,window);RECT bounds{};GetWindowRect(GetDlgItem(pane,control_id(index,19)),&bounds);
        const auto selected=TrackPopupMenuEx(popup.menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_TOPALIGN,bounds.right,bounds.bottom,window,nullptr);
        if(selected)command(static_cast<int>(selected),BN_CLICKED,nullptr);
    }
    void paint_simulated_cards(HDC dc) const {
        const auto saved=SaveDC(dc);
        SelectObject(dc,GetStockObject(NULL_PEN));SelectObject(dc,reader_theme::brush(reader_theme::Surface::card));
        for(auto surface:toolbar_surfaces){OffsetRect(&surface,0,-scroll);FillRect(dc,&surface,reader_theme::brush(laboratory()?reader_theme::Surface::compiler_header:reader_theme::Surface::card_header));}
        for(const auto& surface:simulated_card_surfaces){auto bounds=surface.bounds;OffsetRect(&bounds,0,-scroll);
            RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(12),px(12));
            const auto clip=CreateRoundRectRgn(bounds.left,bounds.top,bounds.right+1,bounds.bottom+1,px(12),px(12));
            const auto state=SaveDC(dc);ExtSelectClipRgn(dc,clip,RGN_AND);DeleteObject(clip);
            auto header=bounds;header.bottom=surface.header_bottom-scroll;FillRect(dc,&header,reader_theme::brush(reader_theme::Surface::card_header));RestoreDC(dc,state);
        }
        RestoreDC(dc,saved);
    }
    void copy_text(std::string_view text) {
        const auto value = wide(text); const auto bytes = (value.size() + 1) * sizeof(wchar_t);
        const auto memory = GlobalAlloc(GMEM_MOVEABLE,bytes);
        if (!memory) throw std::runtime_error(ui("No se pudo copiar el texto.","Could not copy the text.","Impossible de copier le texte."));
        const auto target = GlobalLock(memory);
        if (!target) { GlobalFree(memory); throw std::runtime_error(ui("No se pudo copiar el texto.","Could not copy the text.","Impossible de copier le texte.")); }
        std::memcpy(target,value.c_str(),bytes); GlobalUnlock(memory);
        if (!OpenClipboard(window)) { GlobalFree(memory); throw std::runtime_error(ui("El portapapeles está ocupado.","The clipboard is busy.","Le presse-papiers est occupé.")); }
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT,memory)) { CloseClipboard(); GlobalFree(memory); throw std::runtime_error(ui("No se pudo copiar el texto.","Could not copy the text.","Impossible de copier le texte.")); }
        CloseClipboard();
    }
    void console_present(std::size_t index,int preferred = -1) {
        auto& vm = controls.at(index);
        if(laboratory()){
            const auto before=updating;updating=true;
            set_rich_text(vm.console,vm.console_channels[2]+vm.console_channels[0]+vm.console_channels[1],{},true);
            laboratory_text_style(vm.console,18,RGB(121,226,157));updating=before;reflow();return;
        }
        int selected = preferred;
        if (selected < 0 || selected > 2 || vm.console_channels[static_cast<std::size_t>(selected)].empty()) {
            selected = 0;
            if (vm.console_channels[0].empty()) selected = vm.console_channels[1].empty() ? 2 : 1;
            if (vm.console_channels[static_cast<std::size_t>(selected)].empty()) selected = 0;
        }
        vm.console_channel = selected;
        const auto before = updating; updating = true;
        set_rich_text(vm.console,vm.console_channels[static_cast<std::size_t>(selected)],{},true);
        CHARFORMAT2W color{}; color.cbSize = sizeof(color); color.dwMask = CFM_COLOR;
        const auto colors = reader_theme::palette(window);
        color.crTextColor = colors.high_contrast ? colors.text : selected == 0 ? RGB(121,226,157)
            : selected == 1 ? RGB(245,150,150) : colors.text;
        SendMessageW(vm.console,EM_SETSEL,0,-1);
        SendMessageW(vm.console,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&color));
        SendMessageW(vm.console,EM_SETSEL,0,0);
        toolbar_caption(GetDlgItem(pane,control_id(index,30)),console_label(selected));
        updating = before; reflow();
    }
    void toggle_fullscreen(std::size_t index,bool output) {
        end_drag();
        if (fullscreen_block == index && fullscreen_output == output) fullscreen_block.reset();
        else { fullscreen_block = index; fullscreen_output = output; }
        if(const auto simulated=controls[index].simulated_code){const auto style=GetWindowLongPtrW(simulated,GWL_STYLE);
            SetWindowLongPtrW(simulated,GWL_STYLE,fullscreen_block?style|WS_VSCROLL:style&~static_cast<LONG_PTR>(WS_VSCROLL));apply_wrap(simulated);
        }
        scroll = 0; resize(); notify_content();
        if (fullscreen_block && controls[index].source && !output) SetFocus(controls[index].source);
    }
    void code_menu(std::size_t index,bool output) {
        const auto popup = CreatePopupMenu();
        if (!popup) throw std::runtime_error(ui("No se pudo abrir el menú de la consola.","Could not open the console menu.","Impossible d'ouvrir le menu de la console."));
        const auto add = [&](int part,std::string_view label,UINT flags = MF_STRING) {
            AppendMenuW(popup,flags,static_cast<UINT_PTR>(control_id(index,part)),wide(label).c_str());
        };
        if (output) {
            add(71,ui("Copiar salida","Copy output","Copier la sortie"));
            add(72,ui("Limpiar salida","Clear output","Effacer la sortie"));
            add(76,ui("Pantalla completa","Full screen","Plein écran"));
            const auto& channels = controls[index].console_channels;
            if (std::ranges::count_if(channels,[](const auto& value) { return !value.empty(); }) > 1) {
                AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
                for (int channel = 0; channel < 3; ++channel) if (!channels[static_cast<std::size_t>(channel)].empty())
                    add(78 + channel,console_label(channel),
                        MF_STRING | (controls[index].console_channel == channel ? MF_CHECKED : 0));
            }
        } else {
            add(73,ui("Deshacer","Undo","Annuler")); add(74,ui("Rehacer","Redo","Rétablir"));
            add(70,ui("Copiar código","Copy code","Copier le code"));
            add(12,ui("Reiniciar código","Reset code","Réinitialiser le code"));
            add(75,ui("Pantalla completa","Full screen","Plein écran"));
        }
        RECT bounds{}; GetWindowRect(GetDlgItem(pane,control_id(index,output ? 31 : 19)),&bounds);
        const auto style = prepare_native_menu(popup,window);
        const auto selected = TrackPopupMenuEx(popup,TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,bounds.right,bounds.bottom,window,nullptr);
        DestroyMenu(popup);
        if (selected) command(static_cast<int>(selected),BN_CLICKED,nullptr);
    }
    void update_fonts() {
        auto font = CreateFontW(-MulDiv(18, static_cast<int>(GetDpiForWindow(window)), 96), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Roboto");
        if (!font) return;
        const auto content_font=CreateFontW(-MulDiv(18*content_text_scale(),static_cast<int>(GetDpiForWindow(window)),96*100),0,0,0,
            FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(!content_font){DeleteObject(font);return;}
        const auto compact_font=laboratory()?CreateFontW(-MulDiv(13,static_cast<int>(GetDpiForWindow(window)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto"):nullptr;
        for (auto item : {menu, page_status, back, reader, language, attempts, settings}) if (item) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (auto item : {import_label, import_path, import_browse, import_review, import_commit, import_cancel})
            if (item) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (const auto& row : rows) for (auto item : row.cells) {
            wchar_t name[32]{}; GetClassNameW(item, name, 32);
            if (std::wstring_view(name) != MSFTEDIT_CLASS) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(compact_font&&GetDlgCtrlID(item)>=1000&&(GetDlgCtrlID(item)-1000)%100==36?compact_font:content_font), TRUE);
            else SendMessageW(item,EM_SETZOOM,content_text_scale(),100);
        }
        for(auto& vm:controls){if(vm.math)vm.math->set_text_scale(content_text_scale());if(vm.fill)vm.fill->set_text_scale(content_text_scale());}
        if (button_font) DeleteObject(button_font);
        if (content_button_font) DeleteObject(content_button_font);
        button_font = font;
        content_button_font = content_font;
        if(laboratory_compiler_font)DeleteObject(laboratory_compiler_font);laboratory_compiler_font=compact_font;
    }
    void initialize(HWND target) {
        window = target;
        reader_theme::set_accent(window,reader_theme::appearance_accent(data,laboratory()?std::string_view{}:std::string_view(route)));
        reader_theme::apply(window,laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas);
        title = control(MSFTEDIT_CLASS, "", ES_READONLY | ES_MULTILINE, 105, window);
        set_rich_text(title,ui("Lección","Lesson","Leçon"));
        CHARFORMAT2W heading{}; heading.cbSize = sizeof(heading); heading.dwMask = CFM_SIZE | CFM_BOLD;
        heading.yHeight = rich_text_height(); heading.dwEffects = CFE_BOLD;
        SendMessageW(title,EM_SETSEL,0,-1);
        SendMessageW(title,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&heading));
        SendMessageW(title,EM_SETSEL,0,0);
        page_status = control(PROGRESS_CLASS,"",PBS_SMOOTH,page_status_id,window);
        SendMessageW(page_status,PBM_SETRANGE32,0,static_cast<LPARAM>(lesson.pages.size()));
        SendMessageW(page_status,PBM_SETBARCOLOR,0,reader_theme::palette(window).accent);
        SendMessageW(page_status,PBM_SETBKCOLOR,0,reader_theme::palette(window).header);
        back = control(L"BUTTON",ui("Atrás","Back","Retour"),WS_TABSTOP | BS_PUSHBUTTON,back_id,window);
        reader = control(L"BUTTON",ui("Lector","Reader","Lecteur"),WS_TABSTOP | BS_PUSHBUTTON,reader_id,window);
        language = control(L"BUTTON",ui("Idioma","Language","Langue"),WS_TABSTOP | BS_PUSHBUTTON,language_id,window);
        attempts = control(L"BUTTON",ui("Intentos disponibles","Available attempts","Essais disponibles"),WS_TABSTOP | BS_PUSHBUTTON,attempts_id,window);
        settings = control(L"BUTTON",ui("Ajustes y páginas","Settings and pages","Réglages et pages"),WS_TABSTOP | BS_PUSHBUTTON,menu_id,window);
        menu = settings;
        decorate_icon(back,Icon::back,ui("Volver","Back","Retour")); decorate_icon(reader,Icon::speaker,ui("Lector","Reader","Lecteur"));
        decorate_icon(language,Icon::language,ui("Idioma","Language","Langue"));
        decorate_icon(attempts,Icon::terminal,ui("Intentos disponibles","Available attempts","Essais disponibles"));
        if(!laboratory()){
            const auto before_attempt_migration=data;
            const bool existing_reserve=state::find(before_attempt_migration,"attempt_reserve_seconds_test","reserve_"+route)
                &&state::find(before_attempt_migration,"attempt_reserve_seconds_test","updated_at_"+route);
            lgaed::attempts::initialize(data,route,now_ms());refresh_attempts();
            if(existing_reserve&&data!=before_attempt_migration)mark_dirty();
        }
        decorate_icon(settings,Icon::settings,ui("Ajustes y páginas","Settings and pages","Réglages et pages"));
        fullscreen_close=control(L"BUTTON","",WS_TABSTOP|BS_PUSHBUTTON,fullscreen_close_id,window);
        decorate_icon(fullscreen_close,Icon::close,ui("Salir de pantalla completa","Exit full screen","Quitter le plein écran"));
        ShowWindow(fullscreen_close,SW_HIDE);
        pane = CreateWindowExW(WS_EX_CONTROLPARENT, pane_class, L"Contenido de la lección",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
            window, nullptr, GetModuleHandleW(nullptr), this);
        if (!pane) throw std::runtime_error("Cannot create the lesson viewport");
        SetWindowTextW(pane,wide(ui("Contenido de la lección","Lesson content","Contenu de la leçon")).c_str());
        attach_gestures(pane);
        reader_theme::apply(pane,laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas);
        if(route!="programming")editing_bar = std::make_unique<EditingBar>(window,[this](HWND focused) -> CodeEditor* {
            if (import_open || updating) return nullptr;
            for (std::size_t index = 0; index < controls.size(); ++index) {
                auto& vm = controls[index];
                if (focused == vm.source && vm.source_editor) return vm.source_editor.get();
                if (focused == vm.answer && vm.answer_editor && block(index).type == BlockType::fill_code)
                    return vm.answer_editor.get();
                if(vm.fill) if(auto* editor=vm.fill->editor(focused)) return editor;
            }
            return nullptr;
        },[this] { if (ready) resize(); });
        if(editing_bar)editing_bar->set_shortcuts(parse_state_shortcuts(data));
        render_page(); ready = true; resize();if(!laboratory())SetTimer(window,attempts_timer,1000,nullptr);
        verify_saved_math();
    }
    void refresh_interface_labels() {
        const auto was_notifying = notifying; notifying = true;
        struct NotificationGuard { bool& value; bool previous; ~NotificationGuard() { value = previous; } } guard{notifying,was_notifying};
        SetWindowTextW(window,wide(ui("Lección","Lesson","Leçon")).c_str());
        SetWindowTextW(pane,wide(ui("Contenido de la lección","Lesson content","Contenu de la leçon")).c_str());
        label_radio(back,ui("Volver","Back","Retour")); label_radio(reader,ui("Lector","Reader","Lecteur"));
        label_radio(language,ui("Idioma","Language","Langue"));
        label_radio(attempts,ui("Intentos disponibles","Available attempts","Essais disponibles"));
        label_radio(settings,ui("Ajustes y páginas","Settings and pages","Réglages et pages"));
        for (std::size_t index = 0; index < controls.size(); ++index) {
            auto& vm = controls[index]; const auto& current = block(index); if (!vm.rendered) continue;
            if(laboratory()){
                if(const auto heading=GetDlgItem(pane,lesson_heading_id)){set_rich_text(heading,laboratory_title(),{},false,RichTextRole::lesson_title);laboratory_text_style(heading,28,RGB(255,255,255),true);}
                if(const auto description=GetDlgItem(pane,lesson_heading_id+1)){set_rich_text(description,laboratory_description());laboratory_text_style(description,18,RGB(174,180,190));}
                if(vm.compile)SetWindowTextW(vm.compile,wide(ui("Compilar y ejecutar","Compile and Run","Compiler et exécuter")).c_str());
                if(const auto wrap=GetDlgItem(pane,control_id(index,35)))label_radio(wrap,code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé"):ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));
                refresh_laboratory_status();continue;
            }
            if (vm.locked) {
                set_rich_text(GetDlgItem(pane,control_id(index,18)),ui(
                    "Completa y comprueba correctamente la actividad anterior requerida para abrir este editor.",
                    "Complete and correctly check the required previous activity to open this editor.",
                    "Termine et vérifie correctement l'activité précédente requise pour ouvrir cet éditeur."));
                continue;
            }
            if (current.type == BlockType::note) {
                const auto note = GetDlgItem(pane,control_id(index,19));
                set_rich_text(note,ui("Nota","Note","Note"),{},false,RichTextRole::heading);
            }
            if(vm.simulated_code||current.type==BlockType::multiple_choice||vm.fill||!vm.order.empty()||vm.answer){
                const auto copy=GetDlgItem(pane,control_id(index,18));
                const bool response=exercise(current.type);
                label_radio(copy,response?ui("Copiar ejercicio","Copy exercise","Copier l'exercice"):ui("Copiar código","Copy code","Copier le code"));
                if(response)label_radio(GetDlgItem(pane,control_id(index,12)),ui("Reiniciar respuesta","Reset answer","Réinitialiser la réponse"));
                if(current.type!=BlockType::multiple_choice){label_radio(GetDlgItem(pane,control_id(index,response?13:20)),ui("Pantalla completa","Full screen","Plein écran"));
                    label_radio(GetDlgItem(pane,control_id(index,19)),ui("Opciones del código","Code options","Options du code"));}
            }
            if (vm.source) {
                const auto name = [&](int part,std::string text) { label_radio(GetDlgItem(pane,control_id(index,part)),text); };
                name(12,ui("Reiniciar código","Reset code","Réinitialiser le code"));
                name(18,ui("Copiar código","Copy code","Copier le code")); name(20,ui("Pantalla completa","Full screen","Plein écran"));
                name(19,ui("Opciones del editor","Editor options","Options de l'éditeur"));
                name(23,ui("Copiar salida","Copy output","Copier la sortie")); name(24,ui("Limpiar salida","Clear output","Effacer la sortie"));
                name(25,ui("Ampliar consola","Expand console","Agrandir la console"));
                name(31,ui("Opciones de la consola","Console options","Options de la console"));
                if (vm.input) set_rich_text(GetDlgItem(pane,control_id(index,17)),ui("Entrada","Input","Entrée"));
                SetWindowTextW(vm.compile,wide(current.output_check
                    ? ui("Compilar, ejecutar y comprobar","Compile, run and check","Compiler, exécuter et vérifier")
                    : ui("Compilar y ejecutar","Compile and run","Compiler et exécuter")).c_str());
                toolbar_caption(GetDlgItem(pane,control_id(index,30)),console_label(vm.console_channel));
            }
            sync_quiz_controls(index);
            if (vm.feedback_generated_verdict) {
                const auto boundary = vm.feedback_source.find('\n');
                auto text = verdict_text(vm.feedback_correct);
                if (vm.feedback_default_review) text += '\n' + default_review();
                else if (boundary != std::string::npos) text += vm.feedback_source.substr(boundary);
                feedback(index,text,vm.feedback_correct,true,vm.feedback_default_review);
            }
        }
        update_navigation();
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
        capture_page();
        // Same owner and historical groups as Android. Compiler activities do
        // not manufacture exercise progress or require a separate Finish click.
        const auto complete=curriculum::catalog_lesson_page_progress(data,lesson,[&](const auto& current){
            const auto identity=curriculum::exercise_key(lesson,current);
            const auto response=state::get_response(data,identity);
            return response&&response->correct&&(current.type!=BlockType::math_puzzle||verified_math.contains(identity));
        });
        const bool pending_math=std::ranges::any_of(lesson.pages,[&](const auto& p){return std::ranges::any_of(p.blocks,[&](const auto& current){
            if(current.type!=BlockType::math_puzzle)return false;
            const auto identity=curriculum::exercise_key(lesson,current);const auto response=state::get_response(data,identity);
            return response&&response->correct&&!verified_math.contains(identity);
        });});
        if(!pending_math)data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=complete.total>0&&complete.completed==complete.total;
        store.save(data); dirty = false;
    }
    void flush_selection() {
        if (import_open) return;
        const auto before = data;
        capture_page();
        dirty = dirty || before != data;
        flush();
    }
    bool code_wrap() const {
        if(laboratory()){const auto saved=state::find(data,"native_windows","cpp_laboratory_wrap");return saved&&std::holds_alternative<bool>(*saved)&&std::get<bool>(*saved);}
        return appearance::code_wrap(data,course_id());
    }
    void apply_wrap(HWND item) {
        if (!item) return;
        const bool enabled = code_wrap();
        auto style = GetWindowLongPtrW(item,GWL_STYLE);
        SetWindowLongPtrW(item,GWL_STYLE,enabled ? style & ~static_cast<LONG_PTR>(WS_HSCROLL | ES_AUTOHSCROLL)
            : style | WS_HSCROLL | ES_AUTOHSCROLL);
        SendMessageW(item,EM_SETTARGETDEVICE,0,enabled ? 0 : 1);
        SetWindowPos(item,nullptr,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    void update_preferences(const std::function<void(state::StateData&)>& update) {
        flush_selection();
        auto candidate = data; update(candidate);
        const auto protected_data = [](state::StateData value) {
            value.preferences.erase("lesson_narration_settings");
            value.preferences.erase("lesson_narration_resume_bounded_v1");
            value.preferences.erase("learning_flow_settings");
            if (auto domain = value.preferences.find("keyboard_shortcut_settings"); domain != value.preferences.end()) {
                domain->second.erase("cpp_shortcuts");
                if (domain->second.empty()) value.preferences.erase(domain);
            }
            for(const auto domain_name:{"lesson_text_size_settings","code_wrap_settings"})
                if(auto domain=value.preferences.find(domain_name);domain!=value.preferences.end()){
                    std::erase_if(domain->second,[domain_name](const auto& entry){
                        const auto key=std::string_view(entry.first);
                        if(std::string_view(domain_name)=="lesson_text_size_settings")
                            return key=="global_scale"||(key.starts_with("course_scale_")&&key.size()>13)||(key.starts_with("course_use_global_")&&key.size()>18);
                        return key=="global_wrap"||(key.starts_with("course_wrap_")&&key.size()>12)
                            ||(key.starts_with("course_allow_global_overwrite_")&&key.size()>30);
                    });
                    if(domain->second.empty())value.preferences.erase(domain);
                }
            if (auto domain = value.preferences.find("appearance_settings"); domain != value.preferences.end()) {
                std::erase_if(domain->second,[](const auto& entry) {
                    if (entry.first == "accent_red" || entry.first == "accent_green" || entry.first == "accent_blue") return true;
                    for (const auto route_id : {"programming","math_physics"})
                        for (const auto prefix : {"route_accent_red_","route_accent_green_","route_accent_blue_","route_allow_global_accent_overwrite_"})
                            if (entry.first == std::string(prefix) + route_id) return true;
                    return false;
                });
                if (domain->second.empty()) value.preferences.erase(domain);
            }
            if(auto domain=value.preferences.find("app_settings");domain!=value.preferences.end()){
                domain->second.erase("swipeNavigationEnabled");if(domain->second.empty())value.preferences.erase(domain);
            }
            if (auto domain = value.preferences.find("native_windows"); domain != value.preferences.end()) {
                std::erase_if(domain->second,[](const auto& entry) {
                    return entry.first.starts_with("ui/") || entry.first == "ui_language" || entry.first == "content_language"
                        || entry.first == "automatic_between_lessons" || entry.first == "automatic_between_units"
                        || entry.first == "automatic_between_courses" || entry.first == "swipe_finish" || entry.first == "code_wrap";
                });
                if (domain->second.empty()) value.preferences.erase(domain);
            }
            return value;
        };
        if (protected_data(candidate) != protected_data(data))
            throw std::invalid_argument("A reader preference update cannot change lesson progress or compiler drafts");
        if (candidate == data) return;
        const auto interface_changed = interface_language(candidate) != interface_language(data);
        const auto* previous_shortcuts = state::find(data,"keyboard_shortcut_settings","cpp_shortcuts");
        const auto* next_shortcuts = state::find(candidate,"keyboard_shortcut_settings","cpp_shortcuts");
        const bool shortcuts_changed = (!previous_shortcuts != !next_shortcuts)
            || (previous_shortcuts && next_shortcuts && *previous_shortcuts != *next_shortcuts);
        store.save(candidate); data = std::move(candidate); dirty = false;
        reader_theme::set_accent(window,reader_theme::appearance_accent(data,laboratory()?std::string_view{}:std::string_view(route)));
        SendMessageW(page_status,PBM_SETBARCOLOR,0,reader_theme::palette(window).accent);
        const auto was_updating = updating; updating = true;
        for (std::size_t index = 0; index < controls.size(); ++index) {
            apply_wrap(controls[index].source);
            apply_wrap(controls[index].simulated_code);
            if (block(index).type == BlockType::fill_code) apply_wrap(controls[index].answer);
        }
        if (shortcuts_changed && editing_bar) editing_bar->set_shortcuts(parse_state_shortcuts(data));
        if (interface_changed) refresh_interface_labels();
        update_fonts();
        updating = was_updating; reflow();
        if (interface_changed) notify_content();
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
    void cancel_math(){
        if(math_worker.joinable()){math_worker.request_stop();math_worker.join();}
        if(checking_math&&*checking_math<controls.size()){
            const auto check=GetDlgItem(pane,control_id(*checking_math,3));if(check)EnableWindow(check,TRUE);
        }
        checking_math.reset();std::lock_guard lock(math_mutex);math_result.reset();
    }
    void cancel_restore(){if(restore_worker.joinable()){restore_worker.request_stop();restore_worker.join();}}
    void verify_saved_math(){
        if(!hooks.validate_math)return;
        cancel_restore();std::vector<RestoredMath> pending;
        std::vector<std::string> originals;
        for(const auto& p:lesson.pages)for(const auto& b:p.blocks)if(b.type==BlockType::math_puzzle){
            const auto identity=curriculum::exercise_key(lesson,b);const auto saved=state::get_response(data,identity);
            if(saved&&((saved->correct&&!verified_math.contains(identity))||state::response_reviewed(*saved,b))){
                pending.push_back({identity,saved->raw_json});originals.push_back(b.raw_json);
            }
        }
        if(pending.empty())return;
        auto evaluate=hooks.validate_math;const auto locale=lesson.locale;
        restore_worker=std::jthread([this,pending=std::move(pending),originals=std::move(originals),evaluate,locale](std::stop_token stop)mutable{
            using namespace winrt::Windows::Data::Json;
            for(std::size_t i=0;i<pending.size()&&!stop.stop_requested();++i){
                auto& result=pending[i];
                try{
                    const auto stored=JsonObject::Parse(winrt::to_hstring(result.raw));
                    const auto answers=winrt::to_string(stored.GetNamedObject(L"responses").Stringify());
                    result.reply=evaluate(originals[i],answers,locale,stop);
                    const auto reply=JsonObject::Parse(winrt::to_hstring(result.reply));
                    result.compatible=reply.GetNamedBoolean(L"evaluable",false)
                        &&stored.GetNamedString(L"contentFingerprint",L"")==reply.GetNamedString(L"contentFingerprint",L"missing");
                    result.valid=stored.GetNamedBoolean(L"correct",false)&&result.compatible&&reply.GetNamedBoolean(L"correct",false);
                }catch(...){result.valid=false;}
                if(stop.stop_requested())return;
                {std::lock_guard lock(restore_mutex);restored_math.push_back(std::move(result));}
                PostMessageW(window,math_restore_message,0,0);
            }
        });
    }
    void finish_restore(){
        using namespace winrt::Windows::Data::Json;
        std::vector<RestoredMath> restored;{std::lock_guard lock(restore_mutex);restored.swap(restored_math);}
        for(const auto& result:restored){
            const auto current=state::get_response(data,result.key);
            if(!current||current->raw_json!=result.raw)continue;
            if(result.valid)verified_math.insert(result.key);
            for(std::size_t i=0;i<controls.size();++i)if(key(i)==result.key){
                auto& vm=controls[i];if(!vm.math)continue;
                vm.response.correct=result.valid;
                if(result.valid||(result.compatible&&!current->correct)){
                    const auto reply=JsonObject::Parse(winrt::to_hstring(result.reply));
                    if(result.valid||!reply.GetNamedBoolean(L"correct",false))feedback(i,winrt::to_string(reply.GetNamedString(L"feedback",L"")),result.valid);
                }
                else feedback(i,ui("Comprueba de nuevo esta respuesta guardada. Se conservan todos sus datos.",
                    "Check this saved answer again. All its data has been kept.","Vérifie à nouveau cette réponse sauvegardée. Toutes ses données sont conservées."),false);
                sync_quiz_controls(i);
            }
        }
        if(!restored.empty())mark_dirty();
        update_navigation();notify_content();
    }
    void capture_composition(std::size_t index){
        using namespace winrt::Windows::Data::Json;
        auto& vm=controls.at(index);
        auto raw=vm.response.raw_json.empty()?JsonObject():JsonObject::Parse(winrt::to_hstring(vm.response.raw_json));
        if(vm.fill){
            const auto current=JsonObject::Parse(winrt::to_hstring(vm.fill->response_json()));
            const auto field=block(index).type==BlockType::drag_fill_code?L"slots":L"answers";
            raw.Insert(field,current.GetNamedArray(field));
        }
        if(vm.math)raw.Insert(L"responses",JsonObject::Parse(winrt::to_hstring(vm.math->answers_json())));
        vm.response.raw_json=winrt::to_string(raw.Stringify());
    }
    void composition_changed(std::size_t index){
        if(updating||index>=controls.size())return;
        if(checking_math==index)cancel_math();
        verified_math.erase(key(index));
        auto& vm=controls[index];++vm.revision;capture_composition(index);
        vm.response.correct=false;state::clear_response_review(vm.response);
        state::put_response(data,key(index),vm.response);mark_dirty();feedback(index,"",false);sync_quiz_controls(index);
        reflow();notify_content();
    }
    void check_math(std::size_t index){
        if(!hooks.validate_math){feedback(index,ui("La comprobación matemática no está disponible.",
            "Mathematical checking is unavailable.","La vérification mathématique est indisponible."),false);return;}
        if(!may_evaluate())return;
        cancel_math();auto& vm=controls.at(index);capture_composition(index);
        state::put_response(data,key(index),vm.response);mark_dirty();flush();
        const auto revision=vm.revision;const auto raw=block(index).raw_json,answers=vm.math->answers_json(),locale=lesson.locale;
        auto evaluate=hooks.validate_math;checking_math=index;
        EnableWindow(GetDlgItem(pane,control_id(index,3)),FALSE);
        math_worker=std::jthread([this,index,revision,raw,answers,locale,evaluate](std::stop_token stop){
            MathResult result{index,revision};
            try{result.reply=evaluate(raw,answers,locale,stop);}catch(const std::exception& error){result.error=error.what();}
            if(stop.stop_requested())return;
            {std::lock_guard lock(math_mutex);math_result=std::move(result);}
            PostMessageW(window,math_result_message,0,0);
        });
    }
    void finish_math(){
        using namespace winrt::Windows::Data::Json;
        std::optional<MathResult> result;{std::lock_guard lock(math_mutex);result=std::move(math_result);math_result.reset();}
        if(!result||checking_math!=result->block)return;
        const auto index=result->block;checking_math.reset();if(math_worker.joinable())math_worker.join();
        auto& vm=controls.at(index);EnableWindow(GetDlgItem(pane,control_id(index,3)),TRUE);
        if(result->revision!=vm.revision)return;
        if(!result->error.empty()){feedback(index,result->error,false);return;}
        const auto reply=JsonObject::Parse(winrt::to_hstring(result->reply));
        const auto text=winrt::to_string(reply.GetNamedString(L"feedback",L""));
        if(!reply.GetNamedBoolean(L"evaluable",false)){feedback(index,text,false);return;}
        vm.response.correct=reply.GetNamedBoolean(L"correct",false);
        record_attempt(index,vm.response.correct);
        if(vm.response.correct)verified_math.insert(key(index));else verified_math.erase(key(index));
        auto saved=JsonObject::Parse(winrt::to_hstring(vm.response.raw_json));
        if(reply.HasKey(L"contentFingerprint"))saved.Insert(L"contentFingerprint",reply.GetNamedValue(L"contentFingerprint"));
        vm.response.raw_json=winrt::to_string(saved.Stringify());
        state::mark_response_reviewed(vm.response,block(index));state::put_response(data,key(index),vm.response);mark_dirty();flush();
        feedback(index,text,vm.response.correct);sync_quiz_controls(index);
        if(hooks.feedback_changed)hooks.feedback_changed(window,index,vm.response.correct);
    }
    void feedback(std::size_t index, std::string_view text, bool correct,
                  bool generated_verdict = false, bool uses_default_review = false) {
        if(laboratory())return;
        const auto previous_update = updating; updating = true;
        std::string formatted(text);
        const auto verdict=verdict_text(correct);
        if(generated_verdict&&formatted.starts_with(verdict)) {
            auto explanation=formatted.substr(verdict.size());
            while(!explanation.empty()&&(explanation.front()=='\n'||explanation.front()=='\r'))explanation.erase(explanation.begin());
            formatted=(correct?"✓ ":"✗ ")+verdict+(explanation.empty()?"":"\n\n"+explanation);
        }
        text=formatted;
        controls.at(index).feedback_source = std::string(text); controls.at(index).feedback_correct = correct;
        controls.at(index).feedback_generated_verdict = generated_verdict;
        controls.at(index).feedback_default_review = uses_default_review;
        const auto colors = reader_theme::palette(window);
        const auto bg = reader_theme::background(controls.at(index).feedback);
        const bool dark = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) < 384;
        const auto ink = colors.high_contrast ? colors.text
            : correct ? (dark ? RGB(180,220,190) : RGB(20,112,48))
                      : (dark ? (block(index).type==BlockType::math_puzzle?RGB(240,166,166):RGB(240,150,150))
                              : RGB(160,35,35));
        reader_theme::set_text_color(controls.at(index).feedback,ink);
        set_rich_text(controls.at(index).feedback, text);
        (void)set_native_math_inline_text(controls.at(index).feedback,text);
        updating = previous_update; update_navigation(); reflow(); notify_content();
    }
    bool can_advance(std::size_t index) const {
        const auto& candidate = lesson.pages.at(index);
        const auto proofs = completion_snapshot();
        for (const auto& item : candidate.blocks) {
            if (item.type == BlockType::cpp_compiler && !activity_available(item,proofs)) return false;
            if (!exercise(item.type)) continue;
            if(item.type==BlockType::math_puzzle&&!verified_math.contains(curriculum::exercise_key(lesson,item)))return false;
            const auto response = state::get_response(data, curriculum::exercise_key(lesson, item));
            if (!response || !response->correct) return false;
        }
        return true;
    }
    bool can_enter(std::size_t index) const {
        const auto proofs = completion_snapshot();
        return std::ranges::all_of(lesson.pages.at(index).blocks,[&](const auto& item) {
            // A same-page prerequisite must remain reachable so the student
            // can complete it there. Only earlier pages gate page entry;
            // can_advance still requires every declared prerequisite.
            for (const auto& position : curriculum::activity_prerequisites(lesson,item)) {
                if (position.page < index) {
                    const auto found = proofs.find(lesson.pages[position.page].blocks[position.block].stable_id);
                    if (found == proofs.end() || !found->second) return false;
                }
            }
            return true;
        });
    }
    void update_navigation() {
        if (page_status && title) {
            auto label = hooks.lesson_label.empty() || hooks.lesson_label == "Lección"
                ? ui("Lección","Lesson","Leçon") : hooks.lesson_label;
            for (const auto prefix : {std::string_view("Lección "),std::string_view("Lesson "),std::string_view("Leçon ")}) {
                if (!label.starts_with(prefix)) continue;
                const auto ordinal = std::string_view(label).substr(prefix.size());
                if (!ordinal.empty() && std::ranges::all_of(ordinal,[](char c) { return c>='0'&&c<='9'; }))
                    label = ui("Lección ","Lesson ","Leçon ") + std::string(ordinal);
                break;
            }
            const auto caption = label + "  " + std::to_string(page + 1) + ui(" de "," of "," sur ") + std::to_string(lesson.pages.size());
            set_rich_text(title,caption);
            CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_BOLD|CFM_COLOR|CFM_SIZE;
            const auto colors=reader_theme::palette(title);
            format.dwEffects=CFE_BOLD;format.yHeight=18*15;format.crTextColor=colors.high_contrast?colors.text:RGB(255,255,255);
            SendMessageW(title,EM_SETSEL,0,static_cast<LPARAM>(wide(label).size()));SendMessageW(title,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
            format.dwEffects=0;format.yHeight=15*15;format.crTextColor=colors.high_contrast?colors.text:RGB(174,180,190);
            SendMessageW(title,EM_SETSEL,wide(label).size(),-1);SendMessageW(title,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(title,EM_SETSEL,0,0);
            SendMessageW(page_status,PBM_SETPOS,page + 1,0);
        }
        if(const auto next=GetDlgItem(pane,continue_id)){
            SetWindowTextW(next,wide(page+1==lesson.pages.size()?ui("Finalizar lección","Finish lesson","Terminer la leçon"):ui("Continuar","Continue","Continuer")).c_str());
            EnableWindow(next,can_advance(page));
        }
    }
    bool can_visit(std::size_t value) const {
        if (value >= lesson.pages.size()) return false;
        if (value <= page) return true;
        for (std::size_t required = page; required <= value; ++required)
            if (!can_enter(required)) return false;
        for (std::size_t required = page; required < value; ++required)
            if (!can_advance(required)) return false;
        return true;
    }
    LessonPageState page_state() const {
        LessonPageState result{page,lesson.pages.size(),page > 0,page + 1 < lesson.pages.size() && can_visit(page + 1),
            lesson.id,lesson.pages.at(page).stable_id,lesson.locale};
        result.narration_language=lesson.pages.at(page).narration_language.value_or(lesson.narration_language.value_or(""));
        result.lesson_title=lesson.title;
        const auto base=route+'/'+lesson.id;
        result.narration_page_identity=base+'/'+std::to_string(page);
        if(lesson.state_layout.version){
            const auto& record=lesson.state_layout.current.at(page);auto order=record.id;
            for(const auto& id:record.blocks)order+=','+id;
            result.narration_scope=base+'/'+lesson.state_layout.lineage;
            result.narration_page_identity=base+"/state-layout-v1/"+lesson.state_layout.lineage+'/'+state::sha256(order);
        }
        return result;
    }
    void sync_quiz_controls(std::size_t index) {
        auto& vm = controls.at(index);
        if (!exercise(block(index).type)) return;
        const bool editable = !vm.response.correct;
        if (vm.answer) EnableWindow(vm.answer, editable);
        if(vm.fill)vm.fill->set_enabled(editable);
        if(vm.math)vm.math->set_enabled(editable);
        for (auto choice : vm.choices) EnableWindow(choice, editable);
        bool changed=vm.response.correct||vm.response.selected>=0;
        if(vm.answer_editor)changed=changed||!vm.answer_editor->document().text.empty();
        if(vm.fill){
            if(block(index).type==BlockType::inline_fill_code)
                changed=changed||std::ranges::any_of(vm.fill->answers(),[](const auto& text){return !text.empty();});
            else changed=changed||std::ranges::any_of(vm.fill->slots(),[](const auto slot){return slot!=std::numeric_limits<std::size_t>::max();});
        }
        if(!vm.order.empty())changed=changed||!std::ranges::is_sorted(vm.order);
        if(const auto reset=GetDlgItem(pane,control_id(index,12)))EnableWindow(reset,changed);
        for (std::size_t position = 0; position < vm.order.size(); ++position) {
            EnableWindow(GetDlgItem(pane, control_id(index, 20 + static_cast<int>(position) * 3)), editable && position > 0);
            EnableWindow(GetDlgItem(pane, control_id(index, 21 + static_cast<int>(position) * 3)), editable && position + 1 < vm.order.size());
        }
        const auto check=GetDlgItem(pane,control_id(index,3));
        SetWindowTextW(check,wide(editable ? ui("Comprobar","Check","Vérifier") : ui("Correcto ✓","Correct ✓","Correct ✓")).c_str());
        EnableWindow(check,editable);
    }
    void refresh_order(std::size_t index) {
        const auto previous_update = updating; updating = true;
        const auto& vm = controls.at(index);
        for (std::size_t position = 0; position < vm.order.size(); ++position)
            set_rich_text(GetDlgItem(pane, control_id(index, 22 + static_cast<int>(position) * 3)),
                block(index).fragments.at(vm.order[position]), {}, true);
        updating = previous_update; sync_quiz_controls(index); reflow();
        notify_content();
    }
    void create_feedback(std::size_t index) {
        auto& vm = controls[index];
        vm.feedback = rich("", control_id(index, 4));
        rows.push_back({{vm.feedback}, {0}, 0, true});
        rows.back().padding_before=12;rows.back().padding_after=24;
        if (vm.response.correct) {
            auto explanation = block(index).explanation;
            feedback(index, verdict_text(true) + (explanation.empty() ? "" : "\n" + explanation), true, true);
        } else if (exercise(block(index).type) && state::response_reviewed(vm.response, block(index))) {
            const auto& current = block(index);
            // Restoring a reviewed attempt is not a new submission. Never
            // attach a success explanation to an imported incorrect attempt.
            const auto review=curriculum::incorrect_feedback(current,vm.response.selected<0?std::nullopt:std::optional<std::size_t>(vm.response.selected));
            feedback(index, verdict_text(false) + '\n' + (review.empty()
                ? default_review() : review), false, true, review.empty());
        }
        sync_quiz_controls(index);
    }
    void render_block(std::size_t index) {
        const auto& current = block(index); auto& vm = controls[index];
        // Locked references have no HWND, text provider or scrollable row.
        if (!reference_available(current)) return;
        vm.rendered = true;
        if (current.type == BlockType::cpp_compiler && !activity_available(current)) {
            vm.locked = true;
            text_row(ui("Completa y comprueba correctamente la actividad anterior requerida para abrir este editor.",
                "Complete and correctly check the required previous activity to open this editor.",
                "Termine et vérifie correctement l'activité précédente requise pour ouvrir cet éditeur."),control_id(index,18));
            return;
        }
        if (exercise(current.type))
            if (const auto saved = state::get_response(data, key(index))) vm.response = *saved;
        if(current.type==BlockType::math_puzzle&&!verified_math.contains(key(index)))vm.response.correct=false;
        if (current.type == BlockType::heading) { text_row(current.text, control_id(index, 0), false, current.emphasis, RichTextRole::heading); return; }
        if (current.type == BlockType::note) text_row(ui("Nota","Note","Note"),control_id(index,19),false,{},RichTextRole::heading);
        if (current.type == BlockType::paragraph || current.type == BlockType::note) { text_row(current.text, control_id(index, 0), false, current.emphasis); return; }
        if (current.type == BlockType::code) {
            simulated_header(index,false);vm.simulated_code=rich(current.code,control_id(index,1),true);apply_wrap(vm.simulated_code);
            rows.push_back({{vm.simulated_code},{0},26,true});rows.back().simulated_card=index;rows.back().card_end=true;return;
        }
        if(current.type==BlockType::math_puzzle){
            std::string saved="{}";
            if(!vm.response.raw_json.empty()){
                const auto old=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(vm.response.raw_json));
                if(old.HasKey(L"responses"))saved=winrt::to_string(old.GetNamedObject(L"responses").Stringify());
            }
            vm.math=std::make_unique<MathPuzzleView>(pane,current.raw_json,lesson.locale,saved,
                MathPuzzleCallbacks{[this,index]{composition_changed(index);},[this,index](std::string){check_answer(index);},
                    [this]{if(!updating){reflow();reveal_math_input(GetFocus());}},[this](std::string_view request){
                        if(!hooks.math_editor_request)throw std::runtime_error("El editor matemático no tiene conectado su modelo de edición.");
                        return hooks.math_editor_request(request);
                    },[this](HWND field,bool focused){
                        if(!focused){hide_math_keyboard(field);reflow();return;}
                        if(hooks.math_editor_request){show_math_keyboard(field,hooks.math_editor_request,math_field_keyboard_context(field),lesson.locale);reflow();reveal_math_input(field);}
                    },[this](HWND field){
                        if(hooks.calculator_requested)hooks.calculator_requested(window,field);
                    },[this](HWND origin,UINT message,WPARAM keys,LPARAM point){
                        return drag_message(origin,message,keys,point);
                    },[this]{
                        (void)navigate_back();
                    }});
            SetWindowLongPtrW(vm.math->window(),GWLP_ID,control_id(index,5));
            rows.push_back({{vm.math->window()},{0},40,false});
            auto check=button(ui("Comprobar","Check","Vérifier"),control_id(index,3),{0,ActionType::check,index});
            rows.push_back({{check},{0},48,false});create_feedback(index);return;
        }
        if (!current.question.empty()) text_row(current.question, control_id(index, 0), false, current.emphasis, RichTextRole::heading);
        const bool simulated=exercise(current.type);
        if(simulated)simulated_header(index,true);
        if (!current.code.empty()){
            if(simulated){vm.simulated_code=rich(current.code,control_id(index,1),true);apply_wrap(vm.simulated_code);
                rows.push_back({{vm.simulated_code},{0},26,true});rows.back().simulated_card=index;}
            else text_row(current.code,control_id(index,1),true);
        }
        if (current.type == BlockType::cpp_compiler) {
            auto caption = rich("</> C++", control_id(index, 16),false,{},false,RichTextRole::prompt);
            reader_theme::apply(caption,reader_theme::Surface::card_header); set_rich_text(caption,"</> C++",{},false,RichTextRole::prompt);
            auto reset = icon_button(ui("Reiniciar código","Reset code","Réinitialiser le code"),control_id(index,12),{0,ActionType::reset,index},Icon::reset);
            auto copy = icon_button(ui("Copiar código","Copy code","Copier le code"),control_id(index,18),{0,ActionType::copy_source,index},Icon::copy);
            auto expand = icon_button(ui("Pantalla completa","Full screen","Plein écran"),control_id(index,20),{0,ActionType::fullscreen_source,index},Icon::expand);
            const auto spacer=control(L"STATIC","",0,control_id(index,33));
            if(laboratory()){
                const auto compiler=button("MSVC ▾",control_id(index,36),{0,ActionType::compiler_info,index});reader_theme::icon_button(compiler);
                const auto wrap=icon_button(code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé"):ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"),control_id(index,35),{0,ActionType::toggle_wrap,index},code_wrap()?Icon::wrap_on:Icon::wrap_off);
                rows.push_back({{caption,reset,compiler,spacer,wrap,copy,expand},{92,44,72,0,44,44,44},64,false});
            }else{
                auto options = icon_button(ui("Opciones del editor","Editor options","Options de l'éditeur"),control_id(index,19),{0,ActionType::editor_menu,index},Icon::settings);
                rows.push_back({{caption,reset,spacer,copy,expand,options},{100,44,0,44,44,44},48,false});
            }
            rows.back().toolbar=true;
            if(laboratory())rows.back().padding_after=0;
            for(const auto item:rows.back().cells)reader_theme::apply(item,laboratory()?reader_theme::Surface::compiler_header:reader_theme::Surface::card_header);
            toolbar_caption(caption,"</> C++");
            const auto saved = state::get_draft(data, draft_key(index), current.initial_code, current.standard_input.value_or(""));
            auto document = saved.has_saved
                ? windows::EditorDocument{saved.source, state::indentation_units(saved.source, saved.indentation_json), saved.selection_start, saved.selection_end}
                : predefined_document(current.initial_code);
            vm.source = rich("", control_id(index, 10), true, {}, true);
            vm.source_editor = std::make_unique<CodeEditor>(vm.source, std::move(document), true);
            if(laboratory())windows::set_code_gutter_appearance(vm.source,RGB(24,26,31),RGB(111,119,130),3);
            apply_wrap(vm.source);
            if(laboratory()){reader_theme::apply(vm.source,reader_theme::Surface::compiler_editor);reader_theme::set_text_color(vm.source,RGB(230,237,243));}
            style_cpp(vm.source);
            rows.push_back({{vm.source}, {0}, laboratory()?200:96, !laboratory(),0,laboratory()?0:320});
            if(laboratory())rows.back().padding_after=20;
            if (current.standard_input) {
                text_row(ui("Entrada","Input","Entrée"), control_id(index, 17));
                vm.input = rich("", control_id(index, 15), false, {}, true);
                set_edit_text(vm.input, saved.input);
                vm.input_original = saved.input; vm.input_visual = edit_text(vm.input);
                rows.push_back({{vm.input}, {0}, 74, false});
            }
            vm.compile = button(current.output_check ? ui("Compilar, ejecutar y comprobar","Compile, run and check","Compiler, exécuter et vérifier")
                : ui("Compilar y ejecutar","Compile and run","Compiler et exécuter"), control_id(index, 11), {0, ActionType::run, index});
            rows.push_back({{vm.compile}, {0}, 48, false});
            if(laboratory()){
                rows.back().horizontal_inset=24;rows.back().padding_after=20;reader_theme::primary_button(vm.compile);
                vm.status=rich("",control_id(index,34));rows.push_back({{vm.status},{0},20,true});rows.back().horizontal_inset=24;rows.back().padding_after=10;
                vm.console=rich("",control_id(index,14),true);reader_theme::apply(vm.console,reader_theme::Surface::compiler_output);
                SendMessageW(vm.console,EM_SETTARGETDEVICE,0,0);rows.push_back({{vm.console},{0},120,true});rows.back().horizontal_inset=24;
                laboratory_text_style(vm.console,18,RGB(121,226,157));refresh_laboratory_status();return;
            }
            auto console_title = rich(console_label(0),control_id(index,30));
            reader_theme::apply(console_title,reader_theme::Surface::header); set_rich_text(console_title,console_label(0));
            auto console_copy = icon_button(ui("Copiar salida","Copy output","Copier la sortie"),control_id(index,23),{0,ActionType::copy_console,index},Icon::copy);
            auto console_clear = icon_button(ui("Limpiar salida","Clear output","Effacer la sortie"),control_id(index,24),{0,ActionType::clear_console,index},Icon::reset);
            auto console_expand = icon_button(ui("Ampliar consola","Expand console","Agrandir la console"),control_id(index,25),{0,ActionType::fullscreen_console,index},Icon::expand);
            auto console_menu = icon_button(ui("Opciones de la consola","Console options","Options de la console"),control_id(index,31),{0,ActionType::console_menu,index},Icon::settings);
            rows.push_back({{console_title,console_copy,console_clear,console_expand,console_menu},{0,44,44,44,44},48,true});
            rows.back().toolbar=true;
            for(const auto item:rows.back().cells)reader_theme::apply(item,reader_theme::Surface::card_header);
            toolbar_caption(console_title,console_label(0));
            vm.console = rich("", control_id(index, 14), true);
            SetWindowLongPtrW(vm.console,GWL_STYLE,GetWindowLongPtrW(vm.console,GWL_STYLE) | WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL);
            SendMessageW(vm.console,EM_SETTARGETDEVICE,0,1);
            rows.push_back({{vm.console}, {0}, 52, true,0,240});
            for (const auto& [part,type] : std::array<std::pair<int,ActionType>,9>{{
                {70,ActionType::copy_source},{71,ActionType::copy_console},{72,ActionType::clear_console},
                {73,ActionType::undo},{74,ActionType::redo},{75,ActionType::fullscreen_source},{76,ActionType::fullscreen_console},
                {78,ActionType::select_stdout},{79,ActionType::select_stderr}}}) actions.push_back({control_id(index,part),type,index});
            actions.push_back({control_id(index,80),ActionType::select_diagnostics,index});
            create_feedback(index); return;
        }
        if(current.type==BlockType::inline_fill_code||current.type==BlockType::drag_fill_code){
            vm.fill=std::make_unique<CppFillView>(pane,current,vm.response.raw_json,
                [this,index]{composition_changed(index);},[this,index]{check_answer(index);});
            SetWindowLongPtrW(vm.fill->window(),GWLP_ID,control_id(index,5));
            rows.push_back({{vm.fill->window()},{0},80,false});
        } else if (current.type == BlockType::multiple_choice) {
            for (std::size_t option = 0; option < current.options.size(); ++option) {
                const auto radio = control(L"BUTTON", "", BS_RADIOBUTTON | WS_TABSTOP,
                    control_id(index, 20 + static_cast<int>(option) * 2));
                label_radio(radio,current.options[option]);
                actions.push_back({control_id(index, 20 + static_cast<int>(option) * 2), ActionType::choose, index, option});
                SendMessageW(radio, BM_SETCHECK, vm.response.selected == static_cast<std::int64_t>(option) ? BST_CHECKED : BST_UNCHECKED, 0);
                vm.choices.push_back(radio);
                auto label = rich(current.options[option], control_id(index, 21 + static_cast<int>(option) * 2));
                reader_theme::apply(radio,reader_theme::Surface::card);reader_theme::apply(label,reader_theme::Surface::card);
                rows.push_back({{radio, label}, {28, 0}, 28, true});
                rows.back().simulated_card=index;rows.back().card_end=option+1==current.options.size();
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
            if (current.type == BlockType::fill_code) apply_wrap(vm.answer);
            rows.push_back({{vm.answer}, {0}, multiline ? 82 : 36, false});
        }
        if(simulated){
            const auto first=std::ranges::find_if(rows,[index](const auto& row){return row.simulated_card==index&&row.card_header;});
            for(auto it=first;it!=rows.end();++it)it->simulated_card=index;
            rows.back().card_end=true;
        }
        auto check = button(ui("Comprobar","Check","Vérifier"), control_id(index, 3), {0, ActionType::check, index});
        rows.push_back({{check}, {0}, 48, false}); create_feedback(index);
    }
    void render_page() {
        cancel_math();
        end_drag(); fullscreen_block.reset();
        updating = true;
        controls.clear();
        for (const auto& row : rows) for (auto item : row.cells) destroy_control(item);
        rows.clear(); actions.clear();simulated_card_surfaces.clear();
        controls.resize(lesson.pages.at(page).blocks.size());
        if(page==0){
            text_row(laboratory()?laboratory_title():lesson.title,lesson_heading_id,false,{},RichTextRole::lesson_title);
            if(laboratory()){
                rows.back().horizontal_inset=24;rows.back().padding_after=8;laboratory_text_style(GetDlgItem(pane,lesson_heading_id),28,RGB(255,255,255),true);
                text_row(laboratory_description(),lesson_heading_id+1);rows.back().horizontal_inset=24;rows.back().padding_after=24;laboratory_text_style(GetDlgItem(pane,lesson_heading_id+1),18,RGB(174,180,190));
            }
        }
        for (std::size_t index = 0; index < controls.size(); ++index) render_block(index);
        if(lesson.id!="native_cpp_laboratory"){
            const auto next=control(L"BUTTON","",WS_TABSTOP|BS_PUSHBUTTON,continue_id);
            rows.push_back({{next},{0},58,false});rows.back().padding_before=8;rows.back().padding_after=32;
            rows.back().horizontal_inset=24;
        }
        scroll = px(static_cast<int>(state::integer_value(data, "native_windows", scroll_key())));
        updating = false; update_fonts(); update_navigation(); reflow();
        if (editing_bar) editing_bar->refresh();
        notify_content();
    }
    void resize() {
        if (!ready) return;
        RECT area{}; GetClientRect(window, &area);
        const int margin = px(4), available = static_cast<int>(area.right) - margin * 2;
        MoveWindow(back,margin,px(12),px(48),px(48),TRUE);
        int header_right = static_cast<int>(area.right) - margin - px(48);
        for (const auto item : {settings,attempts,language,reader}) {
            const int width=item==attempts?px(69):px(48);
            header_right-=width-px(48);
            MoveWindow(item,header_right,px(4),width,item==attempts?px(64):px(56),TRUE); header_right -= px(48);
        }
        MoveWindow(title,margin + px(58),px(10),std::max(px(60),header_right - margin - px(54)),px(56),TRUE);
        MoveWindow(page_status,0,px(72),static_cast<int>(area.right),px(5),TRUE);
        const int viewport_top=fullscreen_block?0:px(laboratory()?48:lesson_header_height);
        for(const auto item:{back,title,page_status,settings,attempts,language,reader})ShowWindow(item,fullscreen_block||(laboratory()&&item!=back)?SW_HIDE:SW_SHOWNOACTIVATE);
        if(laboratory())MoveWindow(back,px(4),0,px(48),px(48),TRUE);
        ShowWindow(fullscreen_close,fullscreen_block?SW_SHOWNOACTIVATE:SW_HIDE);
        SetWindowPos(fullscreen_close,HWND_TOP,area.right-px(52),px(4),px(48),px(48),SWP_NOACTIVATE);
        const int nav_top = static_cast<int>(area.bottom);
        if (reader_bar && IsWindow(reader_bar)) {
            const RECT floating_area{margin,viewport_top,std::max<LONG>(margin,area.right-margin),std::max<LONG>(viewport_top,area.bottom-px(4))};
            layout_lesson_reader_bar(reader_bar,floating_area);
        }
        const bool assisted = editing_bar && editing_bar->visible() && !import_open;
        const int bar_height = editing_bar ? editing_bar->logical_height() : 0;
        const int editing_height = assisted ? px(bar_height + 8) : 0;
        if (editing_bar) editing_bar->place(std::max(margin,(static_cast<int>(area.right) - std::min(available,px(1050))) / 2),
            nav_top - px(bar_height + 12),std::min(available,px(1050)),px(bar_height));
        MoveWindow(pane,0,viewport_top,static_cast<int>(area.right),std::max(px(18),nav_top - viewport_top - editing_height),TRUE);
        if(fullscreen_block)SetWindowPos(fullscreen_close,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        if (import_open) layout_import();
        reflow();
    }
    void open_menu() {
        const auto popup = CreatePopupMenu();
        if (!popup) throw std::runtime_error("Cannot open the lesson menu");
        AppendMenuW(popup,MF_STRING,import_id,wide(ui("Importar estado Android…","Import Android state…","Importer l'état Android…")).c_str());
        if (hooks.settings_requested) AppendMenuW(popup,MF_STRING,settings_id,wide(ui("Ajustes de lectura y apariencia…",
            "Reading and appearance settings…","Réglages de lecture et d'apparence…")).c_str());
        const auto pages = CreatePopupMenu();
        if (!pages) { DestroyMenu(popup); throw std::runtime_error("Cannot open the page menu"); }
        for (std::size_t index = 0; index < lesson.pages.size(); ++index) {
            const auto caption = wide(ui("Página ","Page ","Page ")) + std::to_wstring(index + 1);
            AppendMenuW(pages,MF_STRING | (index == page ? MF_CHECKED : 0) | (can_visit(index) ? MF_ENABLED : MF_GRAYED),
                pages_menu_base + index,caption.c_str());
        }
        AppendMenuW(popup,MF_POPUP,reinterpret_cast<UINT_PTR>(pages),wide(ui("Páginas","Pages","Pages")).c_str());
        AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
        AppendMenuW(popup,MF_STRING,page_top_id,wide(ui("Ir al principio de la página","Go to the top of the page","Aller au début de la page")).c_str());
        AppendMenuW(popup,MF_STRING,page_bottom_id,wide(ui("Ir al final de la página","Go to the bottom of the page","Aller à la fin de la page")).c_str());
        if(page+1==lesson.pages.size()&&hooks.lesson_completed)AppendMenuW(popup,MF_STRING|(can_advance(page)?MF_ENABLED:MF_GRAYED),finish_id,
            wide(ui("Finalizar lección","Finish lesson","Terminer la leçon")).c_str());
        RECT bounds{}; GetWindowRect(menu,&bounds);
        const auto style = prepare_native_menu(popup,window);
        const auto selected = TrackPopupMenuEx(popup,TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
            bounds.right,bounds.bottom,window,nullptr);
        DestroyMenu(popup);
        if (selected) command(static_cast<int>(selected),BN_CLICKED,menu);
    }
    void reflow() {
        if (!pane) return;
        if (laying_out) { layout_pending = true; return; }
        laying_out = true;
        struct LayoutGuard { bool& value; ~LayoutGuard() { value = false; } } guard{laying_out};
        RECT area{}; GetClientRect(pane, &area);
        const int margin = 0, gap = px(8);
        const int width = std::max(px(100), static_cast<int>(area.right) - margin * 2);
        const int left = std::max(margin, (static_cast<int>(area.right) - width) / 2);
        struct Placement { HWND item; int x, y, width, height; };
        std::vector<Placement> placements;
        simulated_card_surfaces.clear();toolbar_surfaces.clear();std::map<std::size_t,std::size_t> card_surfaces;
        // A RichEdit's requested height depends on its final width. Measure
        // without moving/painting provisional rectangles over adjacent rows.
        constexpr UINT placement_flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW;
        int top = fullscreen_block?px(56):px(24);
        for (auto& row : rows) {
            bool shown = !fullscreen_block || row_block(row) == *fullscreen_block;
            if(shown&&fullscreen_block&&exercise(block(*fullscreen_block).type)){
                const int part=(GetDlgCtrlID(row.cells.front())-1000)%100;
                shown=part!=0&&part!=3&&part!=4;
            }
            if (shown && fullscreen_block && fullscreen_output) {
                const int part = (GetDlgCtrlID(row.cells.front()) - 1000) % 100;
                shown = part == 14 || part == 30;
            }
            if(shown&&fullscreen_block&&!fullscreen_output&&block(*fullscreen_block).type==BlockType::cpp_compiler){
                const int part=(GetDlgCtrlID(row.cells.front())-1000)%100;
                shown=part==16||part==10;
            }
            for (const auto item : row.cells)
                if (((GetWindowLongPtrW(item,GWL_STYLE) & WS_VISIBLE) != 0) != shown)
                    ShowWindow(item,shown ? SW_SHOWNOACTIVATE : SW_HIDE);
            if (!shown) continue;
            if(row.padding_before&&(!row.size_to_text||GetWindowTextLengthW(row.cells.front())>0))top+=px(row.padding_before);
            const int inset=row.simulated_card?px(row.card_header?14:18):px(row.horizontal_inset),row_width=width-inset*2;
            int fixed = 0; for (auto value : row.widths) if (value) fixed += px(value);
            const int remaining = std::max(px(30), row_width - fixed - gap * (static_cast<int>(row.cells.size()) - 1));
            if(row.cells.size()==1){
                const auto index=row_block(row);
                if(index<controls.size()){
                    auto& vm=controls[index];
                    if(vm.math&&row.cells[0]==vm.math->window())row.logical_height=MulDiv(vm.math->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                    if(vm.fill&&row.cells[0]==vm.fill->window())row.logical_height=MulDiv(vm.fill->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                }
            }
            if(laboratory()&&!controls.empty()&&row.cells.front()==controls[0].source&&!fullscreen_block){
                POINTL first{},second{};SendMessageW(row.cells.front(),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
                const auto next_line=SendMessageW(row.cells.front(),EM_LINEINDEX,1,0);if(next_line>=0)SendMessageW(row.cells.front(),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second),next_line);
                const auto line_height=next_line>=0?std::max<LONG>(1,second.y-first.y):px(21*content_text_scale()/100);
                row.logical_height=std::max(200,MulDiv(line_height*10,96,static_cast<int>(GetDpiForWindow(window)))+32);row.requested_height=0;
            }
            int x = left+inset;
            int control_height=0;
            const auto provisional = std::max(px(row.logical_height), row.requested_height);
            for (std::size_t i = 0; i < row.cells.size(); ++i) {
                const int cell_width = row.widths[i] ? px(row.widths[i]) : remaining;
                SetWindowPos(row.cells[i], nullptr, 0, 0, cell_width, std::max(px(1), provisional),
                    placement_flags | SWP_NOMOVE);
                if(laboratory()&&!controls.empty()&&(row.cells[i]==controls[0].source||row.cells[i]==controls[0].console)){
                    RECT content{};SendMessageW(row.cells[i],EM_GETRECT,0,reinterpret_cast<LPARAM>(&content));const bool output=row.cells[i]==controls[0].console;
                    content.left=output?px(18):std::max<LONG>(content.left,px(10));content.top=px(output?18:16);content.right=cell_width-px(output?18:16);content.bottom=std::max<LONG>(content.top+1,provisional-px(output?18:16));
                    SendMessageW(row.cells[i],EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
                }
                if (row.size_to_text) SendMessageW(row.cells[i], EM_REQUESTRESIZE, 0, 0);
                wchar_t name[32]{};GetClassNameW(row.cells[i],name,32);
                if(_wcsicmp(name,L"BUTTON")==0&&GetWindowTextLengthW(row.cells[i])>0){
                    const auto dc=GetDC(row.cells[i]);const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(row.cells[i],WM_GETFONT,0,0)));
                    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(row.cells[i]))+1,L'\0');
                    text.resize(static_cast<std::size_t>(GetWindowTextW(row.cells[i],text.data(),static_cast<int>(text.size()))));
                    RECT measured{0,0,std::max(1,cell_width-px(14)),0};DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                    control_height=std::max(control_height,static_cast<int>(measured.bottom)+px(12));SelectObject(dc,old);ReleaseDC(row.cells[i],dc);
                }else if(_wcsnicmp(name,L"RichEdit",8)==0&&!row.size_to_text){
                    CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(row.cells[i],EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
                    control_height=std::max(control_height,MulDiv(format.yHeight*content_text_scale(),static_cast<int>(GetDpiForWindow(window)),1440*100)+px(12));
                }
                x += cell_width + gap;
            }
            int height = row.size_to_text ? std::max(px(row.logical_height), row.requested_height) : px(row.logical_height);
            height=std::max(height,control_height);
            if (row.max_logical_height) height = std::min(height,px(row.max_logical_height));
            if (fullscreen_block && GetDlgCtrlID(row.cells.front()) == control_id(*fullscreen_block,14) && fullscreen_output)
                height = std::max(px(80),static_cast<int>(area.bottom) - top - px(16));
            if(fullscreen_block&&GetDlgCtrlID(row.cells.front())==control_id(*fullscreen_block,10)&&!fullscreen_output)
                height=std::max(px(80),static_cast<int>(area.bottom)-top-px(16));
            if(fullscreen_block&&block(*fullscreen_block).type==BlockType::code&&controls[*fullscreen_block].simulated_code==row.cells.front())
                height=std::max(px(80),static_cast<int>(area.bottom)-top-px(24));
            x = left+inset;
            if(row.toolbar)toolbar_surfaces.push_back({left,top,left+width,top+height});
            for (std::size_t i = 0; i < row.cells.size(); ++i) {
                const int cell_width = row.widths[i] ? px(row.widths[i]) : remaining;
                placements.push_back({row.cells[i],x,top,cell_width,std::max(0,height)});
                const int part=(GetDlgCtrlID(row.cells[i])-1000)%100;
                if((row.card_header||row.toolbar)&&(part==16||part==30)){
                    RECT content{laboratory()?px(14):0,px(laboratory()?20:12),cell_width,std::max(0,height)};
                    SendMessageW(row.cells[i],EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
                }
                x += cell_width + gap;
            }
            if(row.simulated_card){
                if(row.card_header){card_surfaces[*row.simulated_card]=simulated_card_surfaces.size();
                    simulated_card_surfaces.push_back({{left,top,left+width,top+height},top+height});}
                if(const auto found=card_surfaces.find(*row.simulated_card);found!=card_surfaces.end())
                    simulated_card_surfaces[found->second].bounds.bottom=top+height+(row.card_end?px(16):0);
            }
            top += height + (height ? px(row.simulated_card?(row.card_end?16:row.card_header?8:8):row.padding_after) : 0);
        }
        const auto math_keyboard=math_keyboard_window(GetAncestor(window,GA_ROOT));RECT keyboard_area{};
        if(math_keyboard&&IsWindowVisible(math_keyboard))GetWindowRect(math_keyboard,&keyboard_area);
        extent = top + px(16)+static_cast<int>(keyboard_area.bottom-keyboard_area.top);
        scroll = std::clamp(scroll, 0, std::max(0, extent - static_cast<int>(area.bottom)));
        // Commit only final, clamped positions. Suppress the default bit-copy:
        // reused client pixels are stale when several child rows move past
        // one another. Repaint parent and children once after every move.
        auto deferred = placements.empty() ? nullptr : BeginDeferWindowPos(static_cast<int>(placements.size()));
        if (deferred) {
            for (const auto& item : placements) {
                deferred = DeferWindowPos(deferred,item.item,nullptr,item.x,item.y - scroll,
                    item.width,item.height,placement_flags);
                if (!deferred) break;
            }
            if (deferred && !EndDeferWindowPos(deferred)) deferred = nullptr;
        }
        if (!deferred) for (const auto& item : placements)
            SetWindowPos(item.item,nullptr,item.x,item.y - scroll,item.width,item.height,placement_flags);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0; info.nMax = std::max(0, extent - 1); info.nPage = static_cast<UINT>(std::max<LONG>(0, area.bottom)); info.nPos = scroll;
        SetScrollInfo(pane, SB_VERT, &info, TRUE);
        ShowScrollBar(pane,SB_VERT,FALSE);
        RedrawWindow(pane,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        laying_out = false;
        if (layout_pending) { layout_pending = false; reflow(); }
    }
    void scroll_to(int position) {
        RECT area{}; GetClientRect(pane, &area);
        const int new_scroll = std::clamp(position, 0, std::max(0, extent - static_cast<int>(area.bottom)));
        if (new_scroll == scroll) return;
        scroll = new_scroll; remember_scroll(); mark_dirty(); reflow();
    }
    bool finish_lesson(){
        if(!hooks.lesson_completed)return false;
        for(std::size_t required=0;required<lesson.pages.size();++required)if(!can_advance(required))return false;
        data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=true;
        mark_dirty();flush();PostMessageW(window,finish_lesson_message,0,0);return true;
    }
    bool change_page(std::size_t value) {
        if (value == page) return true;
        if(value==lesson.pages.size()&&page+1==value&&hooks.lesson_completed){
            const auto separator=lesson.id.find("__");const auto course=separator==std::string::npos?std::string{}:lesson.id.substr(0,separator);
            return curriculum::learning_flow(data,course).swipe_finish&&finish_lesson();
        }
        if (!can_visit(value)) return false;
        // A selector jump cannot bypass a declared prerequisite, even when
        // old progress or an imported bookmark already reaches a later page.
        if (hooks.before_page_change) hooks.before_page_change(window);
        capture_page(); dirty = true; flush(); cancel_run();
        page = value; scroll = 0;
        state::save_current_page(data, lesson, page); mark_dirty(); render_page(); return true;
    }
    void reset_answer(std::size_t index) {
            auto& vm=controls.at(index);
            // Explicit reset affects only this quiz; compiler drafts and jobs stay intact.
            updating = true; vm.response = {};
            verified_math.erase(key(index));
            if (vm.answer_editor) vm.answer_editor->reset(windows::EditorDocument{});
            if(vm.fill)vm.fill->reset();
            if(vm.math)vm.math->reset();
            for (auto choice : vm.choices) SendMessageW(choice, BM_SETCHECK, BST_UNCHECKED, 0);
            if (!vm.order.empty()) std::iota(vm.order.begin(), vm.order.end(), 0);
            updating = false;
            if (!vm.order.empty()) refresh_order(index);
            state::put_response(data, key(index), vm.response); mark_dirty(); flush();
            feedback(index, "", false); sync_quiz_controls(index);
    }
    void check_answer(std::size_t index) {
        auto& vm = controls.at(index); const auto& current = block(index);
        if (vm.response.correct) return;
        curriculum::AnswerEvaluation result;
        if(current.type==BlockType::math_puzzle){check_math(index);return;}
        if(!may_evaluate())return;
        if(vm.fill){
            capture_composition(index);
            result=current.type==BlockType::drag_fill_code?curriculum::evaluate_drag_answers(current,vm.fill->slots())
                :curriculum::evaluate_hole_answers(current,vm.fill->answers());
        } else if (current.type == BlockType::multiple_choice) {
            if (vm.response.selected < 0) { feedback(index,ui("Selecciona una opción antes de comprobar.",
                "Select an option before checking.","Sélectionne une option avant de vérifier."),false); return; }
            result = curriculum::evaluate_choice(current, static_cast<std::size_t>(vm.response.selected));
        } else if (current.type == BlockType::order_code) {
            result = curriculum::evaluate_order(current, vm.order);
            vm.response.order.assign(vm.order.begin(), vm.order.end());
        } else {
            vm.response.answer = vm.answer_editor->document().text;
            result = curriculum::evaluate_text_answer(current, vm.response.answer);
        }
        vm.response.correct = result.correct;
        record_attempt(index,result.correct);
        state::mark_response_reviewed(vm.response, current);
        state::put_response(data, key(index), vm.response); mark_dirty(); flush();
        auto text = verdict_text(result.correct);
        if (!result.feedback.empty()) text += "\n" + result.feedback;
        else if (!result.correct) text += '\n' + default_review();
        feedback(index, text, result.correct, true, !result.correct && result.feedback.empty());
        sync_quiz_controls(index);
        if (hooks.feedback_changed) hooks.feedback_changed(window,index,result.correct);
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
                feedback(index, "", false);sync_quiz_controls(index);mark_dirty(); return;
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
                vm.console_channels = {};
                updating = true; set_rich_text(vm.console,ui("El programa cambió. Ejecuta de nuevo para obtener una salida actual.",
                    "The program changed. Run it again to obtain current output.","Le programme a changé. Exécute-le à nouveau pour obtenir une sortie actuelle."));
                if (sender == vm.source) style_cpp(vm.source);
                updating = false; mark_dirty(); return;
            }
        }
    }
    void run(std::size_t index) {
        cancel_run(); capture_draft(index); clear_reference_completion(index); mark_dirty(); flush();
        auto& vm = controls.at(index);
        vm.console_channels = {};
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
        if(laboratory()){laboratory_status=LaboratoryStatus::running;refresh_laboratory_status();}
        updating = true; set_rich_text(vm.console,laboratory()?"":ui("Compilando…","Compiling…","Compilation…")); updating = false;
        worker = std::jthread([this, request = std::move(request)](std::stop_token stop) {
            windows::CppRunResult result;
            try{
                auto priority=hooks.priority_work?hooks.priority_work(stop):std::shared_ptr<void>{};
                if(stop.stop_requested())return;
                result=windows::run_cpp(request, stop);
            }catch(const std::exception& error){
                if(stop.stop_requested())return;
                result.request_id=request.request_id;result.revision=request.revision;result.error_utf8=error.what();
            }
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
            output = console_label(2) + '\n' + diagnostic_text(result->compilation.stdout_bytes + result->compilation.stderr_bytes);
            if (!result->error_utf8.empty()) output += "\n" + result->error_utf8;
            if (result->compilation.status == windows::CppPhaseStatus::timed_out) output += '\n' + ui("La compilación superó el tiempo permitido.",
                "Compilation exceeded the time limit.","La compilation a dépassé le temps autorisé.");
        } else if (!result->execution.succeeded()) {
            output = ui("La ejecución no terminó correctamente.\n","Execution did not finish successfully.\n","L'exécution ne s'est pas terminée correctement.\n") + diagnostic_text(result->execution.stdout_bytes);
            output += diagnostic_text(result->execution.stderr_bytes);
            if (!result->error_utf8.empty()) output += "\n" + result->error_utf8;
            if (result->execution.status == windows::CppPhaseStatus::timed_out) output += '\n' + ui("Se detuvo por el límite de tiempo.",
                "Stopped at the time limit.","Arrêt à la limite de temps.");
            if (result->execution.status == windows::CppPhaseStatus::output_limit) output += '\n' + ui("Se detuvo por el límite de salida.",
                "Stopped at the output limit.","Arrêt à la limite de sortie.");
        } else {
            output = diagnostic_text(result->execution.stdout_bytes);
            if (!result->execution.stderr_bytes.empty()) output += '\n' + console_label(1) + '\n' + diagnostic_text(result->execution.stderr_bytes);
        }
        vm.console_channels[0] = diagnostic_text(result->execution.stdout_bytes);
        vm.console_channels[1] = diagnostic_text(result->execution.stderr_bytes);
        vm.console_channels[2] = diagnostic_text(result->compilation.stdout_bytes + result->compilation.stderr_bytes);
        if(laboratory()){
            laboratory_exit_code=static_cast<int>((result->execution.exit_code?result->execution.exit_code:result->compilation.exit_code).value_or(static_cast<std::uint32_t>(-1)));
            laboratory_status=!result->compilation.succeeded()?LaboratoryStatus::compilation_failed:result->execution.status==windows::CppPhaseStatus::completed?LaboratoryStatus::finished:LaboratoryStatus::failed;
            if(!result->error_utf8.empty())vm.console_channels[1]+=(vm.console_channels[1].empty()?"":"\n")+result->error_utf8;
            refresh_laboratory_status();console_present(index);return;
        }
        int preferred_channel = -1;
        if (!result->compilation.succeeded()) { vm.console_channels[2] = output; preferred_channel = 2; }
        else if (!result->execution.succeeded()) { vm.console_channels[1] = output; preferred_channel = 1; }
        console_present(index,preferred_channel);
        if (block(index).output_check) {
            curriculum::FreshExecution fresh;
            fresh.source_revision = result->revision;
            fresh.compilation_succeeded = result->compilation.succeeded(); fresh.execution_succeeded = result->execution.succeeded();
            fresh.exit_code = result->execution.exit_code ? static_cast<int>(*result->execution.exit_code) : -1;
            fresh.standard_input = running_input; fresh.standard_output = result->execution.stdout_bytes;
            auto evaluation = curriculum::evaluate_output_check(*block(index).output_check, fresh, vm.revision);
            if (evaluation.verdict != curriculum::OutputVerdict::unavailable) {
                const bool correct = evaluation.verdict == curriculum::OutputVerdict::correct;
                auto text = verdict_text(correct);
                if (!evaluation.feedback.empty()) text += "\n" + evaluation.feedback;
                feedback(index, text, correct, true);
                if (hooks.feedback_changed) hooks.feedback_changed(window,index,correct);
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
        reader_theme::apply(item,reader_theme::Surface::canvas);
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
        set_rich_text(import_summary,ui("Pulsa Revisar para verificar la carpeta antes de importar. Todavía no se ha cambiado ningún dato.",
            "Select Review to verify the folder before importing. No data has been changed yet.",
            "Sélectionne Vérifier pour vérifier le dossier avant l'importation. Aucune donnée n'a encore été modifiée."));
    }
    void import_state() {
        flush_selection();
        flush();
        if (!state::can_import_android_profile(store.load())) { show_error(ui(
            "Este perfil ya contiene progreso o datos propios. La importación conserva los perfiles vacíos o usados únicamente para navegar por el catálogo.",
            "This profile already contains progress or personal data. Import is available only for empty profiles or profiles used solely to browse the catalog.",
            "Ce profil contient déjà une progression ou des données personnelles. L'importation est réservée aux profils vides ou utilisés uniquement pour parcourir le catalogue.")); return; }
        if (hooks.before_page_change) hooks.before_page_change(window);
        import_done = false;
        import_prompt = import_text(import_prompt_id,ui("Importar estado Android\nSelecciona o escribe la carpeta exportada que contiene shared_prefs y, si corresponde, files.",
            "Import Android state\nSelect or enter the exported folder containing shared_prefs and, where applicable, files.",
            "Importer l'état Android\nSélectionne ou saisis le dossier exporté contenant shared_prefs et, le cas échéant, files."),false);
        import_label = control(L"STATIC",ui("Carpeta exportada:","Exported folder:","Dossier exporté :"), SS_LEFT, import_label_id, window);
        import_path = control(L"EDIT", "", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, import_path_id, window);
        reader_theme::apply(import_path,reader_theme::Surface::field);
        import_browse = control(L"BUTTON",ui("Examinar…","Browse…","Parcourir…"), BS_PUSHBUTTON | WS_TABSTOP, import_browse_id, window);
        import_review = control(L"BUTTON",ui("Revisar","Review","Vérifier"), BS_PUSHBUTTON | WS_TABSTOP, import_review_id, window);
        import_summary = import_text(import_summary_id, "");
        import_commit = control(L"BUTTON",ui("Confirmar importación","Confirm import","Confirmer l'importation"), BS_PUSHBUTTON | WS_TABSTOP, import_commit_id, window);
        import_cancel = control(L"BUTTON",ui("Cancelar","Cancel","Annuler"), BS_PUSHBUTTON | WS_TABSTOP, import_cancel_id, window);
        import_open = true;
        if (editing_bar) editing_bar->refresh();
        invalidate_import();
        ShowWindow(pane, SW_HIDE);
        if (reader_bar && IsWindow(reader_bar)) ShowWindow(reader_bar,SW_HIDE);
        notify_content();
        EnableWindow(menu,FALSE);
        update_fonts(); layout_import(); SetFocus(import_path);
    }
    void browse_import() {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Cannot open the Windows folder selector");
        DWORD options{}; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(wide(ui("Selecciona una carpeta exportada de Android (shared_prefs y files)",
            "Select an exported Android folder (shared_prefs and files)","Sélectionne un dossier Android exporté (shared_prefs et files)")).c_str());
        const auto shown = dialog->Show(window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        if (FAILED(shown)) throw std::runtime_error(ui("No se pudo abrir el selector de carpetas.","Could not open the folder selector.","Impossible d'ouvrir le sélecteur de dossier."));
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
            if (text.empty()) throw std::runtime_error(ui("Selecciona o escribe la carpeta exportada.","Select or enter the exported folder.","Sélectionne ou saisis le dossier exporté."));
            auto preview = state::inspect_android_export(std::filesystem::path(wide(text)));
            // Validate against the original lesson before publishing active state.
            // The preview itself stays byte-faithful to the exported preferences.
            state::validate_lesson_state(preview.data, lesson,route);
            auto summary = ui("Se verificaron ","Verified ","Vérification de ") + std::to_string(preview.preference_count)
                + ui(" preferencias, "," preferences, "," préférences, ") + std::to_string(preview.files.size())
                + ui(" archivos y "," files and "," fichiers et ") + std::to_string(preview.project_revisions)
                + ui(" revisiones de proyectos.\n\nEl origen se conservará intacto. La importación se hará en este perfil vacío.\n\nLos resultados antiguos de compilación necesitarán una nueva ejecución.",
                    " project revisions.\n\nThe source will remain intact. Import will use this empty profile.\n\nOld compilation results require a fresh execution.",
                    " révisions de projets.\n\nLa source sera conservée intacte. L'importation utilisera ce profil vide.\n\nLes anciens résultats de compilation nécessitent une nouvelle exécution.");
            for (const auto& note : preview.notes) summary += "\n\n" + note;
            set_rich_text(import_summary, summary);
            import_preview = std::move(preview); EnableWindow(import_commit, TRUE);
        } catch (const std::exception& error) {
            set_rich_text(import_summary,ui("No se puede importar:\n","Cannot import:\n","Importation impossible :\n") + error.what());
        }
    }
    void confirm_import() {
        if (!import_preview || import_done) return;
        try {
            state::validate_lesson_state(import_preview->data, lesson,route);
            state::commit_android_import(*import_preview, environment.paths.root);
            data = store.load(); state::prepare_lesson_state(data, lesson);
            page = state::current_page(data, lesson); dirty = false; import_done = true;
            import_preview.reset();
            SendMessageW(import_path, EM_SETREADONLY, TRUE, 0);
            for (auto item : {import_browse, import_review, import_commit}) EnableWindow(item, FALSE);
            SetWindowTextW(import_cancel,wide(ui("Volver a la lección","Back to lesson","Retour à la leçon")).c_str());
            set_rich_text(import_summary,ui("Importación completada y verificada.\n\nEl origen se conserva intacto. Vuelve a la lección para continuar desde el estado importado.",
                "Import completed and verified.\n\nThe source remains intact. Return to the lesson to continue from the imported state.",
                "Importation terminée et vérifiée.\n\nLa source reste intacte. Reviens à la leçon pour continuer depuis l'état importé."));
            SetFocus(import_cancel);
        } catch (const std::exception& error) {
            import_preview.reset(); EnableWindow(import_commit, FALSE);
            set_rich_text(import_summary,ui("No se ha completado la importación:\n","Import did not complete:\n","L'importation n'est pas terminée :\n") + error.what()
                + ui("\n\nRevisa la carpeta de nuevo antes de reintentar.","\n\nReview the folder again before retrying.","\n\nVérifie à nouveau le dossier avant de réessayer."));
        }
    }
    void close_import() {
        import_open = false; import_preview.reset();
        for (auto* item : {&import_prompt, &import_label, &import_path, &import_browse, &import_review, &import_summary, &import_commit, &import_cancel}) {
            DestroyWindow(*item); *item = nullptr;
        }
        EnableWindow(menu,TRUE);
        if (import_done) { scroll = 0; render_page(); }
        else update_navigation();
        ShowWindow(pane, SW_SHOW); SetFocus(menu);
        if (reader_bar && IsWindow(reader_bar)) ShowWindow(reader_bar,SW_SHOWNOACTIVATE);
        resize(); notify_content();
    }
    bool navigate_back(){
        if(import_open){close_import();return true;}
        if(fullscreen_block){toggle_fullscreen(*fullscreen_block,fullscreen_output);return true;}
        const auto root=GetAncestor(window,GA_ROOT),keyboard=math_keyboard_window(root);
        if(keyboard&&IsWindowVisible(keyboard)){hide_math_keyboard(root);reflow();return true;}
        if(!hooks.back_to_catalog)return false;
        // Defer destruction until an originating native child callback has
        // returned, exactly like the existing header Back action.
        return PostMessageW(window,header_action_message,back_id,0)!=FALSE;
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
        if ((notification == EN_SETFOCUS || notification == EN_KILLFOCUS) && editing_bar) editing_bar->refresh();
        if (notification == EN_KILLFOCUS) {
            for (std::size_t i = 0; i < controls.size(); ++i) if (sender == controls[i].source && !updating) { capture_draft(i); mark_dirty(); }
        }
        if (notification != BN_CLICKED) return;
        if (id >= pages_menu_base && id < pages_menu_base + static_cast<int>(lesson.pages.size()))
            { change_page(static_cast<std::size_t>(id - pages_menu_base)); return; }
        if (id == import_id) { import_state(); return; }
        if(id==fullscreen_close_id&&fullscreen_block){toggle_fullscreen(*fullscreen_block,fullscreen_output);return;}
        if(id==finish_id){(void)finish_lesson();return;}
        if(id==continue_id){if(page+1<lesson.pages.size())change_page(page+1);else (void)finish_lesson();return;}
        if (id == menu_id) {
            if(hooks.settings_requested)PostMessageW(window,header_action_message,settings_id,0);
            else open_menu();return;
        }
        if(id==back_id){(void)navigate_back();return;}
        if (id == reader_id || id == language_id || id == attempts_id || id == settings_id) {
            PostMessageW(window,header_action_message,static_cast<WPARAM>(id),0); return;
        }
        if (id == page_top_id) { scroll_to(0); return; }
        if (id == page_bottom_id) { scroll_to(extent); return; }
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
            state::put_response(data, key(action.block), vm.response); feedback(action.block, "", false);sync_quiz_controls(action.block);mark_dirty(); break;
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
        case ActionType::toggle_wrap:{
            const bool enabled=!code_wrap();data.preferences["native_windows"]["cpp_laboratory_wrap"]=enabled;apply_wrap(vm.source);
            const auto toggle=GetDlgItem(pane,control_id(action.block,35));icon_buttons[toggle]=enabled?Icon::wrap_on:Icon::wrap_off;
            label_radio(toggle,enabled?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé"):ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));InvalidateRect(toggle,nullptr,FALSE);mark_dirty();reflow();break;}
        case ActionType::compiler_info:{
            const auto popup=CreatePopupMenu();if(!popup)throw std::runtime_error("Cannot create the compiler information menu");
            AppendMenuW(popup,MF_STRING|MF_CHECKED,1,L"MSVC C++20 · x64");const auto style=prepare_native_menu(popup,window);RECT bounds{};GetWindowRect(GetDlgItem(pane,control_id(action.block,36)),&bounds);
            (void)TrackPopupMenuEx(popup,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,bounds.left,bounds.bottom,window,nullptr);DestroyMenu(popup);break;}
        case ActionType::editor_menu: code_menu(action.block,false); break;
        case ActionType::console_menu: code_menu(action.block,true); break;
        case ActionType::simulated_menu: simulated_menu(action.block);break;
        case ActionType::copy_simulated: copy_simulated(action.block);break;
        case ActionType::reset_answer: reset_answer(action.block);break;
        case ActionType::copy_source: copy_text(vm.source_editor->document().text); break;
        case ActionType::copy_console: copy_text(edit_text(vm.console)); break;
        case ActionType::clear_console: vm.console_channels = {}; console_present(action.block); break;
        case ActionType::undo: SetFocus(vm.source); vm.source_editor->undo(); break;
        case ActionType::redo: SetFocus(vm.source); vm.source_editor->redo(); break;
        case ActionType::fullscreen_source: toggle_fullscreen(action.block,false); break;
        case ActionType::fullscreen_console: toggle_fullscreen(action.block,true); break;
        case ActionType::select_stdout: console_present(action.block,0); break;
        case ActionType::select_stderr: console_present(action.block,1); break;
        case ActionType::select_diagnostics: console_present(action.block,2); break;
        case ActionType::reset:
            if (running_block == action.block) cancel_run();
            updating = true;
            vm.source_editor->reset(predefined_document(block(action.block).initial_code));
            updating = false;
            ++vm.revision; capture_draft(action.block); feedback(action.block, "", false);
            clear_reference_completion(action.block);
            vm.console_channels = {};
            updating = true; set_rich_text(vm.console, ""); style_cpp(vm.source); updating = false;
            if(laboratory()){
                laboratory_status=LaboratoryStatus::ready;refresh_laboratory_status();data.preferences["native_windows"]["cpp_laboratory_wrap"]=false;apply_wrap(vm.source);
                const auto wrap=GetDlgItem(pane,control_id(action.block,35));icon_buttons[wrap]=Icon::wrap_off;label_radio(wrap,ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));InvalidateRect(wrap,nullptr,FALSE);
            }
            EnableWindow(vm.compile, TRUE); mark_dirty(); break;
        case ActionType::tab: SetFocus(vm.source); vm.source_editor->insert_tab(); break;
        }
    }
};

LRESULT CALLBACK gesture_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
    auto* view = reinterpret_cast<LessonView*>(value);
    if(message==WM_LBUTTONDBLCLK&&view->double_click_zoom(item))return 0;
    if(message==WM_KEYDOWN&&wparam==VK_ESCAPE&&navigate_lesson_back(view->window))return 0;
    if (message == WM_CONTEXTMENU) for (std::size_t index = 0; index < view->controls.size(); ++index) {
        if(view->controls[index].simulated_code==item){view->simulated_menu(index);return 0;}
        if (view->controls[index].source == item || view->controls[index].console == item) {
            view->code_menu(index,view->controls[index].console == item); return 0;
        }
    }
    if (message == WM_GESTURE) {
        GESTUREINFO info{sizeof(info)};
        if (GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lparam),&info) && (info.dwID == GID_PAN || info.dwID == GID_END)) {
            POINT point{info.ptsLocation.x,info.ptsLocation.y}; ScreenToClient(item,&point);
            const auto location = MAKELPARAM(point.x,point.y);
            const auto mouse = (info.dwFlags & GF_BEGIN) ? WM_LBUTTONDOWN
                : (info.dwFlags & GF_END) || info.dwID == GID_END ? WM_LBUTTONUP : WM_MOUSEMOVE;
            if (view->drag_message(item,mouse,MK_LBUTTON,location)) {
                CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam)); return 0;
            }
        }
    } else if (const auto result = view->drag_message(item,message,wparam,lparam)) {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(item,gesture_proc,2);
        return *result;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(item,gesture_proc,2);
    return DefSubclassProc(item,message,wparam,lparam);
}

LRESULT CALLBACK pane_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* view = reinterpret_cast<LessonView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        view = static_cast<LessonView*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }
    LRESULT themed{};
    if (message == WM_MEASUREITEM && measure_native_menu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
    if (message == WM_DRAWITEM && draw_native_menu(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
    if (message == WM_MENUCHAR) {
        LRESULT result = 0;
        if (native_menu_character(reinterpret_cast<HMENU>(lparam),static_cast<wchar_t>(LOWORD(wparam)),result)) return result;
    }
    if (reader_theme::control_color(message,wparam,lparam,themed)) return themed;
    if (message == WM_DRAWITEM && (view ? view->draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))
            : reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))) return TRUE;
    if (message == WM_ERASEBKGND) {
        RECT area{}; GetClientRect(window,&area);
        FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(view&&view->laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas));
        if(view)view->paint_simulated_cards(reinterpret_cast<HDC>(wparam));return TRUE;
    }
    if(message==WM_PAINT&&view){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);
        RECT area{};GetClientRect(window,&area);FillRect(dc,&area,reader_theme::brush(view->laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas));view->paint_simulated_cards(dc);EndPaint(window,&paint);return 0;}
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
        LRESULT themed{};
        if (message == WM_MEASUREITEM && measure_native_menu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
        if (message == WM_DRAWITEM && draw_native_menu(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
        if (message == WM_MENUCHAR) {
            LRESULT result = 0;
            if (native_menu_character(reinterpret_cast<HMENU>(lparam),static_cast<wchar_t>(LOWORD(wparam)),result)) return result;
        }
        if (reader_theme::control_color(message,wparam,lparam,themed)) return themed;
        if (message == WM_DRAWITEM && view->draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
        switch (message) {
        case WM_KEYDOWN: if(wparam==VK_ESCAPE&&view->navigate_back())return 0;break;
        case header_action_message: {
            std::function<void(HWND)> action;
            if (wparam == back_id) action = view->hooks.back_to_catalog;
            else if (wparam == reader_id) action = view->hooks.reader_requested;
            else if (wparam == language_id) action = view->hooks.language_requested;
            else if (wparam == attempts_id) action = view->hooks.attempts_requested;
            else if (wparam == settings_id) action = view->hooks.settings_requested;
            if (action) action(window); return 0;
        }
        case lesson_state_read_message:
            if (lparam) *reinterpret_cast<state::StateData*>(lparam) = view->data; return 1;
        case attempts_state_message:
            if(lparam){view->attempt_state=*reinterpret_cast<const AttemptsState*>(lparam);InvalidateRect(view->attempts,nullptr,FALSE);}return 1;
        case lesson_preferences_update_message: {
            auto& request = *reinterpret_cast<PreferenceUpdateRequest*>(lparam);
            try { view->update_preferences(*request.update); }
            catch (...) { request.error = std::current_exception(); }
            return !request.error;
        }
        case lesson_page_query_message:
            if (lparam) *reinterpret_cast<LessonPageState*>(lparam) = view->page_state(); return 1;
        case lesson_page_navigate_message: return !view->import_open && view->change_page(static_cast<std::size_t>(wparam));
        case lesson_reading_regions_message:
            if (lparam) *reinterpret_cast<std::vector<LessonReadingRegion>*>(lparam) = view->reading_regions(); return 1;
        case lesson_view_hooks_message:
            if (lparam) view->hooks = *reinterpret_cast<LessonViewHooks*>(lparam);
            view->verify_saved_math();
            view->update_navigation(); view->notify_content(); return 1;
        case lesson_reader_bar_message:
            view->reader_bar = reinterpret_cast<HWND>(wparam); view->reader_height = static_cast<int>(lparam);
            view->resize(); return 1;
        case WM_CREATE: view->initialize(window); return 0;
        case WM_SIZE: view->resize(); return 0;
        case WM_DPICHANGED_AFTERPARENT: view->update_fonts(); view->resize(); return 0;
        case WM_ERASEBKGND: {
            const auto dc = reinterpret_cast<HDC>(wparam); RECT area{}; GetClientRect(window,&area);
            FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::canvas));
            auto header = area; header.bottom = view->px(lesson_header_height);
            FillRect(dc,&header,reader_theme::brush(reader_theme::Surface::header));
            return TRUE;
        }
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
        case WM_TIMER: if (wparam == save_timer) { view->flush(); return 0; }
            if(wparam==attempts_timer){view->refresh_attempts();return 0;} break;
        case result_message: view->finish_run(); return 0;
        case math_result_message: view->finish_math(); return 0;
        case math_restore_message: view->finish_restore(); return 0;
        case finish_lesson_message: if(view->hooks.lesson_completed)view->hooks.lesson_completed(window);return 0;
        case close_import_message: if (view->import_open) view->close_import(); return 0;
        case flush_message: view->flush_selection(); return 1;
        case WM_DESTROY:
            if (!view->destroy_notified && view->hooks.before_destroy) {
                view->destroy_notified = true; view->hooks.before_destroy(window);
            }
            view->clear_accessible_names(); view->cancel_restore(); view->cancel_math(); view->cancel_run(); KillTimer(window, save_timer);KillTimer(window,attempts_timer); return 0;
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

HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment, curriculum::Lesson lesson,LessonViewHooks hooks) {
    static const bool registered = [] {
        for (const auto& entry : std::array<std::pair<const wchar_t*, WNDPROC>, 2>{{{view_class, view_proc}, {pane_class, pane_proc}}}) {
            WNDCLASSEXW definition{}; definition.cbSize = sizeof(definition);
            definition.hInstance = GetModuleHandleW(nullptr); definition.lpfnWndProc = entry.second;
            definition.hCursor = LoadCursorW(nullptr, IDC_ARROW); definition.hbrBackground = reader_theme::brush(reader_theme::Surface::canvas);
            definition.lpszClassName = entry.first;
            if (!RegisterClassExW(&definition)) throw std::runtime_error("Cannot register the native lesson window");
        }
        return true;
    }();
    (void)registered;
    auto view = std::make_unique<LessonView>(environment,std::move(lesson));
    view->hooks=std::move(hooks);
    auto window = CreateWindowExW(WS_EX_CONTROLPARENT, view_class,wide(view->ui("Lección","Lesson","Leçon")).c_str(), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), view.get());
    if (!window) { view.release(); throw std::runtime_error("Cannot open the lesson; see its content contract or saved-state diagnostic"); }
    view.release(); return window;
}

bool flush_lesson_view(HWND window) {
    return !window || SendMessageW(window, flush_message, 0, 0) != 0;
}

LessonPageState lesson_page_state(HWND window) {
    LessonPageState result; if (window) SendMessageW(window,lesson_page_query_message,0,reinterpret_cast<LPARAM>(&result)); return result;
}
bool navigate_lesson_view(HWND window,std::size_t page) {
    return window && SendMessageW(window,lesson_page_navigate_message,page,0) != 0;
}
bool navigate_lesson_back(HWND window){
    if(!IsWindow(window))return false;wchar_t name[96]{};GetClassNameW(window,name,96);if(std::wstring_view(name)!=view_class)return false;
    const auto view=reinterpret_cast<LessonView*>(GetWindowLongPtrW(window,GWLP_USERDATA));return view&&view->navigate_back();
}
std::vector<LessonReadingRegion> lesson_reading_regions(HWND window) {
    std::vector<LessonReadingRegion> result;
    if (window) SendMessageW(window,lesson_reading_regions_message,0,reinterpret_cast<LPARAM>(&result)); return result;
}
void set_lesson_view_hooks(HWND window,LessonViewHooks hooks) {
    if (window) SendMessageW(window,lesson_view_hooks_message,0,reinterpret_cast<LPARAM>(&hooks));
}
void attach_lesson_reader_bar(HWND window,HWND reader_bar,int logical_height) {
    if (reader_bar && (!IsWindow(reader_bar) || GetParent(reader_bar) != window || logical_height < 0))
        throw std::invalid_argument("The reader bar must belong to the lesson view");
    if (window) SendMessageW(window,lesson_reader_bar_message,reinterpret_cast<WPARAM>(reader_bar),logical_height);
}
void set_lesson_attempts_state(HWND window,int available,int maximum,double progress,bool premium) {
    const AttemptsState value{available,maximum,std::clamp(progress,0.,1.),premium};
    if(IsWindow(window))SendMessageW(window,attempts_state_message,0,reinterpret_cast<LPARAM>(&value));
}
state::StateData read_lesson_state(HWND window) {
    state::StateData result;
    if (window) SendMessageW(window,lesson_state_read_message,0,reinterpret_cast<LPARAM>(&result)); return result;
}
void update_lesson_preferences(HWND window,const std::function<void(state::StateData&)>& update) {
    if (!window || !IsWindow(window)) throw std::invalid_argument("The lesson view is not available");
    PreferenceUpdateRequest request{&update,{}};
    SendMessageW(window,lesson_preferences_update_message,0,reinterpret_cast<LPARAM>(&request));
    if (request.error) std::rethrow_exception(request.error);
}

} // namespace lgaed::presentation
