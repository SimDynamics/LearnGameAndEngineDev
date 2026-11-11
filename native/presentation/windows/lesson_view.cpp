#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/editing_bar.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/attempt_indicator.hpp"
#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/cpp_project_view.hpp"
#include "platform/windows/cpp_history_training.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/text_semantic_composition.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/reading_gate_policy.hpp"
#include "presentation/windows/order_code_view.hpp"
#include "core/order_source_text.hpp"
#include "presentation/windows/curricular_image_view.hpp"
#include "core/learning_flow.hpp"
#include "core/compiler_flow.hpp"
#include "core/lesson_entry_policy.hpp"
#include "core/lesson_page_reach.hpp"
#include "core/positional_reading_archive.hpp"
#include "core/catalog.hpp"
#include "core/view_preferences.hpp"
#include "core/attempts.hpp"
#include "core/attempt_ledger.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/compiler_completion.hpp"
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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
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
constexpr UINT answer_viewport_message = WM_APP + 79;
constexpr UINT_PTR save_timer = 73;
constexpr UINT_PTR attempts_timer = 94;
constexpr int import_id = 104;
constexpr int page_status_id = 106, menu_id = 107, page_top_id = 108, page_bottom_id = 109;
constexpr int back_id = 110, reader_id = 111, language_id = 112, attempts_id = 113, settings_id = 114;
constexpr int finish_id=115;
thread_local std::string lesson_creation_error;
constexpr int continue_id=117;
constexpr int fullscreen_palette_id=118;
constexpr UINT attempts_state_message=WM_APP+187;
constexpr UINT enter_key_message=WM_APP+188;
struct AttemptsState { int available=8, maximum=8; double progress=1; bool premium=false; };
constexpr int pages_menu_base = 40000;
constexpr int lesson_heading_id = 990;
constexpr int lesson_header_height = 80;
constexpr int import_prompt_id = 200, import_path_id = 201, import_browse_id = 202;
constexpr int import_review_id = 203, import_summary_id = 204, import_commit_id = 205, import_cancel_id = 206;
constexpr int import_label_id = 207;

enum class ActionType { check, choose, move_up, move_down, run, reset, tab, editor_menu, console_menu,
    copy_source, copy_console, clear_console, undo, redo, fullscreen_source, fullscreen_console,
    select_stdout, select_stderr, select_diagnostics, copy_simulated, copy_readonly, reset_answer, simulated_menu,
    toggle_wrap, compiler_info, cut_source,paste_source,copy_source_selection,select_all_source,
    copy_console_selection,select_all_console,copy_compiler_append,image_detail };
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
    int column_gap=8;
    int empty_padding_after=0;
};
struct BlockControls {
    HWND answer = nullptr, source = nullptr, input = nullptr, feedback = nullptr;
    HWND console = nullptr, compile = nullptr, status = nullptr;
    HWND simulated_code = nullptr;
    std::unique_ptr<CodeEditor> answer_editor, source_editor, input_editor;
    // Destroy the project controls before their shared source editor.
    std::unique_ptr<CppProjectView> project;
    // Ephemeral, source-bound observation. It never rewrites saved grades.
    std::unique_ptr<windows::CmakeSelectiveRecoveryTrace> selective_recovery;
    std::unique_ptr<windows::CodeGutter> simulated_gutter;
    std::unique_ptr<CppFillView> fill;
    std::unique_ptr<MathPuzzleView> math;
    std::unique_ptr<OrderCodeView> order_view;
    std::unique_ptr<CurricularImageView> image;
    std::vector<HWND> choices;
    state::Response response;
    std::vector<std::size_t> order;
    std::uint64_t revision = 0;
    std::string input_original, input_visual;
    std::string feedback_source;
    std::string feedback_field_path;
    std::optional<curriculum::TextSemantics> feedback_semantics;
    std::string math_semantic_identity;
    bool feedback_correct = false;
    bool feedback_generated_verdict = false, feedback_default_review = false;
    std::array<std::string,3> console_channels;
    int console_channel = 0;
    bool rendered = false, locked = false;
    bool order_started=false;
};
struct LessonView;
LRESULT CALLBACK view_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK pane_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK gesture_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK console_icon_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK quiz_answer_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

int control_id(std::size_t block, int part) { return 1000 + static_cast<int>(block) * 100 + part; }
bool exercise(BlockType type) {
    return type == BlockType::multiple_choice || type == BlockType::output_prediction
        || type == BlockType::fill_code || type == BlockType::order_code
        || type == BlockType::inline_fill_code || type == BlockType::drag_fill_code || type == BlockType::math_puzzle;
}
bool blank_answer(std::string_view text) {
    const auto characters=wide(text);
    return std::ranges::all_of(characters,[](wchar_t character) {
        WORD kind=0;
        return GetStringTypeW(CT_CTYPE1,&character,1,&kind)&&(kind&C1_SPACE)!=0;
    });
}
Microsoft::WRL::ComPtr<ITextDocument> edit_text_document(HWND edit) {
    Microsoft::WRL::ComPtr<IRichEditOle> rich;
    SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument> document;
    if(rich)(void)rich.As(&document);
    return document;
}
std::optional<RECT> edit_character_bounds(ITextDocument* document,LONG character) {
    if(!document)return {};
    Microsoft::WRL::ComPtr<ITextRange> range;
    if(document->Range(character,character,&range)!=S_OK||!range)return {};
    RECT bounds{};
    constexpr long coordinates=tomStart|tomClientCoord|tomAllowOffClient;
    if(range->GetPoint(coordinates|TA_TOP|TA_LEFT,&bounds.left,&bounds.top)!=S_OK||
        range->GetPoint(coordinates|TA_BOTTOM|TA_RIGHT,&bounds.right,&bounds.bottom)!=S_OK||bounds.bottom<=bounds.top)return {};
    return bounds;
}
int edit_line_height(HWND edit,int scale) {
    // TextServices owns the actual zoomed line box, including font fallback
    // and leading. A separately created GDI font can underestimate it.
    if(const auto bounds=edit_character_bounds(edit_text_document(edit).Get(),0))
        return static_cast<int>(bounds->bottom-bounds->top);
    CHARFORMAT2W style{};style.cbSize=sizeof(style);SendMessageW(edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&style));
    const auto height=MulDiv(MulDiv(style.yHeight,static_cast<int>(GetDpiForWindow(edit)),1440),scale,100);
    const auto font=CreateFontW(-std::max(1,height),0,0,0,(style.dwEffects&CFE_BOLD)?FW_BOLD:FW_NORMAL,
        (style.dwEffects&CFE_ITALIC)!=0,FALSE,FALSE,style.bCharSet,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,style.bPitchAndFamily,style.szFaceName);
    if(!font)return height;
    const auto dc=GetDC(edit);if(!dc){DeleteObject(font);return height;}
    const auto previous=SelectObject(dc,font);TEXTMETRICW metrics{};const bool measured=GetTextMetricsW(dc,&metrics)!=FALSE;
    SelectObject(dc,previous);ReleaseDC(edit,dc);DeleteObject(font);
    return measured?metrics.tmHeight+metrics.tmExternalLeading:height;
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

std::string saved_math_answer_identity(std::string_view raw) {
    using namespace winrt::Windows::Data::Json;
    if(raw.empty())return {};
    const auto stored=JsonObject::Parse(wide(raw));
    if(!stored.HasKey(L"responses"))return {};
    auto responses=JsonObject::Parse(stored.GetNamedObject(L"responses").Stringify());
    for(const auto& response:responses)if(response.Value().ValueType()==JsonValueType::Object){
        auto value=response.Value().GetObject();value.Remove(L"editorDraft");value.Remove(L"editorDrafts");value.Remove(L"interactionDraft");
    }
    return math_answer_identity(responses);
}

std::string saved_math_fingerprint(std::string_view raw) {
    if(raw.empty())return {};
    return winrt::to_string(winrt::Windows::Data::Json::JsonObject::Parse(wide(raw)).GetNamedString(L"contentFingerprint",L""));
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
// Autosized lesson text hands the wheel to its viewport. An overflowing
// editor/console consumes only directions in which its own scrollbar can move;
// reaching either edge must not strand the lesson underneath the pointer.
LRESULT CALLBACK wheel_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                            UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL && !(GET_KEYSTATE_WPARAM(wparam)&(MK_CONTROL|MK_SHIFT))) {
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
        const auto delta=GET_WHEEL_DELTA_WPARAM(wparam);
        const bool range=scrolls_internally(window,SB_VERT)&&GetScrollInfo(window,SB_VERT,&info);
        const auto last=info.nMax-std::max(0,static_cast<int>(info.nPage)-1);
        const bool can_move=range&&((delta>0&&info.nPos>info.nMin)||(delta<0&&info.nPos<last));
        if(delta&&!can_move)return SendMessageW(GetParent(window),message,wparam,lparam);
        if(delta&&can_move&&std::abs(delta)>=WHEEL_DELTA){
            POINT before{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
            HWND outer=GetParent(window);while(outer&&!scrolls_internally(outer,SB_VERT))outer=GetParent(outer);
            SCROLLINFO old_parent{sizeof(old_parent),SIF_POS};if(outer)GetScrollInfo(outer,SB_VERT,&old_parent);
            const auto result=DefSubclassProc(window,message,wparam,lparam);
            POINT after{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
            SCROLLINFO new_parent{sizeof(new_parent),SIF_POS};if(outer)GetScrollInfo(outer,SB_VERT,&new_parent);
            if(before.y==after.y&&(!outer||old_parent.nPos==new_parent.nPos))
                return SendMessageW(GetParent(window),message,wparam,lparam);
            return result;
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
    std::string route = "programming";
    state::StateStore store;
    state::StateData data;
    HWND window = nullptr, pane = nullptr, fullscreen_header = nullptr, title = nullptr;
    HWND menu = nullptr, page_status = nullptr;
    HWND back = nullptr, reader = nullptr, language = nullptr, attempts = nullptr, settings = nullptr, reader_bar = nullptr;
    HWND fullscreen_palette = nullptr;
    std::optional<std::size_t> fullscreen_palette_block;
    int fullscreen_palette_height=0,fullscreen_palette_max_height=0;
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
    struct Placement { HWND item; int x, y, width, height; bool fixed=false; HWND parent=nullptr;int origin=0; };
    std::vector<Placement> scroll_placements;
    SIZE scroll_layout_size{};
    bool scroll_layout_valid=false;
    std::map<int,HWND> overflow_panes;
    std::map<HWND,int> overflow_view_origins;
    struct SimulatedCardSurface { RECT bounds; int header_bottom; };
    std::vector<SimulatedCardSurface> simulated_card_surfaces;
    std::vector<RECT> math_card_surfaces;
    std::vector<RECT> toolbar_surfaces;
    std::vector<RECT> console_surfaces;
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
    std::map<HWND,bool> wrap_modes;
    struct ConsoleFocus { HWND editor=nullptr;CHARRANGE selection{};bool native_selection=false; };
    std::map<HWND,ConsoleFocus> icon_focus;
    std::map<HWND,bool> answer_disabled_readonly;
    std::map<HWND,bool> pending_answer_viewports;
    bool syncing_answer_viewport=false;
    std::map<HWND,HWND> choice_labels;
    std::optional<std::size_t> selected_puzzle;
    bool enter_held=false;
    std::optional<std::size_t> fullscreen_block;
    std::optional<int> fullscreen_lesson_scroll;
    bool fullscreen_output = false;
    std::map<HWND,int> zoomed;
    std::jthread worker;
    std::mutex result_mutex;
    std::optional<windows::CppRunResult> completed;
    std::uint64_t request_id = 0;
    std::optional<std::size_t> running_block;
    std::string running_input;
    windows::CppRunRequest running_program;
    windows::CppRunRequest::Action running_action=windows::CppRunRequest::Action::compile_run;
    struct MathResult { std::size_t block; std::uint64_t revision; std::string reply,error; };
    std::jthread math_worker;
    std::mutex math_mutex;
    std::optional<MathResult> math_result;
    std::optional<std::size_t> checking_math;
    struct RestoredMath {
        std::string key,raw,reply,answer_identity,content_fingerprint;
        std::uint64_t generation=0,restore_generation=0,entry_generation=0;
        bool valid=false,compatible=false;
    };
    std::jthread restore_worker;
    std::mutex restore_mutex;
    std::vector<RestoredMath> restored_math;
    std::set<std::string> verified_math;
    std::set<std::string> rejected_math;
    std::uint64_t restore_generation=0;
    curriculum::DeferredLessonEntry deferred_entry;
    // Stable exercise keys survive page/view recreation. Only a real answer
    // change, explicit check or reset can invalidate its pending restoration.
    std::map<std::string,std::uint64_t> math_answer_generations;
    enum class DragAxis { pending, lesson_vertical, page_horizontal, editor_vertical, editor_horizontal };
    struct Drag {
        HWND origin = nullptr; POINT start{}, last{}, editor_scroll{};
        int lesson_scroll = 0;
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
        begin_entry(state::current_page(data, lesson));
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
    const curriculum::TextSemantics* text_semantics(HWND target,std::string_view text) const {
        const auto id=GetDlgCtrlID(target);
        if(id==lesson_heading_id&&!laboratory()){
            const auto found=lesson.text_semantics.find("/title");
            return found!=lesson.text_semantics.end()&&found->second.source==text?&found->second:nullptr;
        }
        if(id<1000)return nullptr;
        const auto index=static_cast<std::size_t>((id-1000)/100);
        if(index>=controls.size())return nullptr;
        const auto part=(id-1000)%100;
        if(part==4){const auto& value=controls[index].feedback_semantics;
            return value&&value->source==text?&*value:nullptr;}
        const auto& current=block(index);std::string path;
        if(current.type==BlockType::image&&(part==0||part==19))path=part==0?"/alt":"/caption";
        else if(part==0)path=current.type==BlockType::heading||current.type==BlockType::paragraph||current.type==BlockType::note?"/text":"/question";
        else if(current.type==BlockType::multiple_choice&&part>=21&&(part-21)%2==0&&
            static_cast<std::size_t>((part-21)/2)<current.options.size())path="/options/"+std::to_string((part-21)/2);
        const auto found=current.text_semantics.find(path);
        return found!=current.text_semantics.end()&&found->second.source==text?&found->second:nullptr;
    }
    void set_rich_text(HWND target,std::string_view text,std::span<const curriculum::Emphasis> emphasis={},
        bool code=false,RichTextRole role=RichTextRole::body,std::string_view locale={},
        std::span<const std::pair<std::size_t,std::size_t>> mathematics={},
        const curriculum::TextSemantics* explicit_semantics=nullptr) const {
        const auto selected=locale.empty()?std::string(interface_language(data)):std::string(locale);
        RichTextContext context;
        std::string visible_code,visible_prose;
        const auto id=GetDlgCtrlID(target);
        if(!code&&route=="programming"&&id>=1000){
            const auto index=static_cast<std::size_t>((id-1000)/100);
            if(index<controls.size()&&controls[index].rendered&&!controls[index].locked){
                const auto& current=block(index);
                // Only the visible question/options/fragments and current source can
                // disambiguate a mention. Never use answer keys or feedback.
                visible_prose=current.question;
                if(current.type==BlockType::multiple_choice)for(const auto& option:current.options){
                    visible_prose+='\n';visible_prose+=option;
                }
                const auto part=(id-1000)%100;
                if(current.type==BlockType::multiple_choice&&part>=21&&(part-21)%2==0&&
                    static_cast<std::size_t>((part-21)/2)<current.options.size()){
                    context.technical_sample=rich_text_naming_choice_samples(current.question,current.options);
                    context.reference_role=rich_text_choice_reference_role(current.question,text);
                }
                if(current.type==BlockType::order_code)for(const auto& fragment:current.fragments){
                    visible_prose+='\n';visible_prose+=fragment;
                }
                context.prose=visible_prose;
                if(controls[index].source){visible_code=edit_text(controls[index].source);context.code=visible_code;}
                else context.code=current.code;
            }
        }
        presentation::set_rich_text(target,text,emphasis,code,role,selected,mathematics,context,
            code?nullptr:explicit_semantics?explicit_semantics:text_semantics(target,text));
    }
    std::string verdict_text(bool correct) const {
        return correct ? ui("Correcto","Correct","Correct") : ui("Incorrecto","Incorrect","Incorrect");
    }
    std::string default_review() const {
        return ui("Revisa tu respuesta e inténtalo de nuevo.","Review your answer and try again.","Vérifie ta réponse et réessaie.");
    }
    std::string laboratory_title()const{return ui("Prototipo del motor C++","C++ Engine Prototype","Prototype du moteur C++");}
    std::string secondary_instruction(BlockType type,bool optional_separators=false)const{
        if(type==BlockType::order_code){
            if(optional_separators)return ui(
                "Ordena las instrucciones. Los bloques vacíos son opcionales: puedes arrastrarlos, ocultarlos con doble clic o Supr y recuperarlos con Insert.",
                "Order the instructions. Blank cards are optional: drag them, hide with double-click or Delete, restore with Insert.",
                "Ordonnez les instructions. Les cartes vides sont facultatives : déplacez-les, masquez-les par double-clic ou Suppr, restaurez-les avec Inser.");
            return ui("Mantén pulsado un bloque y arrástralo hasta la posición correcta.",
            "Press and hold a block, then drag it to the correct position.","Maintenez le doigt sur un bloc, puis faites-le glisser jusqu’à la bonne position.");
        }
        if(type==BlockType::drag_fill_code)return ui("Mantén pulsado un bloque y arrástralo hacia un espacio vacío.",
            "Press and hold a block, then drag it to an empty space.","Maintenez le doigt sur un bloc, puis faites-le glisser vers un emplacement vide.");
        return {};
    }
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
        return channel == 2 ? ui("Diagnósticos de compilación y enlace","Compilation and linking diagnostics","Diagnostics de compilation et d’édition de liens")
            : channel == 1 ? ui("Errores del programa (stderr)","Program errors (stderr)","Erreurs du programme (stderr)")
            : ui("Salida del programa (stdout)","Program output (stdout)","Sortie du programme (stdout)");
    }
    std::string course_id() const { const auto end=lesson.id.find("__");return end==std::string::npos?std::string{}:lesson.id.substr(0,end); }
    int content_text_scale() const { return appearance::text_scale(data,course_id()); }
    int compiler_source_minimum_height(HWND source) const {
        // Keep writing space when a student shortens or clears a program.
        // Match Android's 220dp floor while preserving ten native line boxes
        // at the current font/zoom, within the embedded editor's 320dp cap.
        return std::clamp(MulDiv(edit_line_height(source,content_text_scale())*10,96,
            static_cast<int>(GetDpiForWindow(source)))+32,220,320);
    }
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
        else if(block(index).type==BlockType::multiple_choice){
            identity=lgaed::attempts::ExerciseAnswerIdentity::of(vm.response.selected);
            if(block(index).choice_revision)identity=state::sha256(curriculum::choice_representation_frame(block(index)))+'/'+identity;
        }
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
            RECT check{};GetWindowRect(find_item(control_id(i,3)),&check);
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
            const bool choice_label=choice_labels.contains(item);
            bool editor = false;
            if (rich_control) {
                CHARRANGE selection{}; SendMessageW(item,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
                if (!(GetWindowLongPtrW(item,GWL_STYLE) & ES_READONLY)) {
                    editor = std::ranges::any_of(controls,[&](const auto& vm) { return vm.source == item && vm.source_editor; });
                    if (!editor || hits_text(item,{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)})) return {};
                }
                // A blank-space pan does not edit or collapse an existing
                // source selection. Hits on actual editable text stay native.
                if (!editor&&!choice_label&&selection.cpMin != selection.cpMax) return {};
            }
            end_drag(); drag.origin = item; drag.start = {GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
            drag.lesson_scroll=scroll;
            drag.down_keys = wparam; drag.down_point = lparam; drag.started = GetTickCount64();
            drag.editor = editor || own_scroll(item,SB_VERT) || own_scroll(item,SB_HORZ);
            drag.text = rich_control && !choice_label && hits_text(item,drag.start);
            ClientToScreen(item,&drag.start); drag.last = drag.start;
            if (drag.editor) {
                if(rich_control)SendMessageW(item,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&drag.editor_scroll));
                else {SCROLLINFO info{sizeof(info),SIF_POS};GetScrollInfo(item,SB_HORZ,&info);drag.editor_scroll.x=info.nPos;}
            }
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
                    drag.axis = choice_labels.contains(item) || (drag.editor && own_scroll(item,SB_HORZ)) ? DragAxis::editor_horizontal : DragAxis::page_horizontal;
            }
            if (drag.axis == DragAxis::lesson_vertical) scroll_to(scroll - (point.y - drag.last.y));
            else if (drag.axis == DragAxis::editor_vertical || drag.axis == DragAxis::editor_horizontal) {
                auto position = drag.editor_scroll;
                const auto vertical=drag.axis==DragAxis::editor_vertical;
                if(vertical){SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE};GetScrollInfo(item,SB_VERT,&range);
                    // EM_SETSCROLLPOS accepts positions beyond the wheel's
                    // useful viewport (leaving most of it blank). Keep a pan
                    // within the published content extent minus its viewport.
                    const auto last=std::max(range.nMin,range.nMax-static_cast<int>(range.nPage));
                    position.y=std::clamp<LONG>(drag.editor_scroll.y-dy,range.nMin,last);}
                else position.x = std::max<LONG>(0,position.x - dx);
                const auto fill=std::ranges::find_if(controls,[&](const auto& vm){return vm.fill&&vm.fill->window()==item;});
                if(fill!=controls.end())fill->fill->scroll_horizontal_to(position.x);
                else SendMessageW(item,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
                if(vertical){POINT after{};SendMessageW(item,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
                    SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE|SIF_POS};GetScrollInfo(item,SB_VERT,&range);
                    // EM_SETSCROLLPOS rounds to complete lines. Retain the
                    // gesture's absolute requested offset so sub-line moves
                    // accumulate inside the editor instead of leaking into
                    // the parent at every rounded native step.
                    const auto first=SendMessageW(item,EM_GETFIRSTVISIBLELINE,0,0);
                    const auto a=SendMessageW(item,EM_LINEINDEX,first,0);
                    auto b=SendMessageW(item,EM_LINEINDEX,first+1,0);
                    if(b<0&&first>0)b=SendMessageW(item,EM_LINEINDEX,first-1,0);
                    POINTL line_a{},line_b{};
                    SendMessageW(item,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&line_a),a);
                    if(b>=0)SendMessageW(item,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&line_b),b);
                    const auto line=std::max<LONG>(1,b>=0?std::abs(line_b.y-line_a.y):1);
                    const auto requested=drag.editor_scroll.y-dy;
                    const auto last=range.nMax-std::max(0,static_cast<int>(range.nPage)-1);
                    const bool edge=requested<after.y?range.nPos<=range.nMin:last-range.nPos<line;
                    const auto remainder=edge?requested-after.y:0;
                    scroll_to(drag.lesson_scroll+remainder);}
            }
            drag.last = point; return 0;
        }
        if (message == WM_LBUTTONUP) {
            const auto axis = drag.axis; const auto down_keys = drag.down_keys; const auto down_point = drag.down_point;
            end_drag();
            if (axis == DragAxis::pending) {
                if(const auto label=choice_labels.find(item);label!=choice_labels.end()){
                    const auto radio=label->second;
                    if(IsWindowEnabled(radio)){SetFocus(radio);command(GetDlgCtrlID(radio),BN_CLICKED,radio);}
                    return 0;
                }
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
        if(choice_labels.contains(item))return true;
        const bool display=std::ranges::any_of(controls,[&](const auto& vm){return vm.simulated_code==item||vm.console==item;});
        if(!display||!fullscreen_block)return false;
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
    HWND find_item(int id) const {
        if(const auto found=GetDlgItem(pane,id))return found;
        for(const auto& [origin,host]:overflow_panes)if(const auto found=GetDlgItem(host,id))return found;
        return fullscreen_header?GetDlgItem(fullscreen_header,id):nullptr;
    }
    Row* fixed_header_row() {
        if(!fullscreen_block||block(*fullscreen_block).type==BlockType::math_puzzle)return nullptr;
        const int caption=control_id(*fullscreen_block,fullscreen_output?30:16);
        const auto found=std::ranges::find_if(rows,[caption](const auto& row){return !row.cells.empty()&&GetDlgCtrlID(row.cells.front())==caption;});
        return found==rows.end()?nullptr:&*found;
    }
    void restore_fullscreen_header() {
        if(!fullscreen_header)return;
        const auto focus=GetFocus();
        while(const auto child=GetWindow(fullscreen_header,GW_CHILD))SetParent(child,pane);
        if(focus&&IsWindow(focus))SetFocus(focus);
        ShowWindow(fullscreen_header,SW_HIDE);
    }
    void restore_fullscreen_palette() {
        if(!fullscreen_palette)return;
        const auto focused=GetFocus();
        for(auto& vm:controls)if(vm.fill&&vm.fill->palette_window()&&GetParent(vm.fill->palette_window())==fullscreen_palette)
            vm.fill->set_palette_host();
        fullscreen_palette_block.reset();fullscreen_palette_height=0;
        ShowWindow(fullscreen_palette,SW_HIDE);
        if(focused&&IsWindow(focused)&&IsWindowEnabled(focused)&&IsWindowVisible(focused)&&GetFocus()!=focused)SetFocus(focused);
    }
    bool sync_fullscreen_palette() {
        std::optional<std::size_t> next;
        if(fullscreen_block&&!fullscreen_output&&*fullscreen_block<controls.size()&&block(*fullscreen_block).type==BlockType::drag_fill_code
            &&controls[*fullscreen_block].fill&&controls[*fullscreen_block].fill->palette_window())next=*fullscreen_block;
        bool reparented=false;
        for(std::size_t index=0;index<controls.size();++index)if(auto& vm=controls[index];vm.fill&&vm.fill->palette_window()){
            const auto bank=vm.fill->palette_window();
            if(next==index&&GetParent(bank)!=fullscreen_palette){vm.fill->set_palette_host(fullscreen_palette);reparented=true;}
            else if(next!=index&&GetParent(bank)==fullscreen_palette){vm.fill->set_palette_host();reparented=true;}
        }
        fullscreen_palette_block=next;
        if(!next){fullscreen_palette_height=0;ShowWindow(fullscreen_palette,SW_HIDE);}
        return reparented;
    }
    void reading_context(LessonReadingRegion& region,const curriculum::Block& current) const {
        const bool cpp_puzzle=current.type==BlockType::multiple_choice||current.type==BlockType::fill_code
            ||current.type==BlockType::inline_fill_code||current.type==BlockType::output_prediction
            ||current.type==BlockType::order_code||current.type==BlockType::drag_fill_code;
        if(route=="programming"&&cpp_puzzle
            &&(current.language.empty()||current.language=="cpp"||current.language=="c++"))region.reading_context="cpp";
        if(!current.raw_json.empty()){
            const auto raw=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(current.raw_json));
            region.math_mode=winrt::to_string(raw.GetNamedString(L"mathMode",L"auto"));
        }
    }
    std::vector<LessonReadingRegion> reading_regions(int script_version=0) const {
        const bool legacy_programming=script_version==1;
        std::vector<LessonReadingRegion> result;
        if (import_open || deferred_entry.waiting() || !pane) return result;
        if (const auto heading = find_item(lesson_heading_id); heading){
            result.push_back({lesson.id + '/' + lesson.locale + '/' + lesson.pages[page].stable_id + "/title",
                lesson.title,heading,std::numeric_limits<std::size_t>::max(),false,false,false,true});
            if(script_version==0&&!laboratory())if(auto semantics=rich_text_semantics(heading);semantics&&semantics->source==lesson.title)
                result.back().text_semantics=std::move(semantics);
        }
        const auto add = [&](std::size_t index,HWND target,std::string_view source,bool is_feedback = false) {
            // Fullscreen only reparents/presents the existing page. Its hidden
            // siblings remain part of the same narration script and bookmark.
            if (!target || source.empty() || (!fullscreen_block&&!(GetWindowLongPtrW(target,GWL_STYLE) & WS_VISIBLE))) return;
            const auto& current = block(index); const auto& vm = controls[index];
            const bool compiler_feedback=is_feedback&&current.type==BlockType::cpp_compiler;
            // Narration must wait for every authored compiler objective. The
            // lesson-wide navigation flag and activity dependencies do not
            // decide whether an individual checked program is still pending.
            const bool compiler_gate=!laboratory()&&reading_gate::compiler_objective(current);
            result.push_back({lesson.id + '/' + lesson.locale + '/' + lesson.pages[page].stable_id + '/'
                + current.stable_id + '/' + std::to_string(GetDlgCtrlID(target)),std::string(source),target,index,
                is_feedback,is_feedback && vm.feedback_correct,exercise(current.type)||compiler_feedback||compiler_gate,
                compiler_gate?windows::compiler_puzzle_completed(data,lesson,current):compiler_feedback?vm.feedback_correct:vm.response.correct});
            result.back().required_compiler=compiler_gate;
            reading_context(result.back(),current);
            result.back().compiler_guidance=is_feedback&&compiler_gate&&vm.feedback_correct&&lesson.state_layout.version==2&&
                ((current.compile_check&&vm.feedback_field_path=="/compileCheck/explanation")||
                 (current.output_check&&current.output_check->cases.size()==1&&vm.feedback_field_path=="/outputCheck/cases/0/explanation"));
            if(auto semantics=rich_text_semantics(target);semantics&&semantics->source==source)
                result.back().text_semantics=std::move(semantics);
            result.back().anchor=lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+current.stable_id
                +"/part/"+std::to_string(GetDlgCtrlID(target)-control_id(index,0));
        };
        for (std::size_t index = 0; index < controls.size(); ++index) {
            const auto& current = block(index); const auto& vm = controls[index];
            if (!vm.rendered || vm.locked) continue;
            if(current.type==BlockType::image){
                if(vm.image&&!vm.image->loaded())add(index,vm.status,vm.image->error());
                add(index,find_item(control_id(index,0)),current.image->alt);
                add(index,find_item(control_id(index,19)),current.image->caption);continue;
            }
            if (current.type == BlockType::note) add(index,find_item(control_id(index,19)),ui("Nota","Note","Note"));
            if (current.type == BlockType::heading || current.type == BlockType::paragraph || current.type == BlockType::note)
                add(index,find_item(control_id(index,0)),current.text);
            else add(index,find_item(control_id(index,0)),current.question);
            add(index,find_item(control_id(index,40)),choice_history_text(index));
            for (std::size_t option = 0; option < current.options.size(); ++option)
                add(index,find_item(control_id(index,21 + static_cast<int>(option) * 2)),current.options[option]);
            if(legacy_programming){if(vm.order_view)for(const auto& [target,text]:vm.order_view->reading_regions())add(index,target,text);}
            else add(index,find_item(control_id(index,37)),secondary_instruction(current.type,current.optional_order_separators>0));
            if(script_version==0&&current.type!=BlockType::multiple_choice&&
                (current.type==BlockType::code||current.type==BlockType::cpp_compiler||exercise(current.type))
                &&current.type!=BlockType::math_puzzle){
                auto code_language=current.language;
                if(code_language.empty()||code_language=="cpp"||code_language=="c++")code_language=ui("C más más","C plus plus","C plus plus");
                else if(code_language=="cmake")code_language="CMake";
                const auto label=current.type==BlockType::cpp_compiler?ui("Editor de código C más más.","C plus plus code editor.","Éditeur de code C plus plus.")
                    :current.type==BlockType::code?ui("Ejemplo de código ","Example of ","Exemple de code ")+code_language+ui("."," code.",".")
                    :ui("Ejercicio de código ","Code exercise in ","Exercice de code ")+code_language+".";
                add(index,find_item(control_id(index,16)),label);
                if(!result.empty()&&result.back().target==find_item(control_id(index,16)))result.back().passive_label=true;
            }
            // Android narrates the prompt/instruction of a code-order puzzle,
            // not its source fragments, templates or compiler/editor contents.
            if(vm.math) for(const auto& region:vm.math->reading_regions()) {
                if(!region.target || region.source_text.empty()) continue;
                result.push_back({lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+current.stable_id+'/'+region.key,
                    region.source_text,region.target,index,false,false,true,vm.response.correct});
                reading_context(result.back(),current);
                if(script_version==0&&region.math_mode&&*region.math_mode!=result.back().math_mode){
                    result.back().previous_math_mode=result.back().math_mode;
                    result.back().math_mode=*region.math_mode;
                }
                if(region.text_semantics&&region.text_semantics->source==region.source_text)
                    result.back().text_semantics=region.text_semantics;
                else if(auto semantics=rich_text_semantics(region.target);semantics&&semantics->source==region.source_text)
                    result.back().text_semantics=std::move(semantics);
            }
            add(index,vm.feedback,vm.feedback_source,true);
            if(script_version==0&&current.type==BlockType::cpp_compiler)
                add(index,find_item(control_id(index,40)),compiler_history_text(index));
        }
        return result;
    }

    std::vector<LessonReadingArchive> positional_reading_archives(const std::vector<LessonReadingRegion>& displayed) const {
        std::vector<LessonReadingArchive> archives;
        for(const auto& saved:lesson.state_layout.positional_reading_pages){
            if(saved.page!=page||saved.current_owners.size()!=saved.original.blocks.size())continue;
            LessonReadingArchive archived;
            archived.page_identity=route+'/'+lesson.id+'/'+std::to_string(saved.page);
            archived.positional_page_identity=true;
            auto previous=displayed;std::vector<curriculum::PositionalReadingBlock> order;bool complete=true;
            for(std::size_t original_index=0;original_index<saved.original.blocks.size();++original_index){
                const auto& original=saved.original.blocks[original_index];
                const auto position=curriculum::stable_block_position(lesson,saved.current_owners[original_index]);
                if(!position||position->page!=saved.page||position->block>=controls.size()
                    ||block(position->block).type!=original.type){complete=false;break;}
                const auto index=position->block;
                const auto prefix=lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+saved.current_owners[original_index];
                const bool prose=original.type==BlockType::heading||original.type==BlockType::paragraph||original.type==BlockType::note;
                // An in-flight control rebuild has no complete visible script
                // yet. Only authenticated prose behind today's compiler may
                // lack a control: reconstruct it in the private archive without
                // creating a HWND, exposing active text or opening that barrier.
                if(std::ranges::none_of(displayed,[&](const auto& region){return region.block_index==index;})){
                    if(!prose||original.text.empty()||controls[index].rendered||controls[index].locked||reference_available(block(index))){
                        complete=false;break;
                    }
                    const auto emit=[&](int part,std::string text){
                        LessonReadingRegion region;region.block_index=index;region.source_text=std::move(text);
                        region.key=prefix+'/'+std::to_string(control_id(index,part));region.anchor=prefix+"/part/"+std::to_string(part);
                        reading_context(region,original);previous.push_back(std::move(region));
                    };
                    if(original.type==BlockType::note)emit(19,ui("Nota","Note","Note"));
                    emit(0,original.text);
                }
                const bool compiler=original.type==BlockType::cpp_compiler;
                const bool completed=compiler&&windows::compiler_puzzle_completed(data,lesson,original);
                order.push_back({index,compiler,completed});
                for(auto& region:previous)if(region.block_index==index){
                    region.text_semantics.reset();region.previous_math_mode.reset();region.compiler_guidance=false;
                    region.reading_context="auto";reading_context(region,original);
                    if(!compiler&&!region.feedback&&region.key==prefix+'/'+std::to_string(control_id(index,0))){
                        const auto& text=prose?original.text:original.question;
                        if(region.source_text!=text)archived.revised_regions.push_back(region.key);
                        region.source_text=text;
                    }
                }
                const bool ordinary=exercise(original.type);
                if(!compiler&&!ordinary)continue;
                if(!controls[index].feedback||(compiler&&!controls[index].source_editor)){complete=false;break;}
                // These originals have no compiler-revision history. Read the
                // real old receipt against the draft, or the persisted ordinary
                // answer against the original explanation/review contract.
                std::erase_if(previous,[&](const auto& region){return region.block_index==index
                    &&(region.feedback||(compiler&&region.target==find_item(control_id(index,40))));});
                bool correct=false;std::string body;
                if(compiler){
                    const auto feedback=windows::compiler_puzzle_current_feedback(data,lesson,original,program_snapshot(index));
                    if(!feedback)continue;
                    correct=feedback->correct;const auto first_break=feedback->text.find_first_of("\r\n");
                    if(first_break!=std::string::npos)body=feedback->text.substr(first_break);
                }else{
                    const auto response=state::get_response(data,lesson,original);
                    if(!response)continue;
                    correct=response->correct;
                    if(correct)body=original.type==BlockType::multiple_choice?state::choice_correct_feedback(*response,original):original.explanation;
                    else if(state::response_reviewed(*response,original)){
                        body=original.type==BlockType::multiple_choice?state::choice_incorrect_feedback(*response,original)
                            :curriculum::incorrect_feedback(original,response->selected<0?std::nullopt:std::optional<std::size_t>(response->selected));
                        if(body.empty())body=default_review();
                    }else continue;
                }
                const auto first_text=body.find_first_not_of("\r\n");
                body=first_text==std::string::npos?std::string{}:body.substr(first_text);
                LessonReadingRegion region;region.block_index=index;region.target=controls[index].feedback;
                region.source_text=(correct?"✓ ":"✗ ")+verdict_text(correct)+(body.empty()?"":"\n\n"+body);
                region.feedback=true;region.correct=correct;region.is_exercise=true;
                region.required_compiler=compiler;region.solved=compiler?completed:correct;reading_context(region,original);
                region.key=prefix+'/'+std::to_string(control_id(index,4));region.anchor=prefix+"/part/4";
                const auto active=std::ranges::find(displayed,region.key,&LessonReadingRegion::key);
                if(active!=displayed.end()&&active->source_text!=region.source_text)archived.revised_regions.push_back(region.key);
                previous.push_back(std::move(region));
            }
            if(!complete)continue;
            // Only the original earned receipt opens prose behind the old
            // editor in this private archive; active v2 objectives and controls
            // are unaffected, including any prose still hidden behind them.
            archived.regions=curriculum::positional_reading_regions(previous,order);
            archives.push_back(std::move(archived));
        }
        return archives;
    }

    std::vector<LessonReadingArchive> editorial_reading_archives() const {
        if(import_open||deferred_entry.waiting()||!pane||!IsWindow(pane))return {};
        auto regions=reading_regions();auto archives=positional_reading_archives(regions);
        const auto identity=page_state().narration_page_identity;
        // These snapshots are complete prior editorial prefixes authenticated
        // by the loader. Do not combine individual old fields into page scripts
        // that never existed. Answers keep their ordinary current response slot.
        for(const auto& revision:lesson.state_layout.reading_revisions){
            LessonReadingArchive archived;archived.page_identity=identity;archived.regions=reading_regions();
            archived.positional_page_identity=revision.positional_page_identity;
            archived.bibliography_only=revision.bibliography_only;
            if(revision.positional_page_identity)archived.page_identity=route+'/'+lesson.id+'/'+std::to_string(page);
            for(const auto& [id,raw]:revision.previous_blocks){
                const auto at=curriculum::stable_block_position(lesson,id);if(!at||at->page!=page)continue;
                const auto previous=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw));
                const auto old_text=winrt::to_string(previous.GetNamedString(L"text",L""));
                const auto old_question=winrt::to_string(previous.GetNamedString(L"question",L""));
                const auto old_explanation=winrt::to_string(previous.GetNamedString(L"explanation",L""));
                auto original=block(at->block);original.question=old_question;original.explanation=old_explanation;
                original.raw_json=raw;
                // This closed positional adoption predates all five authored
                // wrong-answer guides. Reconstruct the actual generic review,
                // never combine current guides with an old page script.
                if(revision.positional_page_identity){original.review_feedback.clear();original.review_feedback_options.clear();}
                const auto response=state::get_response(data,lesson,original);
                const auto question=find_item(control_id(at->block,0));
                for(auto& region:archived.regions){
                    if(region.block_index!=at->block)continue;
                    auto source=region.source_text;
                    if(!region.feedback&&region.target==question)source=
                        original.type==BlockType::paragraph||original.type==BlockType::heading||original.type==BlockType::note?old_text:old_question;
                    else if(region.feedback&&region.correct&&region.solved){
                        auto body=old_explanation;
                        if(revision.bibliography_only){const auto first=body.find_first_not_of("\r\n");body=first==std::string::npos?"":body.substr(first);}
                        source="✓ "+verdict_text(true)+(body.empty()?"":"\n\n"+body);
                    }
                    else if(revision.positional_page_identity&&region.feedback&&response&&state::response_reviewed(*response,original)){
                        auto review=original.type==BlockType::multiple_choice?state::choice_incorrect_feedback(*response,original)
                            :curriculum::incorrect_feedback(original,response->selected<0?std::nullopt:std::optional<std::size_t>(response->selected));
                        if(review.empty())review=default_review();
                        source="✗ "+verdict_text(false)+"\n\n"+review;
                    }
                    if(source==region.source_text)continue;
                    archived.revised_regions.push_back(region.key);region.source_text=std::move(source);region.text_semantics.reset();
                    if(revision.bibliography_only&&region.feedback&&region.correct&&region.solved){
                        const auto previous_descriptor=revision.previous_explanation_semantics.find(id);
                        if(previous_descriptor==revision.previous_explanation_semantics.end())throw std::invalid_argument("A bibliographic reading archive lost its source descriptor");
                        if(previous_descriptor->second){
                            region.text_semantics=verdict_text_semantics(region.source_text,"✓ "+verdict_text(true),&*previous_descriptor->second);
                            if(!region.text_semantics)throw std::invalid_argument("A bibliographic feedback archive has a different source");
                        }
                    }
                }
            }
            if(!archived.revised_regions.empty())archives.push_back(std::move(archived));
        }
        for(auto prior=lesson.state_layout.editorial_returns.rbegin();prior!=lesson.state_layout.editorial_returns.rend();++prior){
            const auto at=curriculum::stable_block_position(lesson,prior->id);
            if(!at||at->page!=page)continue;
            const auto type=block(at->block).type;
            if(type!=BlockType::heading&&type!=BlockType::paragraph&&type!=BlockType::note)continue;
            const auto previous=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(prior->previous_raw_json));
            const auto old_type=previous.GetNamedString(L"type");
            if(old_type!=L"heading"&&old_type!=L"paragraph"&&old_type!=L"note")continue;
            const auto anchor=lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+prior->id+"/part/0";
            const auto target=find_item(control_id(at->block,0));
            const auto region=std::ranges::find_if(regions,[&](const auto& item){
                return item.block_index==at->block&&!item.feedback&&item.anchor==anchor&&item.target==target;
            });
            if(region==regions.end())continue;
            // Keep the current HWND and stable anchor. Archived data changes
            // only this prose source; it creates no controls or old feedback.
            region->source_text=winrt::to_string(previous.GetNamedString(L"text"));
            region->text_semantics.reset();
            region->math_mode=winrt::to_string(previous.GetNamedString(L"mathMode",L"auto"));
            archives.push_back({identity,regions});
        }
        // Reconstruct only the closed label review supplied by the loader.
        // Card placement, current HWNDs, answers and all other text stay as
        // displayed; no validator or hidden feedback is used as narration.
        auto previous_scripts=archives;
        previous_scripts.push_back({identity,reading_regions()});
        for(auto& archived:previous_scripts){bool changed=false;
            for(const auto& current:lesson.pages[page].blocks)if(current.math_label_revision){
                const auto& revision=*current.math_label_revision;
                const auto key=lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+current.stable_id+'/'+revision.region_key;
                const auto exact=[&](const auto& region){return region.key==key&&!region.feedback&&region.source_text==revision.current_text;};
                if(std::ranges::count_if(archived.regions,exact)!=1)continue;
                auto found=std::ranges::find_if(archived.regions,exact);found->source_text=revision.previous_text;found->text_semantics.reset();
                archived.revised_regions.push_back(key);changed=true;
            }
            if(changed)archives.push_back(std::move(archived));
        }
        // A reviewed choice owns a complete authenticated original, including
        // its old response slot. Rebuild its spoken script independently of
        // any newer selection; the historical summary is new UI, not old audio.
        previous_scripts=archives;
        previous_scripts.push_back({identity,reading_regions()});
        for(std::size_t index=0;index<lesson.pages[page].blocks.size();++index){
            const auto& current=block(index);if(!current.choice_revision)continue;
            const auto& revision=*current.choice_revision;auto original=current;
            original.options=revision.original_options;original.question=revision.original_question;
            original.correct_index=revision.original_correct_index;original.explanation=revision.original_explanation;
            original.review_feedback=revision.original_review_feedback;original.review_feedback_options=revision.original_review_feedback_options;
            original.raw_json=revision.raw_json;original.choice_revision.reset();
            const auto response=state::get_response(data,lesson,original);
            const bool solved=response&&response->correct;
            std::string old_feedback;
            if(solved)old_feedback="✓ "+verdict_text(true)+(original.explanation.empty()?"":"\n\n"+original.explanation);
            else if(response&&state::response_reviewed(*response,original)){
                auto review=state::choice_incorrect_feedback(*response,original);if(review.empty())review=default_review();
                old_feedback="✗ "+verdict_text(false)+"\n\n"+review;
            }
            std::vector<LessonReadingArchive> variants;
            for(const auto& previous:previous_scripts){
                const auto question=std::ranges::find_if(previous.regions,[&](const auto& region){
                    return region.block_index==index&&region.target==find_item(control_id(index,0))&&!region.feedback;
                });
                if(question==previous.regions.end())continue; // Hidden/locked activity was never narrated.
                auto archived=previous;auto feedback_region=*question;
                feedback_region.key=lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+current.stable_id+'/'+std::to_string(control_id(index,4));
                feedback_region.anchor=lesson.id+'/'+lesson.locale+'/'+lesson.pages[page].stable_id+'/'+current.stable_id+"/part/4";
                feedback_region.target=controls[index].feedback;feedback_region.source_text=old_feedback;feedback_region.text_semantics.reset();
                feedback_region.feedback=true;feedback_region.correct=solved;feedback_region.solved=solved;
                reading_context(feedback_region,original);
                std::erase_if(archived.regions,[&](const auto& region){return region.block_index==index
                    &&(region.feedback||region.target==find_item(control_id(index,40)));});
                for(auto& region:archived.regions)if(region.block_index==index){
                    region.text_semantics.reset();
                    region.solved=solved;region.correct=false;reading_context(region,original);
                    if(region.target==find_item(control_id(index,0)))region.source_text=original.question;
                    for(std::size_t option=0;option<original.options.size();++option)
                        if(region.target==find_item(control_id(index,21+static_cast<int>(option)*2))){
                            if(region.source_text!=original.options[option])archived.revised_regions.push_back(region.key);
                            region.source_text=original.options[option];
                        }
                }
                if(!old_feedback.empty()){
                    const auto last=std::find_if(archived.regions.rbegin(),archived.regions.rend(),[&](const auto& region){return region.block_index==index;});
                    archived.regions.insert(last.base(),std::move(feedback_region));
                }
                variants.push_back(std::move(archived));
            }
            archives.insert(archives.end(),variants.begin(),variants.end());
            previous_scripts.insert(previous_scripts.end(),std::make_move_iterator(variants.begin()),std::make_move_iterator(variants.end()));
        }
        return archives;
    }

    std::vector<LessonReadingArchive> retired_reading_archives() const {
        if(import_open||deferred_entry.waiting()||!pane||!IsWindow(pane))return {};
        std::vector<LessonReadingArchive> archives;
        for(const auto& saved:lesson.state_layout.retirement_history){
            auto historical=lesson;historical.pages=saved.pages;historical.state_layout.current=saved.current;
            historical.compiler_puzzles_required=saved.compiler_puzzles_required;
            historical.reference_after=saved.reference_after;historical.activity_after=saved.activity_after;
            if(page>=historical.pages.size())continue;
            const auto page_id=historical.pages[page].stable_id;
            auto order=saved.current[page].id;for(const auto& id:saved.current[page].blocks)order+=','+id;
            LessonReadingArchive archived;archived.page_identity=route+'/'+lesson.id+"/state-layout-v1/"+lesson.state_layout.lineage+'/'+state::sha256(order);
            auto& result=archived.regions;const auto proofs=completion_snapshot(historical);
            const auto available=[&](std::size_t index,const curriculum::Block& current){
                if(historical.compiler_puzzles_required)for(std::size_t prior=0;prior<index;++prior){const auto& candidate=historical.pages[page].blocks[prior];
                    if(curriculum::has_compiler_objective(candidate)&&!windows::compiler_puzzle_completed(data,historical,candidate))return false;}
                if(const auto dependency=curriculum::reference_activity(historical,current)){
                    const auto& owner=historical.pages[dependency->page].blocks[dependency->block];
                    const auto found=proofs.find(owner.stable_id);if(found==proofs.end()||!found->second)return false;
                }
                for(const auto& prerequisite:curriculum::activity_prerequisites(historical,current)){
                    const auto& owner=historical.pages[prerequisite.page].blocks[prerequisite.block];
                    const auto found=proofs.find(owner.stable_id);if(found==proofs.end()||!found->second)return false;
                }
                return true;
            };
            const auto emit=[&](std::size_t index,const curriculum::Block& current,int part,std::string text,bool passive=false){
                if(text.empty())return;
                LessonReadingRegion region;region.block_index=index;region.source_text=std::move(text);region.passive_label=passive;
                reading_context(region,current);
                const auto prefix=lesson.id+'/'+lesson.locale+'/'+page_id+'/'+current.stable_id;
                region.key=prefix+'/'+std::to_string(control_id(index,part));region.anchor=prefix+"/part/"+std::to_string(part);
                if(curriculum::retired_block(lesson,current.stable_id))if(const auto next=curriculum::active_block_successor(lesson,current.stable_id))
                    region.retired_successor=lesson.id+'/'+lesson.locale+'/'+lesson.pages[next->page].stable_id+'/'+lesson.pages[next->page].blocks[next->block].stable_id+"/part/";
                result.push_back(std::move(region));
            };
            if(page==0&&find_item(lesson_heading_id))result.push_back({lesson.id+'/'+lesson.locale+'/'+page_id+"/title",lesson.title,nullptr,std::numeric_limits<std::size_t>::max(),false,false,false,true});
            for(std::size_t index=0;index<historical.pages[page].blocks.size();++index){const auto& current=historical.pages[page].blocks[index];
                if(!available(index,current))continue;
                const auto response=state::get_response(data,historical,current);
                if(current.type==BlockType::image){
                    emit(index,current,0,current.image->alt);
                    emit(index,current,19,current.image->caption);continue;
                }
                if(current.type==BlockType::note)emit(index,current,19,ui("Nota","Note","Note"));
                emit(index,current,0,current.type==BlockType::heading||current.type==BlockType::paragraph||current.type==BlockType::note?current.text:current.question);
                if(response)emit(index,current,40,choice_history_text(current,*response));
                for(std::size_t option=0;option<current.options.size();++option)emit(index,current,21+static_cast<int>(option)*2,current.options[option]);
                emit(index,current,37,secondary_instruction(current.type,current.optional_order_separators>0));
                if(current.type!=BlockType::multiple_choice&&(current.type==BlockType::code||current.type==BlockType::cpp_compiler||exercise(current.type))&&current.type!=BlockType::math_puzzle){
                    auto code_language=current.language;if(code_language.empty()||code_language=="cpp"||code_language=="c++")code_language=ui("C más más","C plus plus","C plus plus");else if(code_language=="cmake")code_language="CMake";
                    emit(index,current,16,current.type==BlockType::cpp_compiler?ui("Editor de código C más más.","C plus plus code editor.","Éditeur de code C plus plus.")
                        :current.type==BlockType::code?ui("Ejemplo de código ","Example of ","Exemple de code ")+code_language+ui("."," code.",".")
                        :ui("Ejercicio de código ","Code exercise in ","Exercice de code ")+code_language+".",true);
                }
                std::string prior_feedback;bool correct=false;
                if(current.type==BlockType::cpp_compiler&&windows::compiler_puzzle_completed(data,historical,current)){
                    const auto stored=windows::compiler_puzzle_feedback(data,historical,current);const auto body=stored.find_first_of("\r\n");
                    prior_feedback=verdict_text(true);if(body!=std::string::npos)prior_feedback+=stored.substr(body);correct=true;
                }else if(response&&response->correct){
                    const auto explanation=current.type==BlockType::multiple_choice
                        ?state::choice_correct_feedback(*response,current):current.explanation;
                    prior_feedback=verdict_text(true)+(explanation.empty()?"":"\n"+explanation);correct=true;
                }
                else if(response&&exercise(current.type)&&state::response_reviewed(*response,current)){
                    const auto review=current.type==BlockType::multiple_choice?state::choice_incorrect_feedback(*response,current)
                        :curriculum::incorrect_feedback(current,response->selected<0?std::nullopt:std::optional<std::size_t>(response->selected));
                    prior_feedback=verdict_text(false)+'\n'+(review.empty()?default_review():review);
                }
                if(!prior_feedback.empty()){
                    const auto verdict=verdict_text(correct);
                    if(prior_feedback.starts_with(verdict)){
                        auto explanation=prior_feedback.substr(verdict.size());
                        while(!explanation.empty()&&(explanation.front()=='\n'||explanation.front()=='\r'))explanation.erase(explanation.begin());
                        prior_feedback=(correct?"✓ ":"✗ ")+verdict+(explanation.empty()?"":"\n\n"+explanation);
                    }
                    emit(index,current,4,std::move(prior_feedback));result.back().feedback=true;result.back().correct=correct;result.back().is_exercise=true;result.back().solved=correct;
                }
                if(current.type==BlockType::cpp_compiler)emit(index,current,40,compiler_history_text(historical,current));
            }
            archives.push_back(std::move(archived));
        }
        return archives;
    }
    std::vector<LessonReadingRegion> retired_reading_regions() const {
        auto archives=retired_reading_archives();return archives.empty()?std::vector<LessonReadingRegion>{}:std::move(archives.back().regions);
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
        if(activity.project_files.empty())objective += "/source/" + std::to_string(draft.source.size()) + ':' + draft.source;
        else{
            using namespace winrt::Windows::Data::Json;
            auto files=activity.project_files;auto sources=activity.source_files;auto cmake=activity.cmake;
            const auto base=curriculum::draft_key(route,lesson,activity);
            const auto project=state::string_value(data,"cpp_lesson_drafts",base+"/project");
            if(!project.empty()){const auto saved=JsonObject::Parse(winrt::to_hstring(project));files.clear();sources.clear();
                for(const auto& entry:saved.GetNamedArray(L"files")){const auto file=entry.GetObject();files.emplace_back(winrt::to_string(file.GetNamedString(L"path")),winrt::to_string(file.GetNamedString(L"code")));}
                if(!cmake)for(const auto& entry:saved.GetNamedArray(L"sources"))sources.push_back(winrt::to_string(entry.GetString()));}
            const auto configuration=state::string_value(data,"cpp_lesson_drafts",base+"/cmake");
            if(cmake&&!configuration.empty()){const auto saved=JsonObject::Parse(winrt::to_hstring(configuration));cmake->target=winrt::to_string(saved.GetNamedString(L"target"));
                cmake->options=curriculum::parse_cmake_options(winrt::to_string(saved.GetNamedString(L"optionsText")));cmake->build_type=winrt::to_string(saved.GetNamedString(L"buildType"));}
            const auto frame=curriculum::cpp_project_frame(files,sources,cmake);
            objective+="/project/"+std::to_string(frame.size())+':'+frame;
        }
        objective += "/input/" + std::to_string(input.size()) + ':' + input;
        return state::sha256(objective);
    }
    using CompletionSnapshot = curriculum::CompilerCompletionSnapshot;
    CompletionSnapshot completion_snapshot(const curriculum::Lesson& source) const {
        return curriculum::compiler_completion_snapshot(source,[&](const auto& current){
            const auto receipt = state::string_value(data,"native_windows",reference_key(current));
            return windows::compiler_puzzle_completed(data,source,current)||(!receipt.empty()&&receipt==reference_seal(current));
        });
    }
    CompletionSnapshot completion_snapshot() const { return completion_snapshot(lesson); }
    bool activity_available(const curriculum::Block& activity,const CompletionSnapshot& proofs) const {
        return curriculum::compiler_activity_available(lesson,activity,proofs);
    }
    bool activity_available(const curriculum::Block& activity) const {
        return activity_available(activity,completion_snapshot());
    }
    bool activity_completed(const curriculum::Block& activity) const {
        const auto proofs = completion_snapshot(); const auto found = proofs.find(activity.stable_id);
        return found != proofs.end() && found->second;
    }
    static bool compiler_completion_required(const curriculum::Lesson& model,const curriculum::Block& activity) {
        return model.id!="native_cpp_laboratory"&&curriculum::compiler_requires_completion(activity);
    }
    std::string unavailable_compiler_status() const {
        return ui("La comprobación de esta actividad aún no está disponible. El avance queda pendiente.",
            "Checking this activity is not available yet. Progress remains pending.",
            "La vérification de cette activité n’est pas encore disponible. La progression reste en attente.");
    }
    curriculum::LessonEntryDecision entry_decision(std::size_t requested) const {
        using Completion=curriculum::EntryCompletion;
        std::vector<curriculum::EntryRequirement> requirements;
        const auto proofs=completion_snapshot();
        for(std::size_t prior=0;prior<lesson.pages.size();++prior)for(const auto& current:lesson.pages[prior].blocks){
            if(compiler_completion_required(lesson,current)){
                const auto found=proofs.find(current.stable_id);
                requirements.push_back({prior,found!=proofs.end()&&found->second?Completion::complete:Completion::incomplete});
            }else if(exercise(current.type)){
                const auto response=state::get_response(data,lesson,current);
                auto completion=response&&response->correct?Completion::complete:Completion::incomplete;
                if(completion==Completion::complete&&current.type==BlockType::math_puzzle){
                    const auto identity=curriculum::exercise_key(lesson,current);
                    try{
                        if(rejected_math.contains(identity)||saved_math_answer_identity(response->raw_json).empty()||
                                saved_math_fingerprint(response->raw_json).empty())completion=Completion::incomplete;
                        else if(!verified_math.contains(identity))completion=Completion::pending_math;
                    }catch(...){completion=Completion::incomplete;}
                }
                requirements.push_back({prior,completion});
            }
        }
        return curriculum::lesson_entry_decision(state::clamp_reachable_page(data,lesson,requested),lesson.pages.size(),requirements);
    }
    void begin_entry(std::size_t requested) {
        deferred_entry.cancel();const auto decision=entry_decision(requested);page=decision.page;
        if(decision.pending_math)deferred_entry.defer(requested);
        else (void)state::mark_page_reached(data,lesson,page);
    }
    bool finish_entry(std::uint64_t generation) {
        const auto requested=deferred_entry.requested(generation);
        if(!requested)return false;
        const auto decision=entry_decision(*requested);
        if(decision.pending_math)return false;
        deferred_entry.cancel();page=decision.page;
        (void)state::mark_page_reached(data,lesson,page);
        if(!import_open)render_page();
        return true;
    }
    bool reference_available(const curriculum::Block& reference) const {
        return curriculum::compiler_reference_available(lesson,page,reference,completion_snapshot(),[&](const auto& prior){
            return windows::compiler_puzzle_completed(data,lesson,prior);
        });
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
            const auto ink=colors.accent;
            const int side=MulDiv(px(43),4,5);const int x=bounds.left+(bounds.right-bounds.left-side)/2;
            const RECT glyph{x,bounds.top+px(12),x+side,bounds.top+px(12)+side};
            const auto route_icon=route=="math_physics"?Icon::attempt_math:Icon::terminal;
            const auto progress=attempt_state.premium?1.:std::clamp(attempt_state.progress,0.,1.);
            attempt_indicator::draw(item.hDC,glyph,route_icon,ink,
                colors.high_contrast?colors.text:RGB(255,255,255),progress);
            if(attempt_state.premium){
                // The original 30x26dp ImageView sits at (8,-1) inside the
                // 80%-scaled Android attempt indicator; it replaces both labels.
                const int left=bounds.left+MulDiv(px(8),4,5),top=bounds.top-MulDiv(px(1),4,5);
                const RECT infinity{left,top,left+MulDiv(px(30),4,5),top+MulDiv(px(26),4,5)};
                draw_icon(item.hDC,infinity,Icon::infinity_premium,colors.high_contrast?colors.text:ink);
                return true;
            }
            const auto badge=CreateFontW(-px(12),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
            const auto old=SelectObject(item.hDC,badge);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);
            auto left=bounds;left.left+=MulDiv(px(12),4,5);left.right=left.left+MulDiv(px(22),4,5);left.bottom=left.top+px(15);
            const auto count=std::to_wstring(attempt_state.available);
            DrawTextW(item.hDC,count.c_str(),-1,&left,DT_CENTER|DT_SINGLELINE);
            auto right=bounds;right.right-=MulDiv(px(4),4,5);right.bottom=right.top+px(15);
            const auto percent=std::to_wstring(static_cast<int>(std::round(attempt_state.progress*100)))+L"%";
            DrawTextW(item.hDC,percent.c_str(),-1,&right,DT_RIGHT|DT_SINGLELINE);
            auto remaining=attempt_indicator::recovery_bounds(bounds,glyph);
            const auto time=wide(lgaed::attempts::recovery_label(attempt_state.maximum,attempt_state.maximum*attempt_state.progress,attempt_state.premium));
            DrawTextW(item.hDC,time.c_str(),-1,&remaining,DT_CENTER|DT_SINGLELINE);
            SelectObject(item.hDC,old);DeleteObject(badge);return true;
        }
        const bool header=GetParent(item.hwndItem)==window;
        const int inset=header?(item.hwndItem==reader?9:item.hwndItem==language?5:4):10;
        InflateRect(&bounds,-px(inset),-px(inset));
        auto ink=colors.high_contrast?colors.text:item.hwndItem==reader?colors.accent:
            item.hwndItem==back?RGB(255,255,255):RGB(170,174,183);
        if(item.itemState&ODS_DISABLED){
            if(icon->second==Icon::reset&&!colors.high_contrast){
                const auto bg=reader_theme::background(item.hwndItem);
                ink=RGB((GetRValue(ink)*35+GetRValue(bg)*65)/100,(GetGValue(ink)*35+GetGValue(bg)*65)/100,
                    (GetBValue(ink)*35+GetBValue(bg)*65)/100);
            }else ink=colors.muted;
        }
        draw_icon(item.hDC,bounds,icon->second,ink);
        return true;
    }
    void clear_accessible_names() {
        if (!accessibility) return;
        const MSAAPROPID property = PROPID_ACC_NAME;
        for (const auto& row : rows) for (const auto item : row.cells)
            if (IsWindow(item)) accessibility->ClearHwndProps(item,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,&property,1);
    }
    void destroy_control(HWND item) {
        icon_focus.erase(item);
        wrap_modes.erase(item);
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
        if (checking_math == index) cancel_math();
        if (controls[index].math) cancel_restore();
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
            const bool locked = available && !activity_available(current);
            if (controls[index].rendered == available && (!available || controls[index].locked == locked)) continue;
            changed = true;
            remove_block(index);
            if (available) render_block(index);
        }
        if (changed) {
            std::stable_sort(rows.begin(),rows.end(),[](const auto& left, const auto& right) {
                // Navigation shares the non-block ID range with the heading,
                // but belongs after every active block when rows are revealed.
                const bool left_navigation=GetDlgCtrlID(left.cells.front())==continue_id;
                const bool right_navigation=GetDlgCtrlID(right.cells.front())==continue_id;
                if(left_navigation||right_navigation)return !left_navigation&&right_navigation;
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
        internal_message_box(window, wide(message).c_str(), L"LearnGameAndEngineDev", MB_OK | MB_ICONERROR);
    }
    void show_incomplete(std::size_t index) {
        show_settings_notice(window,block(index).type==BlockType::drag_fill_code
            ?ui("Completa todos los espacios primero.","Fill in all the blanks first.","Remplissez d’abord tous les espaces vides.")
            :block(index).type==BlockType::multiple_choice
            ?ui("Selecciona una respuesta.","Select an answer.","Sélectionnez une réponse.")
            :ui("Completa todos los campos de respuesta.","Complete all answer fields.","Remplissez tous les champs de réponse."));
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
    std::string math_mode(std::size_t index) const {
        if(index>=controls.size()||block(index).raw_json.empty())return "auto";
        const auto object=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(block(index).raw_json));
        return winrt::to_string(object.GetNamedString(L"mathMode",L"auto"));
    }
    HWND rich(std::string_view text, int id, bool code = false,
              std::span<const curriculum::Emphasis> emphasis = {}, bool editable = false,
              RichTextRole role = RichTextRole::body,
              std::optional<reader_theme::Surface> surface = {}) {
        auto result = control(MSFTEDIT_CLASS, "", ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP
            | (code&&!editable?WS_HSCROLL|ES_AUTOHSCROLL:0)
            | (editable ? WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL : ES_READONLY), id);
        reader_theme::apply(result,surface.value_or(laboratory()?reader_theme::Surface::screen:editable ? reader_theme::Surface::field
            : code ? reader_theme::Surface::card : reader_theme::Surface::canvas));
        if(code)reader_theme::use_native_scrollbar_theme(result);
        SendMessageW(result, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE | ENM_CHANGE | ENM_SELCHANGE);
        SendMessageW(result, EM_EXLIMITTEXT, 0, 2 * 1024 * 1024);
        set_rich_text(result, text, emphasis, code, role, lesson.locale);
        if(!code&&!editable){
            const auto* semantics=text_semantics(result,text);
            const bool block_prose=id>=1000&&((id-1000)%100==0||(id-1000)%100==19);
            if(route=="math_physics"&&(id==lesson_heading_id||block_prose))
                (void)set_original_math_text(result,text,block_prose?math_mode(static_cast<std::size_t>((id-1000)/100)):"auto",hooks.math_editor_request,emphasis,role,lesson.locale,semantics);
            else (void)set_native_math_inline_text(result,text,emphasis,role,lesson.locale,semantics);
        }
        SendMessageW(result,EM_SETZOOM,content_text_scale(),100);
        if (editable) {SendMessageW(result, EM_SETTARGETDEVICE, 0, 1);wrap_modes[result]=false;}
        SetWindowSubclass(result, wheel_proc, 1, 0);
        attach_gestures(result);
        return result;
    }
    void text_row(std::string_view text, int id, bool code = false,
                  std::span<const curriculum::Emphasis> emphasis = {}, RichTextRole role = RichTextRole::body,
                  std::optional<reader_theme::Surface> surface = {}) {
        const auto item = rich(text, id, code, emphasis, false, role, surface);
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
        auto result = button("",id,action); decorate_icon(result,icon,text);
        if(action.type==ActionType::fullscreen_source||action.type==ActionType::fullscreen_console
            ||action.type==ActionType::copy_simulated||action.type==ActionType::copy_readonly
            ||action.type==ActionType::copy_source||action.type==ActionType::copy_console||action.type==ActionType::copy_compiler_append
            ||action.type==ActionType::reset||action.type==ActionType::reset_answer
            ||action.type==ActionType::undo||action.type==ActionType::redo||action.type==ActionType::toggle_wrap)
            SetWindowSubclass(result,console_icon_proc,21,reinterpret_cast<DWORD_PTR>(this));
        return result;
    }
    ConsoleFocus capture_console_focus() const {
        ConsoleFocus snapshot;const auto focused=GetFocus();
        if(!focused||!IsChild(window,focused)||!IsWindowEnabled(focused))return snapshot;
        wchar_t kind[32]{};GetClassNameW(focused,kind,32);
        const bool native=_wcsnicmp(kind,L"RichEdit",8)==0||_wcsicmp(kind,L"EDIT")==0;
        if(!native&&!(route=="math_physics"&&_wcsicmp(kind,L"LearnGameAndEngineDev.MathExpressionEditor")==0))return snapshot;
        // Android's code-input TextViews retain their focused input when a
        // toolbar icon is touched. Readonly prose does not become an editor.
        if(route=="programming"&&(GetWindowLongPtrW(focused,GWL_STYLE)&ES_READONLY))return snapshot;
        snapshot.editor=focused;snapshot.native_selection=native;
        if(native)SendMessageW(focused,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&snapshot.selection));return snapshot;
    }
    void restore_console_focus(const ConsoleFocus& snapshot,bool selection=false) {
        if(!snapshot.editor||!IsWindow(snapshot.editor)||!IsWindowEnabled(snapshot.editor)||!IsWindowVisible(snapshot.editor))return;
        if(GetFocus()!=snapshot.editor)SetFocus(snapshot.editor);
        if(selection&&snapshot.native_selection)SendMessageW(snapshot.editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&snapshot.selection));
    }
    void queue_answer_viewport(HWND answer,bool final_edit=false) {
        if(updating||laying_out||syncing_answer_viewport||!answer||
            std::ranges::none_of(controls,[answer,final_edit](const auto& vm){return vm.answer==answer
                ||(final_edit&&(vm.source==answer||vm.input==answer));}))return;
        // EN_VSCROLL is raised before native caret scrolling is complete.
        // Coalesce selection/scroll notifications and inspect its final state.
        const auto [pending,inserted]=pending_answer_viewports.try_emplace(answer,final_edit);
        pending->second=pending->second||final_edit;
        if(inserted&&
            !PostMessageW(window,answer_viewport_message,reinterpret_cast<WPARAM>(answer),0))
            pending_answer_viewports.erase(answer);
    }
    void settle_prediction_caret(HWND answer) {
        // Only the autosized multiline output prediction needs this deferred
        // caret policy. A RichEdit scroll triggered before the layout update
        // is not evidence that its two rows actually overflow the final view.
        bool prediction=false;
        for(std::size_t index=0;index<controls.size();++index)
            if(controls[index].answer==answer&&block(index).type==BlockType::output_prediction) {
                prediction=!curriculum::uses_single_line_answer(block(index));break;
            }
        if(!prediction||!IsWindow(answer))return;
        RECT format{};SendMessageW(answer,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        const auto line_height=std::max(1,edit_line_height(answer,content_text_scale()));
        const auto lines=std::max<LRESULT>(1,SendMessageW(answer,EM_GETLINECOUNT,0,0));
        if(static_cast<std::int64_t>(lines)*line_height<=format.bottom-format.top){
            POINT origin{};SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&origin));
            if(origin.y){origin.y=0;SendMessageW(answer,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&origin));}
            return;
        }
        // Genuine overflow still scrolls, but only after the final native
        // document height and its visible formatting rectangle are established.
        CHARRANGE cursor{};SendMessageW(answer,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&cursor));
        POINTL caret{};SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&caret),cursor.cpMax);
        if(caret.y<format.top||caret.y+line_height>format.bottom)
            SendMessageW(answer,EM_SCROLLCARET,0,0);
    }
    void sync_answer_viewport(HWND answer) {
        const auto pending=pending_answer_viewports.find(answer);
        if(pending==pending_answer_viewports.end())return;
        const bool final_edit=pending->second;
        pending_answer_viewports.erase(pending);
        if(updating||laying_out||syncing_answer_viewport||!IsWindow(answer)||
            std::ranges::none_of(controls,[answer](const auto& vm){return vm.answer==answer||vm.source==answer||vm.input==answer;}))return;
        syncing_answer_viewport=true;
        struct SyncGuard { bool& value; ~SyncGuard(){value=false;} } guard{syncing_answer_viewport};
        // Measuring an autoheight field temporarily hides its scrollbars.
        // Keep the real native offset before that non-final measurement.
        POINT pre_measure_scroll{};
        SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&pre_measure_scroll));
        const bool compiler_source=!laboratory()&&!fullscreen_block&&
            std::ranges::any_of(controls,[answer](const auto& vm){return vm.source==answer;});
        if(final_edit&&(compiler_source||std::ranges::any_of(controls,[answer](const auto& vm){return vm.answer==answer;}))){
            // Assisted edits and history format while frozen. Only now can an
            // autoheight answer measure its final lines. Reposition following
            // rows if that height changed; cursor-only events stay lightweight.
            const auto body=std::ranges::find_if(rows,[answer](const auto& row){return row.cells.size()==1&&row.cells[0]==answer;});
            if(body!=rows.end()){
                if(compiler_source){
                    body->logical_height=compiler_source_minimum_height(answer);
                    // The source's EN_CHANGE was processed inside Freeze.
                    // Refresh its requested height only after native layout
                    // settles, rather than keeping the previous program's size.
                    SendMessageW(answer,EM_REQUESTRESIZE,0,0);
                }
                const auto fitted=update_answer_scrollbars(answer,16,compiler_source?16:0,16,true);
                auto expected=std::max(px(body->logical_height),fitted);
                if(body->size_to_text)expected=std::max(expected,body->requested_height);
                if(body->max_logical_height)expected=std::min(expected,px(body->max_logical_height));
                RECT bounds{};GetWindowRect(answer,&bounds);
                if(bounds.bottom-bounds.top!=expected){
                    // Restore the native viewport BEFORE reflow snapshots it.
                    // The provisional fit-height pass can otherwise replace a
                    // legitimate line-7 view with the top of the editor.
                    (void)update_answer_scrollbars(answer,16,compiler_source?16:0,16,false,-1,&pre_measure_scroll);
                    reflow();
                    if(final_edit)settle_prediction_caret(answer);
                    return;
                }
            }
        }
        // Home/End and selection moves change no text, so EN_CHANGE does not
        // reflow their answer. Clear a hidden offset only in an axis whose
        // complete document fits; retain native scrolling for real overflow.
        const auto owner=std::ranges::find_if(controls,[answer](const auto& vm){return vm.source==answer||vm.input==answer;});
        if(owner!=controls.end()){
            // A CodeEditor commits text and syntax inside TOM Freeze. Its
            // final layout notification is queued until native line geometry
            // exists; measuring during EN_CHANGE sees the previous document.
            const bool source=owner->source==answer;
            (void)update_answer_scrollbars(answer,source?16:0,source?16:0,source?16:0,false,-1,&pre_measure_scroll);
        }else (void)update_answer_scrollbars(answer,16,0,16,false,-1,&pre_measure_scroll);
        if(final_edit)settle_prediction_caret(answer);
    }
    int update_answer_scrollbars(HWND answer,int top_padding=16,int right_padding=0,int bottom_padding=16,
                                 bool fit_height=false,int left_padding=-1,const POINT* restored_offset=nullptr) {
        const auto style=GetWindowLongPtrW(answer,GWL_STYLE);
        POINT saved{};SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&saved));
        if(restored_offset)saved=*restored_offset;
        RECT format{};SendMessageW(answer,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        int numerator=0,denominator=0;SendMessageW(answer,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        const auto inset=[&](int logical){
            return numerator>0&&denominator>0?MulDiv(px(logical),denominator,numerator):px(logical);
        };
        const auto left=left_padding<0?format.left:inset(left_padding);
        // Measure the full width first. An old vertical bar must not narrow
        // wrapped text enough to keep itself alive after content shrinks.
        if(style&WS_HSCROLL)SendMessageW(answer,EM_SHOWSCROLLBAR,SB_HORZ,FALSE);
        if(style&WS_VSCROLL)SendMessageW(answer,EM_SHOWSCROLLBAR,SB_VERT,FALSE);
        const auto set_padding=[&]{
            RECT content{};GetClientRect(answer,&content);
            content.left=left;
            // EM_SETRECT's four insets belong to the unzoomed document.
            // Convert every inset, not just the top: a 16px bottom inset
            // otherwise becomes 32px at 200%, clipping a line that already
            // fits the outer height measured from TextServices.
            content.top=inset(top_padding);
            content.right=std::max(content.left+1,content.right-inset(right_padding));
            content.bottom=std::max(content.top+1,content.bottom-inset(bottom_padding));
            SendMessageW(answer,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
        };
        set_padding();
        RECT client{};GetClientRect(answer,&client);
        POINT offset{};SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        LONG document_right=0;
        const auto document=edit_text_document(answer);
        const int fallback_height=edit_line_height(answer,content_text_scale());
        LONG document_bottom=0;
        // RichEdit owns tabs, Unicode glyph widths, zoom and display wrapping.
        // Its default 0..100/page=0 range is not a document measurement.
        const bool has_text=GetWindowTextLengthW(answer)>0;
        const auto lines=std::max<LRESULT>(1,SendMessageW(answer,EM_GETLINECOUNT,0,0));
        for(LRESULT line=0;line<lines;++line){
            const auto start=SendMessageW(answer,EM_LINEINDEX,static_cast<WPARAM>(line),0);
            if(start<0)continue;
            const auto length=SendMessageW(answer,EM_LINELENGTH,static_cast<WPARAM>(start),0);
            POINTL end{};SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),start+length);
            document_right=std::max(document_right,end.x+offset.x);
            // The last character's native bottom is the clipping boundary
            // that must fit. Do not derive it from an unrelated GDI font or
            // from the previous outer size of an empty answer.
            const auto character=static_cast<LONG>(start+std::max<LRESULT>(0,length-1));
            if(const auto bounds=edit_character_bounds(document.Get(),character))
                document_bottom=std::max(document_bottom,bounds->bottom+offset.y);
            else document_bottom=std::max(document_bottom,end.y+offset.y+fallback_height);
        }
        const auto dpi=GetDpiForWindow(answer);
        const int bar_width=GetSystemMetricsForDpi(SM_CXVSCROLL,dpi),bar_height=GetSystemMetricsForDpi(SM_CYHSCROLL,dpi);
        // A trailing Enter creates a native row even while TOM reports a
        // stale/offscreen insertion-point rectangle during the edit freeze.
        // Include all visual rows when autosizing: otherwise RichEdit scrolls
        // the first answer line away before the new row receives its height.
        const auto line_floor=std::min<std::int64_t>(std::numeric_limits<LONG>::max(),
            static_cast<std::int64_t>(px(top_padding))+static_cast<std::int64_t>(lines)*fallback_height);
        const auto bottom=std::max<std::int64_t>(line_floor,document_bottom)+px(bottom_padding);
        const auto right=document_right+px(right_padding);
        bool horizontal=has_text&&(style&ES_AUTOHSCROLL)&&right>client.right;
        const bool vertical=!fit_height&&has_text&&bottom>client.bottom-(horizontal?bar_height:0)+px(1);
        if(vertical&&(style&ES_AUTOHSCROLL))horizontal=has_text&&right>client.right-bar_width;
        SendMessageW(answer,EM_SHOWSCROLLBAR,SB_HORZ,horizontal);
        SendMessageW(answer,EM_SHOWSCROLLBAR,SB_VERT,vertical);
        set_padding();
        // Hiding a bar alone can retain a native offset. Preserve real scroll
        // positions, but return a direction with no overflow to its origin.
        if(!horizontal)saved.x=0;
        if(!vertical)saved.y=0;
        SendMessageW(answer,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&saved));
        return static_cast<int>(bottom)+(horizontal?bar_height:0);
    }
    void toolbar_caption(HWND target,std::string_view caption) {
        set_rich_text(target,caption,{},false,RichTextRole::prompt);
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR|CFM_FACE;
        format.crTextColor=reader_theme::palette(target).high_contrast?reader_theme::foreground(target):RGB(225,228,235);
        wcscpy_s(format.szFaceName,L"Roboto");SendMessageW(target,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
    }
    int toolbar_caption_width(HWND target) const {
        CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(target,EM_GETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
        const auto font=CreateFontW(-MulDiv(format.yHeight,static_cast<int>(GetDpiForWindow(window))*content_text_scale(),1440*100),0,0,0,
            (format.dwEffects&CFE_BOLD)?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,format.szFaceName);
        const auto dc=GetDC(target);if(!dc){if(font)DeleteObject(font);return std::max(112,MulDiv(112,content_text_scale(),100));}
        const auto old=font?SelectObject(dc,font):nullptr;
        const auto label=wide(edit_text(target));SIZE size{};GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&size);
        if(font){SelectObject(dc,old);DeleteObject(font);}ReleaseDC(target,dc);
        return std::max(1,MulDiv(size.cx+px(4),96,static_cast<int>(GetDpiForWindow(window))));
    }
    void simulated_header(std::size_t index,bool response) {
        const auto& current=block(index);auto code_language=current.language;
        const bool separate=current.type==BlockType::fill_code||current.type==BlockType::output_prediction;
        if(code_language.empty()||code_language=="cpp"||code_language=="c++")code_language="C++";
        else for(auto& letter:code_language)letter=static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
        const auto caption=rich("</> "+code_language,control_id(index,response&&separate?38:16));
        reader_theme::apply(caption,reader_theme::Surface::card_header);set_rich_text(caption,"</> "+code_language,{},false,RichTextRole::prompt);
        const auto copy=icon_button(response?(separate?ui("Copiar texto","Copy Text","Copier le texte"):ui("Copiar ejercicio","Copy exercise","Copier l'exercice")):ui("Copiar código","Copy code","Copier le code"),
            control_id(index,!response&&separate?39:18),{0,!response&&separate?ActionType::copy_readonly:ActionType::copy_simulated,index},Icon::copy);
        if(response){
            const auto reset=icon_button(ui("Reiniciar respuesta","Reset answer","Réinitialiser la réponse"),control_id(index,12),{0,ActionType::reset_answer,index},Icon::reset);
            const auto spacer=control(L"STATIC","",0,control_id(index,17));reader_theme::apply(spacer,reader_theme::Surface::card_header);
            const int caption_width=std::max(112,MulDiv(112,content_text_scale(),100));
            if(current.type==BlockType::multiple_choice||current.type==BlockType::fill_code||current.type==BlockType::output_prediction)
                rows.push_back({{caption,reset,spacer,copy},{caption_width,44,0,44},48});
            else if(current.type==BlockType::order_code){
                const auto expand=icon_button(ui("Pantalla completa","Full screen","Plein écran"),control_id(index,13),{0,ActionType::fullscreen_source,index},Icon::expand);
                rows.push_back({{caption,reset,spacer,copy,expand},{caption_width,44,0,44,44},48});
            }else{
                const auto wrap=wrap_button(index);
                const auto expand=icon_button(ui("Pantalla completa","Full screen","Plein écran"),control_id(index,13),{0,ActionType::fullscreen_source,index},Icon::expand);
                rows.push_back({{caption,reset,spacer,wrap,copy,expand},{caption_width,44,0,44,44,44},48});
            }
        }else{
            const auto wrap=wrap_button(index);
            const auto expand=icon_button(ui("Pantalla completa","Full screen","Plein écran"),control_id(index,separate?13:20),{0,ActionType::fullscreen_source,index},Icon::expand);
            rows.push_back({{caption,wrap,copy,expand},{0,44,44,44},48});
        }
        rows.back().simulated_card=index*2+(response?1:0);rows.back().card_header=true;
        rows.back().column_gap=0;
        // Android addCode() gives standalone reference cards an external 8dp
        // top margin. Do not apply it to the exercise/answer toolbars.
        if(current.type==BlockType::code)rows.back().padding_before=8;
        for(const auto item:rows.back().cells)reader_theme::apply(item,reader_theme::Surface::card_header);
        toolbar_caption(caption,"</> "+code_language);
    }
    HWND wrap_button(std::size_t index) {
        return icon_button(code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé")
            :ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"),
            control_id(index,35),{0,ActionType::toggle_wrap,index},code_wrap()?Icon::wrap_on:Icon::wrap_off);
    }
    void refresh_wrap_buttons() {
        for(std::size_t index=0;index<controls.size();++index)if(const auto toggle=find_item(control_id(index,35))) {
            icon_buttons[toggle]=code_wrap()?Icon::wrap_on:Icon::wrap_off;
            label_radio(toggle,code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé")
                :ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));
            InvalidateRect(toggle,nullptr,FALSE);
        }
    }
    void simulated_gutter(std::size_t index) {
        auto& vm=controls.at(index);
        vm.simulated_gutter=std::make_unique<windows::CodeGutter>(vm.simulated_code);
        windows::set_code_gutter_appearance(vm.simulated_code,RGB(20,20,20),RGB(111,119,130),3,3);
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
            // Use the *visible* required-card order. Presentation-only blank
            // separators never enter this copy, and an authored empty source
            // card still contributes its own physical line boundary.
            const auto order=vm.order_view?vm.order_view->order():vm.order;
            copy_text(curriculum::ordered_source_text(current.fragments,order));
            return;
        }
        if(vm.answer_editor){copy_text(vm.answer_editor->document().text);return;}
        std::string text=current.question+"\n\n";
        for(const auto& option:current.options)text+="• "+option+'\n';
        if(vm.response.selected>=0&&static_cast<std::size_t>(vm.response.selected)<current.options.size())
            text+=(vm.response.historical_choice?ui("\nRespuesta anterior: ","\nPrevious answer: ","\nRéponse précédente : ")
                :ui("\nSeleccionado: ","\nSelected: ","\nSélection : "))+state::selected_choice_text(vm.response,current);
        copy_text(text);
    }
    void simulated_menu(std::size_t index) {
        struct Popup { HMENU menu=CreatePopupMenu();~Popup(){if(menu)DestroyMenu(menu);} } popup;
        if(!popup.menu)throw std::runtime_error("Cannot create the native code menu");
        const bool response=exercise(block(index).type);
        const bool separate=block(index).type==BlockType::fill_code||block(index).type==BlockType::output_prediction;
        AppendMenuW(popup.menu,MF_STRING,control_id(index,separate?39:18),wide(response&&!separate?ui("Copiar ejercicio","Copy exercise","Copier l'exercice"):ui("Copiar código","Copy code","Copier le code")).c_str());
        if(response&&!separate)AppendMenuW(popup.menu,MF_STRING|(IsWindowEnabled(find_item(control_id(index,12)))?MF_ENABLED:MF_GRAYED),control_id(index,12),
            wide(ui("Reiniciar respuesta","Reset answer","Réinitialiser la réponse")).c_str());
        if(find_item(control_id(index,response?13:20)))
            AppendMenuW(popup.menu,MF_STRING,control_id(index,response?13:20),wide(ui("Pantalla completa","Full screen","Plein écran")).c_str());
        if(find_item(control_id(index,35)))
            AppendMenuW(popup.menu,MF_STRING|(code_wrap()?MF_CHECKED:0),control_id(index,35),wide(ui("Ajuste de línea","Line wrap","Retour à la ligne")).c_str());
        const auto style=prepare_native_menu(popup.menu,window);RECT bounds{};GetWindowRect(controls[index].simulated_code,&bounds);
        const auto selected=TrackPopupMenuEx(popup.menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_TOPALIGN,bounds.right,bounds.bottom,window,nullptr);
        if(selected)command(static_cast<int>(selected),BN_CLICKED,nullptr);
    }
    int pane_paint_offset(HWND host) const {
        if(const auto visible=overflow_view_origins.find(host);visible!=overflow_view_origins.end())return visible->second;
        for(const auto& [origin,layer]:overflow_panes)if(layer==host)return origin;
        return scroll;
    }
    void paint_simulated_cards(HDC dc,int offset) const {
        const auto saved=SaveDC(dc);
        SelectObject(dc,GetStockObject(NULL_PEN));SelectObject(dc,reader_theme::brush(reader_theme::Surface::card));
        for(auto surface:toolbar_surfaces){OffsetRect(&surface,0,-offset);FillRect(dc,&surface,reader_theme::brush(laboratory()?reader_theme::Surface::compiler_header:reader_theme::Surface::card_header));}
        for(auto surface:console_surfaces){OffsetRect(&surface,0,-offset);FillRect(dc,&surface,reader_theme::brush(reader_theme::Surface::compiler_output));}
        for(const auto& surface:simulated_card_surfaces){auto bounds=surface.bounds;OffsetRect(&bounds,0,-offset);
            RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(12),px(12));
            const auto clip=CreateRoundRectRgn(bounds.left,bounds.top,bounds.right+1,bounds.bottom+1,px(12),px(12));
            const auto state=SaveDC(dc);ExtSelectClipRgn(dc,clip,RGN_AND);DeleteObject(clip);
            auto header=bounds;header.bottom=surface.header_bottom-offset;FillRect(dc,&header,reader_theme::brush(reader_theme::Surface::card_header));RestoreDC(dc,state);
        }
        const auto outline=CreateSolidBrush(reader_theme::palette(window).outline);
        for(auto bounds:math_card_surfaces){OffsetRect(&bounds,0,-offset);
            const auto shape=CreateRoundRectRgn(bounds.left,bounds.top,bounds.right+1,bounds.bottom+1,px(20),px(20));
            FillRgn(dc,shape,reader_theme::brush(reader_theme::Surface::card));FrameRgn(dc,shape,outline,px(1),px(1));DeleteObject(shape);
        }
        DeleteObject(outline);
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
        if(!EmptyClipboard()){CloseClipboard();GlobalFree(memory);throw std::runtime_error(ui("No se pudo copiar el texto.","Could not copy the text.","Impossible de copier le texte."));}
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
        // An explicit channel selection must remain visible even when it has
        // zero bytes. Only automatic selection falls back to a nonempty stream.
        if (selected < 0 || selected > 2) {
            selected = 0;
            if (vm.console_channels[0].empty()) selected = vm.console_channels[1].empty() ? 2 : 1;
            if (vm.console_channels[static_cast<std::size_t>(selected)].empty()) selected = 0;
        }
        vm.console_channel = selected;
        const auto before = updating; updating = true;
        set_rich_text(vm.console,vm.console_channels[static_cast<std::size_t>(selected)],{},true);
        CHARFORMAT2W color{}; color.cbSize = sizeof(color); color.dwMask = CFM_COLOR;
        const auto colors = reader_theme::palette(window);
        color.crTextColor = colors.high_contrast ? colors.text : RGB(121,226,157);
        SendMessageW(vm.console,EM_SETSEL,0,-1);
        SendMessageW(vm.console,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&color));
        SendMessageW(vm.console,EM_SETSEL,0,0);
        console_caption(index);
        updating = before; reflow();
    }
    void toggle_fullscreen(std::size_t index,bool output) {
        auto focused=capture_console_focus();
        if(!focused.editor)for(const auto& [icon,snapshot]:icon_focus)if(snapshot.editor){focused=snapshot;break;}
        end_drag();
        restore_fullscreen_palette();
        restore_fullscreen_header();
        if (fullscreen_block == index && fullscreen_output == output) {
            fullscreen_block.reset();scroll=fullscreen_lesson_scroll.value_or(scroll);fullscreen_lesson_scroll.reset();
        }else {
            if(!fullscreen_block)fullscreen_lesson_scroll=scroll;
            fullscreen_block = index; fullscreen_output = output;scroll=0;
        }
        if(const auto simulated=controls[index].simulated_code)apply_wrap(simulated);
        resize();
        restore_console_focus(focused,true);
    }
    void code_menu(std::size_t index,bool output,bool context=false,std::optional<POINT> context_location={}) {
        const auto popup = CreatePopupMenu();
        if (!popup) throw std::runtime_error(ui("No se pudo abrir el menú de la consola.","Could not open the console menu.","Impossible d'ouvrir le menu de la console."));
        const auto add = [&](int part,std::string_view label,UINT flags = MF_STRING) {
            AppendMenuW(popup,flags,static_cast<UINT_PTR>(control_id(index,part)),wide(label).c_str());
        };
        CHARRANGE selection{};const auto edit=output?controls[index].console:controls[index].source;SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        const bool selected=selection.cpMax>selection.cpMin;
        if(output&&context){add(85,ui("Copiar","Copy","Copier"),MF_STRING|(selected?MF_ENABLED:MF_GRAYED));
            add(86,ui("Seleccionar todo","Select all","Tout sélectionner"),MF_STRING|(GetWindowTextLengthW(edit)>0?MF_ENABLED:MF_GRAYED));}
        else if (output) {
            const auto& channels = controls[index].console_channels;
            if (std::ranges::count_if(channels,[](const auto& value) { return !value.empty(); }) > 1) {
                for (int channel = 0; channel < 3; ++channel) if (!channels[static_cast<std::size_t>(channel)].empty())
                    add(78 + channel,console_label(channel),
                        MF_STRING | (controls[index].console_channel == channel ? MF_CHECKED : 0));
            }
        } else {
            add(73,ui("Deshacer","Undo","Annuler"),MF_STRING|(SendMessageW(edit,EM_CANUNDO,0,0)?MF_ENABLED:MF_GRAYED));
            add(74,ui("Rehacer","Redo","Rétablir"),MF_STRING|(SendMessageW(edit,EM_CANREDO,0,0)?MF_ENABLED:MF_GRAYED));
            if(context){AppendMenuW(popup,MF_SEPARATOR,0,nullptr);const bool editable=IsWindowEnabled(edit)&&!(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY);
                add(81,ui("Cortar","Cut","Couper"),MF_STRING|(editable&&selected?MF_ENABLED:MF_GRAYED));
                add(83,ui("Copiar","Copy","Copier"),MF_STRING|(selected?MF_ENABLED:MF_GRAYED));
                add(82,ui("Pegar","Paste","Coller"),MF_STRING|(editable&&(IsClipboardFormatAvailable(CF_UNICODETEXT)||IsClipboardFormatAvailable(CF_TEXT))?MF_ENABLED:MF_GRAYED));
                add(84,ui("Seleccionar todo","Select all","Tout sélectionner"),MF_STRING|(GetWindowTextLengthW(edit)>0?MF_ENABLED:MF_GRAYED));
            }else{
            add(70,ui("Copiar código","Copy code","Copier le code"));
            add(12,ui("Reiniciar código","Reset code","Réinitialiser le code"));
            add(75,ui("Pantalla completa","Full screen","Plein écran"));
            add(35,ui("Ajuste de línea","Line wrap","Retour à la ligne"),MF_STRING|(code_wrap()?MF_CHECKED:0));
            }
        }
        RECT bounds{};const auto anchor=find_item(control_id(index,output?31:19));
        GetWindowRect(anchor?anchor:controls[index].source,&bounds);
        if(context_location){auto location=*context_location;
            if(location.x==-1&&location.y==-1){POINTL caret{};SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&caret),selection.cpMin);location={caret.x,caret.y+px(24)};ClientToScreen(edit,&location);}
            bounds.right=location.x;bounds.bottom=location.y;}
        const auto style = prepare_native_menu(popup,window);
        const auto command_id = TrackPopupMenuEx(popup,TPM_RETURNCMD | (context?TPM_LEFTALIGN:TPM_RIGHTALIGN) | TPM_TOPALIGN,bounds.right,bounds.bottom,window,nullptr);
        DestroyMenu(popup);
        if (command_id) command(static_cast<int>(command_id),BN_CLICKED,nullptr);
    }
    void console_caption(std::size_t index){
        const auto caption=find_item(control_id(index,30));if(!caption)return;
        const auto before=updating;updating=true;set_rich_text(caption,console_label(controls[index].console_channel));
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE|CFM_BOLD|CFM_COLOR;
        format.yHeight=15*14;format.crTextColor=reader_theme::palette(window).high_contrast?reader_theme::palette(window).text:RGB(174,180,190);
        SendMessageW(caption,EM_SETSEL,0,-1);SendMessageW(caption,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(caption,EM_SETSEL,0,0);
        SendMessageW(caption,EM_SETZOOM,content_text_scale(),100);updating=before;
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
        for(auto& vm:controls){if(vm.math)vm.math->set_text_scale(content_text_scale());if(vm.fill)vm.fill->set_text_scale(content_text_scale());if(vm.order_view)vm.order_view->set_text_scale(content_text_scale());if(vm.project)vm.project->set_text_scale(content_text_scale());}
        if (button_font) DeleteObject(button_font);
        if (content_button_font) DeleteObject(content_button_font);
        button_font = font;
        content_button_font = content_font;
        if(laboratory_compiler_font)DeleteObject(laboratory_compiler_font);laboratory_compiler_font=compact_font;
    }
    void initialize(HWND target) {
        window = target;
        reader_theme::set_reference_mathematics(window,route=="math_physics");
        reader_theme::set_accent(window,reader_theme::appearance_accent(data,laboratory()?std::string_view{}:std::string_view(route)));
        reader_theme::apply(window,laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas);
        title = control(MSFTEDIT_CLASS, "", ES_READONLY | ES_MULTILINE, 105, window);
        set_rich_text(title,ui("Lección","Lesson","Leçon"),{},false,RichTextRole::lesson_title);
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
        settings = control(L"BUTTON",ui("Ajustes de la lección","Lesson Settings","Paramètres de la leçon"),WS_TABSTOP | BS_PUSHBUTTON,menu_id,window);
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
        decorate_icon(settings,Icon::settings,ui("Ajustes de la lección","Lesson Settings","Paramètres de la leçon"));
        pane = CreateWindowExW(WS_EX_CONTROLPARENT, pane_class, L"Contenido de la lección",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
            window, nullptr, GetModuleHandleW(nullptr), this);
        if (!pane) throw std::runtime_error("Cannot create the lesson viewport");
        fullscreen_header=CreateWindowExW(WS_EX_CONTROLPARENT,pane_class,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,0,0,window,nullptr,GetModuleHandleW(nullptr),this);
        if(!fullscreen_header)throw std::runtime_error("Cannot create the fixed fullscreen header");
        reader_theme::apply(fullscreen_header,reader_theme::Surface::card_header);
        fullscreen_palette=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(fullscreen_palette_id)),GetModuleHandleW(nullptr),nullptr);
        if(!fullscreen_palette)throw std::runtime_error("Cannot create the fullscreen code-card bank");
        reader_theme::apply(fullscreen_palette,reader_theme::Surface::inset);
        SetWindowTextW(pane,wide(ui("Contenido de la lección","Lesson content","Contenu de la leçon")).c_str());
        attach_gestures(pane);
        reader_theme::apply(pane,laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas);
        reader_theme::set_scrollbar_frame_hidden(pane,true);
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
        label_radio(settings,ui("Ajustes de la lección","Lesson Settings","Paramètres de la leçon"));
        for (std::size_t index = 0; index < controls.size(); ++index) {
            auto& vm = controls[index]; const auto& current = block(index); if (!vm.rendered) continue;
            if(laboratory()){
                if(const auto heading=find_item(lesson_heading_id)){set_rich_text(heading,laboratory_title(),{},false,RichTextRole::lesson_title);laboratory_text_style(heading,28,RGB(255,255,255),true);}
                if(const auto description=find_item(lesson_heading_id+1)){set_rich_text(description,laboratory_description());laboratory_text_style(description,18,RGB(174,180,190));}
                if(vm.compile)SetWindowTextW(vm.compile,wide(ui("Compilar y ejecutar","Compile and Run","Compiler et exécuter")).c_str());
                if(const auto wrap=find_item(control_id(index,35)))label_radio(wrap,code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé"):ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));
                refresh_laboratory_status();continue;
            }
            if (vm.locked) {
                set_rich_text(find_item(control_id(index,18)),ui(
                    "Completa y comprueba correctamente la actividad anterior requerida para abrir este editor.",
                    "Complete and correctly check the required previous activity to open this editor.",
                    "Termine et vérifie correctement l'activité précédente requise pour ouvrir cet éditeur."));
                continue;
            }
            if (current.type == BlockType::note) {
                const auto note = find_item(control_id(index,19));
                set_rich_text(note,ui("Nota","Note","Note"),{},false,RichTextRole::heading);
            }
            if(const auto instruction=find_item(control_id(index,37)))set_rich_text(instruction,secondary_instruction(current.type,current.optional_order_separators>0));
            if(vm.simulated_code||current.type==BlockType::multiple_choice||vm.fill||!vm.order.empty()||vm.answer){
                const auto copy=find_item(control_id(index,18));
                const bool response=exercise(current.type);
                const bool separate=current.type==BlockType::fill_code||current.type==BlockType::output_prediction;
                label_radio(copy,response?(separate?ui("Copiar texto","Copy Text","Copier le texte"):ui("Copiar ejercicio","Copy exercise","Copier l'exercice")):ui("Copiar código","Copy code","Copier le code"));
                if(separate)label_radio(find_item(control_id(index,39)),ui("Copiar código","Copy code","Copier le code"));
                if(vm.answer)set_edit_hint(vm.answer,current.type==BlockType::fill_code
                    ?ui("Escribe el código faltante","Enter the Missing Code","Saisissez le code manquant")
                    :ui("Escribe la salida","Enter the Output","Saisissez la sortie"));
                if(response)label_radio(find_item(control_id(index,12)),ui("Reiniciar respuesta","Reset answer","Réinitialiser la réponse"));
                if(const auto expand=find_item(control_id(index,response?13:20)))label_radio(expand,ui("Pantalla completa","Full screen","Plein écran"));
                if(const auto wrap=find_item(control_id(index,35)))label_radio(wrap,code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé")
                    :ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));
            }
            if (vm.source) {
                if(vm.project)vm.project->set_language(interface_language(data));
                const auto name = [&](int part,std::string text) { label_radio(find_item(control_id(index,part)),text); };
                name(12,ui("Reiniciar código","Reset code","Réinitialiser le code"));
                name(18,ui("Copiar código","Copy code","Copier le code")); name(20,ui("Pantalla completa","Full screen","Plein écran"));
                name(35,code_wrap()?ui("Ajuste de línea activado","Line wrap enabled","Retour à la ligne activé")
                    :ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));
                name(31,ui("Seleccionar canal de la consola","Choose console channel","Choisir le canal de la console"));
                if (vm.input) set_rich_text(find_item(control_id(index,17)),ui("Entrada","Input","Entrée"));
                SetWindowTextW(vm.compile,wide(current.compile_check
                    ? ui("Compilar y comprobar diagnóstico","Compile and check diagnostic","Compiler et vérifier le diagnostic")
                    : current.output_check
                    ? ui("Compilar, ejecutar y comprobar","Compile, run and check","Compiler, exécuter et vérifier")
                    : current.cmake?ui("Ejecutar objetivo construido","Run built target","Exécuter la cible construite")
                    : ui("Compilar y ejecutar","Compile and run","Compiler et exécuter")).c_str());
                console_caption(index);
            }
            sync_quiz_controls(index);
            if (vm.feedback_generated_verdict) {
                const auto boundary = vm.feedback_source.find('\n');
                auto text = verdict_text(vm.feedback_correct);
                if (vm.feedback_default_review) text += '\n' + default_review();
                else if (boundary != std::string::npos) text += vm.feedback_source.substr(boundary);
                const auto path=vm.feedback_field_path;
                feedback(index,text,vm.feedback_correct,true,vm.feedback_default_review,path);
            }
        }
        update_navigation();
    }
    void mark_dirty() { dirty = true; SetTimer(window, save_timer, 300, nullptr); }
    void remember_scroll() {
        if(deferred_entry.waiting())return;
        const auto lesson_scroll=fullscreen_block?fullscreen_lesson_scroll.value_or(scroll):scroll;
        data.preferences["native_windows"][scroll_key()] = static_cast<std::int64_t>(MulDiv(lesson_scroll, 96, static_cast<int>(GetDpiForWindow(window))));
    }
    void capture_draft(std::size_t index) {
        auto& vm = controls.at(index);
        if (!vm.source_editor) return;
        auto document = vm.source_editor->document();
        state::Draft value;
        const auto preserved = state::get_draft(data, draft_key(index), block(index).initial_code, block(index).standard_input.value_or(""));
        if(vm.project){vm.project->capture();value=preserved;}
        else {value.source=document.text;value.indentation_json=state::make_indentation_metadata(value.source,document.indentation_starts);
            value.selection_start=document.selection_start;value.selection_end=document.selection_end;}
        value.input = preserved.input;
        if (vm.input) {
            const auto visible = edit_text(vm.input);
            value.input = visible == vm.input_visual ? vm.input_original : visible;
        }
        state::put_draft(data, draft_key(index), value);
    }
    windows::CppRunRequest program_snapshot(std::size_t index) const {
        auto& vm=controls.at(index);windows::CppRunRequest request;
        request.revision=vm.revision;request.source_utf8=vm.source_editor->document().text;
        request.stdin_utf8=block(index).standard_input.value_or("");
        if(vm.input){const auto visible=edit_text(vm.input);request.stdin_utf8=visible==vm.input_visual?vm.input_original:visible;}
        request.projects_directory=environment.paths.projects;request.project_key=draft_key(index);
        if(vm.project)vm.project->snapshot(request);
        // All C++ exercises use the native MSVC backend while the optional WASI
        // alternative is not confirmed compatible with Windows app protection.
        if(block(index).compile_check){
            request.diagnostic=windows::prepare_cpp_diagnostic_request(block(index),request);
        }
        const auto workflow=block(index).compiler_workflow_check;
        if(workflow==curriculum::CompilerWorkflowCheck::assertion_abort_wasi_v1||
           workflow==curriculum::CompilerWorkflowCheck::assertion_suppressed_wasi_v1||
           workflow==curriculum::CompilerWorkflowCheck::assertion_repaired_wasi_v1) {
            const auto mode=workflow==curriculum::CompilerWorkflowCheck::assertion_abort_wasi_v1
                ?curriculum::AssertionMode::enabled_abort
                :workflow==curriculum::CompilerWorkflowCheck::assertion_suppressed_wasi_v1
                    ?curriculum::AssertionMode::disabled:curriculum::AssertionMode::repaired;
            // Assertions are observed on the actual WASI toolchain, never a
            // guessed MSVC termination code or an unsigned native replacement.
            request.backend=windows::CppRunRequest::Backend::wasi;
            if(curriculum::assertion_source_allowed(mode,block(index).initial_code,request.source_utf8))
                request.assertion_mode=mode;
        }
        if(workflow==curriculum::CompilerWorkflowCheck::observation_complete_v1||
           workflow==curriculum::CompilerWorkflowCheck::observation_differences_v1) {
            const auto mode=workflow==curriculum::CompilerWorkflowCheck::observation_complete_v1
                ?curriculum::ReportMode::complete:curriculum::ReportMode::differences;
            if(curriculum::report_main_only_edit(block(index).initial_code,request.source_utf8))
                request.observation_probe=mode;
        }
        if(workflow==curriculum::CompilerWorkflowCheck::semantic_rejection_boxes_v1||
           workflow==curriculum::CompilerWorkflowCheck::semantic_rejection_balance_v1){
            const auto operation=workflow==curriculum::CompilerWorkflowCheck::semantic_rejection_boxes_v1
                ?curriculum::RejectionOperation::boxes:curriculum::RejectionOperation::balance;
            if(curriculum::rejection_function_only_edit(block(index).initial_code,request.source_utf8,operation))
                request.rejection_probe=operation;
        }
        return request;
    }
    void refresh_project_actions(std::size_t index) {
        auto& vm=controls.at(index);if(!vm.project||!vm.compile)return;
        const bool busy=running_block==index;bool configured=false,built=false,valid=true;
        try{const auto state=windows::inspect_cpp_project(program_snapshot(index));configured=state.configured;built=state.built;}
        catch(...){valid=false;}
        vm.project->set_availability(busy,configured,built);
        EnableWindow(vm.compile,!busy&&valid&&(!block(index).cmake||
            (block(index).output_check&&block(index).compiler_workflow_check!=
                curriculum::CompilerWorkflowCheck::exact_original_project_runtime_v1)||built));
    }
    void project_changed(std::size_t index,bool source_changed) {
        if(updating)return;auto& vm=controls.at(index);
        if(source_changed){if(vm.selective_recovery&&vm.selective_recovery->complete())vm.selective_recovery->invalidate();
            ++vm.revision;clear_reference_completion(index);if(running_block==index)worker.request_stop();
            const auto current=program_snapshot(index);
            windows::invalidate_compiler_puzzle_attempt(data,lesson,block(index),current);
            // An earned objective does not keep a rejection for an old draft
            // visible or available to the reader after its capture expires.
            if(!windows::compiler_puzzle_completed(data,lesson,block(index))||
                (!vm.feedback_source.empty()&&!vm.feedback_correct&&
                    windows::compiler_puzzle_review(data,lesson,block(index),current).empty()))feedback(index,"",false);
            vm.console_channels={};const auto before=updating;updating=true;
            set_rich_text(vm.console,ui("El programa cambió. Ejecuta de nuevo para obtener una salida actual.",
                "The program changed. Run it again to obtain current output.","Le programme a changé. Exécute-le à nouveau pour obtenir une sortie actuelle."));updating=before;}
        if(source_changed&&block(index).cmake&&vm.status)set_rich_text(vm.status,ui(
            "El proyecto cambió. Construye la versión vigente; si cambiaste su descripción u opciones, configura de nuevo.",
            "The project changed. Build the current revision; configure again if its description or options changed.",
            "Le projet a changé. Construisez la version actuelle ; reconfigurez si sa description ou ses options ont changé."));
        style_cpp(vm.source);apply_wrap(vm.source);refresh_project_actions(index);mark_dirty();reflow();
    }
    void capture_page() {
        if(deferred_entry.waiting())return;
        for (std::size_t i = 0; i < controls.size(); ++i) if (controls[i].source_editor) capture_draft(i);
        remember_scroll();
        state::save_current_page(data, lesson, page);
    }
    void flush() {
        KillTimer(window, save_timer);
        if(deferred_entry.waiting())return;
        if (!dirty) return;
        capture_page();
        // Historical groups retain ownership; opted-in compiler objectives use
        // their captured execution proofs rather than an editable answer flag.
        const auto complete=curriculum::catalog_lesson_page_progress(data,lesson,[&](const auto& current){
            if(current.type==BlockType::cpp_compiler)return windows::compiler_puzzle_completed(data,lesson,current);
            const auto identity=curriculum::exercise_key(lesson,current);
            const auto response=state::get_response(data,lesson,current);
            return response&&response->correct&&(current.type!=BlockType::math_puzzle||verified_math.contains(identity));
        });
        const bool pending_math=std::ranges::any_of(lesson.pages,[&](const auto& p){return std::ranges::any_of(p.blocks,[&](const auto& current){
            if(current.type!=BlockType::math_puzzle)return false;
            const auto identity=curriculum::exercise_key(lesson,current);const auto response=state::get_response(data,lesson,current);
            return response&&response->correct&&!verified_math.contains(identity);
        });});
        const bool reached_all=state::all_pages_reached(data,lesson);
        if(!reached_all||!pending_math)data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=
            reached_all&&complete.total>0&&complete.completed==complete.total;
        store.save(data); dirty = false;
    }
    void flush_selection() {
        if (import_open || deferred_entry.waiting()) return;
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
        // Readonly sources declare scrollbar support at creation; RichEdit
        // then owns their automatic visibility. Forcing WS_HSCROLL afterwards
        // creates an empty nonclient bar without a native scroll range.
        const auto horizontal_flags=static_cast<LONG_PTR>(ES_AUTOHSCROLL);
        const auto desired=enabled ? style & ~horizontal_flags : style | horizontal_flags;
        const auto previous=wrap_modes.find(item);
        if(previous!=wrap_modes.end()&&previous->second==enabled&&desired==style)return;
        if(desired!=style)SetWindowLongPtrW(item,GWL_STYLE,desired);
        if(previous==wrap_modes.end()||previous->second!=enabled)SendMessageW(item,EM_SETTARGETDEVICE,0,enabled ? 0 : 1);
        wrap_modes[item]=enabled;
        if(desired!=style)SetWindowPos(item,nullptr,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
        const bool scale_changed = appearance::text_scale(candidate,course_id()) != content_text_scale();
        const bool wrap_changed = !laboratory() && appearance::code_wrap(candidate,course_id()) != code_wrap();
        const auto accent_route=laboratory()?std::string_view{}:std::string_view(route);
        const auto next_accent=reader_theme::appearance_accent(candidate,accent_route);
        const bool accent_changed = next_accent != reader_theme::appearance_accent(data,accent_route);
        store.save(candidate); data = std::move(candidate); dirty = false;
        // Narration checkpoints arrive during playback. Persist their history
        // without resetting zoom, layout or every child window's painting.
        // Compare effective course preferences so unrelated courses are quiet.
        if (accent_changed) {
            reader_theme::set_accent(window,next_accent);
            SendMessageW(page_status,PBM_SETBARCOLOR,0,reader_theme::palette(window).accent);
        }
        const auto was_updating = updating; updating = true;
        if (wrap_changed) {
            for (std::size_t index = 0; index < controls.size(); ++index) {
                apply_wrap(controls[index].source);
                apply_wrap(controls[index].simulated_code);
                if (block(index).type == BlockType::fill_code) apply_wrap(controls[index].answer);
                if(controls[index].order_view)controls[index].order_view->set_wrap(code_wrap());
                if(controls[index].fill)controls[index].fill->set_wrap(code_wrap());
            }
            refresh_wrap_buttons();
        }
        if (shortcuts_changed && editing_bar) editing_bar->set_shortcuts(parse_state_shortcuts(data));
        if (interface_changed) refresh_interface_labels();
        if (scale_changed || interface_changed) update_fonts();
        updating = was_updating;
        if (scale_changed || wrap_changed || interface_changed || (shortcuts_changed && editing_bar)) reflow();
        if (interface_changed) notify_content();
    }
    void cancel_run() {
        if (worker.joinable()) { worker.request_stop(); worker.join(); }
        const auto previous=running_block;
        if (running_block && *running_block < controls.size()) {
            const auto compile = controls[*running_block].compile;
            if (compile && IsWindow(compile)) EnableWindow(compile, TRUE);
        }
        running_block.reset();
        std::lock_guard guard(result_mutex); completed.reset();
        if(previous&&*previous<controls.size())refresh_project_actions(*previous);
    }
    void cancel_math(){
        if(math_worker.joinable()){math_worker.request_stop();math_worker.join();}
        if(checking_math&&*checking_math<controls.size()){
            const auto check=find_item(control_id(*checking_math,3));if(check)EnableWindow(check,TRUE);
        }
        checking_math.reset();std::lock_guard lock(math_mutex);math_result.reset();
    }
    void cancel_restore(){++restore_generation;if(restore_worker.joinable()){restore_worker.request_stop();restore_worker.join();}}
    void verify_saved_math(){
        if(!hooks.validate_math)return;
        cancel_restore();std::vector<RestoredMath> pending;
        std::vector<std::string> originals;
        for(const auto& p:lesson.pages)for(const auto& b:p.blocks)if(b.type==BlockType::math_puzzle){
            const auto identity=curriculum::exercise_key(lesson,b);const auto saved=state::get_response(data,identity);
            if(saved&&((saved->correct&&!verified_math.contains(identity))||state::response_reviewed(*saved,b))){
                RestoredMath request;request.key=identity;request.raw=saved->raw_json;
                try{
                    request.answer_identity=saved_math_answer_identity(request.raw);
                    request.content_fingerprint=saved_math_fingerprint(request.raw);
                }catch(...){rejected_math.insert(identity);continue;}
                request.generation=math_answer_generations[identity];
                request.restore_generation=restore_generation;request.entry_generation=deferred_entry.generation();
                pending.push_back(std::move(request));originals.push_back(b.raw_json);
            }
        }
        if(pending.empty()){(void)finish_entry(deferred_entry.generation());return;}
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
    std::optional<curriculum::TextSemantics> math_feedback_semantics(std::size_t index,
            const winrt::Windows::Data::Json::JsonObject& reply) const {
        using namespace winrt::Windows::Data::Json;
        const auto& vm=controls.at(index);
        if(!vm.math||!reply.HasKey(L"feedbackSources"))return {};
        std::vector<MathFeedbackSource> origins;
        for(const auto& item:reply.GetNamedArray(L"feedbackSources")){
            const auto source=item.GetObject();
            const auto first=source.GetNamedNumber(L"start"),last=source.GetNamedNumber(L"end");
            if(!std::isfinite(first)||!std::isfinite(last)||first<0||last<first||last>8*1024*1024||first!=std::floor(first)||last!=std::floor(last))
                throw std::runtime_error("Invalid math feedback source extent");
            origins.push_back({winrt::to_string(source.GetNamedString(L"path")),
                static_cast<std::size_t>(first),static_cast<std::size_t>(last)});
        }
        return vm.math->feedback_semantics(winrt::to_string(reply.GetNamedString(L"feedback",L"")),origins);
    }
    void finish_restore(){
        using namespace winrt::Windows::Data::Json;
        std::vector<RestoredMath> restored;{std::lock_guard lock(restore_mutex);restored.swap(restored_math);}
        bool accepted=false;std::optional<std::uint64_t> entry_generation;
        for(const auto& result:restored){
            if(result.restore_generation!=restore_generation)continue;
            const auto current=state::get_response(data,result.key);
            if(!current||math_answer_generations[result.key]!=result.generation||result.answer_identity.empty()||
                saved_math_answer_identity(current->raw_json)!=result.answer_identity||
                saved_math_fingerprint(current->raw_json)!=result.content_fingerprint)continue;
            accepted=true;entry_generation=result.entry_generation;
            if(result.valid){verified_math.insert(result.key);rejected_math.erase(result.key);}
            else {verified_math.erase(result.key);rejected_math.insert(result.key);}
            for(std::size_t i=0;i<controls.size();++i)if(key(i)==result.key){
                auto& vm=controls[i];if(!vm.math)continue;
                // Keep the latest presentation draft and its review checksum;
                // the worker evaluated only the unchanged mathematical answer.
                vm.response=*current;
                vm.response.correct=result.valid;
                if(result.valid||(result.compatible&&!current->correct)){
                    const auto reply=JsonObject::Parse(winrt::to_hstring(result.reply));
                    if(result.valid||!reply.GetNamedBoolean(L"correct",false)){
                        const auto semantics=math_feedback_semantics(i,reply);
                        feedback(i,winrt::to_string(reply.GetNamedString(L"feedback",L"")),result.valid,false,false,{},semantics?&*semantics:nullptr);
                    }
                }
                else feedback(i,ui("Comprueba de nuevo esta respuesta guardada. Se conservan todos sus datos.",
                    "Check this saved answer again. All its data has been kept.","Vérifie à nouveau cette réponse sauvegardée. Toutes ses données sont conservées."),false);
                sync_quiz_controls(i);
            }
        }
        if(entry_generation)(void)finish_entry(*entry_generation);
        if(accepted&&!deferred_entry.waiting())mark_dirty();
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
        if(vm.math){
            raw.Insert(L"responses",JsonObject::Parse(winrt::to_hstring(vm.math->answers_json())));
            vm.math_semantic_identity=math_answer_identity(JsonObject::Parse(winrt::to_hstring(vm.math->answer_identity_json())));
        }
        if(vm.order_view){vm.order=vm.order_view->order();vm.response.order.assign(vm.order.begin(),vm.order.end());}
        vm.response.raw_json=winrt::to_string(raw.Stringify());
    }
    void composition_changed(std::size_t index){
        if(updating||index>=controls.size())return;
        selected_puzzle=index;
        if(checking_math==index)cancel_math();
        verified_math.erase(key(index));
        rejected_math.erase(key(index));deferred_entry.cancel();
        if(controls[index].math)++math_answer_generations[key(index)];
        auto& vm=controls[index];++vm.revision;capture_composition(index);
        if(vm.order_view)vm.order_started=true;
        vm.response.correct=false;state::clear_response_review(vm.response);
        state::put_response(data, lesson, block(index), vm.response);mark_dirty();feedback(index,"",false);sync_quiz_controls(index);
        reflow();notify_content();
    }
    void composition_draft_changed(std::size_t index){
        if(updating||index>=controls.size()||!controls[index].math)return;
        auto& vm=controls[index];
        const auto identity=math_answer_identity(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(vm.math->answer_identity_json())));
        // A control cannot preserve an old verdict by routing a changed point
        // through the presentation-only callback. In-flight evaluation remains
        // valid only while its actual mathematical answer stays identical.
        if(identity!=vm.math_semantic_identity){composition_changed(index);return;}
        const auto previous=state::get_response(data,lesson,block(index));
        const bool reviewed=state::response_reviewed(vm.response,block(index));
        capture_composition(index);
        // The persisted legacy review checksum owns the complete raw payload,
        // including presentation drafts. Refresh that checksum only after the
        // semantic guard proves this is still the same reviewed answer.
        if(reviewed)state::mark_response_reviewed(vm.response,block(index));
        auto persisted=vm.response;
        if(!persisted.correct&&previous&&previous->correct&&!verified_math.contains(key(index))&&
            saved_math_answer_identity(previous->raw_json)==identity&&
            saved_math_fingerprint(previous->raw_json)==saved_math_fingerprint(persisted.raw_json)) {
            // Rendering temporarily hides an unverified success. A camera/tool
            // update must not overwrite the persisted result while it is checked.
            persisted.correct=true;
        }
        state::put_response(data,lesson,block(index),persisted);mark_dirty();
        sync_quiz_controls(index);
    }
    void check_math(std::size_t index){
        if(!hooks.validate_math){feedback(index,ui("La comprobación matemática no está disponible.",
            "Mathematical checking is unavailable.","La vérification mathématique est indisponible."),false);return;}
        if(!may_evaluate())return;
        cancel_math();auto& vm=controls.at(index);++math_answer_generations[key(index)];capture_composition(index);
        state::put_response(data, lesson, block(index), vm.response);mark_dirty();flush();
        const auto revision=vm.revision;const auto raw=block(index).raw_json,answers=vm.math->answers_json(),locale=lesson.locale;
        auto evaluate=hooks.validate_math;checking_math=index;
        EnableWindow(find_item(control_id(index,3)),FALSE);
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
        auto& vm=controls.at(index);EnableWindow(find_item(control_id(index,3)),TRUE);
        if(result->revision!=vm.revision)return;
        if(!result->error.empty()){feedback(index,result->error,false);return;}
        const auto reply=JsonObject::Parse(winrt::to_hstring(result->reply));
        const auto text=winrt::to_string(reply.GetNamedString(L"feedback",L""));
        const auto semantics=math_feedback_semantics(index,reply);
        if(!reply.GetNamedBoolean(L"evaluable",false)){
            feedback(index,text,false,false,false,{},semantics?&*semantics:nullptr);return;
        }
        vm.response.correct=reply.GetNamedBoolean(L"correct",false);
        rejected_math.erase(key(index));deferred_entry.cancel();
        record_attempt(index,vm.response.correct);
        if(vm.response.correct)verified_math.insert(key(index));else verified_math.erase(key(index));
        auto saved=JsonObject::Parse(winrt::to_hstring(vm.response.raw_json));
        if(reply.HasKey(L"contentFingerprint"))saved.Insert(L"contentFingerprint",reply.GetNamedValue(L"contentFingerprint"));
        vm.response.raw_json=winrt::to_string(saved.Stringify());
        state::mark_response_reviewed(vm.response,block(index));state::put_response(data, lesson, block(index), vm.response);mark_dirty();flush();
        feedback(index,text,vm.response.correct,false,false,{},semantics?&*semantics:nullptr);sync_quiz_controls(index);
        if(hooks.feedback_changed)hooks.feedback_changed(window,index,vm.response.correct);
    }
    void feedback(std::size_t index, std::string_view text, bool correct,
                  bool generated_verdict = false, bool uses_default_review = false,
                  std::string_view field_path={},const curriculum::TextSemantics* composed=nullptr) {
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
        auto& vm=controls.at(index);const auto& current=block(index);
        std::string path(field_path);
        if(path.empty()&&generated_verdict&&!uses_default_review&&exercise(current.type)&&current.type!=BlockType::math_puzzle){
            if(correct)path="/explanation";
            else if(current.type==BlockType::multiple_choice&&vm.response.selected>=0&&
                    static_cast<std::size_t>(vm.response.selected)<current.review_feedback_options.size())
                path="/reviewFeedback/"+std::to_string(vm.response.selected);
            else path="/reviewFeedback";
        }
        vm.feedback_field_path=path;vm.feedback_semantics.reset();
        if(composed){
            if(composed->source!=text)throw std::invalid_argument("Feedback annotations refer to a different source");
            vm.feedback_semantics=*composed;
        }else if(generated_verdict&&!uses_default_review){
            const auto found=current.text_semantics.find(path);
            vm.feedback_semantics=verdict_text_semantics(text,(correct?"✓ ":"✗ ")+verdict,
                found==current.text_semantics.end()?nullptr:&found->second);
        }
        const auto* semantics=vm.feedback_semantics?&*vm.feedback_semantics:nullptr;
        controls.at(index).feedback_source = std::string(text); controls.at(index).feedback_correct = correct;
        if(controls.at(index).compile&&curriculum::has_compiler_objective(block(index)))
            reader_theme::completed_control(controls.at(index).compile,correct&&!text.empty());
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
        set_rich_text(controls.at(index).feedback, text,{},false,RichTextRole::body,lesson.locale,{},semantics);
        if(route=="math_physics")(void)set_original_math_text(controls.at(index).feedback,text,math_mode(index),hooks.math_editor_request,{},RichTextRole::body,lesson.locale,semantics);
        else (void)set_native_math_inline_text(controls.at(index).feedback,text,{},RichTextRole::body,lesson.locale,semantics);
        if(block(index).type!=BlockType::math_puzzle){
            const auto target=controls.at(index).feedback;
            CHARRANGE previous{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
            SendMessageW(target,EM_SETSEL,0,-1);
            // Android feedback keeps native single spacing, unlike lesson
            // paragraphs (1.25). Preserve its exact text and narration offsets.
            PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_LINESPACING;
            paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;
            SendMessageW(target,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
            if(block(index).type==BlockType::fill_code&&!native_math_text_mapping(target,text).has_value()){
                CHARFORMAT2W size{};size.cbSize=sizeof(size);size.dwMask=CFM_SIZE;size.yHeight=16*15;
                SendMessageW(target,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&size));
            }
            if(formatted.starts_with("✓ ")||formatted.starts_with("✗ ")){
                // Bind the verdict glyph and its literal space to a font
                // that supplies both, without substituting source characters.
                SendMessageW(target,EM_SETSEL,0,2);
                CHARFORMAT2W symbol{};symbol.cbSize=sizeof(symbol);symbol.dwMask=CFM_FACE;
                wcscpy_s(symbol.szFaceName,L"Segoe UI Symbol");
                SendMessageW(target,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&symbol));
            }
            SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));
        }
        // The canonical math feedback already owns its verdict and source
        // line break. Keep those exact narration offsets and add the Android
        // blank line through presentation-only paragraph spacing.
        if(!generated_verdict&&block(index).type==BlockType::math_puzzle&&
           formatted.starts_with(correct?"✓ ":"✗ ")) {
            const auto boundary=formatted.find_first_of("\r\n");
            auto body=boundary;
            if(body!=std::string::npos) {
                if(formatted[body]=='\r'&&body+1<formatted.size()&&formatted[body+1]=='\n')++body;
                ++body;
                if(body<formatted.size()&&formatted[body]!='\r'&&formatted[body]!='\n') {
                    const auto target=controls.at(index).feedback;
                    CHARRANGE previous{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
                    CHARRANGE heading{0,static_cast<LONG>(wide(std::string_view(formatted).substr(0,boundary)).size())};
                    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&heading));
                    PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_SPACEAFTER;
                    paragraph.dySpaceAfter=MulDiv(rich_text_height(),25,20);
                    SendMessageW(target,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
                    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));
                }
            }
        }
        updating = previous_update; update_navigation(); reflow(); notify_content();
    }
    bool can_advance(std::size_t index) const {
        if(deferred_entry.waiting())return false;
        const auto& candidate = lesson.pages.at(index);
        const auto proofs = completion_snapshot();
        for (const auto& item : candidate.blocks) {
            if (!activity_available(item,proofs)) return false;
            if(compiler_completion_required(lesson,item)
                &&!windows::compiler_puzzle_completed(data,lesson,item))return false;
            if (!exercise(item.type)) continue;
            if(item.type==BlockType::math_puzzle&&!verified_math.contains(curriculum::exercise_key(lesson,item)))return false;
            const auto response = state::get_response(data, lesson, item);
            if (!response || !response->correct) return false;
        }
        return true;
    }
    bool can_enter(std::size_t index) const {
        if(deferred_entry.waiting())return false;
        const auto entry=entry_decision(index);
        if(entry.page!=index||entry.pending_math)return false;
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
    bool can_finish_lesson() const {
        if(!hooks.lesson_completed||!state::all_pages_reached(data,lesson))return false;
        for(std::size_t required=0;required<lesson.pages.size();++required)if(!can_advance(required))return false;
        return true;
    }
    void update_navigation() {
        if(reader)EnableWindow(reader,!deferred_entry.waiting());
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
            set_rich_text(title,caption,{},false,RichTextRole::lesson_title);
            SendMessageW(title,EM_SETSEL,0,-1);
            PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_LINESPACING;
            paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;
            SendMessageW(title,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
            CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_BOLD|CFM_COLOR|CFM_SIZE;
            const auto colors=reader_theme::palette(title);
            format.dwEffects=CFE_BOLD;format.yHeight=18*15;format.crTextColor=colors.high_contrast?colors.text:RGB(255,255,255);
            SendMessageW(title,EM_SETSEL,0,static_cast<LPARAM>(wide(label).size()));SendMessageW(title,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
            format.dwEffects=0;format.yHeight=15*15;format.crTextColor=colors.high_contrast?colors.text:RGB(174,180,190);
            SendMessageW(title,EM_SETSEL,wide(label).size(),-1);SendMessageW(title,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(title,EM_SETSEL,0,0);
            SendMessageW(page_status,PBM_SETPOS,page + 1,0);
        }
        if(const auto next=find_item(continue_id)){
            SetWindowTextW(next,wide(page+1==lesson.pages.size()?ui("Finalizar lección","Finish lesson","Terminer la leçon"):ui("Continuar","Continue","Continuer")).c_str());
            EnableWindow(next,page+1==lesson.pages.size()?can_finish_lesson():can_visit(page+1));
        }
    }
    bool can_visit(std::size_t value) const {
        if(deferred_entry.waiting())return false;
        if (value >= lesson.pages.size()) return false;
        if (!state::can_reach_page(data,lesson,value)) return false;
        if (value <= page) return true;
        for (std::size_t required = page; required <= value; ++required)
            if (!can_enter(required)) return false;
        for (std::size_t required = page; required < value; ++required)
            if (!can_advance(required)) return false;
        return true;
    }
    LessonPageState page_state() const {
        LessonPageState result{page,lesson.pages.size(),!deferred_entry.waiting()&&page > 0,page + 1 < lesson.pages.size() && can_visit(page + 1),
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
    LessonOptionsSnapshot options_snapshot() const {
        LessonOptionsSnapshot result;result.current_page=page;result.page_count=lesson.pages.size();
        if(!import_open)for(std::size_t index=0;index<lesson.pages.size();++index)
            if(can_visit(index))result.available_pages.push_back(index);
        result.finish_visible=page+1==lesson.pages.size()&&static_cast<bool>(hooks.lesson_completed);
        result.finish_enabled=result.finish_visible&&!import_open&&can_finish_lesson();
        return result;
    }
    bool invoke_option(LessonOptionAction action){
        if(import_open||deferred_entry.waiting())return false;
        switch(action){
        case LessonOptionAction::import_android_state:import_state();return import_open;
        case LessonOptionAction::page_top:scroll_to(0);return true;
        case LessonOptionAction::page_bottom:scroll_to(extent);return true;
        case LessonOptionAction::finish:return options_snapshot().finish_enabled&&finish_lesson();
        }
        return false;
    }
    void sync_quiz_controls(std::size_t index) {
        auto& vm = controls.at(index);
        if (!exercise(block(index).type)) return;
        const bool editable = !vm.response.correct;
        const auto focused=GetFocus();
        const auto check=find_item(control_id(index,3));
        const bool disables_focus=!editable&&focused&&(focused==vm.answer||focused==check||
            (vm.fill&&IsChild(vm.fill->window(),focused))||(vm.math&&IsChild(vm.math->window(),focused))||
            (vm.order_view&&IsChild(vm.order_view->window(),focused))||std::ranges::find(vm.choices,focused)!=vm.choices.end());
        if (vm.answer) EnableWindow(vm.answer, editable);
        reader_theme::completed_control(check,vm.response.correct);
        if(vm.answer)reader_theme::completed_control(vm.answer,vm.response.correct);
        if(vm.fill){vm.fill->set_enabled(editable);vm.fill->set_completed(vm.response.correct);}
        if(vm.math)vm.math->set_enabled(editable);
        if(vm.order_view){vm.order_view->set_enabled(editable);vm.order_view->set_completed(vm.response.correct);}
        for (auto choice : vm.choices) EnableWindow(choice, editable);
        bool changed=vm.response.correct||vm.response.selected>=0;
        if(vm.answer_editor)changed=changed||!vm.answer_editor->document().text.empty();
        if(vm.fill){
            if(block(index).type==BlockType::inline_fill_code)
                changed=changed||std::ranges::any_of(vm.fill->answers(),[](const auto& text){return !text.empty();});
            else changed=changed||std::ranges::any_of(vm.fill->slots(),[](const auto slot){return slot!=std::numeric_limits<std::size_t>::max();});
        }
        if(!vm.order.empty())changed=changed||!std::ranges::is_sorted(vm.order);
        if(const auto reset=find_item(control_id(index,12)))EnableWindow(reset,changed);
        SetWindowTextW(check,wide(editable ? ui("Comprobar","Check","Vérifier") : ui("Correcto ✓","Correct ✓","Correct ✓")).c_str());
        bool prepared=true;
        const auto& current=block(index);
        if(current.type==BlockType::multiple_choice)prepared=vm.response.selected>=0&&!vm.response.historical_choice;
        else if(vm.answer_editor){
            const auto answer=vm.answer_editor->document().text;
            prepared=(current.type==BlockType::fill_code?!answer.empty():!blank_answer(answer))
                ||(blank_answer(answer)&&curriculum::evaluate_text_answer(current,answer).correct);
        }else if(vm.fill){
            if(current.type==BlockType::inline_fill_code){const auto answers=vm.fill->answers();
                prepared=std::ranges::any_of(answers,[](const auto& answer){return !answer.empty();})
                    ||curriculum::evaluate_hole_answers(current,answers).correct;
            }else prepared=std::ranges::any_of(vm.fill->slots(),[&](auto slot){return slot<current.choices.size();});
        }
        EnableWindow(check,editable&&prepared&&(!vm.order_view||vm.order_started));
        // Disabling the focused native answer can leave the thread without a
        // focus window. Retain a live lesson anchor for Escape and the next
        // physical key, without moving focus away from another lesson control.
        if(disables_focus&&(!GetFocus()||GetFocus()==focused||!IsChild(window,GetFocus())))SetFocus(window);
    }
    void refresh_order(std::size_t index) {
        const auto previous_update = updating; updating = true;
        updating = previous_update; sync_quiz_controls(index); reflow();
        notify_content();
    }
    std::string compiler_progress_text(std::size_t index,bool just_passed=false) const {
        const auto& current=block(index);
        const auto checked=windows::compiler_puzzle_case_count(data,lesson,current);
        const auto total=current.output_check?current.output_check->cases.size():0;
        auto message=just_passed?ui("Caso correcto. ","Case passed. ","Cas correct. "):std::string{};
        message+=ui("Casos comprobados: ","Checked cases: ","Cas vérifiés : ")+std::to_string(checked)
            +ui(" de "," of "," sur ")+std::to_string(total)+".";
        if(checked<total)message+=ui(
            " Basta con comprobar correctamente el primer caso para avanzar; los demás son ensayos optativos.",
            "Passing the first authored case unlocks progress; the remaining cases are optional experiments.",
            "La réussite du premier cas suffit pour avancer ; les autres sont des essais facultatifs.");
        return message;
    }
    std::string compiler_history_text(const curriculum::Lesson& model,const curriculum::Block& current) const {
        if(current.type!=BlockType::cpp_compiler||!current.compiler_revision||
            windows::compiler_puzzle_completed(data,model,current))return {};
        const auto count=windows::compiler_puzzle_historical_case_count(data,model,current);
        if(!count)return {};
        auto text=windows::compiler_puzzle_historical_completed(data,model,current)
            ?ui("Logro anterior conservado. La ampliación de esta actividad sigue pendiente.",
                "Previous achievement retained. The expanded activity is still pending.",
                "Réussite précédente conservée. L’activité élargie reste à vérifier.")
            :ui("Comprobaciones anteriores conservadas. La ampliación de esta actividad sigue pendiente.",
                "Previous checks retained. The expanded activity is still pending.",
                "Vérifications précédentes conservées. L’activité élargie reste à vérifier.");
        const auto previous=windows::compiler_puzzle_historical_feedback(data,model,current);
        if(!previous.empty())text+='\n'+previous;
        return text;
    }
    std::string compiler_history_text(std::size_t index) const {return compiler_history_text(lesson,block(index));}
    void refresh_compiler_history(std::size_t index) {
        if(block(index).type!=BlockType::cpp_compiler||!block(index).compiler_revision)return;
        if(const auto target=find_item(control_id(index,40))){const auto text=compiler_history_text(index);
            if(edit_text(target)!=text)set_rich_text(target,text,{},false,RichTextRole::body,lesson.locale);}
    }
    std::string choice_history_text(const curriculum::Block& current,const state::Response& response) const {
        if(current.type!=BlockType::multiple_choice||!response.historical_choice||response.selected<0)return {};
        const auto label=response.correct?ui("Respuesta anterior (correcta): ","Previous answer (correct): ","Réponse précédente (correcte) : ")
            :state::response_reviewed(response,current)?ui("Respuesta anterior (incorrecta): ","Previous answer (incorrect): ","Réponse précédente (incorrecte) : ")
            :ui("Selección anterior: ","Previous selection: ","Sélection précédente : ");
        return label+state::selected_choice_text(response,current);
    }
    std::string choice_history_text(std::size_t index) const {
        return choice_history_text(block(index),controls[index].response);
    }
    void refresh_choice_history(std::size_t index) {
        if(block(index).type!=BlockType::multiple_choice||!block(index).choice_revision)return;
        if(const auto target=find_item(control_id(index,40)))
            set_rich_text(target,choice_history_text(index),{},false,RichTextRole::body,lesson.locale);
    }
    void create_feedback(std::size_t index) {
        auto& vm = controls[index];
        vm.feedback = rich("", control_id(index, 4));
        if(block(index).type==BlockType::math_puzzle)reader_theme::apply(vm.feedback,reader_theme::Surface::card);
        rows.push_back({{vm.feedback}, {0}, 0, true});
        const bool mathematical=block(index).type==BlockType::math_puzzle;
        if(mathematical){rows.back().horizontal_inset=6;rows.back().empty_padding_after=36;}
        // MathPuzzleRenderer owns 12dp inside its card and a separate 24dp
        // bottomMargin. The contour below closes after only the inner 12dp.
        rows.back().padding_before=12;rows.back().padding_after=mathematical?36:24;
        if(block(index).type==BlockType::cpp_compiler){
            if(const auto restored=windows::compiler_puzzle_current_feedback(data,lesson,block(index),program_snapshot(index))){
                const auto body=restored->text.find_first_of("\r\n");
                auto text=verdict_text(restored->correct);if(body!=std::string::npos)text+=restored->text.substr(body);
                feedback(index,text,restored->correct,true,false,restored->source_path);
            }
        } else if (vm.response.correct) {
            const auto& current=block(index);
            const auto explanation=current.type==BlockType::multiple_choice
                ?state::choice_correct_feedback(vm.response,current):current.explanation;
            feedback(index, verdict_text(true) + (explanation.empty() ? "" : "\n" + explanation), true, true);
        } else if (exercise(block(index).type) && state::response_reviewed(vm.response, block(index))) {
            const auto& current = block(index);
            // Restoring a reviewed attempt is not a new submission. Never
            // attach a success explanation to an imported incorrect attempt.
            const auto review=current.type==BlockType::multiple_choice?state::choice_incorrect_feedback(vm.response,current)
                :curriculum::incorrect_feedback(current,vm.response.selected<0?std::nullopt:std::optional<std::size_t>(vm.response.selected));
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
        if (!activity_available(current)) {
            vm.locked = true;
            text_row(current.type==BlockType::cpp_compiler?ui("Completa y comprueba correctamente la actividad anterior requerida para abrir este editor.",
                "Complete and correctly check the required previous activity to open this editor.",
                "Termine et vérifie correctement l'activité précédente requise pour ouvrir cet éditeur.")
                :ui("Completa y comprueba correctamente la actividad anterior requerida para abrir este ejercicio.",
                "Complete and correctly check the required previous activity to open this exercise.",
                "Termine et vérifie correctement l'activité précédente requise pour ouvrir cet exercice."),control_id(index,18));
            return;
        }
        if (exercise(current.type))
            if (const auto saved = state::get_response(data, lesson, current)) vm.response = *saved;
        if(current.type==BlockType::math_puzzle&&!verified_math.contains(key(index)))vm.response.correct=false;
        if (current.type == BlockType::heading) { text_row(current.text, control_id(index, 0), false, current.emphasis, RichTextRole::heading); return; }
        if(current.type==BlockType::image){
            vm.image=std::make_unique<CurricularImageView>(pane,control_id(index,5),lesson.source_path,*current.image);
            actions.push_back({control_id(index,5),ActionType::image_detail,index});
            rows.push_back({{vm.image->window()},{0},100,false});
            if(!vm.image->loaded()){text_row(vm.image->error(),control_id(index,18));vm.status=find_item(control_id(index,18));}
            text_row(current.image->alt,control_id(index,0));
            if(!current.image->caption.empty())text_row(current.image->caption,control_id(index,19));
            return;
        }
        if (current.type == BlockType::note) text_row(ui("Nota","Note","Note"),control_id(index,19),false,{},RichTextRole::heading);
        if (current.type == BlockType::paragraph || current.type == BlockType::note) { text_row(current.text, control_id(index, 0), false, current.emphasis); return; }
        if (current.type == BlockType::code) {
            simulated_header(index,false);vm.simulated_code=rich(current.code,control_id(index,1),true);simulated_gutter(index);apply_wrap(vm.simulated_code);
            rows.push_back({{vm.simulated_code},{0},26,true});rows.back().simulated_card=index*2;rows.back().card_end=true;return;
        }
        if(current.type==BlockType::math_puzzle){
            std::string saved="{}";
            if(!vm.response.raw_json.empty()){
                const auto old=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(vm.response.raw_json));
                if(old.HasKey(L"responses"))saved=winrt::to_string(old.GetNamedObject(L"responses").Stringify());
            }
            vm.math=std::make_unique<MathPuzzleView>(pane,current.raw_json,lesson.locale,saved,
                MathPuzzleCallbacks{[this,index]{composition_changed(index);},[this,index](std::string){check_answer(index);},
                    [this]{if(!updating){reflow();reveal_math_input(GetFocus());}},hooks.math_editor_request,[this](HWND field,bool focused){
                        if(!focused){hide_math_keyboard(field);reflow();return;}
                        if(hooks.math_editor_request){show_math_keyboard(field,hooks.math_editor_request,math_field_keyboard_context(field),lesson.locale);reflow();reveal_math_input(field);}
                    },[this](HWND field){
                        if(hooks.calculator_requested)hooks.calculator_requested(window,field);
                    },[this](HWND origin,UINT message,WPARAM keys,LPARAM point){
                        return drag_message(origin,message,keys,point);
                    },[this]{
                        (void)navigate_back();
                    },[this,index]{
                        toggle_fullscreen(index,false);
                    },[this,index]{
                        reset_answer(index);
                    },[this,index]{
                        composition_draft_changed(index);
                    }},current.text_semantics);
            vm.math_semantic_identity=math_answer_identity(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(vm.math->answer_identity_json())));
            SetWindowLongPtrW(vm.math->window(),GWLP_ID,control_id(index,5));
            SetWindowSubclass(vm.math->fullscreen_button(),console_icon_proc,21,reinterpret_cast<DWORD_PTR>(this));
            rows.push_back({{vm.math->window()},{0},40,false});
            rows.back().padding_after=0;
            auto check=button(ui("Comprobar","Check","Vérifier"),control_id(index,3),{0,ActionType::check,index});
            rows.push_back({{check},{0},48,false});rows.back().horizontal_inset=6;rows.back().padding_before=12;
            rows.back().padding_after=0;reader_theme::primary_button(check);create_feedback(index);return;
        }
        if (!current.question.empty()) text_row(current.question, control_id(index, 0), false, current.emphasis, RichTextRole::heading);
        if(const auto instruction=secondary_instruction(current.type,current.optional_order_separators>0);!instruction.empty()){
            text_row(instruction,control_id(index,37));
            reader_theme::set_text_color(find_item(control_id(index,37)),reader_theme::palette(window).high_contrast?reader_theme::foreground(window):RGB(180,185,195));
        }
        const bool simulated=exercise(current.type);
        const bool separate=current.type==BlockType::fill_code||current.type==BlockType::output_prediction;
        if(simulated&&(!separate||!current.code.empty()))simulated_header(index,!separate);
        if (!current.code.empty()){
            if(simulated){vm.simulated_code=rich(current.code,control_id(index,1),true);simulated_gutter(index);apply_wrap(vm.simulated_code);
                rows.push_back({{vm.simulated_code},{0},26,true});rows.back().simulated_card=index*2+(separate?0:1);rows.back().card_end=separate;}
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
                const auto wrap=wrap_button(index);
                rows.push_back({{caption,reset,spacer,wrap,copy,expand},{std::max(112,MulDiv(112,content_text_scale(),100)),44,0,44,44,44},48,false});
            }
            rows.back().toolbar=true;
            if(!laboratory()){rows.back().column_gap=0;rows.back().horizontal_inset=14;}
            rows.back().padding_after=0;
            for(const auto item:rows.back().cells)reader_theme::apply(item,laboratory()?reader_theme::Surface::compiler_header:reader_theme::Surface::card_header);
            toolbar_caption(caption,"</> C++");
            const auto saved = state::get_draft(data, draft_key(index), current.initial_code, current.standard_input.value_or(""));
            auto document = saved.has_saved
                ? windows::EditorDocument{saved.source, state::indentation_units(saved.source, saved.indentation_json), saved.selection_start, saved.selection_end}
                : predefined_document(current.initial_code);
            vm.source = rich("", control_id(index, 10), true, {}, true);
            vm.source_editor = std::make_unique<CodeEditor>(vm.source, std::move(document), true);
            if(!current.project_files.empty()){
                const auto saved_project=state::string_value(data,"cpp_lesson_drafts",draft_key(index)+"/project");
                const auto saved_cmake=state::string_value(data,"cpp_lesson_drafts",draft_key(index)+"/cmake");
                const auto active=store.root()/"active";
                const auto history_root=(std::filesystem::exists(active/"android")?active/"android":active)/"files"/"cpp_project_history";
                if(windows::compiler_selective_history_required(lesson,current))
                    vm.selective_recovery=std::make_unique<windows::CmakeSelectiveRecoveryTrace>(current);
                vm.project=std::make_unique<CppProjectView>(pane,current,vm.source,*vm.source_editor,saved_project,saved_cmake,
                    history_root,draft_key(index),interface_language(data),CppProjectCallbacks{
                        [this,index](std::string project,std::string cmake){auto& preferences=data.preferences["cpp_lesson_drafts"];
                            const auto base=draft_key(index);bool changed=false;
                            if(state::string_value(data,"cpp_lesson_drafts",base+"/project")!=project){preferences[base+"/project"]=std::move(project);changed=true;}
                            if(!cmake.empty()&&state::string_value(data,"cpp_lesson_drafts",base+"/cmake")!=cmake){preferences[base+"/cmake"]=std::move(cmake);changed=true;}
                            if(changed)mark_dirty();},
                        [this,index](bool changed){project_changed(index,changed);},
                        [this,index](windows::CppRunRequest::Action action){run(index,action);},
                        [this,index](const windows::CppHistoryRecoveryEvidence& proof){
                            if(auto* trace=controls.at(index).selective_recovery.get())trace->restore(proof);},
                        [this,index](const windows::CppHistoryComparisonEvidence& proof){
                            if(auto* trace=controls.at(index).selective_recovery.get())trace->compare(proof);},
                        [this,index](const windows::CppHistoryRecordEvidence& proof){
                            if(auto* trace=controls.at(index).selective_recovery.get())trace->record(proof);}});
                SetWindowLongPtrW(vm.project->window(),GWLP_ID,control_id(index,5));
                rows.push_back({{vm.project->window()},{0},48,false});rows.back().padding_after=0;
            }
            windows::set_code_gutter_appearance(vm.source,laboratory()?RGB(24,26,31):RGB(20,20,20),RGB(111,119,130),3);
            apply_wrap(vm.source);
            if(laboratory()){reader_theme::apply(vm.source,reader_theme::Surface::compiler_editor);reader_theme::set_text_color(vm.source,RGB(230,237,243));}
            style_cpp(vm.source);
            rows.push_back({{vm.source}, {0}, laboratory()?200:220, !laboratory(),0,laboratory()?0:320});
            if(laboratory())rows.back().padding_after=20;
            if(current.compiler_revision&&current.compiler_revision->starter_append){
                // The loader supplies only the independently reviewed test-call
                // insertion. Never replace or automatically edit a saved draft.
                const auto label=rich(ui("Caso adicional de la batería","Additional test case","Cas de test supplémentaire"),control_id(index,41));
                const auto append_copy=icon_button(ui("Copiar caso adicional","Copy additional case","Copier le cas supplémentaire"),
                    control_id(index,43),{0,ActionType::copy_compiler_append,index},Icon::copy);
                rows.push_back({{label,append_copy},{0,44},44,true});rows.back().padding_after=0;
                const auto snippet=rich(*current.compiler_revision->starter_append,control_id(index,42),true);
                style_cpp(snippet);apply_wrap(snippet);
                rows.push_back({{snippet},{0},48,true});
            }
            if(vm.project&&vm.project->configuration_window()){
                SetWindowLongPtrW(vm.project->configuration_window(),GWLP_ID,control_id(index,6));
                rows.push_back({{vm.project->configuration_window()},{0},160,false});
                vm.status=rich("",control_id(index,34));rows.push_back({{vm.status},{0},20,true});
            }
            if (current.standard_input) {
                text_row(ui("Entrada","Input","Entrée"), control_id(index, 17));
                vm.input = rich("", control_id(index, 15), false, {}, true);
                set_edit_text(vm.input, saved.input);
                vm.input_original = saved.input; vm.input_visual = edit_text(vm.input);
                rows.push_back({{vm.input}, {0}, 74, false});
            }
            vm.compile = button(current.compile_check ? ui("Compilar y comprobar diagnóstico","Compile and check diagnostic","Compiler et vérifier le diagnostic")
                : current.output_check&&current.compiler_workflow_check!=
                    curriculum::CompilerWorkflowCheck::exact_original_project_runtime_v1
                    ? ui("Compilar, ejecutar y comprobar","Compile, run and check","Compiler, exécuter et vérifier")
                : current.cmake?ui("Ejecutar objetivo construido","Run built target","Exécuter la cible construite")
                : ui("Compilar y ejecutar","Compile and run","Compiler et exécuter"), control_id(index, 11), {0, ActionType::run, index});
            rows.push_back({{vm.compile}, {0}, 48, false});
            reader_theme::primary_button(vm.compile);
            if(!laboratory()){rows.back().padding_before=12;rows.back().padding_after=4;rows.back().horizontal_inset=4;}
            if(!laboratory()&&!vm.status){
                vm.status=rich("",control_id(index,34));rows.push_back({{vm.status},{0},0,true});
                rows.back().padding_before=8;rows.back().padding_after=8;
                reader_theme::set_text_color(vm.status,reader_theme::palette(window).high_contrast?reader_theme::foreground(vm.status):RGB(174,180,190));
                set_rich_text(vm.status,current.compile_check
                    ?ui("Listo para comprobar el diagnóstico de compilación.","Ready to check the compilation diagnostic.","Prêt à vérifier le diagnostic de compilation.")
                    :ui("Listo para ejecutar.","Ready to Run.","Prêt pour l’exécution."));
            }
            for (const auto& [part,type] : std::array<std::pair<int,ActionType>,9>{{
                {70,ActionType::copy_source},{71,ActionType::copy_console},{72,ActionType::clear_console},
                {73,ActionType::undo},{74,ActionType::redo},{75,ActionType::fullscreen_source},{76,ActionType::fullscreen_console},
                {78,ActionType::select_stdout},{79,ActionType::select_stderr}}}) actions.push_back({control_id(index,part),type,index});
            actions.push_back({control_id(index,80),ActionType::select_diagnostics,index});
            for(const auto& [part,type]:std::array<std::pair<int,ActionType>,6>{{{81,ActionType::cut_source},{82,ActionType::paste_source},
                {83,ActionType::copy_source_selection},{84,ActionType::select_all_source},{85,ActionType::copy_console_selection},{86,ActionType::select_all_console}}})
                actions.push_back({control_id(index,part),type,index});
            if(laboratory()){
                rows.back().horizontal_inset=24;rows.back().padding_after=20;reader_theme::primary_button(vm.compile);
                vm.status=rich("",control_id(index,34));rows.push_back({{vm.status},{0},20,true});rows.back().horizontal_inset=24;rows.back().padding_after=10;
                vm.console=rich("",control_id(index,14),true);reader_theme::apply(vm.console,reader_theme::Surface::compiler_output);
                SetWindowLongPtrW(vm.console,GWL_STYLE,GetWindowLongPtrW(vm.console,GWL_STYLE)&~static_cast<LONG_PTR>(ES_AUTOHSCROLL));
                SendMessageW(vm.console,EM_SETTARGETDEVICE,0,0);rows.push_back({{vm.console},{0},120,true});rows.back().horizontal_inset=24;
                laboratory_text_style(vm.console,18,RGB(121,226,157));refresh_laboratory_status();return;
            }
            auto console_title = rich(console_label(0),control_id(index,30));
            reader_theme::apply(console_title,reader_theme::Surface::compiler_output);
            auto selector=button("▾",control_id(index,31),{0,ActionType::console_menu,index});reader_theme::icon_button(selector);
            reader_theme::apply(selector,reader_theme::Surface::compiler_output);label_radio(selector,ui("Seleccionar canal de la consola","Choose console channel","Choisir le canal de la console"));
            rows.push_back({{console_title,selector},{0,48},48,false});rows.back().horizontal_inset=16;rows.back().padding_after=0;
            console_caption(index);
            vm.console = rich("", control_id(index, 14), true);
            reader_theme::apply(vm.console,reader_theme::Surface::compiler_output);
            SetWindowLongPtrW(vm.console,GWL_STYLE,GetWindowLongPtrW(vm.console,GWL_STYLE) | WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL);
            SendMessageW(vm.console,EM_SETTARGETDEVICE,0,1);
            rows.push_back({{vm.console}, {0}, 72, true,0,240});rows.back().horizontal_inset=16;rows.back().padding_after=0;
            reader_theme::set_text_color(vm.console,RGB(121,226,157));
            create_feedback(index);refresh_project_actions(index);
            if(!laboratory()&&!curriculum::has_compiler_objective(current)&&vm.status)set_rich_text(vm.status,unavailable_compiler_status());
            if(current.compiler_revision){
                const auto history=rich(compiler_history_text(index),control_id(index,40));
                rows.push_back({{history},{0},0,true});
            }
            if(current.output_check&&vm.status&&!windows::compiler_puzzle_completed(data,lesson,current)){
                const auto count=windows::compiler_puzzle_case_count(data,lesson,current);
                if(count)set_rich_text(vm.status,compiler_progress_text(index));
            }
            return;
        }
        if(current.type==BlockType::inline_fill_code||current.type==BlockType::drag_fill_code){
            vm.fill=std::make_unique<CppFillView>(pane,current,vm.response.raw_json,
                [this,index]{composition_changed(index);},[this,index]{check_answer(index);},
                [this](HWND origin,UINT message,WPARAM keys,LPARAM point){return drag_message(origin,message,keys,point);});
            vm.fill->set_wrap(code_wrap());
            SetWindowLongPtrW(vm.fill->window(),GWLP_ID,control_id(index,5));
            rows.push_back({{vm.fill->window()},{0},80,false});
        } else if (current.type == BlockType::multiple_choice) {
            if(const auto history=choice_history_text(index);!history.empty()){
                text_row(history,control_id(index,40),false,{},RichTextRole::body,reader_theme::Surface::card);
            }
            for (std::size_t option = 0; option < current.options.size(); ++option) {
                const auto radio = control(L"BUTTON", "", BS_RADIOBUTTON | WS_TABSTOP,
                    control_id(index, 20 + static_cast<int>(option) * 2));
                label_radio(radio,current.options[option]);
                actions.push_back({control_id(index, 20 + static_cast<int>(option) * 2), ActionType::choose, index, option});
                SendMessageW(radio, BM_SETCHECK, !vm.response.historical_choice&&vm.response.selected == static_cast<std::int64_t>(option) ? BST_CHECKED : BST_UNCHECKED, 0);
                vm.choices.push_back(radio);
                auto label = rich(current.options[option], control_id(index, 21 + static_cast<int>(option) * 2),
                    false,{},false,RichTextRole::body,reader_theme::Surface::card);
                choice_labels.emplace(label,radio);
                reader_theme::apply(radio,reader_theme::Surface::card);
                rows.push_back({{radio, label}, {28, 0}, 28, true});
                rows.back().simulated_card=index*2+1;rows.back().card_end=option+1==current.options.size();
            }
        } else if (current.type == BlockType::order_code) {
            vm.order.resize(current.fragments.size()); std::iota(vm.order.begin(), vm.order.end(), 0);
            if (vm.response.order.size() == vm.order.size()) {
                auto candidate = vm.response.order; std::ranges::sort(candidate);
                bool valid = true;
                for (std::size_t i = 0; i < candidate.size(); ++i) valid = valid && candidate[i] == static_cast<std::int64_t>(i);
                if (valid) for (std::size_t i = 0; i < vm.order.size(); ++i) vm.order[i] = static_cast<std::size_t>(vm.response.order[i]);
            }
            vm.order_view=std::make_unique<OrderCodeView>(pane,current.fragments,vm.order,
                [this,index]{selected_puzzle=index;composition_changed(index);},
                current.optional_order_separators);
            vm.order_started=!std::ranges::is_sorted(vm.order);
            SetWindowLongPtrW(vm.order_view->window(),GWLP_ID,control_id(index,5));
            vm.order_view->set_text_scale(content_text_scale());
            vm.order_view->set_wrap(code_wrap());
            rows.push_back({{vm.order_view->window()},{0},40,false});
        } else {
            if(separate)simulated_header(index,true);
            vm.answer = rich("", control_id(index, 2), true, {}, true);
            SendMessageW(vm.answer,EM_SETEVENTMASK,0,SendMessageW(vm.answer,EM_GETEVENTMASK,0,0)|ENM_SCROLL);
            reader_theme::apply(vm.answer,reader_theme::Surface::card);
            CHARFORMAT2W answer_format{};answer_format.cbSize=sizeof(answer_format);
            answer_format.dwMask=CFM_SIZE|CFM_COLOR;answer_format.yHeight=(current.type==BlockType::fill_code?17:18)*15;
            answer_format.crTextColor=reader_theme::palette(window).high_contrast?reader_theme::foreground(vm.answer):RGB(255,255,255);
            SendMessageW(vm.answer,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&answer_format));
            SendMessageW(vm.answer,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&answer_format));
            const bool multiline = !curriculum::uses_single_line_answer(current);
            vm.answer_editor = std::make_unique<CodeEditor>(vm.answer, windows::EditorDocument{vm.response.answer}, multiline,
                [this, index] { check_answer(index); }, true, current.type == BlockType::fill_code, false);
            SetWindowSubclass(vm.answer,quiz_answer_proc,24,reinterpret_cast<DWORD_PTR>(this));
            set_edit_hint(vm.answer,current.type==BlockType::fill_code
                ?ui("Escribe el código faltante","Enter the Missing Code","Saisissez le code manquant")
                :ui("Escribe la salida","Enter the Output","Saisissez la sortie"));
            if (current.type == BlockType::fill_code) apply_wrap(vm.answer);
            rows.push_back({{vm.answer}, {0}, 1, false});
        }
        if(simulated){
            const auto first=std::ranges::find_if(rows,[index](const auto& row){return row.simulated_card==index*2+1&&row.card_header;});
            for(auto it=first;it!=rows.end();++it)it->simulated_card=index*2+1;
            rows.back().card_end=true;
        }
        auto check = button(ui("Comprobar","Check","Vérifier"), control_id(index, 3), {0, ActionType::check, index});
        rows.push_back({{check}, {0}, 48, false});
        rows.back().padding_before=12;rows.back().horizontal_inset=4;
        reader_theme::primary_button(check);create_feedback(index);
    }
    void render_page() {
        scroll_layout_valid=false;scroll_placements.clear();
        cancel_math();
        choice_labels.clear();selected_puzzle.reset();
        pending_answer_viewports.clear();
        end_drag();restore_fullscreen_palette();restore_fullscreen_header(); fullscreen_block.reset();fullscreen_lesson_scroll.reset();
        updating = true;
        controls.clear();
        for (const auto& row : rows) for (auto item : row.cells) destroy_control(item);
        rows.clear(); actions.clear();simulated_card_surfaces.clear();math_card_surfaces.clear();
        for(const auto& [origin,host]:overflow_panes)DestroyWindow(host);overflow_panes.clear();overflow_view_origins.clear();
        if(deferred_entry.waiting()){
            text_row(ui("Verificando las respuestas guardadas antes de continuar…",
                "Verifying saved answers before continuing…","Vérification des réponses enregistrées avant de continuer…"),lesson_heading_id);
            scroll=0;updating=false;update_fonts();update_navigation();reflow();notify_content();return;
        }
        controls.resize(lesson.pages.at(page).blocks.size());
        if(page==0){
            text_row(laboratory()?laboratory_title():lesson.title,lesson_heading_id,false,{},RichTextRole::lesson_title);
            if(laboratory()){
                rows.back().horizontal_inset=24;rows.back().padding_after=8;laboratory_text_style(find_item(lesson_heading_id),28,RGB(255,255,255),true);
                text_row(laboratory_description(),lesson_heading_id+1);rows.back().horizontal_inset=24;rows.back().padding_after=24;laboratory_text_style(find_item(lesson_heading_id+1),18,RGB(174,180,190));
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
    void place_reader_bar() {
        if(!reader_bar||!IsWindow(reader_bar)||!pane)return;
        RECT area{},viewport{};GetClientRect(window,&area);GetWindowRect(pane,&viewport);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&viewport),2);
        int bottom=static_cast<int>(area.bottom)-px(4);
        if(fullscreen_palette&&IsWindowVisible(fullscreen_palette)){
            RECT bank{};GetWindowRect(fullscreen_palette,&bank);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bank),2);
            bottom=std::min(bottom,static_cast<int>(bank.top)-px(4));
        }
        if(const auto keyboard=math_keyboard_window(GetAncestor(window,GA_ROOT));keyboard&&IsWindowVisible(keyboard)){
            RECT bounds{};GetWindowRect(keyboard,&bounds);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
            if(bounds.left<area.right&&bounds.right>0&&bounds.bottom>viewport.top)bottom=std::min(bottom,static_cast<int>(bounds.top)-px(4));
        }
        const RECT available{px(4),viewport.top,std::max<LONG>(px(4),area.right-px(4)),std::max<LONG>(viewport.top,bottom)};
        layout_lesson_reader_bar(reader_bar,available);
    }
    void resize() {
        if (!ready) return;
        const auto focused=GetFocus();const bool palette_reparented=sync_fullscreen_palette();
        RECT area{}; GetClientRect(window, &area);
        const int margin = px(4), available = static_cast<int>(area.right) - margin * 2;
        MoveWindow(back,margin,px(12),px(48),px(48),TRUE);
        int header_right = static_cast<int>(area.right) - margin - px(48);
        for (const auto item : {settings,attempts,language,reader}) {
            const int width=item==attempts?px(69):px(48);
            header_right-=width-px(48);
            MoveWindow(item,header_right,px(4),width,item==attempts?px(64):px(56),TRUE); header_right -= px(48);
        }
        MoveWindow(title,margin + px(58),px(4),std::max(px(60),header_right - margin - px(54)),px(64),TRUE);
        RECT title_content{};GetClientRect(title,&title_content);
        title_content.top=std::max(0L,(title_content.bottom-edit_line_height(title,100))/2);
        SendMessageW(title,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&title_content));
        MoveWindow(page_status,0,px(72),static_cast<int>(area.right),px(5),TRUE);
        auto fixed_header=fixed_header_row();
        int fixed_header_height=0;
        if(fixed_header){
            fixed_header_height=std::max(px(fixed_header->logical_height),edit_line_height(fixed_header->cells.front(),content_text_scale())+px(24));
            const auto focus=GetFocus();
            for(const auto child:fixed_header->cells)if(GetParent(child)!=fullscreen_header)SetParent(child,fullscreen_header);
            if(focus&&IsWindow(focus))SetFocus(focus);
            SetWindowPos(fullscreen_header,HWND_TOP,0,0,area.right,fixed_header_height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        }else restore_fullscreen_header();
        const int viewport_top=fullscreen_block?fixed_header_height:px(laboratory()?48:lesson_header_height);
        for(const auto item:{back,title,page_status,settings,attempts,language,reader})ShowWindow(item,fullscreen_block||(laboratory()&&item!=back)?SW_HIDE:SW_SHOWNOACTIVATE);
        if(laboratory())MoveWindow(back,px(4),0,px(48),px(48),TRUE);
        int nav_top = static_cast<int>(area.bottom);
        const bool assisted = editing_bar && editing_bar->visible() && !import_open;
        const int bar_height = editing_bar ? editing_bar->logical_height() : 0;
        const int editing_height = assisted ? px(bar_height + 8) : 0;
        fullscreen_palette_max_height=std::max(0,nav_top-viewport_top-editing_height-px(18));
        if(fullscreen_palette_block){
            auto& fill=*controls[*fullscreen_palette_block].fill;
            fullscreen_palette_height=std::min(fullscreen_palette_max_height,fill.palette_height_for_width(area.right));
            nav_top-=fullscreen_palette_height;
            SetWindowPos(fullscreen_palette,HWND_TOP,0,nav_top,area.right,fullscreen_palette_height,SWP_NOACTIVATE|SWP_SHOWWINDOW|SWP_NOCOPYBITS);
            SetWindowPos(fill.palette_window(),nullptr,0,0,area.right,fullscreen_palette_height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
        }
        if (editing_bar) editing_bar->place(std::max(margin,(static_cast<int>(area.right) - std::min(available,px(1050))) / 2),
            nav_top - px(bar_height + 12),std::min(available,px(1050)),px(bar_height));
        MoveWindow(pane,0,viewport_top,static_cast<int>(area.right),std::max(px(18),nav_top - viewport_top - editing_height),TRUE);
        if (import_open) layout_import();
        reflow();
        if(palette_reparented&&focused&&IsWindow(focused)&&IsWindowEnabled(focused)&&IsWindowVisible(focused)&&GetFocus()!=focused)SetFocus(focused);
    }
    void open_lesson_options() {
        // The application hosts the complete settings page. Its lesson actions
        // use options_snapshot/invoke_option and the original navigation gates.
        if(hooks.settings_requested)PostMessageW(window,header_action_message,settings_id,0);
    }
    // Translation keeps the measured document, native edit viewports and
    // mathematical layouts intact. Only a content/size change measures again.
    void position_rows(std::vector<Placement>& placements,const RECT& area,bool resize_rows) {
        const UINT placement_flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW
            |(resize_rows?0:SWP_NOSIZE);
        // Commit only final, clamped positions. Suppress the default bit-copy:
        // reused client pixels are stale when several child rows move past
        // one another. Repaint parent and children once after every move.
        // USER clamps child-window coordinates at signed 16-bit limits. Keep
        // very long lesson rows in local bands rather than moving a late
        // answer to y>32767, which overlaps its still-growing readonly source.
        constexpr int band_size=30000;
        std::set<int> active_bands;
        std::map<int,int> band_tops,band_bottoms;
        const auto focused=GetFocus();
        std::map<HWND,std::vector<const Placement*>> groups;
        for(auto& item:placements){
            if(item.fixed&&!resize_rows)continue;
            item.origin=item.fixed||fullscreen_block?0:(item.y/band_size)*band_size;
            item.parent=item.fixed?fullscreen_header:pane;
            if(item.origin){
                auto& host=overflow_panes[item.origin];
                if(!host){host=CreateWindowExW(WS_EX_CONTROLPARENT,pane_class,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
                    0,0,0,0,pane,nullptr,GetModuleHandleW(nullptr),this);
                    if(!host)throw std::runtime_error("Cannot create a long-lesson viewport band");
                    reader_theme::apply(host,reader_theme::Surface::canvas);attach_gestures(host);}
                active_bands.insert(item.origin);item.parent=host;
                const auto [first,inserted]=band_tops.try_emplace(item.origin,item.y);
                if(!inserted)first->second=std::min(first->second,item.y);
                band_bottoms[item.origin]=std::max(band_bottoms[item.origin],item.y+item.height);
            }
            if(GetParent(item.item)!=item.parent)SetParent(item.item,item.parent);
            groups[item.parent].push_back(&item);
        }
        overflow_view_origins.clear();
        for(const auto& [origin,host]:overflow_panes){
            const bool intersects=active_bands.contains(origin)&&band_bottoms.at(origin)>scroll&&band_tops.at(origin)<scroll+area.bottom;
            if(!intersects){
                if(active_bands.contains(origin))SetWindowPos(host,HWND_BOTTOM,0,origin-scroll,area.right,
                    std::max(1,band_bottoms.at(origin)-origin),SWP_NOACTIVATE|SWP_HIDEWINDOW|SWP_NOCOPYBITS|SWP_NOREDRAW);
                else ShowWindow(host,SW_HIDE);
            }
            else overflow_view_origins.emplace(host,std::max(band_tops.at(origin),scroll));
        }
        for(const auto& [host,visible_origin]:overflow_view_origins){
            const auto band=std::ranges::find_if(overflow_panes,[host](const auto& entry){return entry.second==host;});
            const auto bottom=std::min(band_bottoms.at(band->first),scroll+static_cast<int>(area.bottom));
            // Clip the host itself to the viewport and move its existing
            // children relative to that visible document origin. Otherwise
            // USER clamps a distant negative host Y to -32768 and an old
            // opaque band can cover the current answer on a very long page.
            SetWindowPos(host,HWND_BOTTOM,0,visible_origin-scroll,area.right,std::max(1,bottom-visible_origin),SWP_NOACTIVATE|SWP_SHOWWINDOW|SWP_NOCOPYBITS|SWP_NOREDRAW);
        }
        // Visible hosts begin at their first actual child, never at an empty
        // slice containing the preceding band's long row. Hidden hosts keep
        // their stable local origin to avoid USER's signed16 coordinate clamp.
        for(auto& item:placements)if(item.origin)if(const auto visible=overflow_view_origins.find(item.parent);visible!=overflow_view_origins.end())item.origin=visible->second;
        if(focused&&IsWindow(focused)&&GetFocus()!=focused)SetFocus(focused);
        // DeferWindowPos requires every HWND in a batch to share its parent.
        for(const auto& [parent,items]:groups){
            auto deferred=BeginDeferWindowPos(static_cast<int>(items.size()));
            if(deferred){for(const auto* item:items){
                const int y=item->y-(item->fixed?0:item->origin?item->origin:scroll);
                deferred=DeferWindowPos(deferred,item->item,nullptr,item->x,y,item->width,item->height,placement_flags);
                if(!deferred)break;
            }
            if(deferred&&!EndDeferWindowPos(deferred))deferred=nullptr;}
            if(!deferred)for(const auto* item:items)SetWindowPos(item->item,nullptr,item->x,
                item->y-(item->fixed?0:item->origin?item->origin:scroll),item->width,item->height,placement_flags);
        }
    }
    void paint_scrolled_viewport(bool synchronous=false) {
        RedrawWindow(pane,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN
            |(synchronous?RDW_UPDATENOW:0));
    }
    void reflow() {
        if (!pane) return;
        if (laying_out) { layout_pending = true; return; }
        scroll_layout_valid=false;
        if(fullscreen_palette_block&&*fullscreen_palette_block<controls.size()&&controls[*fullscreen_palette_block].fill){
            RECT area{};GetClientRect(window,&area);
            const auto height=std::min(fullscreen_palette_max_height,controls[*fullscreen_palette_block].fill->palette_height_for_width(area.right));
            if(height!=fullscreen_palette_height){resize();return;}
        }
        laying_out = true;
        struct LayoutGuard { bool& value; ~LayoutGuard() { value = false; } } guard{laying_out};
        RECT area{}; GetClientRect(pane, &area);
        const int margin = 0;
        const int width = std::max(px(100), static_cast<int>(area.right) - margin * 2);
        const int left = std::max(margin, (static_cast<int>(area.right) - width) / 2);
        std::vector<Placement> placements;
        std::vector<HWND> toolbar_captions;
        std::map<HWND,POINT> editor_positions;
        simulated_card_surfaces.clear();math_card_surfaces.clear();toolbar_surfaces.clear();console_surfaces.clear();std::map<std::size_t,std::size_t> card_surfaces;
        // A RichEdit's requested height depends on its final width. Measure
        // without moving/painting provisional rectangles over adjacent rows.
        constexpr UINT placement_flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW;
        const auto pinned=fixed_header_row();
        int top = fullscreen_block?0:px(24);
        for (auto& row : rows) {
            const int gap=px(row.column_gap);
            const bool fixed=&row==pinned;
            const int body_top=top;
            if(fixed)top=0;
            bool shown = !fullscreen_block || row_block(row) == *fullscreen_block;
            if(shown&&fullscreen_block&&exercise(block(*fullscreen_block).type)&&block(*fullscreen_block).type!=BlockType::math_puzzle){
                const int part=(GetDlgCtrlID(row.cells.front())-1000)%100;
                shown=part!=0&&part!=3&&part!=4&&part!=37;
            }
            if (shown && fullscreen_block && fullscreen_output) {
                const int part = (GetDlgCtrlID(row.cells.front()) - 1000) % 100;
                shown = part == 14 || part == 30;
            }
            if(shown&&fullscreen_block&&!fullscreen_output&&block(*fullscreen_block).type==BlockType::cpp_compiler){
                const int part=(GetDlgCtrlID(row.cells.front())-1000)%100;
                shown=part==16||part==10;
            }
            for (const auto item : row.cells){bool visible=shown;
                const auto index=row_block(row);
                if(index<controls.size()&&GetDlgCtrlID(item)==control_id(index,31))visible=shown&&std::ranges::count_if(controls[index].console_channels,[](const auto& value){return !value.empty();})>1;
                if (((GetWindowLongPtrW(item,GWL_STYLE) & WS_VISIBLE) != 0) != visible)
                    ShowWindow(item,visible ? SW_SHOWNOACTIVATE : SW_HIDE);}
            if (!shown) continue;
            if(!laboratory()&&(row.card_header||row.toolbar)&&!row.widths.empty()&&row.widths[0]>0
                &&((GetDlgCtrlID(row.cells.front())-1000)%100==16||(GetDlgCtrlID(row.cells.front())-1000)%100==38))
            {
                // Android places Reset immediately after the intrinsic label,
                // rather than reserving a fixed, scaled caption column.
                row.widths[0]=toolbar_caption_width(row.cells.front());
            }
            if(row.padding_before&&(!row.size_to_text||GetWindowTextLengthW(row.cells.front())>0))top+=px(row.padding_before);
            const auto block_index=row_block(row);
            const bool order_body=block_index<controls.size()&&controls[block_index].order_view&&row.cells.size()==1
                &&row.cells.front()==controls[block_index].order_view->window();
            const bool readonly_body=block_index<controls.size()&&row.cells.front()==controls[block_index].simulated_code;
            const bool fill_body=block_index<controls.size()&&controls[block_index].fill&&row.cells.size()==1
                &&row.cells.front()==controls[block_index].fill->window();
            const int inset=row.simulated_card?px(row.card_header?14:order_body||readonly_body||fill_body?0:18):px(row.horizontal_inset),row_width=width-inset*2;
            int fixed_width = 0; for (auto value : row.widths) if (value) fixed_width += px(value);
            const int remaining = std::max(px(30), row_width - fixed_width - gap * (static_cast<int>(row.cells.size()) - 1));
            if(row.cells.size()==1){
                const auto index=row_block(row);
                if(index<controls.size()){
                    auto& vm=controls[index];
                    if(vm.math&&row.cells[0]==vm.math->window()){
                        SetWindowSubclass(vm.math->fullscreen_button(),console_icon_proc,21,reinterpret_cast<DWORD_PTR>(this));
                        row.logical_height=MulDiv(vm.math->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                    }
                    if(vm.fill&&row.cells[0]==vm.fill->window())row.logical_height=MulDiv(vm.fill->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                    if(vm.order_view&&row.cells[0]==vm.order_view->window())row.logical_height=MulDiv(vm.order_view->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                    if(vm.image&&row.cells[0]==vm.image->window())row.logical_height=MulDiv(vm.image->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                    if(vm.project&&row.cells[0]==vm.project->window())row.logical_height=MulDiv(vm.project->height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                    if(vm.project&&row.cells[0]==vm.project->configuration_window())row.logical_height=MulDiv(vm.project->configuration_height_for_width(remaining),96,static_cast<int>(GetDpiForWindow(window)));
                }
            }
            if(laboratory()&&!controls.empty()&&row.cells.front()==controls[0].source&&!fullscreen_block){
                POINTL first{},second{};SendMessageW(row.cells.front(),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
                const auto next_line=SendMessageW(row.cells.front(),EM_LINEINDEX,1,0);if(next_line>=0)SendMessageW(row.cells.front(),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second),next_line);
                const auto line_height=next_line>=0?std::max<LONG>(1,second.y-first.y):px(21*content_text_scale()/100);
                row.logical_height=std::max(200,MulDiv(line_height*10,96,static_cast<int>(GetDpiForWindow(window)))+32);row.requested_height=0;
            }
            if(!laboratory()&&!fullscreen_block&&block_index<controls.size()&&row.cells.front()==controls[block_index].source)
                row.logical_height=compiler_source_minimum_height(row.cells.front());
            int x = left+inset;
            int control_height=0;
            int readonly_nonclient_height=0;
            const auto provisional = std::max(px(row.logical_height), row.requested_height);
            for (std::size_t i = 0; i < row.cells.size(); ++i) {
                const int cell_width = row.widths[i] ? px(row.widths[i]) : remaining;
                const bool answer=std::ranges::any_of(controls,[&](const auto& vm){return vm.answer==row.cells[i];});
                const bool readonly_source=std::ranges::any_of(controls,[&](const auto& vm){return vm.simulated_code==row.cells[i];});
                if(answer||readonly_source||(block_index<controls.size()&&(row.cells[i]==controls[block_index].source
                    ||row.cells[i]==controls[block_index].input||row.cells[i]==controls[block_index].console))){
                    POINT position{};SendMessageW(row.cells[i],EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
                    editor_positions.emplace(row.cells[i],position);
                }
                if(readonly_source){
                    // Embedded source is bottomless. Measure the full usable
                    // width, rather than allowing an old fullscreen scrollbar
                    // to narrow wrapped code and sustain its own overflow.
                    if(GetWindowLongPtrW(row.cells[i],GWL_STYLE)&WS_VSCROLL)SendMessageW(row.cells[i],EM_SHOWSCROLLBAR,SB_VERT,FALSE);
                }
                const auto answer_bar=(GetWindowLongPtrW(row.cells[i],GWL_STYLE)&WS_HSCROLL)?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(window)):0;
                const auto measured_height=answer?std::max(provisional,edit_line_height(row.cells[i],content_text_scale())+px(32)+answer_bar):provisional;
                SetWindowPos(row.cells[i], nullptr, 0, 0, cell_width, std::max(px(1), measured_height),
                    placement_flags | SWP_NOMOVE);
                if(answer)control_height=std::max(control_height,update_answer_scrollbars(row.cells[i],16,0,16,true));
                else if(block_index<controls.size()&&row.cells[i]==controls[block_index].source){
                    const auto fitted=update_answer_scrollbars(row.cells[i],16,16,16,true);
                    if(row.size_to_text)control_height=std::max(control_height,fitted);
                }else if(block_index<controls.size()&&row.cells[i]==controls[block_index].console)
                    control_height=std::max(control_height,update_answer_scrollbars(row.cells[i],laboratory()?18:12,laboratory()?18:0,laboratory()?18:16,true,laboratory()?18:-1));
                if(readonly_source){
                    RECT outer{},client{};GetWindowRect(row.cells[i],&outer);GetClientRect(row.cells[i],&client);
                    // EN_REQUESTRESIZE measures the text viewport. A real
                    // horizontal bar consumes additional nonclient height;
                    // keep it below the final line instead of covering it.
                    readonly_nonclient_height=std::max(0L,outer.bottom-outer.top-client.bottom);
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
                    const bool horizontal_visible=(GetWindowLongPtrW(row.cells[i],GWL_STYLE)&WS_HSCROLL)!=0;
                    const auto scrollbar_height=horizontal_visible?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(window)):0;
                    const bool padded_answer=row.simulated_card&&(GetDlgCtrlID(row.cells[i])-1000)%100==2;
                    control_height=std::max(control_height,padded_answer
                        ?edit_line_height(row.cells[i],content_text_scale())+px(32)+scrollbar_height
                        :MulDiv(format.yHeight*content_text_scale(),static_cast<int>(GetDpiForWindow(window)),1440*100)+px(12)+scrollbar_height);
                }
                x += cell_width + gap;
            }
            int height = row.size_to_text ? std::max(px(row.logical_height), row.requested_height) : px(row.logical_height);
            height+=readonly_nonclient_height;
            height=std::max(height,control_height);
            if(fixed){
                // The pinned toolbar and its RichEdit/button cells share one
                // measured height. At enlarged text sizes its host needs more
                // space than the ordinary 48dp row; leaving shorter cells here
                // creates an otherwise empty band before the editor body.
                RECT header_area{};GetClientRect(fullscreen_header,&header_area);
                height=std::max(height,static_cast<int>(header_area.bottom));
            }
            // Android's empty verdict/status TextViews are gone until they
            // own text. RichEdit still requests a blank line for an empty
            // document; do not turn that native request into visible spacing.
            if(row.size_to_text&&row.logical_height==0&&GetWindowTextLengthW(row.cells.front())==0)height=0;
            if (row.max_logical_height) height = std::min(height,px(row.max_logical_height));
            if (fullscreen_block && GetDlgCtrlID(row.cells.front()) == control_id(*fullscreen_block,14) && fullscreen_output)
                height = std::max(px(80),static_cast<int>(area.bottom) - top - px(16));
            if(fullscreen_block&&GetDlgCtrlID(row.cells.front())==control_id(*fullscreen_block,10)&&!fullscreen_output)
                height=std::max(px(80),static_cast<int>(area.bottom)-top-px(16));
            if(fullscreen_block&&block(*fullscreen_block).type==BlockType::code&&controls[*fullscreen_block].simulated_code==row.cells.front())
                height=std::max(px(80),static_cast<int>(area.bottom)-top-px(24));
            x = left+inset;
            if(row.toolbar&&!fixed)toolbar_surfaces.push_back({left,top,left+width,top+height});
            if(const auto index=row_block(row);index<controls.size()&&!laboratory()&&(row.cells.front()==controls[index].console||GetDlgCtrlID(row.cells.front())==control_id(index,30)))
                console_surfaces.push_back({left,top,left+width,top+height+px(row.padding_after)});
            for (std::size_t i = 0; i < row.cells.size(); ++i) {
                const int cell_width = row.widths[i] ? px(row.widths[i]) : remaining;
                int cell_height=height;
                if(row.cells.size()==2&&choice_labels.contains(row.cells[1])&&i==0){
                    // Android aligns the radio to the first text line, even
                    // when the option wraps. The glyph must not drift toward
                    // the middle of a multi-line paragraph.
                    POINTL first{},next{};SendMessageW(row.cells[1],EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
                    const auto second=SendMessageW(row.cells[1],EM_LINEINDEX,1,0);
                    if(second>=0)SendMessageW(row.cells[1],EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next),second);
                    const auto line_height=second>=0?next.y-first.y:px(MulDiv(26,content_text_scale(),100));
                    cell_height=std::min(height,std::max(px(28),static_cast<int>(line_height)));
                }
                placements.push_back({row.cells[i],x,top,cell_width,std::max(0,cell_height),fixed});
                const int part=(GetDlgCtrlID(row.cells[i])-1000)%100;
                if((row.card_header||row.toolbar)&&(part==16||part==30||part==38))
                    toolbar_captions.push_back(row.cells[i]);
                x += cell_width + gap;
            }
            const bool separate_response=block_index<controls.size()&&
                (block(block_index).type==BlockType::fill_code||block(block_index).type==BlockType::output_prediction);
            const bool answer_body=separate_response&&row.cells.front()==controls[block_index].answer;
            const int card_bottom=row.card_end?(answer_body?0:16):0;
            // The 16dp card_bottom is inside the painted card; Android's
            // addCode() has a further 24dp external bottom margin. Combined
            // with the next reference's 8dp top margin, adjacent code cards
            // retain a 32dp gap without altering their source or syntax ink.
            const bool standalone_reference=row.card_end&&block_index<controls.size()
                &&block(block_index).type==BlockType::code;
            const int card_margin=standalone_reference?24
                :row.card_end&&separate_response?(answer_body?18:readonly_body?32:0):0;
            if(row.simulated_card&&!fixed){
                if(row.card_header){card_surfaces[*row.simulated_card]=simulated_card_surfaces.size();
                    simulated_card_surfaces.push_back({{left,top,left+width,top+height},top+height});}
                if(const auto found=card_surfaces.find(*row.simulated_card);found!=card_surfaces.end())
                    simulated_card_surfaces[found->second].bounds.bottom=top+height+px(card_bottom);
            }
            if(fixed)top=body_top;
            else top += height + (height ? px(row.simulated_card?(row.card_end?card_bottom+card_margin:8):row.padding_after) : px(row.empty_padding_after));
        }
        const auto math_keyboard=math_keyboard_window(GetAncestor(window,GA_ROOT));RECT keyboard_area{};
        if(math_keyboard&&IsWindowVisible(math_keyboard))GetWindowRect(math_keyboard,&keyboard_area);
        extent = top + px(16)+static_cast<int>(keyboard_area.bottom-keyboard_area.top);
        scroll = std::clamp(scroll, 0, std::max(0, extent - static_cast<int>(area.bottom)));
        scroll_placements=placements;
        scroll_layout_size={area.right,area.bottom};
        position_rows(placements,area,true);
        // RichEdit preserves formatting insets when its client size changes.
        // Setting this rectangle at the provisional measurement height can
        // therefore clip the first caption after its final, smaller placement.
        for(const auto caption:toolbar_captions){
            RECT content{};GetClientRect(caption,&content);
            content.left=laboratory()?px(14):0;
            content.top=laboratory()?px(20):std::max(0L,(content.bottom-edit_line_height(caption,content_text_scale()))/2);
            SendMessageW(caption,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
        }
        for(const auto& vm:controls){
            const auto sync=[&](HWND item,int top_padding,int right_padding,int bottom_padding,int left_padding=-1){
                if(!item||std::ranges::find(placements,item,&Placement::item)==placements.end())return;
                const auto saved=editor_positions.find(item);
                (void)update_answer_scrollbars(item,top_padding,right_padding,bottom_padding,false,left_padding,
                    saved==editor_positions.end()?nullptr:&saved->second);
            };
            sync(vm.answer,16,0,16);
            sync(vm.source,16,16,16);
            sync(vm.input,0,0,0);
            sync(vm.console,laboratory()?18:12,laboratory()?18:0,laboratory()?18:16,laboratory()?18:-1);
        }
        for(std::size_t index=0;index<controls.size();++index)if(const auto source=controls[index].simulated_code;source){
            const auto placed=std::ranges::find(placements,source,&Placement::item);
            if(placed==placements.end())continue;
            const auto measured=std::ranges::find_if(rows,[source](const auto& row){return row.cells.size()==1&&row.cells[0]==source;});
            RECT viewport{};GetClientRect(source,&viewport);
            // A readonly source delegates vertical movement to the lesson in
            // its normal autoheight card. Only a genuinely smaller fullscreen
            // viewport needs its own vertical scrollbar; use RichEdit's actual
            // requested document height, including the active text zoom.
            const bool overflow=fullscreen_block==index&&measured!=rows.end()&&measured->requested_height>viewport.bottom;
            if(!!(GetWindowLongPtrW(source,GWL_STYLE)&WS_VSCROLL)!=overflow)SendMessageW(source,EM_SHOWSCROLLBAR,SB_VERT,overflow);
            if(const auto saved=editor_positions.find(source);saved!=editor_positions.end())
                SendMessageW(source,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&saved->second));
        }
        // Measuring a mathematical composition deliberately does not position
        // its children. Commit its own layout after the final lesson placement;
        // resizing only its host leaves every rich label/radio at its origin.
        for(auto& vm:controls)if(vm.math){
            const auto placed=std::ranges::find(placements,vm.math->window(),&Placement::item);
            if(placed!=placements.end()){
                vm.math->layout(placed->x,placed->y-(placed->origin?placed->origin:scroll),placed->width);
                RECT bounds{placed->x,placed->y,placed->x+placed->width,placed->y+placed->height};
                const auto feedback=std::ranges::find(placements,vm.feedback,&Placement::item);
                if(feedback!=placements.end())bounds.bottom=feedback->y+feedback->height+px(12);
                math_card_surfaces.push_back(bounds);
                // The host already pads its contents. Leave just the outer
                // contour to the parent; inset check/feedback stay untouched.
                const auto interior=CreateRoundRectRgn(px(1),px(1),bounds.right-bounds.left-px(1),
                    bounds.bottom-bounds.top-px(1),px(18),px(18));
                if(interior&&!SetWindowRgn(vm.math->window(),interior,FALSE))DeleteObject(interior);
            }
        }
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0; info.nMax = std::max(0, extent - 1); info.nPage = static_cast<UINT>(std::max<LONG>(0, area.bottom)); info.nPos = scroll;
        SetScrollInfo(pane, SB_VERT, &info, FALSE);
        ShowScrollBar(pane,SB_VERT,FALSE);
        place_reader_bar();
        RedrawWindow(pane,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        if(pinned)RedrawWindow(fullscreen_header,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        scroll_layout_valid=true;
        laying_out = false;
        if (layout_pending) { layout_pending = false; reflow(); }
    }
    void scroll_to(int position) {
        RECT area{}; GetClientRect(pane, &area);
        const int new_scroll = std::clamp(position, 0, std::max(0, extent - static_cast<int>(area.bottom)));
        if (new_scroll == scroll) return;
        scroll = new_scroll;
        if(!fullscreen_block){remember_scroll();mark_dirty();}
        if(!laying_out&&scroll_layout_valid&&area.right==scroll_layout_size.cx&&area.bottom==scroll_layout_size.cy){
            laying_out=true;
            struct PositionGuard { bool& active; ~PositionGuard(){active=false;} } guard{laying_out};
            auto placements=scroll_placements;
            position_rows(placements,area,false);
            SCROLLINFO info{sizeof(info),SIF_POS};info.nPos=scroll;
            SetScrollInfo(pane,SB_VERT,&info,FALSE);
            // A held mouse supplies more moves before the ordinary paint
            // queue drains. Present one complete translated frame per move.
            paint_scrolled_viewport(drag.axis==DragAxis::lesson_vertical);
            laying_out=false;
            if(layout_pending){layout_pending=false;reflow();}
        }else reflow();
    }
    bool enter_key(const MSG& message){
        if(import_open)return false;
        if(message.message==WM_KEYUP){const bool handled=enter_held;enter_held=false;return handled;}
        if(message.message!=WM_KEYDOWN)return false;
        if(enter_held&&(message.lParam&(LPARAM{1}<<30)))return true;
        const auto focused=GetFocus();
        std::optional<std::size_t> focused_block;
        bool editor_owns=false;
        for(std::size_t i=0;focused&&i<controls.size();++i){const auto& vm=controls[i];
            if(vm.project&&((focused==vm.project->window()||IsChild(vm.project->window(),focused))
                ||(vm.project->configuration_window()&&(focused==vm.project->configuration_window()||IsChild(vm.project->configuration_window(),focused)))))return false;
            if(focused==vm.source||focused==vm.input){focused_block=i;editor_owns=true;break;}
            if(focused==vm.answer){focused_block=i;editor_owns=!curriculum::uses_single_line_answer(block(i));break;}
            if(vm.order_view&&(focused==vm.order_view->window()||IsChild(vm.order_view->window(),focused)))focused_block=i;
            if((vm.math&&(focused==vm.math->window()||IsChild(vm.math->window(),focused)))
                ||(vm.fill&&(focused==vm.fill->window()||IsChild(vm.fill->window(),focused)))){focused_block=i;
                wchar_t name[96]{};GetClassNameW(focused,name,96);editor_owns=std::wstring_view(name)==L"LearnGameAndEngineDev.MathExpressionEditor";break;}
            if(std::ranges::find(vm.choices,focused)!=vm.choices.end()||focused==find_item(control_id(i,3)))focused_block=i;
        }
        const auto eligible=[&](std::size_t i){const auto check=find_item(control_id(i,3));return check
            &&(IsWindowVisible(check)||fullscreen_block==i)&&IsWindowEnabled(check)&&exercise(block(i).type);};
        if(editor_owns)return false;
        if(fullscreen_block&&block(*fullscreen_block).type==BlockType::cpp_compiler)return false;
        // Windows key-repeat and UP belong to the initial DOWN even if checking
        // disables its answer field or re-creates the next page's controls.
        if(message.lParam&(LPARAM{1}<<30))return true;
        enter_held=true;
        if(!fullscreen_block&&can_advance(page)){
            PostMessageW(window,WM_COMMAND,MAKEWPARAM(continue_id,BN_CLICKED),reinterpret_cast<LPARAM>(find_item(continue_id)));return true;
        }
        std::optional<std::size_t> chosen;
        if(selected_puzzle&&eligible(*selected_puzzle))chosen=selected_puzzle;
        else if(focused_block&&eligible(*focused_block))chosen=focused_block;
        else for(std::size_t i=0;i<controls.size();++i)if(eligible(i)){if(chosen){chosen.reset();break;}chosen=i;}
        if(chosen){selected_puzzle=chosen;check_answer(*chosen);}
        return std::ranges::any_of(controls,[&](const auto& vm){return vm.rendered;});
    }
    bool finish_lesson(){
        if(!can_finish_lesson())return false;
        data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=true;
        mark_dirty();flush();PostMessageW(window,finish_lesson_message,0,0);return true;
    }
    bool change_page(std::size_t value) {
        if(deferred_entry.waiting())return false;
        if (value == page) return true;
        if(value==lesson.pages.size()&&page+1==value&&hooks.lesson_completed){
            const auto separator=lesson.id.find("__");const auto course=separator==std::string::npos?std::string{}:lesson.id.substr(0,separator);
            return curriculum::learning_flow(data,course).swipe_finish&&finish_lesson();
        }
        if(value>page){const auto entry=entry_decision(value);if(entry.pending_math)return false;const auto prerequisite=entry.page;
            if(prerequisite!=value){if(prerequisite!=page&&can_visit(prerequisite))(void)change_page(prerequisite);return false;}}
        if (!can_visit(value)) return false;
        deferred_entry.cancel();
        // A selector jump cannot bypass a declared prerequisite, even when
        // old progress or an imported bookmark already reaches a later page.
        if (hooks.before_page_change) hooks.before_page_change(window);
        capture_page(); dirty = true; flush(); cancel_run();
        page = value; scroll = 0;
        state::save_current_page(data, lesson, page); mark_dirty(); render_page(); return true;
    }
    void reset_answer(std::size_t index) {
            auto& vm=controls.at(index);
            if(checking_math==index)cancel_math();
            if(vm.math)++math_answer_generations[key(index)];
            ++vm.revision;
            // Explicit reset affects only this quiz; compiler drafts and jobs stay intact.
            updating = true; vm.response = {};
            verified_math.erase(key(index));
            rejected_math.erase(key(index));deferred_entry.cancel();
            if (vm.answer_editor) vm.answer_editor->reset(windows::EditorDocument{});
            if(vm.fill)vm.fill->reset();
            if(vm.math){
                vm.math->reset();
                vm.math_semantic_identity=math_answer_identity(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(vm.math->answer_identity_json())));
            }
            if(vm.order_view)vm.order_view->reset();
            vm.order_started=false;
            for (auto choice : vm.choices) SendMessageW(choice, BM_SETCHECK, BST_UNCHECKED, 0);
            if (!vm.order.empty()) std::iota(vm.order.begin(), vm.order.end(), 0);
            updating = false;
            if (!vm.order.empty()) refresh_order(index);
            state::put_response(data, lesson, block(index), vm.response); mark_dirty(); flush();
            refresh_choice_history(index);
            feedback(index, "", false); sync_quiz_controls(index);
    }
    void check_answer(std::size_t index) {
        auto& vm = controls.at(index); const auto& current = block(index);
        if (vm.response.correct) return;
        if(vm.order_view&&!vm.order_started)return;
        curriculum::AnswerEvaluation result;
        if(current.type==BlockType::math_puzzle){check_math(index);return;}
        if(vm.fill){
            result=current.type==BlockType::drag_fill_code?curriculum::evaluate_drag_answers(current,vm.fill->slots())
                :curriculum::evaluate_hole_answers(current,vm.fill->answers());
            const bool incomplete=current.type==BlockType::drag_fill_code
                ?std::ranges::any_of(vm.fill->slots(),[&](auto slot){return slot>=current.choices.size();})
                :std::ranges::any_of(vm.fill->answers(),[](const auto& answer){return blank_answer(answer);});
            if(!result.correct&&incomplete){show_incomplete(index);return;}
        } else if (current.type == BlockType::multiple_choice) {
            if (vm.response.selected < 0||vm.response.historical_choice) {show_incomplete(index);return;}
            result = curriculum::evaluate_choice(current, static_cast<std::size_t>(vm.response.selected));
        } else if (current.type == BlockType::order_code) {
            result = curriculum::evaluate_order(current, vm.order);
            vm.response.order.assign(vm.order.begin(), vm.order.end());
        } else {
            const auto answer=vm.answer_editor->document().text;
            result = curriculum::evaluate_text_answer(current, answer);
            // Authored exact/literal whitespace is a real answer. An empty or
            // unfinished UI draft is not an evaluated incorrect attempt.
            if(!result.correct&&blank_answer(answer)){show_incomplete(index);return;}
        }
        if(!may_evaluate())return;
        if(vm.fill)capture_composition(index);
        else if(vm.answer_editor)vm.response.answer=vm.answer_editor->document().text;
        vm.response.correct = result.correct;
        if(!result.correct&&vm.order_view)vm.order_view->restart_tap_sequence();
        record_attempt(index,result.correct);
        state::mark_response_reviewed(vm.response, current);
        state::put_response(data, lesson, block(index), vm.response); mark_dirty(); flush();
        auto text = verdict_text(result.correct);
        if (!result.feedback.empty()) text += "\n" + result.feedback;
        else if (!result.correct) text += '\n' + default_review();
        feedback(index, text, result.correct, true, !result.correct && result.feedback.empty());
        sync_quiz_controls(index);
        if (hooks.feedback_changed) hooks.feedback_changed(window,index,result.correct);
        // The page-level Enter gate decides before checking. This callback
        // never advances a page that became complete in the same key press.
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
                state::put_response(data, lesson, block(index), vm.response);
                feedback(index, "", false);sync_quiz_controls(index);mark_dirty(); return;
            }
            if (sender == vm.source || sender == vm.input) {
                if(vm.project&&vm.project->loading())return;
                const auto before = state::get_draft(data, draft_key(index), block(index).initial_code,
                    block(index).standard_input.value_or(""));
                if (sender == vm.source) vm.source_editor->capture_change();
                auto input = before.input;
                if (vm.input) {
                    const auto visible = edit_text(vm.input);
                    input = visible == vm.input_visual ? vm.input_original : visible;
                }
                if(vm.project){const bool source_changed=sender==vm.source&&vm.project->capture();
                    if(!source_changed&&before.input==input)return;
                    capture_draft(index);project_changed(index,true);return;}
                if (before.source == vm.source_editor->document().text && before.input == input) return;
                ++vm.revision;
                clear_reference_completion(index);
                if (running_block == index) { worker.request_stop(); }
                capture_draft(index);
                const auto current=program_snapshot(index);
                windows::invalidate_compiler_puzzle_attempt(data,lesson,block(index),current);
                if(!windows::compiler_puzzle_completed(data,lesson,block(index))||
                    (!vm.feedback_source.empty()&&!vm.feedback_correct&&
                        windows::compiler_puzzle_review(data,lesson,block(index),current).empty()))feedback(index, "", false);
                vm.console_channels = {};
                updating = true; set_rich_text(vm.console,block(index).compile_check
                    ?ui("El código cambió. Compila de nuevo para comprobar el diagnóstico actual.",
                        "The code changed. Compile again to check the current diagnostic.","Le code a changé. Recompilez pour vérifier le diagnostic actuel.")
                    :ui("El programa cambió. Ejecuta de nuevo para obtener una salida actual.",
                        "The program changed. Run it again to obtain current output.","Le programme a changé. Exécute-le à nouveau pour obtenir une sortie actuelle."));
                if (sender == vm.source) style_cpp(vm.source);
                updating = false; mark_dirty(); reflow(); return;
            }
        }
    }
    void run(std::size_t index,windows::CppRunRequest::Action action=windows::CppRunRequest::Action::compile_run) {
        cancel_run(); capture_draft(index); clear_reference_completion(index);
        auto request=program_snapshot(index);
        // Starting or abandoning a job is not another evaluated answer. Keep
        // its exact latest review until a real verdict or incompatible edit.
        windows::invalidate_compiler_puzzle_attempt(data,lesson,block(index),request);mark_dirty();flush();
        auto& vm = controls.at(index);
        vm.console_channels = {};
        request.request_id = ++request_id; request.revision = vm.revision;
        if(block(index).cmake&&action==windows::CppRunRequest::Action::compile_run&&
            (!block(index).output_check||block(index).compiler_workflow_check==
                curriculum::CompilerWorkflowCheck::exact_original_project_runtime_v1))
            action=windows::CppRunRequest::Action::execute;
        request.action=action;running_action=action;
        // Disabling the focused run button leaves the active app without a
        // keyboard target. Hand focus back synchronously; completion must not
        // reclaim it if the student switches to another control or app.
        const auto root=GetAncestor(window,GA_ROOT);
        if(vm.compile&&GetFocus()==vm.compile&&GetActiveWindow()==root&&GetForegroundWindow()==root&&
            vm.source&&IsWindowVisible(vm.source)&&IsWindowEnabled(vm.source)){
            POINT before{},after{};SendMessageW(vm.source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
            SetFocus(vm.source);SendMessageW(vm.source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
            if(before.x!=after.x||before.y!=after.y)SendMessageW(vm.source,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
        }
        running_block = index; running_input = request.stdin_utf8;running_program=request;refresh_project_actions(index);
        EnableWindow(vm.compile, FALSE);
        // The earned objective remains stored; this run needs its own verdict.
        feedback(index, "", false);
        if(laboratory()){laboratory_status=LaboratoryStatus::running;refresh_laboratory_status();}
        updating = true;set_rich_text(vm.console,"");updating = false;
        if(block(index).cmake&&vm.status)set_rich_text(vm.status,action==windows::CppRunRequest::Action::configure?ui("Configurando y generando el proyecto…","Configuring and generating the project…","Configuration et génération du projet…")
            :action==windows::CppRunRequest::Action::build?ui("Construyendo el objetivo…","Building the target…","Construction de la cible…")
            :action==windows::CppRunRequest::Action::compile_run?ui("Compilando y ejecutando...","Compiling and running...","Compilation et exécution en cours...")
            :ui("Ejecutando el objetivo construido…","Running the built target…","Exécution de la cible construite…"));
        else if(!laboratory()&&vm.status)set_rich_text(vm.status,block(index).compile_check
            ?ui("Comprobando el diagnóstico de compilación…","Checking the compilation diagnostic…","Vérification du diagnostic de compilation…")
            :ui("Compilando y ejecutando...","Compiling and running...","Compilation et exécution en cours..."));
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
        auto& vm = controls.at(index); EnableWindow(vm.compile, TRUE);refresh_project_actions(index);
        if (result->revision != vm.revision) return;
        if(vm.selective_recovery)vm.selective_recovery->run(running_program,*result);
        if(block(index).compile_check){
            vm.console_channels={};
            auto diagnostics=diagnostic_text(result->compilation.stdout_bytes+result->compilation.stderr_bytes);
            const bool control_valid=result->diagnostic&&result->diagnostic->control.succeeded();
            if(result->diagnostic&&!control_valid){
                diagnostics=ui("Comprobación del programa inicial:\n","Initial program check:\n","Vérification du programme initial :\n")
                    +diagnostic_text(result->diagnostic->control.stdout_bytes+result->diagnostic->control.stderr_bytes)
                    +(diagnostics.empty()?"":"\n"+diagnostics);
            }
            if(!result->error_utf8.empty())diagnostics+=(diagnostics.empty()?"":"\n")+result->error_utf8;
            vm.console_channels[2]=std::move(diagnostics);console_present(index,2);
            const bool link_diagnostic=result->diagnostic&&result->diagnostic->candidate_link.status!=windows::CppPhaseStatus::not_started;
            if(vm.status)set_rich_text(vm.status,!control_valid||result->compilation.status!=windows::CppPhaseStatus::completed
                ?ui("No se pudo completar la comprobación del diagnóstico.","The diagnostic check could not be completed.","La vérification du diagnostic n’a pas pu être terminée.")
                :link_diagnostic
                    ?(result->compilation.succeeded()
                        ?ui("Enlace aceptado. El programa no se ejecutó.","Link accepted. The program was not run.","Édition de liens acceptée. Le programme n’a pas été exécuté.")
                        :ui("Enlace rechazado. El programa no se ejecutó.","Link rejected. The program was not run.","Édition de liens rejetée. Le programme n’a pas été exécuté."))
                :result->compilation.succeeded()
                    ?ui("Compilación aceptada. El programa no se ejecutó.","Compilation accepted. The program was not run.","Compilation acceptée. Le programme n’a pas été exécuté.")
                    :ui("Compilación rechazada. El programa no se ejecutó.","Compilation rejected. The program was not run.","Compilation rejetée. Le programme n’a pas été exécuté."));
            {
                const bool correct=windows::expected_compile_diagnostic(running_program,*result);
                if(correct){
                    auto text=verdict_text(true)+"\n"+(block(index).compile_check->explanation.empty()
                        ?(link_diagnostic?ui("El enlazador detectó el problema solicitado; el programa no llegó a ejecutarse.",
                            "The linker detected the requested problem; the program did not run.",
                            "L’éditeur de liens a détecté le problème demandé ; le programme n’a pas été exécuté.")
                        :ui("El compilador rechazó la operación solicitada; el programa no llegó a ejecutarse.",
                            "The compiler rejected the requested operation; the program did not run.",
                            "Le compilateur a rejeté l’opération demandée ; le programme n’a pas été exécuté."))
                        :block(index).compile_check->explanation);
                    if(windows::record_compiler_diagnostic_case(data,lesson,block(index),running_program,*result,text)){
                        feedback(index,text,true,true,false,"/compileCheck/explanation");if(hooks.feedback_changed)hooks.feedback_changed(window,index,true);
                        if(has_references(block(index)))data.preferences["native_windows"][reference_key(block(index))]=reference_seal(block(index));
                        mark_dirty();flush();refresh_references();
                    }
                }else if(windows::accepted_compile_diagnostic(running_program,*result)){
                    const auto text=verdict_text(false)+"\n"+ui("Esta compilación fue aceptada; aún falta obtener el rechazo solicitado.",
                        "This compilation was accepted; the requested rejection is still pending.",
                        "Cette compilation a été acceptée ; le rejet demandé reste à obtenir.");
                    if(windows::record_compiler_diagnostic_review(data,lesson,block(index),running_program,*result,text)){
                        feedback(index,text,false,true);if(hooks.feedback_changed)hooks.feedback_changed(window,index,false);
                        mark_dirty();flush();
                    }
                }
            }
            reflow();return;
        }
        std::string output;
        const bool cmake=block(index).cmake.has_value();
        const bool configuration_failed=cmake&&result->configuration.status!=windows::CppPhaseStatus::not_started&&!result->configuration.succeeded();
        const bool executed=running_action==windows::CppRunRequest::Action::compile_run||running_action==windows::CppRunRequest::Action::execute;
        const auto fresh=windows::capture_cpp_execution(running_program,*result);
        const auto evaluation=block(index).output_check&&executed
            ?windows::evaluate_cpp_output_check(*block(index).output_check,fresh,vm.revision):curriculum::OutputEvaluation{};
        // A completed process can return a nonzero application status without
        // crashing. Presentation must not depend on whether this source earned
        // the authored objective (for example, a deliberately edited client).
        // Windows abnormal termination codes lie outside the normal 0..255
        // application-return domain, as do timed out and cancelled phases.
        const bool normal_execution=result->error_utf8.empty()&&
            result->execution.status==windows::CppPhaseStatus::completed&&
            result->execution.exit_code&&*result->execution.exit_code<=255;
        if(configuration_failed){output=diagnostic_text(result->configuration.stdout_bytes+result->configuration.stderr_bytes);if(!result->error_utf8.empty())output+='\n'+result->error_utf8;}
        else if(cmake&&!executed){const auto& phase=running_action==windows::CppRunRequest::Action::configure?result->configuration:result->compilation;
            output=diagnostic_text(phase.stdout_bytes+phase.stderr_bytes);if(!result->error_utf8.empty())output+='\n'+result->error_utf8;
        }
        else if (result->compilation.status!=windows::CppPhaseStatus::not_started&&!result->compilation.succeeded()) {
            output = console_label(2) + '\n' + diagnostic_text(result->compilation.stdout_bytes + result->compilation.stderr_bytes);
            if (!result->error_utf8.empty()) output += "\n" + result->error_utf8;
            if (result->compilation.status == windows::CppPhaseStatus::timed_out) output += '\n' + ui("La compilación superó el tiempo permitido.",
                "Compilation exceeded the time limit.","La compilation a dépassé le temps autorisé.");
        } else if (!result->execution.succeeded()&&!normal_execution) {
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
        if(vm.status&&!laboratory()){
            const auto failed=[&](std::string name,const windows::CppPhaseResult& phase){
                if(phase.status==windows::CppPhaseStatus::timed_out)return ui("La fase excedió el tiempo disponible.","The phase exceeded the time limit.","La phase a dépassé le délai disponible.");
                if(phase.exit_code)return name+ui(": la fase terminó con código ",": the phase exited with code "," : la phase a terminé avec le code ")+std::to_string(*phase.exit_code)+'.';
                return ui("La fase no terminó correctamente.","The phase did not finish successfully.","La phase ne s’est pas terminée correctement.");
            };
            std::string status;
            if(result->error_utf8.find("Windows error 4551")!=std::string::npos)
                status=ui("Windows bloqueó un proceso por su política de control de aplicaciones (error 4551). Consulta los detalles en la consola.",
                    "Windows application control blocked a process (error 4551). See the console for details.",
                    "Le contrôle des applications Windows a bloqué un processus (erreur 4551). Consultez la console.");
            else if(block(index).output_check&&
                     block(index).output_check->assertion_check==curriculum::AssertionMode::enabled_abort&&
                     result->compilation.succeeded()) {
                status=fresh.assertion_abort_verified
                    ?ui("Aserción comprobada: la ejecución fue abortada en WASI.",
                        "Assertion verified: execution aborted in WASI.",
                        "Assertion vérifiée : l'exécution a été interrompue dans WASI.")
                    :ui("No se verificó el aborto por la aserción solicitada.",
                        "The requested assertion abort was not verified.",
                        "L'arrêt par l'assertion demandée n'a pas été vérifié.");
            }
            else if(!cmake){
                if(result->compilation.exit_code&&!result->compilation.succeeded())status=ui("La compilación falló (código ","Compilation failed (code ","Échec de la compilation (code ")+std::to_string(*result->compilation.exit_code)+").";
                else if(result->execution.exit_code)status=(result->execution.succeeded()||normal_execution
                    ?ui("Ejecución terminada (código ","Execution finished (code ","Exécution terminée (code ")
                    :ui("La ejecución falló (código ","Execution failed (code ","L’exécution a échoué (code "))+std::to_string(*result->execution.exit_code)+").";
                else status=ui("No se pudo ejecutar.","Could not execute.","Impossible d’exécuter.");
            }
            else if(configuration_failed)status=failed(ui("Configuración y generación","Configuration and generation","Configuration et génération"),result->configuration);
            else if(running_action==windows::CppRunRequest::Action::configure)status=result->configuration.succeeded()?ui(
                "Configuración y generación completadas. El programa todavía no se ha construido.","Configuration and generation completed. The program has not been built yet.","Configuration et génération terminées. Le programme n’a pas encore été construit.")
                :failed(ui("Configuración y generación","Configuration and generation","Configuration et génération"),result->configuration);
            else if(result->compilation.status!=windows::CppPhaseStatus::not_started&&!result->compilation.succeeded())status=failed(ui("Construcción","Build","Construction"),result->compilation);
            else if(running_action==windows::CppRunRequest::Action::build)status=!result->compilation.succeeded()?failed(ui("Construcción","Build","Construction"),result->compilation):result->artifact.empty()
                ?ui("Objetivo construido; no produce un programa ejecutable.","Target built; it does not produce an executable program.","Cible construite ; elle ne produit pas de programme exécutable.")
                :ui("Objetivo construido. Ejecútalo para observar su comportamiento.","Target built. Run it to observe its behavior.","Cible construite. Exécutez-la pour observer son comportement.");
            else if(result->execution.succeeded()||normal_execution)status=ui("Ejecución terminada (código ","Execution finished (code ","Exécution terminée (code ")+std::to_string(*result->execution.exit_code)+").";
            else status=failed(ui("Ejecución","Execution","Exécution"),result->execution);
            set_rich_text(vm.status,status);
        }
        vm.console_channels[0] = diagnostic_text(result->execution.stdout_bytes);
        vm.console_channels[1] = diagnostic_text(result->execution.stderr_bytes);
        vm.console_channels[2] = diagnostic_text(result->configuration.stdout_bytes+result->configuration.stderr_bytes+result->compilation.stdout_bytes + result->compilation.stderr_bytes);
        if(laboratory()){
            laboratory_exit_code=static_cast<int>((result->execution.exit_code?result->execution.exit_code:result->compilation.exit_code).value_or(static_cast<std::uint32_t>(-1)));
            laboratory_status=!result->compilation.succeeded()?LaboratoryStatus::compilation_failed:result->execution.status==windows::CppPhaseStatus::completed?LaboratoryStatus::finished:LaboratoryStatus::failed;
            if(!result->error_utf8.empty())vm.console_channels[1]+=(vm.console_channels[1].empty()?"":"\n")+result->error_utf8;
            refresh_laboratory_status();console_present(index);return;
        }
        int preferred_channel = -1;
        if (configuration_failed||(cmake&&!executed)||(!cmake&&!result->compilation.succeeded())
            ||(cmake&&result->compilation.status!=windows::CppPhaseStatus::not_started&&!result->compilation.succeeded())) { vm.console_channels[2] = output; preferred_channel = 2; }
        else if (!result->execution.succeeded()&&!normal_execution) { vm.console_channels[1] = output; preferred_channel = 1; }
        console_present(index,preferred_channel);
        if (block(index).output_check&&executed) {
            if (evaluation.verdict != curriculum::OutputVerdict::unavailable) {
                const bool preserved_source=windows::compiler_runtime_request_allowed(
                    block(index),running_program);
                const bool correct = evaluation.verdict == curriculum::OutputVerdict::correct&&preserved_source;
                auto text = verdict_text(correct);
                if(!preserved_source)text+='\n'+ui(
                    "Este objetivo exige conservar todos los archivos originales, la entrada y las opciones CMake.",
                    "This objective requires the exact original project files, input and CMake options.",
                    "Cet objectif exige de conserver les fichiers, l'entrée et les options CMake d'origine.");
                if(evaluation.stdout_correct&&!evaluation.exit_code_correct)text+='\n'+ui(
                    "La salida estándar es correcta; el código de salida no coincide con el requerido.",
                    "Standard output is correct; the process exit code does not match the required code.",
                    "La sortie standard est correcte ; le code de sortie ne correspond pas au code demandé.");
                else if(evaluation.stdout_correct&&!evaluation.stderr_correct)text+='\n'+
                    ((block(index).output_check->rejection_operation||
                       block(index).output_check->observation_report)&&!evaluation.feedback.empty()
                        ?evaluation.feedback
                        :ui("La salida estándar es correcta; falta el diagnóstico solicitado en stderr.",
                            "Standard output is correct; the requested runtime diagnostic is missing from stderr.",
                            "La sortie standard est correcte ; le diagnostic d’exécution demandé manque dans stderr."));
                else if (!evaluation.feedback.empty()) text += "\n" + evaluation.feedback;
                // A prior stdout-only receipt stays stored, but is not a new
                // history achievement. A previously authenticated sequence
                // remains earned across reopening or unrelated edits.
                const bool history_earned=windows::compiler_puzzle_completed(data,lesson,block(index));
                const bool history_ready=!vm.selective_recovery||vm.selective_recovery->complete()||history_earned;
                const bool output_recorded=correct&&history_ready&&
                    windows::record_compiler_puzzle_case(data,lesson,block(index),running_program,fresh,text);
                const bool objective_completed=output_recorded&&
                    (!vm.selective_recovery||history_earned||
                     windows::record_compiler_selective_history_completion(
                         data,lesson,block(index),running_program,fresh,*vm.selective_recovery));
                if(!correct&&windows::record_compiler_puzzle_review(data,lesson,block(index),running_program,fresh,text)){
                    mark_dirty();flush();
                }
                if(!correct||objective_completed){feedback(index,text,objective_completed,true,false,evaluation.feedback_path);if(hooks.feedback_changed)hooks.feedback_changed(window,index,objective_completed);}
                else if(correct&&vm.selective_recovery&&!objective_completed){
                    // This guidance is EXCLUSIVE to the U3 historical CMake
                    // editor. Other C++ activities keep their normal case
                    // progress, even when several inputs must be checked.
                    using Stage=windows::CmakeSelectiveRecoveryTrace::Stage;
                    auto text=ui("Salida correcta, pero falta acreditar la secuencia.",
                        "Output matches, but the history sequence is not yet verified.",
                        "La sortie correspond, mais la séquence historique reste à vérifier.");
                    switch(vm.selective_recovery->stage()){
                    case Stage::unrecorded:
                        text+="\n"+ui("Registra primero la versión inicial de los seis archivos desde Historial.",
                            "First record the original six-file revision in History.",
                            "Enregistrez d'abord la révision initiale des six fichiers dans l'historique.");
                        break;
                    case Stage::recorded:
                        text+="\n"+ui("Modifica la fórmula de cajas.cpp y añade un comentario a CMakeLists.txt; construye el proyecto defectuoso.",
                            "Change the formula in cajas.cpp, add a comment to CMakeLists.txt, then build the defective project.",
                            "Modifiez la formule dans cajas.cpp, ajoutez un commentaire à CMakeLists.txt, puis construisez le projet défectueux.");
                        break;
                    case Stage::failed_built:
                        text+="\n"+ui("Ejecuta el objetivo defectuoso para comprobar los tres fallos y el código de salida 1.",
                            "Run the defective target to observe three failures and exit code 1.",
                            "Exécutez la cible défectueuse pour observer les trois échecs et le code de sortie 1.");
                        break;
                    case Stage::failed_executed:
                    case Stage::compared_one:
                        text+="\n"+ui("Compara las dos rutas modificadas con la misma revisión guardada en Historial.",
                            "Compare both changed files with the same recorded revision in History.",
                            "Comparez les deux fichiers modifiés avec la même révision enregistrée dans l'historique.");
                        break;
                    case Stage::compared_both:
                        text+="\n"+ui("Recupera únicamente componente/src/cajas.cpp desde esa revisión, conservando el comentario de la raíz.",
                            "Restore only componente/src/cajas.cpp from that revision, retaining the root comment.",
                            "Restaurez seulement componente/src/cajas.cpp depuis cette révision, en conservant le commentaire de la racine.");
                        break;
                    case Stage::source_restored:
                        text+="\n"+ui("Construye de nuevo el objetivo con la fuente recuperada.",
                            "Build the target again with the restored source.",
                            "Reconstruisez la cible avec le fichier source restauré.");
                        break;
                    case Stage::restored_built:
                        text+="\n"+ui("Ejecuta de nuevo el objetivo reconstruido y comprueba las siete coincidencias.",
                            "Run the rebuilt target and verify all seven matches.",
                            "Exécutez la cible reconstruite et vérifiez les sept correspondances.");
                        break;
                    case Stage::verified:
                        text+="\n"+ui("Revisa que la ejecución final pertenezca a este mismo proyecto y objetivo.",
                            "Check that the final execution belongs to this project and target.",
                            "Vérifiez que l'exécution finale correspond à ce projet et à cette cible.");
                        break;
                    }
                    feedback(index,text,false,true);
                    if(hooks.feedback_changed)hooks.feedback_changed(window,index,false);
                }
                else if(correct&&vm.status)set_rich_text(vm.status,compiler_progress_text(index,true));
                if (objective_completed && has_references(block(index))) {
                    data.preferences["native_windows"][reference_key(block(index))] = reference_seal(block(index));
                }
                if(correct){refresh_compiler_history(index);mark_dirty();flush();refresh_references();}
            }
        }
        if(!laboratory()&&!curriculum::has_compiler_objective(block(index))&&vm.status)
            set_rich_text(vm.status,unavailable_compiler_status());
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
            cancel_restore();deferred_entry.cancel();verified_math.clear();rejected_math.clear();math_answer_generations.clear();
            data = store.load(); state::prepare_lesson_state(data, lesson);
            begin_entry(state::current_page(data, lesson)); dirty = false; import_done = true;
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
        if (import_done) { scroll = 0; render_page();verify_saved_math(); }
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
        if(notification==CodeEditor::layout_ready_notification){queue_answer_viewport(sender,true);return;}
        if(notification==EN_VSCROLL||notification==EN_HSCROLL){queue_answer_viewport(sender);return;}
        if(notification==EN_SETFOCUS)for(std::size_t i=0;i<controls.size();++i)
            if(sender==controls[i].answer||sender==find_item(control_id(i,3)))selected_puzzle=i;
        if ((notification == EN_SETFOCUS || notification == EN_KILLFOCUS) && editing_bar) editing_bar->refresh();
        if (notification == EN_KILLFOCUS) {
            for (std::size_t i = 0; i < controls.size(); ++i) if (sender == controls[i].source && !updating) { capture_draft(i); mark_dirty(); }
        }
        if (notification != BN_CLICKED) return;
        if (id >= pages_menu_base && id < pages_menu_base + static_cast<int>(lesson.pages.size()))
            { change_page(static_cast<std::size_t>(id - pages_menu_base)); return; }
        if (id == import_id) { import_state(); return; }
        if(id==finish_id){(void)finish_lesson();return;}
        if(id==continue_id){if(page+1<lesson.pages.size())change_page(page+1);else (void)finish_lesson();return;}
        if (id == menu_id) { open_lesson_options(); return; }
        if(id==back_id){(void)navigate_back();return;}
        if (id == reader_id || id == language_id || id == attempts_id || id == settings_id) {
            if(id==attempts_id&&attempt_state.premium)return;
            PostMessageW(window,header_action_message,static_cast<WPARAM>(id),0); return;
        }
        if (id == page_top_id) { scroll_to(0); return; }
        if (id == page_bottom_id) { scroll_to(extent); return; }
        auto found = std::ranges::find_if(actions, [id](const auto& action) { return action.control == id; });
        if (found == actions.end()) return;
        const auto action = *found; auto& vm = controls.at(action.block);
        if(exercise(block(action.block).type)&&(action.type==ActionType::choose||action.type==ActionType::check
            ||action.type==ActionType::move_up||action.type==ActionType::move_down))selected_puzzle=action.block;
        switch (action.type) {
        case ActionType::image_detail: if(controls[action.block].image){flush();controls[action.block].image->show_detail(window);}break;
        case ActionType::check: check_answer(action.block); break;
        case ActionType::choose:
            if (vm.response.correct) break;
            if(vm.response.historical_choice)vm.response={};
            vm.response.selected = static_cast<std::int64_t>(action.option); vm.response.correct = false;
            refresh_choice_history(action.block);
            state::clear_response_review(vm.response);
            for (std::size_t i = 0; i < vm.choices.size(); ++i) SendMessageW(vm.choices[i], BM_SETCHECK, i == action.option ? BST_CHECKED : BST_UNCHECKED, 0);
            state::put_response(data, lesson, block(action.block), vm.response); feedback(action.block, "", false);sync_quiz_controls(action.block);mark_dirty(); break;
        case ActionType::move_up: case ActionType::move_down: {
            if (vm.response.correct) break;
            const auto destination = action.type == ActionType::move_up ? action.option - 1 : action.option + 1;
            if (destination >= vm.order.size()) break;
            std::swap(vm.order[action.option], vm.order[destination]);
            vm.response.order.assign(vm.order.begin(), vm.order.end()); vm.response.correct = false;
            state::clear_response_review(vm.response);
            state::put_response(data, lesson, block(action.block), vm.response); mark_dirty();
            feedback(action.block, "", false); refresh_order(action.block); break;
        }
        case ActionType::run: run(action.block); break;
        case ActionType::toggle_wrap:{
            const bool enabled=!code_wrap();
            if(laboratory())data.preferences["native_windows"]["cpp_laboratory_wrap"]=enabled;
            else if(const auto course=course_id();!course.empty())appearance::save_course_wrap(data,course,enabled);
            else appearance::save_global_wrap(data,enabled,{});
            for(std::size_t i=0;i<controls.size();++i){auto& item=controls[i];apply_wrap(item.source);apply_wrap(item.simulated_code);
                if(item.answer&&item.answer_editor&&block(i).type==BlockType::fill_code)apply_wrap(item.answer);
                if(item.order_view)item.order_view->set_wrap(enabled);if(item.fill)item.fill->set_wrap(enabled);}
            refresh_wrap_buttons();mark_dirty();reflow();break;}
        case ActionType::compiler_info:{
            const auto popup=CreatePopupMenu();if(!popup)throw std::runtime_error("Cannot create the compiler information menu");
            AppendMenuW(popup,MF_STRING|MF_CHECKED,1,L"MSVC C++23 · x64");const auto style=prepare_native_menu(popup,window);RECT bounds{};GetWindowRect(find_item(control_id(action.block,36)),&bounds);
            (void)TrackPopupMenuEx(popup,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,bounds.left,bounds.bottom,window,nullptr);DestroyMenu(popup);break;}
        case ActionType::editor_menu: code_menu(action.block,false); break;
        case ActionType::console_menu: code_menu(action.block,true); break;
        case ActionType::simulated_menu: simulated_menu(action.block);break;
        case ActionType::copy_simulated: copy_simulated(action.block);
            show_settings_notice(window,vm.answer_editor?ui("Texto copiado","Text Copied","Texte copié")
                :block(action.block).type==BlockType::multiple_choice?ui("Ejercicio copiado","Exercise Copied","Exercice copié")
                :ui("Código copiado","Code Copied","Code copié"));break;
        case ActionType::copy_readonly: copy_text(block(action.block).code);show_settings_notice(window,ui("Código copiado","Code Copied","Code copié"));break;
        case ActionType::reset_answer: reset_answer(action.block);break;
        case ActionType::copy_source: copy_text(vm.source_editor->document().text);show_settings_notice(window,ui("Código copiado","Code Copied","Code copié")); break;
        case ActionType::copy_compiler_append:
            if(const auto& revision=block(action.block).compiler_revision;revision&&revision->starter_append){
                copy_text(*revision->starter_append);show_settings_notice(window,ui("Caso copiado","Case Copied","Cas copié"));
            }
            break;
        case ActionType::copy_console: copy_text(edit_text(vm.console)); break;
        case ActionType::clear_console: vm.console_channels = {}; console_present(action.block); break;
        case ActionType::undo: SetFocus(vm.source); vm.source_editor->undo(); break;
        case ActionType::redo: SetFocus(vm.source); vm.source_editor->redo(); break;
        case ActionType::fullscreen_source: toggle_fullscreen(action.block,false); break;
        case ActionType::fullscreen_console: toggle_fullscreen(action.block,true); break;
        case ActionType::select_stdout: console_present(action.block,0); break;
        case ActionType::select_stderr: console_present(action.block,1); break;
        case ActionType::select_diagnostics: console_present(action.block,2); break;
        case ActionType::cut_source:SetFocus(vm.source);SendMessageW(vm.source,WM_CUT,0,0);break;
        case ActionType::paste_source:SetFocus(vm.source);SendMessageW(vm.source,WM_PASTE,0,0);break;
        case ActionType::copy_source_selection:SendMessageW(vm.source,WM_COPY,0,0);break;
        case ActionType::select_all_source:SetFocus(vm.source);SendMessageW(vm.source,EM_SETSEL,0,-1);break;
        case ActionType::copy_console_selection:SendMessageW(vm.console,WM_COPY,0,0);break;
        case ActionType::select_all_console:SetFocus(vm.console);SendMessageW(vm.console,EM_SETSEL,0,-1);break;
        case ActionType::reset:
            if (running_block == action.block) cancel_run();
            windows::reset_compiler_puzzle(data,lesson,block(action.block));
            if(vm.selective_recovery)vm.selective_recovery->invalidate();
            refresh_compiler_history(action.block);
            clear_reference_completion(action.block);feedback(action.block,"",false);mark_dirty();
            if(vm.project){vm.project->reset();capture_draft(action.block);mark_dirty();refresh_references();break;}
            updating = true;
            vm.source_editor->reset(predefined_document(block(action.block).initial_code));
            updating = false;
            ++vm.revision; capture_draft(action.block); feedback(action.block, "", false);
            clear_reference_completion(action.block);
            vm.console_channels = {};
            updating = true; set_rich_text(vm.console, ""); style_cpp(vm.source); updating = false;
            if(laboratory()){
                laboratory_status=LaboratoryStatus::ready;refresh_laboratory_status();data.preferences["native_windows"]["cpp_laboratory_wrap"]=false;apply_wrap(vm.source);
                const auto wrap=find_item(control_id(action.block,35));icon_buttons[wrap]=Icon::wrap_off;label_radio(wrap,ui("Ajuste de línea desactivado","Line wrap disabled","Retour à la ligne désactivé"));InvalidateRect(wrap,nullptr,FALSE);
            }
            EnableWindow(vm.compile, TRUE); mark_dirty(); refresh_references();break;
        case ActionType::tab: SetFocus(vm.source); vm.source_editor->insert_tab(); break;
        }
    }
};

LRESULT CALLBACK console_icon_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
    auto* view=reinterpret_cast<LessonView*>(value);
    if(message==WM_LBUTTONDOWN||message==BM_CLICK){
        if(!view->icon_focus.contains(item))view->icon_focus.emplace(item,view->capture_console_focus());
    }
    const auto result=DefSubclassProc(item,message,wparam,lparam);
    if(message==WM_LBUTTONUP||message==BM_CLICK||message==WM_CANCELMODE){
        if(const auto found=view->icon_focus.find(item);found!=view->icon_focus.end()){
            const auto snapshot=found->second;view->icon_focus.erase(found);view->restore_console_focus(snapshot);
        }
    }
    if(message==WM_NCDESTROY){view->icon_focus.erase(item);RemoveWindowSubclass(item,console_icon_proc,21);}
    return result;
}
LRESULT CALLBACK quiz_answer_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
    auto& view=*reinterpret_cast<LessonView*>(value);
    if(message==WM_ENABLE){
        if(wparam){
            if(const auto before=view.answer_disabled_readonly.find(item);before!=view.answer_disabled_readonly.end()){
                SendMessageW(item,EM_SETREADONLY,before->second,0);view.answer_disabled_readonly.erase(before);
            }
        }else if(!reader_theme::palette(item).high_contrast){
            // WS_DISABLED is already real. Avoid RichEdit's disabled system
            // fill while keeping native read-only text services and glyphs.
            view.answer_disabled_readonly.try_emplace(item,(GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY)!=0);
            SendMessageW(item,EM_SETREADONLY,TRUE,0);SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));
            InvalidateRect(item,nullptr,TRUE);return 0;
        }
        const auto result=DefSubclassProc(item,message,wparam,lparam);
        if(!reader_theme::palette(item).high_contrast)SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));
        return result;
    }
    // This runs above the code-assistance adapter: completed answers cannot
    // accept a pair, Tab, Enter, clipboard edit or Undo through its shortcuts.
    if(!IsWindowEnabled(item)&&(message==WM_CHAR||message==WM_KEYDOWN||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR
        ||message==WM_UNDO||message==EM_UNDO||message==EM_REDO||message==EM_REPLACESEL))return 0;
    if(message==WM_NCDESTROY){view.answer_disabled_readonly.erase(item);view.pending_answer_viewports.erase(item);RemoveWindowSubclass(item,quiz_answer_proc,24);}
    return DefSubclassProc(item,message,wparam,lparam);
}
LRESULT CALLBACK gesture_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
    auto* view = reinterpret_cast<LessonView*>(value);
    if(message==WM_SETFOCUS&&GetWindowTextLengthW(item)==0&&std::ranges::any_of(view->controls,[item](const auto& block){return block.answer==item;})){
        // An empty answer can enter the viewport with the same outer size
        // used by its offscreen provisional layout. Give TextServices the
        // current client before its first native caret is created.
        RECT client{};GetClientRect(item,&client);SendMessageW(item,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom));
    }
    if(message==WM_LBUTTONDBLCLK&&view->double_click_zoom(item))return 0;
    if(message==WM_KEYDOWN&&wparam==VK_ESCAPE&&navigate_lesson_back(view->window))return 0;
    if (message == WM_CONTEXTMENU) for (std::size_t index = 0; index < view->controls.size(); ++index) {
        if(view->controls[index].simulated_code==item){view->simulated_menu(index);return 0;}
        if (view->controls[index].source == item || view->controls[index].console == item) {
            view->code_menu(index,view->controls[index].console == item,true,POINT{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}); return 0;
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
        FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(view&&window==view->fullscreen_header?(view->laboratory()?reader_theme::Surface::compiler_header:reader_theme::Surface::card_header):view&&view->laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas));
        if(view&&window!=view->fullscreen_header)view->paint_simulated_cards(reinterpret_cast<HDC>(wparam),view->pane_paint_offset(window));return TRUE;
    }
    if(message==WM_PAINT&&view){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);
        RECT area{};GetClientRect(window,&area);FillRect(dc,&area,reader_theme::brush(window==view->fullscreen_header?(view->laboratory()?reader_theme::Surface::compiler_header:reader_theme::Surface::card_header):view->laboratory()?reader_theme::Surface::screen:reader_theme::Surface::canvas));if(window!=view->fullscreen_header)view->paint_simulated_cards(dc,view->pane_paint_offset(window));EndPaint(window,&paint);return 0;}
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
        case answer_viewport_message:view->sync_answer_viewport(reinterpret_cast<HWND>(wparam));return 0;
        case WM_KEYDOWN: if(wparam==VK_ESCAPE&&view->navigate_back())return 0;break;
        case enter_key_message:return lparam&&view->enter_key(*reinterpret_cast<const MSG*>(lparam));
        case header_action_message: {
            std::function<void(HWND)> action;
            if (wparam == back_id){
                if(view->deferred_entry.waiting())view->deferred_entry.invalidate();
                action = view->hooks.back_to_catalog;
            }
            else if (wparam == reader_id&&!view->deferred_entry.waiting()) action = view->hooks.reader_requested;
            else if (wparam == language_id) action = view->hooks.language_requested;
            else if (wparam == attempts_id&&!view->attempt_state.premium) action = view->hooks.attempts_requested;
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
        case lesson_options_query_message:
            if(lparam)*reinterpret_cast<LessonOptionsSnapshot*>(lparam)=view->options_snapshot();return 1;
        case lesson_options_action_message:return view->invoke_option(static_cast<LessonOptionAction>(wparam));
        case lesson_page_navigate_message: return !view->import_open && view->change_page(static_cast<std::size_t>(wparam));
        case lesson_reading_regions_message:
            if(lparam){
                if(wparam==5)*reinterpret_cast<std::vector<LessonReadingArchive>*>(lparam)=view->editorial_reading_archives();
                else if(wparam==4)*reinterpret_cast<std::vector<LessonReadingArchive>*>(lparam)=view->retired_reading_archives();
                else *reinterpret_cast<std::vector<LessonReadingRegion>*>(lparam)=wparam==3?view->retired_reading_regions():view->reading_regions(static_cast<int>(wparam));
            }return 1;
        case lesson_view_hooks_message:
            if (lparam) view->hooks = *reinterpret_cast<LessonViewHooks*>(lparam);
            for(auto& controls:view->controls)if(controls.math)controls.math->set_expression_request(view->hooks.math_editor_request);
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
            if(header->code==EN_SELCHANGE)view->queue_answer_viewport(header->hwndFrom);
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
        if (message == WM_CREATE) { lesson_creation_error=error.what();OutputDebugStringW(wide(error.what()).c_str()); return -1; }
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
    lesson_creation_error.clear();
    auto window = CreateWindowExW(WS_EX_CONTROLPARENT, view_class,wide(view->ui("Lección","Lesson","Leçon")).c_str(), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), view.get());
    if (!window) { view.release();throw std::runtime_error(lesson_creation_error.empty()?"Cannot open the lesson; see its content contract or saved-state diagnostic":"Cannot open the lesson: "+lesson_creation_error); }
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
LessonOptionsSnapshot lesson_options_snapshot(HWND window){
    LessonOptionsSnapshot result;
    if(IsWindow(window))SendMessageW(window,lesson_options_query_message,0,reinterpret_cast<LPARAM>(&result));
    return result;
}
bool invoke_lesson_option(HWND window,LessonOptionAction action){
    return IsWindow(window)&&SendMessageW(window,lesson_options_action_message,static_cast<WPARAM>(action),0)!=0;
}
bool navigate_lesson_back(HWND window){
    if(!IsWindow(window))return false;wchar_t name[96]{};GetClassNameW(window,name,96);if(std::wstring_view(name)!=view_class)return false;
    const auto view=reinterpret_cast<LessonView*>(GetWindowLongPtrW(window,GWLP_USERDATA));return view&&view->navigate_back();
}
bool dispatch_lesson_enter(MSG& message){
    if((message.message!=WM_KEYDOWN&&message.message!=WM_KEYUP)||message.wParam!=VK_RETURN
        ||(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)||(GetKeyState(VK_SHIFT)&0x8000))return false;
    auto item=message.hwnd;
    for(;item;item=GetParent(item)){wchar_t name[96]{};GetClassNameW(item,name,96);
        if(std::wstring_view(name)==view_class)return SendMessageW(item,enter_key_message,0,reinterpret_cast<LPARAM>(&message))!=0;}
    // A solved native control can relinquish focus to the app window when it
    // becomes disabled. The same visible lesson still owns the next Enter.
    const auto root=GetAncestor(message.hwnd,GA_ROOT);HWND lesson=nullptr;
    if(root)EnumChildWindows(root,[](HWND child,LPARAM result)->BOOL{wchar_t name[96]{};GetClassNameW(child,name,96);
        if(IsWindowVisible(child)&&std::wstring_view(name)==view_class){*reinterpret_cast<HWND*>(result)=child;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&lesson));
    return lesson&&SendMessageW(lesson,enter_key_message,0,reinterpret_cast<LPARAM>(&message))!=0;
}
std::vector<LessonReadingRegion> lesson_reading_regions(HWND window) {
    std::vector<LessonReadingRegion> result;
    if (window) SendMessageW(window,lesson_reading_regions_message,0,reinterpret_cast<LPARAM>(&result)); return result;
}
std::vector<LessonReadingRegion> pre_caption_lesson_reading_regions(HWND window){
    std::vector<LessonReadingRegion> result;
    if(window)SendMessageW(window,lesson_reading_regions_message,2,reinterpret_cast<LPARAM>(&result));return result;
}
std::vector<LessonReadingRegion> legacy_lesson_reading_regions(HWND window){
    std::vector<LessonReadingRegion> result;
    if(window)SendMessageW(window,lesson_reading_regions_message,1,reinterpret_cast<LPARAM>(&result));return result;
}
std::vector<LessonReadingRegion> retired_lesson_reading_regions(HWND window){
    std::vector<LessonReadingRegion> result;
    if(window)SendMessageW(window,lesson_reading_regions_message,3,reinterpret_cast<LPARAM>(&result));return result;
}
std::vector<LessonReadingArchive> retired_lesson_reading_archives(HWND window){
    std::vector<LessonReadingArchive> result;
    if(window)SendMessageW(window,lesson_reading_regions_message,4,reinterpret_cast<LPARAM>(&result));return result;
}
std::vector<LessonReadingArchive> editorial_lesson_reading_archives(HWND window){
    std::vector<LessonReadingArchive> result;
    if(window)SendMessageW(window,lesson_reading_regions_message,5,reinterpret_cast<LPARAM>(&result));return result;
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
