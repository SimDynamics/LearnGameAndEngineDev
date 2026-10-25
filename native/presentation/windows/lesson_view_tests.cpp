#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/icons.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/narration_worker.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
std::vector<std::pair<std::string,std::string>> reading_script(HWND view){
    std::vector<std::pair<std::string,std::string>> result;
    for(const auto& region:lesson_reading_regions(view))result.emplace_back(region.key,region.source_text);
    return result;
}
CHARFORMAT2W character_format(HWND edit,LONG character = 0) {
    CHARRANGE previous{}; SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
    CHARRANGE range{character,character+1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));return format;
}
RECT rich_edit_viewport(HWND field) {
    Microsoft::WRL::ComPtr<IRichEditOle> rich;
    SendMessageW(field,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument2> document;
    require(rich&&SUCCEEDED(rich.As(&document)),"Cannot query the actual native answer view rectangle");
    RECT bounds{};
    require(document->GetClientRect(tomClientCoord|tomIncludeInset,&bounds.left,&bounds.top,&bounds.right,&bounds.bottom)==S_OK,
        "Cannot retrieve the zoomed native answer view rectangle");
    return bounds;
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
HWND descendant_class(HWND parent,std::wstring_view name) {
    struct Search { std::wstring_view name; HWND result = nullptr; } search{name};
    EnumChildWindows(parent,[](HWND item,LPARAM value) -> BOOL {
        auto& current = *reinterpret_cast<Search*>(value); wchar_t type[128]{}; GetClassNameW(item,type,128);
        if (std::wstring_view(type) == current.name) {
            if(current.name==L"LearnGameAndEngineDev.LessonPane"&&GetWindowTextLengthW(item)==0)return TRUE;
            current.result = item; return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::string accessible_name(HWND item) {
    Microsoft::WRL::ComPtr<IAccessible> accessible;
    require(SUCCEEDED(AccessibleObjectFromWindow(item,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(accessible.GetAddressOf()))),
        "A choice has no Windows accessibility object");
    VARIANT child{}; child.vt = VT_I4; child.lVal = CHILDID_SELF;
    BSTR name = nullptr; require(SUCCEEDED(accessible->get_accName(child,&name)),"A choice has no accessible name");
    const auto value = name ? utf8(std::wstring_view(name,SysStringLen(name))) : std::string{};
    SysFreeString(name); return value;
}
void pump(HWND host) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE&&GetAncestor(message.hwnd,GA_ROOT)==host){
            GUITHREADINFO info{};info.cbSize=sizeof(info);GetGUIThreadInfo(GetCurrentThreadId(),&info);
            if(!(info.flags&(GUI_INMENUMODE|GUI_POPUPMENUMODE|GUI_SYSTEMMENUMODE))){
                const auto lesson=descendant_class(host,L"LearnGameAndEngineDev.LessonView");
                if(lesson&&navigate_lesson_back(lesson))continue;
            }
        }
        if (!dispatch_lesson_enter(message)&&!IsDialogMessageW(host, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
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
    state::StateData initial_data;
    std::filesystem::path content;
    bool supplied_model = false;
    HWND host = nullptr, view = nullptr;
    explicit Fixture(const std::function<void(state::StateData&, const curriculum::Lesson&)>& initialize = {},
        const std::function<curriculum::Lesson(const curriculum::Lesson&)>& load_model = {})
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"native-lesson-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false})),
        content(std::filesystem::path(build::source_root) / "app/src/main/assets/content") {
        require(!std::filesystem::exists(environment.paths.root), "QA instance unexpectedly already exists");
        initialize_profile_directories(environment.paths);
        lesson = windows::load_lesson(content / "es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json");
        if (load_model) { lesson = load_model(lesson); supplied_model = true; }
        state::StateStore seed_store(environment.paths.root); initial_data=seed_store.load();
        // Spanish expectations below are explicit test choices, independent of
        // the PC language. Individual language cases override this preference.
        initial_data.preferences["native_windows"]["ui_language"]=std::string("es");
        if (initialize) initialize(initial_data,lesson);
        seed_store.save(initial_data);
        host = CreateWindowExW(WS_EX_CONTROLPARENT, L"LgaedLessonViewTests", L"Native lesson integration QA",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE, 30, 30, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create native QA host");
        open();
    }
    ~Fixture() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        require(!view, "Lesson view already open");
        view = supplied_model ? create_lesson_view(host,environment,lesson) : create_lesson_view(host, environment, content);
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
    std::size_t page() const { return lesson_page_state(view).index; }
    void click(int value) {
        const auto item = control(value); require(IsWindowEnabled(item), "Attempted a disabled control");
        SendMessageW(item, BM_CLICK, 0, 0); pump(host);
    }
    void click(std::size_t block, int part) { click(id(block,part)); }
    bool fullscreen() const { return !(GetWindowLongPtrW(control(110),GWL_STYLE)&WS_VISIBLE); }
    void escape() {
        PostMessageW(view,WM_KEYDOWN,VK_ESCAPE,1);pump(host);
    }
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
        if (!dispatch_lesson_enter(key)&&!IsDialogMessageW(host, &key)) { TranslateMessage(&key); DispatchMessageW(&key); }
        pump(host);
        key={field,WM_KEYUP,VK_RETURN,LPARAM{1}<<31};
        if(!dispatch_lesson_enter(key))SendMessageW(field, WM_KEYUP, VK_RETURN, 1);pump(host);
    }
    void next() {
        const auto previous = page(); require(navigate_lesson_view(view,previous + 1), "Next page was unexpectedly blocked"); pump(host); require(page() == previous + 1, "Next did not advance one page");
    }
    void select_page(std::size_t value) {
        require(navigate_lesson_view(view,value), "The Pages command rejected an unlocked page");
        pump(host); require(page() == value, "Page navigation did not restore the requested unlocked page");
    }
    void solve(std::size_t block) {
        const auto& current = lesson.pages.at(page()).blocks.at(block);
        if (current.type == BlockType::multiple_choice) {
            click(block,20 + static_cast<int>(*current.correct_index) * 2); click(block,3);
        } else if (current.type == BlockType::order_code) {
            std::vector<std::size_t> order(current.fragments.size()); std::iota(order.begin(), order.end(), 0);
            for (std::size_t position = 0; position < order.size(); ++position) {
                auto found = static_cast<std::size_t>(std::ranges::find(order,current.correct_order[position]) - order.begin());
                const auto row=GetDlgItem(control(block,5),100+static_cast<int>(order[found]));
                require(row!=nullptr,"The real order card is missing its stable code row");
                while (found > position) { SendMessageW(row,WM_SYSKEYDOWN,VK_UP,LPARAM{1}<<29);pump(host);std::swap(order[found],order[found-1]); --found; }
            }
            click(block,3);
        } else {
            replace(block,2,current.answers.at(0)); const auto current_page = page(); enter(block);
            require(page() == current_page, "Enter checked and unexpectedly changed page");
        }
        require(text(block,4).starts_with("✓ Correcto"), "Authored response was not accepted through actual controls");
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
        const auto pane=descendant_class(view,L"LearnGameAndEngineDev.LessonPane");
        require(pane != nullptr, "Lesson pane missing");
        std::vector<RECT> rectangles;
        for (auto child = GetWindow(pane,GW_CHILD); child; child = GetWindow(child,GW_HWNDNEXT)) {
            if (!(GetWindowLongPtrW(child,GWL_STYLE) & WS_VISIBLE)) continue;
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
    void viewport_layout() {
        const auto pane = descendant_class(view,L"LearnGameAndEngineDev.LessonPane");
        require(pane != nullptr,"Cannot locate the lesson viewport");
        RECT viewport{}, header{}, frame{}, menu{};
        GetWindowRect(pane,&viewport); GetWindowRect(control(105),&header);
        GetWindowRect(view,&frame); GetWindowRect(control(107),&menu);
        require(header.bottom < viewport.top && menu.bottom < viewport.top && viewport.bottom <= frame.bottom,
                "The compact header or its menu covers lesson content");
        require(!descendant(view,101) && !descendant(view,102) && !descendant(view,103),
                "Removed previous/next/selector navigation still exists as a hidden or visible control");
        require(!descendant(view,104),"A duplicate import button still occupies the lesson toolbar");
        require(SendMessageW(control(106),PBM_GETPOS,0,0) == static_cast<LRESULT>(page() + 1),
                "The fixed page progress does not match the current lesson page");
    }
    void scroll_repaint() {
        const bool shown = IsWindowVisible(host); if (!shown) { ShowWindow(host,SW_SHOWNOACTIVATE); pump(host); }
        const auto pane = descendant_class(view,L"LearnGameAndEngineDev.LessonPane");
        SendMessageW(view,WM_VSCROLL,SB_TOP,0); pump(host);
        struct Snapshot { HWND item; std::string text; RECT bounds; };
        std::vector<Snapshot> before;
        for (auto item = GetWindow(pane,GW_CHILD); item; item = GetWindow(item,GW_HWNDNEXT)) {
            RECT bounds{}; GetWindowRect(item,&bounds); before.push_back({item,window_text(item),bounds});
        }
        for (const auto delta : {-WHEEL_DELTA,-WHEEL_DELTA,WHEEL_DELTA,-WHEEL_DELTA,WHEEL_DELTA,WHEEL_DELTA}) {
            SCROLLINFO initial{sizeof(initial),SIF_POS}; GetScrollInfo(pane,SB_VERT,&initial);
            RedrawWindow(pane,nullptr,nullptr,RDW_VALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
            SendMessageW(pane,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),0);
            SCROLLINFO after{sizeof(after),SIF_POS}; GetScrollInfo(pane,SB_VERT,&after);
            if (after.nPos != initial.nPos) {
                RECT dirty{}, client{}; GetClientRect(pane,&client);
                require(GetUpdateRect(pane,&dirty,FALSE) && EqualRect(&dirty,&client),
                        "Moving rows did not invalidate the complete old viewport background");
                RECT viewport{}; GetWindowRect(pane,&viewport);
                for (const auto& snapshot : before) {
                    RECT child{}, visible{}; GetWindowRect(snapshot.item,&child);
                    if (IntersectRect(&visible,&viewport,&child) && child.bottom > child.top)
                        require(GetUpdateRect(snapshot.item,nullptr,FALSE),"A moved visible child retained stale client pixels");
                }
            }
            pump(host); non_overlapping_rows(); viewport_layout();
            for (const auto& snapshot : before)
                require(IsWindow(snapshot.item) && window_text(snapshot.item) == snapshot.text,
                        "Wheel scrolling recreated controls, hid text or changed lesson content");
        }
        SendMessageW(view,WM_VSCROLL,SB_TOP,0); pump(host);
        for (const auto& snapshot : before) {
            RECT bounds{}; GetWindowRect(snapshot.item,&bounds);
            require(EqualRect(&bounds,&snapshot.bounds),"Scrolling back to the top changed the layout or row dimensions");
        }
        if (!shown) { ShowWindow(host,SW_HIDE); pump(host); }
    }
    void contextual_editing(std::size_t block) {
        const bool shown = IsWindowVisible(host); if (!shown) { ShowWindow(host,SW_SHOWNOACTIVATE); pump(host); }
        const auto bar = descendant_class(view,L"LearnGameAndEngineDev.EditingBar");
        require(!bar,"Programming must not create the retired floating shortcuts");
        const auto source=control(block,10);const auto before=text(block,10);
        SetFocus(source);SendMessageW(source,WM_KEYDOWN,VK_TAB,0);SendMessageW(source,WM_KEYUP,VK_TAB,0);pump(host);
        require(GetFocus()==source&&text(block,10)!=before,"Physical TAB lost focus or stopped editing without the floating bar");
        SendMessageW(source,WM_UNDO,0,0);pump(host);
        require(text(block,10)==before,"Physical TAB did not preserve the existing Undo history");
        viewport_layout();
        if (!shown) { ShowWindow(host,SW_HIDE); pump(host); }
    }
    void drag_control(HWND item,POINT start,POINT delta) {
        POINT origin = start; ClientToScreen(item,&origin);
        SendMessageW(item,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
        for (int step = 1; step <= 3; ++step) {
            POINT point{origin.x + delta.x * step / 3,origin.y + delta.y * step / 3}; ScreenToClient(item,&point);
            SendMessageW(item,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        }
        POINT end{origin.x + delta.x,origin.y + delta.y}; ScreenToClient(item,&end);
        SendMessageW(item,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y)); pump(host);
    }
    void gesture_navigation(bool unlocked) {
        const bool shown = IsWindowVisible(host); if (!shown) { ShowWindow(host,SW_SHOWNOACTIVATE); pump(host); }
        const auto pane = descendant_class(view,L"LearnGameAndEngineDev.LessonPane");
        const auto before = page();
        SendMessageW(view,WM_VSCROLL,SB_TOP,0); pump(host);
        drag_control(pane,{8,160},{0,-100});
        SCROLLINFO position{sizeof(position),SIF_POS}; GetScrollInfo(pane,SB_VERT,&position);
        require(position.nPos > 0 && page() == before,"A vertical reading drag did not scroll only the current lesson");
        drag_control(pane,{300,160},{-180,0});
        require(page() == before + (unlocked ? 1 : 0),"Horizontal page drag bypassed a gate or failed to navigate an unlocked page");
        if (unlocked) {
            drag_control(pane,{180,160},{180,0});
            require(page() == before,"Backward page dragging did not preserve the previous page");
        }
        if (!shown) { ShowWindow(host,SW_HIDE); pump(host); }
    }
    void editor_gestures(std::size_t block) {
        const bool shown = IsWindowVisible(host); if (!shown) { ShowWindow(host,SW_SHOWNOACTIVATE); pump(host); }
        const auto source = control(block,10); const auto before = text(block,10); const auto old_page = page();
        auto expanded = before + "//" + std::string(400,'x') + "\n";
        for (int line = 0; line < 60; ++line) expanded += "// línea de desplazamiento\n";
        replace(block,10,expanded); pump(host);
        const auto scrolls = [&](int axis) {
            SCROLLINFO value{sizeof(value),SIF_RANGE | SIF_PAGE}; GetScrollInfo(source,axis,&value);
            return value.nMax - value.nMin + 1 > static_cast<int>(value.nPage);
        };
        require(scrolls(SB_HORZ) && scrolls(SB_VERT),"The real editor fixture has no two-axis overflow");
        CHARRANGE selection{0,0}; SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        POINT initial{}, after{}; SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&initial));
        RECT bounds{}; GetClientRect(source,&bounds);
        // The first line is short. Its far-right blank area remains empty
        // after horizontal scrolling; the bottom row can contain the long line.
        const POINT empty_start{bounds.right - 12,8};
        drag_control(source,empty_start,{-100,0});
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
        require(after.x > initial.x && page() == old_page && text(block,10) == expanded,
                "An empty-editor horizontal drag navigated the lesson or changed source text");
        initial = after;
        drag_control(source,empty_start,{0,-80});
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
        require(after.y > initial.y && page() == old_page && text(block,10) == expanded,
                "An editor with vertical overflow handed its drag to the lesson");
        selection = {0,4}; SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        drag_control(source,{20,20},{-160,0});
        require(page() == old_page && text(block,10) == expanded,"Native selection/cursor dragging changed page or source");
        SendMessageW(source,WM_UNDO,0,0); pump(host);
        require(text(block,10) == before,"Gesture ownership interfered with the existing source Undo history");
        if (!shown) { ShowWindow(host,SW_HIDE); pump(host); }
    }
    void compiler_menus(std::size_t block) {
        const auto source = control(block,10); const auto before = text(block,10); const auto verdict = text(block,4);
        const auto original_script=reading_script(view);CHARRANGE original_selection{};
        SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&original_selection));
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(id(block,75),0),0); pump(host);
        require(control(block,10) == source && text(block,10) == before,
                "Editor fullscreen recreated the document or changed the code");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(id(block,76),0),0); pump(host);
        const auto expanded_regions=lesson_reading_regions(view);
        require(!(GetWindowLongPtrW(source,GWL_STYLE) & WS_VISIBLE)&&reading_script(view)==original_script&&
            std::ranges::none_of(expanded_regions,[&](const auto& region){return region.target==source||region.target==control(block,14);}),
            "Console fullscreen changed the existing narration script or began narrating excluded source/output code");
        CHARRANGE expanded_selection{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&expanded_selection));
        require(expanded_selection.cpMin==original_selection.cpMin&&expanded_selection.cpMax==original_selection.cpMax,
            "Console fullscreen changed the hidden source editor's existing selection range");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(id(block,72),0),0); pump(host);
        require(text(block,14).empty() && text(block,10) == before && text(block,4) == verdict,
                "Clearing console output cleared code, answers or the feedback verdict");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(id(block,76),0),0); pump(host);
        require(GetWindowLongPtrW(source,GWL_STYLE) & WS_VISIBLE,"Leaving console fullscreen did not restore its exact editor");
        non_overlapping_rows(); viewport_layout();
    }
};
std::string message_code(const curriculum::Block& block, std::string_view old_message, std::string_view new_message) {
    auto source = block.initial_code;
    const auto position = source.find(old_message); require(position != source.npos, "Authored literal fixture missing");
    source.replace(position,old_message.size(),new_message); return source;
}
curriculum::Lesson with_relationships(const curriculum::Lesson& source,std::string_view references,
                                     std::string_view activities = "[]") {
    // Replace the one root property through the same Windows JSON parser.
    // Appending a second referenceAfter hid newly authored links in this test.
    using namespace winrt::Windows::Data::Json;
    auto object = JsonObject::Parse(winrt::to_hstring(source.raw_json));
    object.Insert(L"referenceAfter",JsonArray::Parse(winrt::to_hstring(references)));
    object.Insert(L"activityAfter",JsonArray::Parse(winrt::to_hstring(activities)));
    return windows::parse_lesson_json(winrt::to_string(object.Stringify()));
}
constexpr std::string_view reference_links = R"([{"activity":"block_0004_0002","blocks":["block_0005_0000","block_0005_0001","block_0005_0002","block_0005_0003"]},{"activity":"block_0005_0006","blocks":["block_0005_0007","block_0005_0008"]}])";

struct PopupCapture {
    HWND owner;
    HHOOK hook = nullptr;
    std::vector<std::string> captions;
    std::vector<UINT> states,commands;
    bool native_styles = true;
    std::string error;
    static inline PopupCapture* active = nullptr;
    explicit PopupCapture(HWND target) : owner(target) {
        require(active == nullptr,"A popup capture is already active"); active = this;
        hook = SetWindowsHookExW(WH_CALLWNDPROC,[](int code,WPARAM wp,LPARAM lp) -> LRESULT {
            auto* self = active;
            if (code >= 0 && self) {
                const auto& message = *reinterpret_cast<CWPSTRUCT*>(lp);
                if (message.hwnd == self->owner && message.message == WM_INITMENUPOPUP && self->captions.empty()) {
                    try {
                        const auto menu = reinterpret_cast<HMENU>(message.wParam);
                        for (int index = 0; index < GetMenuItemCount(menu); ++index) {
                            MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_ID;
                            require(GetMenuItemInfoW(menu,static_cast<UINT>(index),TRUE,&item),"Cannot inspect the real popup");
                            if (item.fType & MFT_SEPARATOR) continue;
                            self->native_styles = self->native_styles && (item.fType & MFT_OWNERDRAW) != 0;
                            std::wstring caption(static_cast<std::size_t>(item.cch)+1,L'\0');
                            item.dwTypeData = caption.data(); item.cch = static_cast<UINT>(caption.size());
                            require(GetMenuItemInfoW(menu,static_cast<UINT>(index),TRUE,&item),"Cannot read the real popup caption");
                            caption.resize(item.cch); self->captions.push_back(utf8(caption));
                            self->states.push_back(item.fState);self->commands.push_back(item.wID);
                        }
                    } catch (const std::exception& failure) { self->error = failure.what(); }
                    // Cancel only this test's own native popup through its
                    // actual owner; no global keyboard/mouse input is emitted.
                    PostMessageW(self->owner,WM_CANCELMODE,0,0);
                }
            }
            return CallNextHookEx(nullptr,code,wp,lp);
        },nullptr,GetCurrentThreadId());
        if (!hook) { active = nullptr; throw std::runtime_error("Cannot observe the native popup lifecycle"); }
    }
    ~PopupCapture() { if(hook)UnhookWindowsHookEx(hook); active=nullptr; }
    void verified() const {
        require(error.empty(),error); require(!captions.empty(),"The actual native popup did not initialize");
        require(native_styles || reader_theme::palette(owner).high_contrast,"The actual popup bypassed the native dark-menu helper");
    }
};

void lesson_interface_languages() {
    for (const auto locale : {"es","en","fr"}) {
        Fixture fixture([locale](state::StateData& data,const curriculum::Lesson&) {
            data.preferences["native_windows"]["ui_language"] = std::string(locale);
        },[](const curriculum::Lesson&) {
            return windows::parse_lesson_json(R"({"id":"qa_interface_labels","title":"Título curricular intacto","pages":[{"blocks":[
                {"type":"note","text":"Nota curricular intacta."},
                {"type":"fill_code","id":"answer","question":"Pregunta curricular intacta.","code":"[]","answers":["int"],"explanation":"Explicación curricular intacta.","reviewFeedback":"Guía curricular intacta."},
                {"type":"cpp_compiler","initialCode":"int main() { return 0; }\n","initialStandardInput":"","outputCheck":{"cases":[{"standardInput":"","outputs":[""]}]}},
                {"type":"cpp_compiler","initialCode":"int main() { return 0; }\n"}
                ]}]})");
        });
        const auto pick = [locale](const char* es,const char* en,const char* fr) {
            return std::string(std::string_view(locale)=="fr"?fr:std::string_view(locale)=="en"?en:es);
        };
        ShowWindow(fixture.host,SW_SHOWNOACTIVATE); pump(fixture.host);
        require(accessible_name(fixture.control(111)) == pick("Lector","Reader","Lecteur") &&
                accessible_name(fixture.control(112)) == pick("Idioma","Language","Langue") &&
                accessible_name(fixture.control(107)) == pick("Ajustes de la lección","Lesson Settings","Paramètres de la leçon") &&
                accessible_name(fixture.control(113)) == pick("Intentos disponibles","Available attempts","Essais disponibles"),
                "Lesson header labels ignored the independent interface language");
        require(window_text(fixture.control(2,11)) == pick("Compilar, ejecutar y comprobar","Compile, run and check","Compiler, exécuter et vérifier") &&
                window_text(fixture.control(3,11)) == pick("Compilar y ejecutar","Compile and run","Compiler et exécuter") &&
                fixture.text(2,17) == pick("Entrada","Input","Entrée") && fixture.text(2,30) == pick("Salida del programa (stdout)","Program output (stdout)","Sortie du programme (stdout)"),
                "Compiler/check/input/output labels did not follow the interface language");
        require(fixture.text(3,34)==pick("Listo para ejecutar.","Ready to Run.","Prêt pour l’exécution."),
                "The ordinary compiler omitted Android's localized ready status");
        fixture.click(3,11);wait_for(fixture.host,[&]{return IsWindowEnabled(fixture.control(3,11));},"The ordinary compiler did not finish its real locale-specific run");
        require(fixture.text(3,34)==pick("Ejecución terminada (código 0).","Execution finished (code 0).","Exécution terminée (code 0).")
                &&fixture.text(3,4).empty(),"An ordinary compiler lost its localized execution status or invented an output verdict");
        require(fixture.text(0,0) == "Nota curricular intacta." && fixture.text(1,0) == "Pregunta curricular intacta." &&
                fixture.text(2,10) == "int main() { return 0; }\n", "Interface localization changed authored text or C++");
        const auto regions = lesson_reading_regions(fixture.view);
        require(std::ranges::any_of(regions,[&](const auto& region) { return region.source_text == pick("Nota","Note","Note"); }),
                "The localized note heading and narration source disagree");
        {
            PopupCapture capture(fixture.view); fixture.click(107); capture.verified();
            require(capture.captions.front() == pick("Importar estado Android…","Import Android state…","Importer l'état Android…") &&
                    capture.captions[1] == pick("Páginas","Pages","Pages"), "The real lesson popup remained in another language");
        }
        {
            PopupCapture capture(fixture.view); SendMessageW(fixture.control(2,10),WM_CONTEXTMENU,0,MAKELPARAM(-1,-1));capture.verified();
            require(capture.captions.front() == pick("Deshacer","Undo","Annuler"),"The real editor popup ignored the interface language");
            for(const auto part:{81,82,83,84})require(std::ranges::find(capture.commands,static_cast<UINT>(id(2,part)))!=capture.commands.end(),
                "The actual editor context menu omits Cut/Paste/Copy selection/Select all");
            const auto cut=std::ranges::find(capture.commands,static_cast<UINT>(id(2,81)))-capture.commands.begin();
            const auto copy=std::ranges::find(capture.commands,static_cast<UINT>(id(2,83)))-capture.commands.begin();
            const auto paste=std::ranges::find(capture.commands,static_cast<UINT>(id(2,82)))-capture.commands.begin();
            require((capture.states[cut]&MFS_DISABLED)&&(capture.states[copy]&MFS_DISABLED),"Cut/Copy is enabled without an actual editor selection");
            require(((capture.states[paste]&MFS_DISABLED)==0)==(IsClipboardFormatAvailable(CF_UNICODETEXT)||IsClipboardFormatAvailable(CF_TEXT)),
                "Paste enablement does not reflect the available native text formats");
            require(capture.captions[cut]==pick("Cortar","Cut","Couper")&&capture.captions[paste]==pick("Pegar","Paste","Coller")&&
                capture.captions.back()==pick("Seleccionar todo","Select all","Tout sélectionner"),"Native editing actions are not localized");
        }
        SendMessageW(fixture.view,WM_COMMAND,MAKEWPARAM(id(2,84),BN_CLICKED),0);pump(fixture.host);
        CHARRANGE all{};SendMessageW(fixture.control(2,10),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&all));
        require(all.cpMin==0&&all.cpMax>10&&GetFocus()==fixture.control(2,10),"Select all did not select the real editor and preserve editing focus");
        {
            PopupCapture capture(fixture.view);SendMessageW(fixture.control(2,10),WM_CONTEXTMENU,0,MAKELPARAM(-1,-1));capture.verified();
            const auto cut=std::ranges::find(capture.commands,static_cast<UINT>(id(2,81)))-capture.commands.begin();
            const auto copy=std::ranges::find(capture.commands,static_cast<UINT>(id(2,83)))-capture.commands.begin();
            require(!(capture.states[cut]&MFS_DISABLED)&&!(capture.states[copy]&MFS_DISABLED),"Cut/Copy stayed disabled after selecting actual source text");
        }
        require(!IsWindowVisible(fixture.control(2,31))&&!descendant(fixture.view,id(2,23))
            &&!descendant(fixture.view,id(2,24))&&!descendant(fixture.view,id(2,25)),
            "An empty real console exposed channel selection or extra controls absent from Android");
        fixture.replace(1,2,"void"); fixture.enter(1);
        require(fixture.text(1,4) == pick("✗ Incorrecto","✗ Incorrect","✗ Incorrect") + "\n\nGuía curricular intacta.",
                "The wrong verdict was not localized or rewrote authored feedback");
        fixture.replace(1,2,"int"); fixture.enter(1);
        require(fixture.text(1,4) == pick("✓ Correcto","✓ Correct","✓ Correct") + "\n\nExplicación curricular intacta.",
                "The correct verdict was not localized or rewrote authored feedback");
        const auto source = fixture.control(2,10); const auto original = fixture.text(2,10);
        fixture.replace(2,10,original+"// historial QA\n");
        CHARRANGE selection{4,8}; SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        update_lesson_preferences(fixture.view,[](state::StateData& data) { data.preferences["native_windows"]["ui_language"]=std::string("fr"); });
        CHARRANGE restored{}; SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored));
        require(fixture.control(2,10)==source && restored.cpMin==selection.cpMin && restored.cpMax==selection.cpMax &&
                fixture.text(1,4)=="✓ Correct\n\nExplicación curricular intacta." && fixture.text(2,17)=="Entrée",
                "Refreshing interface labels recreated editors, moved the selection or changed authored feedback");
        SendMessageW(source,WM_UNDO,0,0); pump(fixture.host);
        require(fixture.text(2,10)==original,"Changing interface language discarded the editor's existing Undo history");
    }
    std::cout << "PASS ES/EN/FR real lesson/editor/console popups, generated verdicts, original curriculum, selection and Undo\n";
}
void laboratory_interface_parity(){
    for(const auto locale:{"es","en","fr"}){
        Fixture fixture([locale](state::StateData& data,const curriculum::Lesson&){data.preferences["native_windows"]["ui_language"]=std::string(locale);},[](const curriculum::Lesson&){
            curriculum::Lesson model;model.id="native_cpp_laboratory";model.title="Prototipo del motor C++";model.locale="es";
            curriculum::Block compiler;compiler.type=BlockType::cpp_compiler;compiler.stable_id="native_lab_editor";compiler.initial_code="#include <iostream>\n\nusing namespace std;\n\nint main()\n{\n    \n}";
            curriculum::Page page;page.stable_id="native_lab";page.blocks.push_back(std::move(compiler));model.pages.push_back(std::move(page));return model;
        });
        const auto pick=[locale](const char* es,const char* en,const char* fr){return std::string(std::string_view(locale)=="fr"?fr:std::string_view(locale)=="en"?en:es);};
        ShowWindow(fixture.host,SW_SHOWNOACTIVATE);pump(fixture.host);
        require(window_text(fixture.control(990))==pick("Prototipo del motor C++","C++ Engine Prototype","Prototype du moteur C++")&&character_format(fixture.control(990)).yHeight==420,"The native laboratory lost Android's original localized28sp title");
        require(window_text(fixture.control(991)).find("MSVC")!=std::string::npos&&window_text(fixture.control(991)).find("x64")!=std::string::npos&&character_format(fixture.control(991)).yHeight==270,"The native laboratory description is absent, untruthful or has the wrong18sp size");
        for(const auto control:{106,111,112,113,107})require(!(GetWindowLongPtrW(fixture.control(control),GWL_STYLE)&WS_VISIBLE),"A synthetic lesson control leaked into the standalone C++ laboratory");
        for(const auto part:{4,15,17,19,23,24,25,30,31})require(!descendant(fixture.view,id(0,part)),"The standalone laboratory gained a lesson feedback/input/console toolbar not present in Android");
        require(window_text(fixture.control(0,34))==pick("Listo para ejecutar.","Ready to Run.","Prêt pour l’exécution.")&&character_format(fixture.control(0,34)).yHeight==240,"The native laboratory lost its localized16sp status line");
        require(reader_theme::palette().high_contrast||reader_theme::background(fixture.control(0,14))==RGB(5,6,7),"The standalone output no longer has Android's black console surface");
        const auto output_ink=character_format(fixture.control(0,14));require(reader_theme::palette().high_contrast||output_ink.crTextColor==RGB(121,226,157),"The standalone output lost Android's green ink");
        RECT editor_bounds{};GetWindowRect(fixture.control(0,10),&editor_bounds);require(editor_bounds.bottom-editor_bounds.top>=MulDiv(200,static_cast<int>(GetDpiForWindow(fixture.view)),96),"The standalone editor omitted its original200dp minimum");
        LOGFONTW compact{};require(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(fixture.control(0,36),WM_GETFONT,0,0)),sizeof(compact),&compact)&&compact.lfHeight==-MulDiv(13,static_cast<int>(GetDpiForWindow(fixture.view)),96),"The compact compiler selector did not preserve Android's13sp label");
        const auto original=fixture.text(0,10);const auto editor=fixture.control(0,10);const auto initial_cursor=static_cast<LONG>(wide(original.substr(0,original.find("\n    \n")+5)).size());
        CHARRANGE initial_selection{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&initial_selection));require(initial_selection.cpMin==initial_cursor&&initial_selection.cpMax==initial_cursor,"A new laboratory draft did not start inside main after its predefined indentation");
        fixture.click(0,35);
        require(fixture.control(0,10)==editor&&fixture.text(0,10)==original&&!(GetWindowLongPtrW(editor,GWL_STYLE)&ES_AUTOHSCROLL),"The direct wrap icon recreated or changed the source instead of wrapping it");
        {PopupCapture capture(fixture.view);fixture.click(0,36);capture.verified();require(capture.captions==std::vector<std::string>{"MSVC C++20 · x64"},"The laboratory compiler control advertised an unavailable backend");}
        fixture.click(0,20);require(fixture.fullscreen(),"The standalone editor did not enter real fullscreen");SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);pump(fixture.host);
        require(!fixture.fullscreen()&&fixture.text(0,10)==original,"Escape from laboratory fullscreen changed the draft or retained an exit overlay");
        CHARRANGE personal_selection{1,3};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&personal_selection));fixture.reopen();
        CHARRANGE restored_selection{};SendMessageW(fixture.control(0,10),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored_selection));
        require(fixture.text(0,10)==original&&!(GetWindowLongPtrW(fixture.control(0,10),GWL_STYLE)&ES_AUTOHSCROLL)&&restored_selection.cpMin==1&&restored_selection.cpMax==3,"Reopening lost the laboratory draft, wrapping preference or restored its cursor to the default");
        fixture.click(0,12);SendMessageW(fixture.control(0,10),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored_selection));
        require(restored_selection.cpMin==initial_cursor&&restored_selection.cpMax==initial_cursor&&(GetWindowLongPtrW(fixture.control(0,10),GWL_STYLE)&ES_AUTOHSCROLL),"An explicit laboratory Reset did not restore the original main caret and wrapping mode");
        if(std::string_view(locale)=="es"){
            fixture.replace(0,10,"#include <iostream>\nint main(){std::cout << \"Salida real\";}\n");fixture.click(0,11);
            wait_for(fixture.host,[&]{return IsWindowEnabled(fixture.control(0,11));},"The standalone laboratory did not finish its real native compile/run");
            require(fixture.text(0,14).find("Salida real")!=std::string::npos&&fixture.text(0,34)=="Ejecución terminada (código 0).","The standalone laboratory did not display its actual native output and status");
            require(!state::find(fixture.saved(),"attempt_reserve_seconds_test","reserve_programming"),"The standalone laboratory created or consumed exercise hearts");
        }
        fixture.non_overlapping_rows();
    }
    std::cout<<"PASS standalone Android-parity laboratory: source title/sizing/status/surfaces, no lesson controls, actual MSVC backend, direct wrap, fullscreen/Escape, draft retention and real x64 output\n";
}
void back_navigation_tests(){
    int returned=0;Fixture fixture({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"({"id":"qa_back","title":"Regreso","pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main() {\n    return 0;\n}\n"}]}]})");});
    LessonViewHooks hooks;hooks.back_to_catalog=[&](HWND){++returned;};set_lesson_view_hooks(fixture.view,hooks);
    ShowWindow(fixture.host,SW_SHOWNOACTIVATE);fixture.replace(0,10,fixture.text(0,10)+"// borrador conservado\n");const auto editor=fixture.control(0,10);const auto draft=fixture.text(0,10);
    CHARRANGE selection{4,8};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));fixture.click(0,20);
    SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);pump(fixture.host);CHARRANGE restored{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored));
    require(returned==0&&!fixture.fullscreen()&&fixture.control(0,10)==editor&&fixture.text(0,10)==draft&&restored.cpMin==selection.cpMin&&restored.cpMax==selection.cpMax,"Escape left the lesson before closing fullscreen or changed the editor draft/selection");
    require(navigate_lesson_back(fixture.view),"Normal Back did not use the existing catalog callback");pump(fixture.host);require(returned==1&&fixture.page()==0&&fixture.text(0,10)==draft,"Normal Back changed page or the editor instead of its existing destination");
    SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);pump(fixture.host);require(returned==2,"Escape inside the real C++ editor did not reuse normal Back");
    {
        Fixture imported;LessonViewHooks events;events.back_to_catalog=[&](HWND){++returned;};set_lesson_view_hooks(imported.view,events);const auto calls=returned;
        SendMessageW(imported.view,WM_COMMAND,MAKEWPARAM(104,BN_CLICKED),0);pump(imported.host);require(descendant(imported.view,201),"The actual import surface did not open on its isolated empty profile");
        require(navigate_lesson_back(imported.view),"Back did not consume the open import surface");pump(imported.host);
        require(!descendant(imported.view,201)&&returned==calls&&imported.page()==0,"Back left the lesson instead of closing import without changing page");
    }
    {
        Fixture mathematical({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"({"id":"qa_back_math","title":"Regreso matemático","pages":[{"blocks":[{"type":"math_puzzle","prompt":"Escribe un número.","responses":[{"id":"value","type":"math_numeric_input"}]}]}]})");});
        LessonViewHooks events;events.back_to_catalog=[&](HWND){++returned;};set_lesson_view_hooks(mathematical.view,events);ShowWindow(mathematical.host,SW_SHOWNOACTIVATE);
        HWND field=nullptr;EnumChildWindows(mathematical.view,[](HWND candidate,LPARAM output)->BOOL{if(!math_field_keyboard_context(candidate).empty()){*reinterpret_cast<HWND*>(output)=candidate;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&field));require(field,"A real mathematical answer field is missing");
        static windows::NarrationWorker worker(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
        SetFocus(field);show_math_keyboard(field,[](std::string_view request){return worker.math_keyboard(request);},math_field_keyboard_context(field),"es");const auto keyboard=math_keyboard_window(GetAncestor(mathematical.view,GA_ROOT));require(keyboard&&IsWindowVisible(keyboard),"The original mathematical keyboard did not open through its real canonical resolver");
        const auto calls=returned;const auto text=edit_text(field);require(navigate_lesson_back(mathematical.view),"Back did not consume the mathematical keyboard");
        require(!IsWindowVisible(keyboard)&&returned==calls&&GetFocus()==field&&edit_text(field)==text&&mathematical.page()==0,"Back from the mathematical keyboard changed focus, answer, page or left the lesson");
        SendMessageW(mathematical.view,WM_KEYDOWN,VK_ESCAPE,0);pump(mathematical.host);require(returned==calls+1,"A second Escape did not return through the original catalog callback");
    }
    std::cout<<"PASS shared Back/Escape: import/fullscreen/math-keyboard priority, real canonical keyboard, retained editor/draft/selection and existing catalog destination\n";
}
void long_prediction_cards_keep_independent_bounds(){
    struct DpiScope{DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}} dpi_scope;
    Fixture fixture({},[](const curriculum::Lesson&){
        auto model=windows::load_lesson(std::filesystem::path(build::source_root)/
            "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_construir_un_programa_de_carga_y_guardado.json");
        model.pages={model.pages.at(9)};model.state_layout={};model.reference_after.clear();model.activity_after.clear();return model;
    });
    for(const auto index:{1u,2u}){
        const auto source=fixture.control(index,1),answer=fixture.control(index,2);RECT source_bounds{},answer_bounds{},viewport{};
        GetWindowRect(source,&source_bounds);GetWindowRect(answer,&answer_bounds);GetWindowRect(GetParent(source),&viewport);
        require(source_bounds.bottom<=answer_bounds.top,"Published page10 prediction "+std::to_string(index)+
            " overlaps its source: source_y="+std::to_string(source_bounds.top)+","+std::to_string(source_bounds.bottom)+
            "; answer_y="+std::to_string(answer_bounds.top)+","+std::to_string(answer_bounds.bottom)+"; viewport_y="+std::to_string(viewport.top));
    }
    const auto source=fixture.control(2,1),answer=fixture.control(2,2),viewport=GetParent(source);
    const auto original=fixture.text(2,1);const auto script=reading_script(fixture.view);fixture.replace(2,2,"Borrador conservado");
    SendMessageW(fixture.view,WM_VSCROLL,SB_BOTTOM,0);pump(fixture.host);
    SetFocus(answer);SendMessageW(answer,EM_SETSEL,4,4);pump(fixture.host);
    SCROLLINFO before{sizeof(before),SIF_POS};GetScrollInfo(viewport,SB_VERT,&before);
    require(before.nPos>0&&fixture.control(2,1)==source&&fixture.control(2,2)==answer&&GetFocus()==answer&&
        fixture.text(2,1)==original&&fixture.text(2,2)=="Borrador conservado"&&reading_script(fixture.view)==script,
        "Scrolling the rebased long page changed a source/answer HWND, focus, draft or narration script");
    fixture.click(2,13);fixture.escape();SCROLLINFO after{sizeof(after),SIF_POS};GetScrollInfo(viewport,SB_VERT,&after);
    require(after.nPos==before.nPos&&fixture.control(2,1)==source&&fixture.control(2,2)==answer&&
        fixture.text(2,1)==original&&fixture.text(2,2)=="Borrador conservado"&&reading_script(fixture.view)==script,
        "Fullscreen and return from a rebased long page lost its original offset, editor identity or draft");
    std::cout<<"PASS actual page10 with two 330-line readonly sources and independent editable prediction cards\n";
}
void long_lesson_bands_keep_independent_hit_targets(){
    struct DpiScope{DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}} dpi_scope;
    std::vector<std::size_t> answers;
    Fixture fixture({},[&](const curriculum::Lesson&){
        curriculum::Lesson lesson;lesson.id="qa_multiple_long_lesson_bands";lesson.title="Bandas independientes";lesson.locale="es";
        curriculum::Page page;page.stable_id="long_page";
        std::string paragraph;for(int line=0;line<100;++line)paragraph+="Contenido de la lección conservado.\n";
        for(int group=0;group<2;++group){
            for(int count=0;count<12;++count){curriculum::Block text;text.type=BlockType::paragraph;text.text=paragraph;
                text.stable_id="paragraph_"+std::to_string(page.blocks.size());page.blocks.push_back(std::move(text));}
            answers.push_back(page.blocks.size());curriculum::Block answer;answer.type=BlockType::output_prediction;
            answer.stable_id="answer_"+std::to_string(group);answer.question="Escribe el mensaje.";answer.code="std::cout << \"Listo\";";answer.answers={"Listo"};page.blocks.push_back(std::move(answer));
        }
        lesson.pages.push_back(std::move(page));return lesson;
    });
    const auto first=fixture.control(answers[0],2),second=fixture.control(answers[1],2);
    const auto pane=descendant_class(fixture.view,L"LearnGameAndEngineDev.LessonPane");
    require(GetParent(first)!=pane&&GetParent(second)!=pane&&GetParent(first)!=GetParent(second),
        "The long-page regression did not reach two independent overflow bands");
    const auto script=reading_script(fixture.view);
    bool crossing_checked=false;
    for(std::size_t index=1;index<fixture.lesson.pages[0].blocks.size();++index){
        if(fixture.lesson.pages[0].blocks[index-1].type!=curriculum::BlockType::paragraph||fixture.lesson.pages[0].blocks[index].type!=curriculum::BlockType::paragraph)continue;
        const auto earlier=fixture.control(index-1,0),later=fixture.control(index,0);
        const auto earlier_host=GetParent(earlier),later_host=GetParent(later);
        if(earlier_host==pane||later_host==pane||earlier_host==later_host)continue;
        // Center the next row's start using the actual scrolling path. Both
        // its leading lines and the prior row's tail must now be on screen.
        for(int move=0;move<20;++move){RECT target{},viewport{};GetWindowRect(later,&target);GetWindowRect(pane,&viewport);
            const auto distance=target.top-(viewport.top+viewport.bottom)/2;
            const auto unit=MulDiv(100,static_cast<int>(GetDpiForWindow(pane)),96);
            if(std::abs(distance)<unit)break;
            const auto steps=std::clamp(distance/unit,-273L,273L);
            SendMessageW(fixture.view,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-steps*WHEEL_DELTA)),0);pump(fixture.host);
        }
        RECT prior{},next{},viewport{},prior_host{},next_host{};GetWindowRect(earlier,&prior);GetWindowRect(later,&next);GetWindowRect(pane,&viewport);
        SCROLLINFO position{sizeof(position),SIF_POS};GetScrollInfo(pane,SB_VERT,&position);
        const auto next_start=next.top-viewport.top+position.nPos,boundary=(next_start/30000)*30000;
        if(prior.top-viewport.top+position.nPos>=boundary||prior.bottom-viewport.top+position.nPos<=boundary)continue;
        GetWindowRect(earlier_host,&prior_host);GetWindowRect(later_host,&next_host);RECT overlap{};
        require(IsWindowVisible(earlier_host)&&IsWindowVisible(later_host)&&!IntersectRect(&overlap,&prior_host,&next_host),
            "A new opaque band covers the earlier band's real text crossing its nominal boundary");
        for(const auto& entry:{std::pair{earlier,POINT{prior.left+5,prior.bottom-5}},std::pair{later,POINT{next.left+5,next.top+5}}}){
            auto point=entry.second;require(PtInRect(&viewport,point),"The crossing-band regression did not reach both real visible text rows");ScreenToClient(pane,&point);
            require(ChildWindowFromPointEx(pane,point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT)==GetParent(entry.first),
                "A crossing row or its successor lost its real mouse hit target to another opaque band");
        }
        require(reading_script(fixture.view)==script,"Testing a crossing band changed its original narration source");
        crossing_checked=true;break;
    }
    require(crossing_checked,"The long-page fixture did not exercise a real row crossing between two overflow hosts");
    const auto reveal=[&](HWND field){
        for(int move=0;move<20;++move){RECT target{},viewport{};GetWindowRect(field,&target);GetWindowRect(pane,&viewport);
            if(target.top>=viewport.top&&target.bottom<=viewport.bottom)break;
            const auto unit=MulDiv(100,static_cast<int>(GetDpiForWindow(pane)),96);
            const auto distance=target.top-(viewport.top+viewport.bottom)/2;
            const auto steps=std::clamp(distance/unit,-273L,273L);
            const auto notches=steps?steps:(distance<0?-1L:1L);
            SendMessageW(fixture.view,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-notches*WHEEL_DELTA)),0);pump(fixture.host);
        }
    };
    for(const auto field:{first,second}){
        reveal(field);
        RECT target{},viewport{};GetWindowRect(field,&target);GetWindowRect(pane,&viewport);
        require(target.top>=viewport.top&&target.bottom<=viewport.bottom,"A rebased answer could not be reached by native scrolling");
        POINT point{(target.left+target.right)/2,(target.top+target.bottom)/2};ScreenToClient(pane,&point);
        const auto hit=ChildWindowFromPointEx(pane,point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT);
        require(hit==GetParent(field),"An earlier long-page band covers the later answer's real mouse hit target");
        require(IsWindowVisible(field)&&IsWindowEnabled(field)&&reading_script(fixture.view)==script,
            "Scrolling between overflow bands changed answer availability or the narration script");
        const auto index=static_cast<std::size_t>((GetDlgCtrlID(field)-1000)/100);
        const auto draft="Borrador "+std::to_string(index);fixture.replace(index,2,draft);
        SetFocus(field);SendMessageW(field,EM_SETSEL,1,4);pump(fixture.host);
        SCROLLINFO before{sizeof(before),SIF_POS};GetScrollInfo(pane,SB_VERT,&before);
        fixture.click(index,13);
        require(GetFocus()==field&&fixture.control(index,2)==field&&edit_text(field)==draft,
            "Fullscreen from a rebased band lost its original editor focus, answer HWND or draft");
        PostMessageW(GetFocus(),WM_KEYDOWN,VK_ESCAPE,1);pump(fixture.host);
        SCROLLINFO after{sizeof(after),SIF_POS};GetScrollInfo(pane,SB_VERT,&after);
        CHARRANGE selection{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        require(!fixture.fullscreen()&&after.nPos==before.nPos&&GetFocus()==field&&
            selection.cpMin==1&&selection.cpMax==4&&edit_text(field)==draft&&reading_script(fixture.view)==script,
            "Escape from a rebased band changed its lesson offset, focused editor, selection, draft or narration source");
    }
    reveal(first);SetFocus(first);pump(fixture.host);CHARRANGE first_selection{};SendMessageW(first,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&first_selection));
    require(GetFocus()==first&&first_selection.cpMin==1&&first_selection.cpMax==4&&edit_text(first)=="Borrador "+std::to_string(answers[0]),
        "Returning to an earlier band lost its original editable draft, selection or native focus");
    require(fixture.control(answers[0],2)==first&&fixture.control(answers[1],2)==second,
        "Scrolling between long-page bands recreated their original answer fields");
    std::cout<<"PASS multiple long-page bands: bounded opaque hosts, native scroll hit targets, same HWNDs and narration source\n";
}
void initial_readonly_source_layout(){
    struct DpiScope{DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}} dpi_scope;
    const auto resize=[&](Fixture& fixture,int width){
        const auto dpi=static_cast<int>(GetDpiForWindow(fixture.view));RECT outer{0,0,MulDiv(width,dpi,96),MulDiv(750,dpi,96)};
        AdjustWindowRectExForDpi(&outer,static_cast<DWORD>(GetWindowLongPtrW(fixture.host,GWL_STYLE)),FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(fixture.host,GWL_EXSTYLE)),dpi);
        SetWindowPos(fixture.host,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        MoveWindow(fixture.view,0,0,MulDiv(width,dpi,96),MulDiv(750,dpi,96),TRUE);pump(fixture.host);
    };
    Fixture fixture;resize(fixture,900);
    const auto source=fixture.control(2,1),caption=fixture.control(2,16);
    const auto visible_bar=[](HWND control,int axis){SCROLLBARINFO info{sizeof(info)};
        return GetScrollBarInfo(control,axis==SB_HORZ?OBJID_HSCROLL:OBJID_VSCROLL,&info)&&
            !(info.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN));
    };
    const auto original=edit_text(source);RECT client{},format{};POINTL first{};POINT offset{};GetClientRect(caption,&client);
    const auto source_extents=[&]{
        const auto raw=wide(edit_text(source));POINT scroll{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        POINTL maximum{};
        for(std::size_t i=0;i<=raw.size();++i)if(i==raw.size()||raw[i]==L'\n'){
            POINTL end{};SendMessageW(source,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),static_cast<LPARAM>(i));
            maximum.x=std::max(maximum.x,end.x+scroll.x);maximum.y=std::max(maximum.y,end.y+scroll.y);
        }
        return maximum;
    };
    const auto describe=[&](const char* message){
        RECT source_client{},source_format{},source_screen{},pane_client{};POINT scroll{};int numerator=0,denominator=0;SCROLLINFO h{sizeof(h),SIF_ALL},v{sizeof(v),SIF_ALL};
        GetClientRect(source,&source_client);SendMessageW(source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&source_format));
        GetWindowRect(source,&source_screen);MapWindowPoints(nullptr,GetParent(source),reinterpret_cast<POINT*>(&source_screen),2);GetClientRect(GetParent(source),&pane_client);
        SendMessageW(source,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));GetScrollInfo(source,SB_HORZ,&h);GetScrollInfo(source,SB_VERT,&v);
        const auto maximum=source_extents();
        return std::string(message)+"; dpi="+std::to_string(GetDpiForWindow(source))+" client="+std::to_string(source_client.right)+","+std::to_string(source_client.bottom)+
            " format="+std::to_string(source_format.left)+","+std::to_string(source_format.top)+","+std::to_string(source_format.right)+","+std::to_string(source_format.bottom)+
            " maxLineEnd="+std::to_string(maximum.x)+","+std::to_string(maximum.y)+" zoom="+std::to_string(numerator)+"/"+std::to_string(denominator)+
            " position="+std::to_string(source_screen.left)+","+std::to_string(source_screen.top)+" pane="+std::to_string(pane_client.right)+","+std::to_string(pane_client.bottom)+
            " h="+std::to_string(h.nMin)+","+std::to_string(h.nMax)+","+std::to_string(h.nPage)+
            " v="+std::to_string(v.nMin)+","+std::to_string(v.nMax)+","+std::to_string(v.nPage)+" bars="+std::to_string(visible_bar(source,SB_HORZ))+","+std::to_string(visible_bar(source,SB_VERT));
    };
    SendMessageW(caption,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));SendMessageW(caption,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
    SendMessageW(caption,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));const auto font=character_format(caption);
    require(first.y>=format.top&&first.y+MulDiv(font.yHeight,static_cast<int>(GetDpiForWindow(caption)),1440)<=client.bottom&&
        offset.y==0&&SendMessageW(caption,EM_GETLINECOUNT,0,0)==1,
        "The first-open production code caption is clipped, internally scrolled or split across rows");
    RECT source_format{};SendMessageW(source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&source_format));
    const auto initial_extents=source_extents();
    const auto source_font=character_format(source);
    const auto line_height=MulDiv(source_font.yHeight,static_cast<int>(GetDpiForWindow(source)),1440);
    require(initial_extents.x<=source_format.right&&initial_extents.y+line_height<=source_format.bottom,
        describe("The DPI-aware fit fixture does not actually fit the original source inside its native format rectangle"));
    require(!visible_bar(source,SB_HORZ)&&!visible_bar(source,SB_VERT),
        describe("The production readonly source shows a scrollbar even though its seven lines fit the actual viewport"));
    int original_numerator=0,original_denominator=0;
    SendMessageW(source,EM_GETZOOM,reinterpret_cast<WPARAM>(&original_numerator),reinterpret_cast<LPARAM>(&original_denominator));
    CHARRANGE selection{8,15};SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    resize(fixture,280);
    SendMessageW(source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&source_format));
    require(source_extents().x>source_format.right,
        describe("The narrowed readonly fixture does not actually overflow its native format rectangle"));
    SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE};GetScrollInfo(source,SB_HORZ,&horizontal);
    require(visible_bar(source,SB_HORZ)&&horizontal.nPage>0&&horizontal.nMax-horizontal.nMin+1>static_cast<int>(horizontal.nPage),
        describe("Narrowing the same readonly source failed to expose its genuine native horizontal overflow"));
    require(!visible_bar(source,SB_VERT)&&source_extents().y+line_height<=source_format.bottom,
        describe("The bottomless readonly source clips its final line or captures vertical lesson scrolling"));
    POINT wanted{60,0};SendMessageW(source,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&wanted));SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
    require(offset.x>0&&edit_text(source)==original,"Readonly horizontal overflow has no real native scroll or changed the code");
    fixture.click(2,35);require(!visible_bar(source,SB_HORZ)&&edit_text(source)==original,
        "Wrap retained an unnecessary readonly horizontal bar or changed the source");
    SendMessageW(source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&source_format));
    require(!visible_bar(source,SB_VERT)&&source_extents().y+line_height<=source_format.bottom,
        describe("Wrapped readonly source no longer grows to its actual document height"));
    fixture.click(2,35);resize(fixture,900);
    require(!visible_bar(source,SB_HORZ),"Widening the same readonly source retained an obsolete overflow scrollbar");
    fixture.click(2,20);require(!visible_bar(source,SB_HORZ)&&!visible_bar(source,SB_VERT),
        "Readonly fullscreen shows bars when the real seven-line source fits");fixture.escape();
    CHARRANGE preserved{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&preserved));
    int final_numerator=0,final_denominator=0;
    SendMessageW(source,EM_GETZOOM,reinterpret_cast<WPARAM>(&final_numerator),reinterpret_cast<LPARAM>(&final_denominator));
    require(preserved.cpMin==selection.cpMin&&preserved.cpMax==selection.cpMax&&
        original_numerator*final_denominator==final_numerator*original_denominator&&source_extents().x==initial_extents.x,
        "Readonly viewport refresh changed its selection, effective zoom or original glyph positions");
    Fixture longer({},[](const curriculum::Lesson& original_lesson){auto lesson=original_lesson;
        for(unsigned i=0;i<65;++i)lesson.pages[0].blocks[2].code+="// línea adicional\n";return lesson;});
    resize(longer,900);longer.click(2,20);const auto long_source=longer.control(2,1);
    SCROLLINFO vertical{sizeof(vertical),SIF_RANGE|SIF_PAGE};GetScrollInfo(long_source,SB_VERT,&vertical);
    RECT long_client{},long_viewport{};GetClientRect(long_source,&long_client);GetClientRect(GetParent(long_source),&long_viewport);
    require(long_client.bottom<long_viewport.bottom,"Fullscreen readonly kept its provisional document height instead of the actual bounded viewport");
    const bool long_scrollable=visible_bar(long_source,SB_VERT)&&vertical.nPage>0&&vertical.nMax-vertical.nMin+1>static_cast<int>(vertical.nPage);
    if(!long_scrollable){RECT diagnostic_client{},diagnostic_format{};GetClientRect(long_source,&diagnostic_client);SendMessageW(long_source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&diagnostic_format));
        POINTL last{};const auto last_line=SendMessageW(long_source,EM_GETLINECOUNT,0,0)-1;const auto at=SendMessageW(long_source,EM_LINEINDEX,last_line,0);SendMessageW(long_source,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&last),at);
        std::cerr<<"LONG SOURCE actual: client="<<diagnostic_client.right<<','<<diagnostic_client.bottom<<" format="<<diagnostic_format.left<<','<<diagnostic_format.top<<','<<diagnostic_format.right<<','<<diagnostic_format.bottom<<" last="<<last.x<<','<<last.y
            <<" range="<<vertical.nMin<<','<<vertical.nMax<<','<<vertical.nPage<<" style="<<GetWindowLongPtrW(long_source,GWL_STYLE)<<" visible="<<visible_bar(long_source,SB_VERT)<<'\n';
    }
    require(long_scrollable,"Long readonly fullscreen lost its genuine native vertical overflow scrollbar");
    SendMessageW(long_source,EM_LINESCROLL,0,6);SendMessageW(long_source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
    require(offset.y>0,"Long readonly fullscreen no longer scrolls its actual source");longer.escape();
    std::cout<<"PASS production readonly source first-open caption and DPI-aware native bars: fit, genuine horizontal/vertical overflow, resize, wrap and fullscreen\n";
}
void mathematical_feedback_paragraph_spacing() {
    windows::NarrationWorker backend(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
    for(const auto locale:{"es","en","fr"}) {
        Fixture fixture([&](state::StateData& state,const curriculum::Lesson&){
            state.preferences["native_windows"]["ui_language"]=std::string(locale);
        },[&](const curriculum::Lesson&){
            auto lesson=windows::parse_lesson_json(R"({"id":"qa_math_feedback_spacing","title":"Feedback","pages":[{"blocks":[{"type":"math_puzzle","schemaVersion":1,"prompt":"Completa la medida.","responses":[{"id":"number","type":"math_numeric_input","label":"Medida","validator":{"type":"numeric","expected":6}}],"feedback":{"correct":"6 × 100 = 600. Explicación intacta.","incorrect":"Revisa la medida."}},{"type":"paragraph","text":"Texto posterior intacto."}]}]})");
            lesson.locale=locale;return lesson;
        });
        std::string canonical_feedback;
        bool completed=false;
        LessonViewHooks hooks;
        hooks.validate_math=[&](std::string_view block,std::string_view answers,std::string_view language,std::stop_token stop){
            auto result=backend.validate_math(block,answers,language,stop);
            canonical_feedback=winrt::to_string(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(result)).GetNamedString(L"feedback"));
            return result;
        };
        hooks.feedback_changed=[&](HWND,std::size_t,bool){completed=true;};
        set_lesson_view_hooks(fixture.view,std::move(hooks));
        HWND field=nullptr;EnumChildWindows(fixture.view,[](HWND candidate,LPARAM output)->BOOL{
            if(!math_field_keyboard_context(candidate).empty()){*reinterpret_cast<HWND*>(output)=candidate;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&field));
        require(field,"The real mathematical answer field is missing from feedback spacing regression");
        RECT initial_host{},initial_check{},following{};GetWindowRect(fixture.control(0,5),&initial_host);
        GetWindowRect(fixture.control(0,3),&initial_check);GetWindowRect(fixture.control(1,0),&following);
        require(initial_check.top==initial_host.bottom&&following.top-initial_check.bottom==MulDiv(36,static_cast<int>(GetDpiForWindow(fixture.view)),96),
            "An unanswered math card omitted its 12dp bottom inset plus Android's separate 24dp bottomMargin, or added a gap before Check");
        const auto original_json=fixture.lesson.raw_json;
        for(const auto answer:{L"5",L"6"}) {
            CHARRANGE all{0,-1};SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
            SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(answer));pump(fixture.host);
            completed=false;
            const bool checked_from_editor=std::wstring_view(answer)==L"6";
            if(checked_from_editor){
                const auto math=fixture.control(0,5);HWND expand=nullptr;
                const auto name=std::string(locale)=="es"?"Pantalla completa":std::string(locale)=="fr"?"Plein écran":"Full screen";
                for(auto child=GetWindow(math,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(accessible_name(child)==name){expand=child;break;}
                require(expand,"The real mathematical fullscreen button is missing before physical Enter");
                SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);pump(fixture.host);CHARRANGE selection_before{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection_before));
                SendMessageW(expand,BM_CLICK,0,0);pump(fixture.host);CHARRANGE selection_after{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection_after));
                require(GetFocus()==field&&selection_before.cpMin==selection_after.cpMin&&selection_before.cpMax==selection_after.cpMax,
                    "The mathematical fullscreen icon lost its existing focused answer or selection");
                MSG key{field,WM_KEYDOWN,VK_RETURN,1};
                require(dispatch_lesson_enter(key),"Physical Enter did not check the focused mathematical answer");
                key={field,WM_KEYUP,VK_RETURN,LPARAM{1}<<31};
                require(dispatch_lesson_enter(key),"The mathematical Enter release escaped its original submission");
                pump(fixture.host);
            }else fixture.click(0,3);
            wait_for(fixture.host,[&]{return completed;},
                "Canonical mathematical feedback did not finish its real validation");
            if(checked_from_editor){
                const auto focused=GetFocus();
                require(focused&&(focused==fixture.view||IsChild(fixture.view,focused))&&IsWindowEnabled(focused)&&!IsWindowEnabled(field),
                    "A correct mathematical answer disabled the focused field without retaining a live lesson keyboard anchor");
                MSG escape{focused,WM_KEYDOWN,VK_ESCAPE,1};TranslateMessage(&escape);DispatchMessageW(&escape);pump(fixture.host);
                require(!fixture.fullscreen()&&fixture.page()==0,
                    "Physical Escape after a correct fullscreen mathematical answer did not leave fullscreen on the same page");
            }
            const auto target=fixture.control(0,4);
            require(fixture.text(0,4)==canonical_feedback&&canonical_feedback.find('\n')!=std::string::npos,
                "Math verdict spacing changed the canonical feedback characters");
            const auto regions=lesson_reading_regions(fixture.view);
            require(std::ranges::any_of(regions,[&](const auto& region){return region.feedback&&region.target==target&&region.source_text==canonical_feedback;}),
                "Math verdict spacing changed the original narration source or offsets");
            CHARRANGE previous{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
            CHARRANGE heading{0,0};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&heading));
            PARAFORMAT2 format{};format.cbSize=sizeof(format);SendMessageW(target,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&format));
            require(format.dySpaceAfter==MulDiv(rich_text_height(),25,20),
                "Math verdict and explanation are not separated by one presentation-only blank line");
            CHARRANGE body{static_cast<LONG>(wide(canonical_feedback.substr(0,canonical_feedback.find('\n'))).size()+1),-1};
            SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&body));
            format={};format.cbSize=sizeof(format);SendMessageW(target,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&format));
            require(format.dySpaceAfter==0,"Math verdict spacing leaked into the authored explanation paragraphs");
            SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));
            const auto math_host=fixture.control(0,5),pane=GetParent(math_host);
            RECT host_bounds{},check_bounds{},feedback_bounds{},canvas{};GetWindowRect(math_host,&host_bounds);
            GetWindowRect(fixture.control(0,3),&check_bounds);GetWindowRect(target,&feedback_bounds);
            for(const auto rect:{&host_bounds,&check_bounds,&feedback_bounds})MapWindowPoints(nullptr,pane,reinterpret_cast<POINT*>(rect),2);
            const auto inset=MulDiv(6,static_cast<int>(GetDpiForWindow(pane)),96);
            require(check_bounds.left==host_bounds.left+inset&&feedback_bounds.left==host_bounds.left+inset&&
                check_bounds.right==host_bounds.right-inset&&feedback_bounds.right==host_bounds.right-inset,
                "The mathematical group contour clips its check/feedback instead of retaining Android's internal padding");
            require(check_bounds.top==host_bounds.bottom,"The mathematical host added an extra gap before its external Check button");
            GetClientRect(pane,&canvas);const auto screen=GetDC(pane),memory=CreateCompatibleDC(screen);
            const auto bitmap=CreateCompatibleBitmap(screen,canvas.right,canvas.bottom);ReleaseDC(pane,screen);
            require(memory&&bitmap,"Cannot create the mathematical group's actual native drawing surface");const auto old=SelectObject(memory,bitmap);
            SendMessageW(pane,WM_ERASEBKGND,reinterpret_cast<WPARAM>(memory),0);
            const auto outline=reader_theme::palette(pane).outline;
            const auto check_pixel=GetPixel(memory,host_bounds.left,(check_bounds.top+check_bounds.bottom)/2);
            const auto left_pixel=GetPixel(memory,host_bounds.left,(feedback_bounds.top+feedback_bounds.bottom)/2);
            const auto right_pixel=GetPixel(memory,host_bounds.right-1,(feedback_bounds.top+feedback_bounds.bottom)/2);
            const bool whole_group=check_pixel==outline&&left_pixel==outline&&right_pixel==outline;
            SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
            const auto rectangle=[](RECT value){return std::to_string(value.left)+","+std::to_string(value.top)+","+std::to_string(value.right)+","+std::to_string(value.bottom);};
            require(whole_group,"The actual mathematical outline does not enclose both Check and the full feedback; host="+rectangle(host_bounds)+
                " check="+rectangle(check_bounds)+" feedback="+rectangle(feedback_bounds)+" canvas="+rectangle(canvas)+
                " pixels="+std::to_string(check_pixel)+","+std::to_string(left_pixel)+","+std::to_string(right_pixel)+" outline="+std::to_string(outline));
            HWND expand=nullptr;const auto expand_name=std::string(locale)=="es"?"Pantalla completa":std::string(locale)=="fr"?"Plein écran":"Full screen";
            for(auto button=GetWindow(math_host,GW_CHILD);button;button=GetWindow(button,GW_HWNDNEXT))if(accessible_name(button)==expand_name){expand=button;break;}
            require(expand,"The original mathematical fullscreen button is missing");
            const auto check=fixture.control(0,3);const auto script=reading_script(fixture.view);
            SendMessageW(expand,BM_CLICK,0,0);pump(fixture.host);
            require(IsWindowVisible(check)&&IsWindowVisible(target)&&fixture.control(0,5)==math_host&&fixture.control(0,3)==check&&fixture.control(0,4)==target&&
                fixture.text(0,4)==canonical_feedback&&reading_script(fixture.view)==script,
                "Mathematical fullscreen omitted its original Check/feedback or recreated the puzzle/narration source");
            fixture.escape();require(fixture.control(0,5)==math_host&&fixture.control(0,3)==check&&fixture.control(0,4)==target&&fixture.text(0,4)==canonical_feedback,
                "Returning from mathematical fullscreen changed its original answer or verdict controls");
            require(fixture.lesson.raw_json==original_json,"Math verdict spacing changed the curriculum");
        }
    }
    std::cout<<"PASS ES/EN/FR real mathematical verdicts: presentation-only blank line, unchanged canonical text, narration source and expressions\n";
}
void native_page_progress_theme() {
    Fixture fixture;const auto progress=fixture.control(106);
    wchar_t class_name[64]{};GetClassNameW(progress,class_name,64);
    require(std::wstring_view(class_name)==PROGRESS_CLASSW,"The lesson progress ceased to be the real native progress control");
    PBRANGE original{};SendMessageW(progress,PBM_GETRANGE,FALSE,reinterpret_cast<LPARAM>(&original));
    const auto original_position=SendMessageW(progress,PBM_GETPOS,0,0);
    require(original.iLow==0&&original.iHigh==static_cast<int>(fixture.lesson.pages.size())&&original_position==1,
        "The original native progress range/position differ from the lesson");
    RECT bounds{};GetClientRect(progress,&bounds);
    require(bounds.right>=8&&bounds.bottom>0,"The real lesson progress has no drawable client area");
    const auto screen=GetDC(progress),memory=CreateCompatibleDC(screen);
    const auto bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);ReleaseDC(progress,screen);
    require(memory&&bitmap,"Create the real progress-control GDI regression surface");
    const auto previous=SelectObject(memory,bitmap);
    const auto assert_paint=[&](int expected_position,int numerator,int denominator,COLORREF accent){
        PBRANGE before{};SendMessageW(progress,PBM_GETRANGE,FALSE,reinterpret_cast<LPARAM>(&before));
        const auto position=SendMessageW(progress,PBM_GETPOS,0,0);
        require(before.iLow==-100&&before.iHigh==300&&static_cast<int>(position)==expected_position,"The native progress messages lost their original range/position semantics");
        FillRect(memory,&bounds,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SendMessageW(progress,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);GdiFlush();
        const auto colors=reader_theme::palette(progress);
        if(!colors.high_contrast){
            const auto filled=static_cast<int>((static_cast<long long>(bounds.right)*numerator)/denominator);
            if(filled>0)require(GetPixel(memory,filled/2,bounds.bottom/2)==accent,
                "The real lesson progress did not paint its inherited route accent");
            if(filled<bounds.right)require(GetPixel(memory,(filled+bounds.right)/2,bounds.bottom/2)==colors.outline,
                "The real lesson progress did not paint the unfilled native range with the theme outline");
        }
        PBRANGE after{};SendMessageW(progress,PBM_GETRANGE,FALSE,reinterpret_cast<LPARAM>(&after));
        require(before.iLow==after.iLow&&before.iHigh==after.iHigh&&SendMessageW(progress,PBM_GETPOS,0,0)==position,
            "Painting the progress changed its native range or position");
    };
    SendMessageW(progress,PBM_SETRANGE32,static_cast<WPARAM>(-100),300);
    SendMessageW(progress,PBM_SETPOS,100,0);assert_paint(100,1,2,RGB(228,50,72));
    reader_theme::set_accent(fixture.view,RGB(108,209,92));assert_paint(100,1,2,RGB(108,209,92));
    reader_theme::set_accent(fixture.view,RGB(0,0,0));assert_paint(100,1,2,RGB(0,0,0));
    reader_theme::set_accent(fixture.view,RGB(228,50,72));
    require(SendMessageW(progress,PBM_DELTAPOS,50,0)==100,"PBM_DELTAPOS no longer returns its native previous position");
    assert_paint(150,5,8,RGB(228,50,72));
    SendMessageW(progress,PBM_SETSTEP,50,0);SendMessageW(progress,PBM_STEPIT,0,0);assert_paint(200,3,4,RGB(228,50,72));
    SendMessageW(progress,PBM_SETPOS,1000,0);assert_paint(300,1,1,RGB(228,50,72));
    SendMessageW(progress,PBM_SETPOS,static_cast<WPARAM>(-1000),0);assert_paint(-100,0,1,RGB(228,50,72));
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);
    SendMessageW(progress,PBM_SETRANGE32,original.iLow,original.iHigh);SendMessageW(progress,PBM_SETPOS,original_position,0);
    std::cout<<"PASS Native lesson progress route colors, real GDI rendering, unchanged range/position, delta/step/clamping and actual high-contrast delegation\n";
}

void native_answer_overflow_bars(){
    Fixture fixture({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"({"id":"qa_answer_native_overflow","title":"Barras reales","pages":[{"blocks":[
        {"type":"fill_code","question":"Completa el operador.","code":"std::cout [] 1;","answers":["<<"]},
        {"type":"output_prediction","question":"Escribe las líneas.","code":"std::cout << \"primera\\nsegunda\";","answers":["primera\nsegunda"]}]}]})");});
    const auto bars=[](HWND item){return GetWindowLongPtrW(item,GWL_STYLE)&(WS_HSCROLL|WS_VSCROLL);};
    const auto height=[](HWND item){RECT bounds{};GetWindowRect(item,&bounds);return bounds.bottom-bounds.top;};
    const auto horizontal=fixture.control(0,2),multiline=fixture.control(1,2);
    const auto empty_height=height(multiline);
    for(const auto item:{horizontal,multiline})require(!bars(item),"An empty answer reserves a native scrollbar without overflow");
    fixture.replace(0,2,std::string(400,'x'));
    SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE};GetScrollInfo(horizontal,SB_HORZ,&range);
    require((bars(horizontal)&WS_HSCROLL)&&range.nPage>0&&range.nMax-range.nMin+1>static_cast<int>(range.nPage),
        "A genuinely long single-line answer did not regain its native horizontal scrollbar");
    fixture.replace(1,2,"Entrada\nSalida");
    const auto two_line_height=height(multiline);
    require(two_line_height>empty_height&&!bars(multiline)&&fixture.control(1,2)==multiline,
        "A two-line answer did not grow with its content or recreated its original editor");
    std::string lines;for(int row=0;row<40;++row)lines+="Línea conservada\n";fixture.replace(1,2,lines);
    require(height(multiline)>two_line_height&&!(bars(multiline)&WS_VSCROLL),
        "An uncapped answer did not grow to its contents and hand vertical scrolling to the lesson");
    SendMessageW(horizontal,WM_HSCROLL,SB_RIGHT,0);
    SendMessageW(horizontal,EM_SETSEL,100,110);SendMessageW(multiline,EM_SETSEL,3,9);
    POINT before{},after{};SendMessageW(horizontal,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
    require(before.x>0,"The overflow regression did not reach a real horizontal scroll position");
    SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,0);pump(fixture.host);
    CHARRANGE selected{};SendMessageW(horizontal,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
    SendMessageW(multiline,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
    require(before.x==after.x&&selected.cpMin==3&&selected.cpMax==9&&fixture.text(1,2)==lines,
        "Reflow changed a native scroll position, selection or multiline answer");
    fixture.replace(0,2,"");fixture.replace(1,2,"");
    for(const auto item:{horizontal,multiline}){POINT offset{};SendMessageW(item,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        require(!bars(item)&&offset.x==0&&offset.y==0,"Clearing an answer retained bars or hidden scrolling");}
    require(height(multiline)==empty_height,"Clearing a multiline answer did not restore its content-sized height");
    SendMessageW(multiline,WM_UNDO,0,0);pump(fixture.host);
    require(fixture.text(1,2)==lines&&height(multiline)>two_line_height&&!(bars(multiline)&WS_VSCROLL),
        "Undo lost the original multiline text or its content-sized geometry");
    std::cout<<"PASS answer automatic height, horizontal overflow, unchanged native controls/selections and Undo\n";
}
void native_answer_end_viewport(){
    Fixture fixture({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"({"id":"qa_answer_end_viewport","title":"Navegación de respuesta","pages":[{"blocks":[
        {"type":"output_prediction","question":"Escribe las dos líneas.","code":"std::cout << \"Entrada\\nSalida\";","answers":["Entrada\nSalida"]}]}]})");});
    const auto answer=fixture.control(0,2);
    fixture.replace(0,2,"Entrada\nSalida");
    const auto end_key=[&](bool control,bool shift){
        std::array<BYTE,256> previous{};require(GetKeyboardState(previous.data()),"Cannot preserve test-thread modifier state");
        struct RestoreKeys { std::array<BYTE,256>& keys; ~RestoreKeys(){SetKeyboardState(keys.data());} } restore{previous};
        auto keys=previous;
        for(const auto key:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL})keys[key]=0;
        for(const auto key:{VK_SHIFT,VK_LSHIFT,VK_RSHIFT})keys[key]=0;
        if(control)keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
        if(shift)keys[VK_SHIFT]=keys[VK_LSHIFT]=0x80;
        require(SetKeyboardState(keys.data()),"Cannot set isolated test-thread modifiers");
        SendMessageW(answer,WM_KEYDOWN,VK_END,1);SendMessageW(answer,WM_KEYUP,VK_END,LPARAM{1}<<31);
        CHARRANGE selected{};SendMessageW(answer,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));return selected;
    };
    const auto same_selection=[&](CHARRANGE expected){
        CHARRANGE actual{};SendMessageW(answer,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&actual));
        require(actual.cpMin==expected.cpMin&&actual.cpMax==expected.cpMax,"Viewport correction changed the native End selection");
    };
    for(const auto zoom:{75,100,125,150,175,200}){
        update_lesson_preferences(fixture.view,[zoom](state::StateData& state){state.preferences["lesson_text_size_settings"]["global_scale"]=static_cast<std::int64_t>(zoom);});
        SendMessageW(fixture.view,WM_VSCROLL,SB_BOTTOM,0);pump(fixture.host);SetFocus(answer);pump(fixture.host);
        const auto second=SendMessageW(answer,EM_LINEINDEX,1,0);
        require(second>0&&SendMessageW(answer,EM_GETLINECOUNT,0,0)==2,"The End fixture lost its two real answer lines");
        for(const auto modifiers:{0,1,2,3}){
            const bool control=(modifiers&1)!=0,shift=(modifiers&2)!=0;
            const auto start=control?0:second;SendMessageW(answer,EM_SETSEL,start,start);pump(fixture.host);
            const auto selected=end_key(control,shift);pump(fixture.host);same_selection(selected);
            POINT offset{};SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
            POINTL first{},last{};SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
            SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&last),second);
            const auto viewport=rich_edit_viewport(answer);
            require(offset.y==0&&first.y>=viewport.top-1&&last.y>first.y&&last.y<viewport.bottom&&
                !(GetWindowLongPtrW(answer,GWL_STYLE)&WS_VSCROLL)&&fixture.control(0,2)==answer&&fixture.text(0,2)=="Entrada\nSalida",
                "End, Ctrl+End or Shift+End hid the first line of a complete two-line answer");
        }
    }
    SendMessageW(answer,WM_UNDO,0,0);pump(fixture.host);
    require(fixture.text(0,2).empty(),"Caret/viewport normalization created an answer Undo entry");
    SendMessageW(answer,EM_REDO,0,0);pump(fixture.host);
    require(fixture.text(0,2)=="Entrada\nSalida","Caret/viewport normalization damaged answer Redo");

    // A direction that truly overflows must retain the position chosen by
    // the native caret, even though the other direction fits completely.
    fixture.replace(0,2,std::string(400,'x'));SetFocus(answer);SendMessageW(answer,EM_SETSEL,0,0);pump(fixture.host);
    const auto horizontal_selection=end_key(true,false);
    POINT before{},after{};SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));pump(fixture.host);
    SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));same_selection(horizontal_selection);
    require(before.x>0&&after.x==before.x&&after.y==0,"End normalization reset a genuine horizontal answer scroll");

    update_lesson_preferences(fixture.view,[](state::StateData& state){state.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{100};});
    std::string lines;for(int row=0;row<40;++row)lines+="Entrada\n";fixture.replace(0,2,lines);
    SendMessageW(fixture.view,WM_VSCROLL,SB_TOP,0);pump(fixture.host);
    RECT bounds{};GetWindowRect(answer,&bounds);
    // Exercise the same real RichEdit with a bounded viewport, as when a
    // host cannot offer its complete automatic height. Do not reflow it.
    SetWindowPos(answer,nullptr,0,0,bounds.right-bounds.left,MulDiv(90,GetDpiForWindow(answer),96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetFocus(answer);SendMessageW(answer,EM_SETSEL,0,0);pump(fixture.host);
    const auto vertical_selection=end_key(true,false);
    SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));pump(fixture.host);
    SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));same_selection(vertical_selection);
    require(before.y>0&&after.y==before.y&&fixture.text(0,2)==lines&&fixture.control(0,2)==answer,
        "End normalization reset a genuine vertical answer scroll or changed its original editor");
    std::cout<<"PASS End/Ctrl+End/Shift+End: both answer lines visible at 75–200 percent, real overflow preserved, unchanged selection/HWND/Undo\n";
}
void native_solved_answer_style(){
    Fixture fixture({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({"id":"qa_solved_answer_surface","title":"Respuestas legibles","pages":[{"blocks":[
        {"type":"output_prediction","question":"Escribe la salida.","code":"std::cout << \"Archivo preparado\";","answers":["Archivo preparado"]},
        {"type":"fill_code","question":"Completa el operador.","code":"std::cout [] \"Listo\";","answers":["<<"]}]}]})json");});
    struct Raster{COLORREF background;std::size_t ink;};
    const auto raster=[](HWND field){
        CHARRANGE selection{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(field,EM_SETSEL,0,0);
        RECT area{};GetClientRect(field,&area);const auto screen=GetDC(field),dc=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(field,screen);
        require(dc&&bitmap&&pixels&&area.right>4&&area.bottom>4,"Cannot capture the real programming answer raster");
        const auto previous=SelectObject(dc,bitmap);const auto sentinel=CreateSolidBrush(RGB(1,2,3));FillRect(dc,&area,sentinel);DeleteObject(sentinel);
        SendMessageW(field,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
        const auto background=GetPixel(dc,area.right-3,area.bottom-3),expected=reader_theme::background(field);
        std::size_t ink=0;const auto values=static_cast<const std::uint32_t*>(pixels);
        for(int y=0;y<area.bottom;++y)for(int x=0;x<area.right;++x){const auto value=values[static_cast<std::size_t>(y)*area.right+x];
            const int red=(value>>16)&255,green=(value>>8)&255,blue=value&255;
            if(red+green+blue>250&&std::abs(red-GetRValue(expected))+std::abs(green-GetGValue(expected))+std::abs(blue-GetBValue(expected))>=96)++ink;
        }
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));return Raster{background,ink};
    };
    const auto legible=[&](HWND field){const auto captured=raster(field);if(!reader_theme::palette(field).high_contrast)
        require(captured.background==reader_theme::background(field)&&captured.ink>8,
            "A real programming answer lost its dark background or legible native glyphs when completed/reopened");};
    for(std::size_t index=0;index<2;++index){
        const auto expected=index==0?"Archivo preparado":"<<";fixture.replace(index,2,expected);SetFocus(fixture.view);legible(fixture.control(index,2));
        const auto undo=SendMessageW(fixture.control(index,2),EM_CANUNDO,0,0);fixture.enter(index);const auto field=fixture.control(index,2);
        require(!IsWindowEnabled(field)&&(GetWindowLongPtrW(field,GWL_STYLE)&WS_DISABLED)&&fixture.text(index,2)==expected,
            "Completing a programming answer changed its text or removed its real disabled state");
        if(!reader_theme::palette(field).high_contrast)require(GetWindowLongPtrW(field,GWL_STYLE)&ES_READONLY,"A disabled programming answer lost its native readonly protection");
        legible(field);SendMessageW(field,WM_CHAR,L'(',1);SendMessageW(field,WM_KEYDOWN,VK_BACK,1);SendMessageW(field,WM_UNDO,0,0);
        require(fixture.text(index,2)==expected&&SendMessageW(field,EM_CANUNDO,0,0)==undo,
            "A completed programming answer accepted code assistance or modified its preserved Undo history");
    }
    fixture.reopen();
    for(std::size_t index=0;index<2;++index){const auto field=fixture.control(index,2);
        require(!IsWindowEnabled(field)&&fixture.text(index,2)==(index==0?"Archivo preparado":"<<"),"Reopening did not restore the same completed programming answer");
        legible(field);fixture.click(index,12);require(IsWindowEnabled(field)&&!(GetWindowLongPtrW(field,GWL_STYLE)&ES_READONLY)&&fixture.text(index,2).empty(),
            "Reset did not restore the original editable state of the same programming answer HWND");
        fixture.replace(index,2,"Personal");SendMessageW(field,EM_SETSEL,static_cast<WPARAM>(-1),-1);SendMessageW(field,WM_CHAR,L'(',1);pump(fixture.host);
        require(fixture.text(index,2)=="Personal()","Unlocking a completed answer lost its original code-pair assistance");legible(field);
    }
    std::cout<<"PASS actual output-prediction/fill rasters: dark readable completed/reopened answers, real disabled/readonly protection, preserved Undo and editable reset\n";
}
void native_compiler_editor_overflow_bars(){
    for(const bool laboratory:{false,true}){
        Fixture fixture({},[laboratory](const curriculum::Lesson&){
            auto model=windows::parse_lesson_json(R"json({"id":"qa_compiler_editor_overflow","title":"Editor real","pages":[{"blocks":[
                {"type":"cpp_compiler","initialCode":"int main() {}","initialStandardInput":""}]}]})json");
            if(laboratory)model.id="native_cpp_laboratory";
            return model;
        });
        const auto source=fixture.control(0,10),input=fixture.control(0,15);
        const auto bars=[](HWND item){return GetWindowLongPtrW(item,GWL_STYLE)&(WS_HSCROLL|WS_VSCROLL);};
        require(!bars(source)&&!bars(input),"A short compiler editor or empty stdin kept unnecessary native bars");
        std::string large;for(int row=0;row<40;++row)large+=std::string(400,'x')+'\n';
        fixture.replace(0,10,large);
        require(bars(source)==(WS_HSCROLL|WS_VSCROLL),"An overflowing compiler editor did not enable both native axes");
        SendMessageW(source,WM_HSCROLL,SB_RIGHT,0);SendMessageW(source,WM_VSCROLL,SB_BOTTOM,0);
        POINT before{},after{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
        SendMessageW(source,EM_SETSEL,3,9);SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,0);pump(fixture.host);
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));CHARRANGE selection{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        require(before.x>0&&before.y>0&&after.x==before.x&&after.y==before.y&&selection.cpMin==3&&selection.cpMax==9,
            "Compiler reflow lost a genuine scroll position or the original selection");
        fixture.click(0,35);
        require(!(bars(source)&WS_HSCROLL)&&(bars(source)&WS_VSCROLL),"Wrapped compiler code retained horizontal scrolling");
        fixture.replace(0,10,"int main() {}");
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
        require(!bars(source)&&after.x==0&&after.y==0&&fixture.control(0,10)==source,
            "Shortening wrapped source retained old bars, offsets or recreated the editor");
        fixture.replace(0,15,large);require(bars(input)==(WS_HSCROLL|WS_VSCROLL),"Long stdin did not acquire real scrolling");
        fixture.replace(0,15,"");require(!bars(input),"Clearing stdin did not remove its scrollbars");
        SendMessageW(source,WM_UNDO,0,0);pump(fixture.host);
        require(fixture.text(0,10)==large&&(bars(source)&WS_VSCROLL)&&!(bars(source)&WS_HSCROLL),
            "Undo lost the source or failed to restore its wrapped vertical overflow");
    }
    std::cout<<"PASS compiler editor/stdin native overflow in lessons and Laboratory, wrap, shrinking, offsets and Undo\n";
}
void native_compiler_output_overflow_bars(){
    Fixture fixture({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({"id":"qa_compiler_console_overflow","title":"Salida real","pages":[{"blocks":[
        {"type":"cpp_compiler","initialCode":"#include <iostream>\n#include <string>\nint main() { for (int i=0;i<40;++i) std::cout << std::string(400,'x') << '\\n'; }\n"}]}]})json");});
    const auto console=fixture.control(0,14),action=fixture.control(0,11);
    require(!(GetWindowLongPtrW(console,GWL_STYLE)&(WS_HSCROLL|WS_VSCROLL)),"An empty compiler console reserves native scrollbars");
    fixture.click(0,11);wait_for(fixture.host,[&]{return IsWindowEnabled(action);},"Large real compiler output did not finish");
    require(fixture.text(0,14).size()==std::size_t{40*401}&&(GetWindowLongPtrW(console,GWL_STYLE)&WS_HSCROLL)&&(GetWindowLongPtrW(console,GWL_STYLE)&WS_VSCROLL),
        "Genuine horizontal and vertical program output did not regain native scrollbars or preserved output bytes");
    SendMessageW(console,WM_VSCROLL,SB_BOTTOM,0);SendMessageW(console,EM_SETSEL,3,9);
    POINT before{},after{};CHARRANGE selection{};SendMessageW(console,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,0);pump(fixture.host);SendMessageW(console,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
    SendMessageW(console,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
    require(before.y>0&&after.y==before.y&&selection.cpMin==3&&selection.cpMax==9,"Console reflow changed its existing native scroll or selection");
    fixture.replace(0,10,"#include <iostream>\nint main() { std::cout << \"Una línea\"; }\n");
    fixture.click(0,11);wait_for(fixture.host,[&]{return IsWindowEnabled(action);},"Short replacement program did not finish");
    require(fixture.text(0,14)=="Una línea"&&!(GetWindowLongPtrW(console,GWL_STYLE)&(WS_HSCROLL|WS_VSCROLL)),
        "A new short execution retained scrollbars from an older overflowing output");
    require(fixture.text(0,4).empty(),"A console-only compiler invented a checked response");
    std::cout<<"PASS real compiler output scrollbars: no empty/one-line bars, genuine two-axis overflow, stable reflow and fresh shortened output\n";
}
void simulated_exercise_code_cards(bool caret_only=false) {
    struct DpiScope{DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}} dpi_scope;
    Fixture fixture({},[](const curriculum::Lesson&){
        return windows::parse_lesson_json(R"json({"id":"qa_simulated_exercise_headers","title":"Cabeceras de ejercicios","pages":[{"blocks":[
            {"type":"output_prediction","question":"Predice el mensaje.","code":"std::cout << \"Listo\";","answers":["Listo"]},
            {"type":"fill_code","question":"Completa el operador.","code":"std::cout [] \"Listo\";","answers":["<<"]},
            {"type":"order_code","question":"Ordena las líneas.","fragments":["return 0;","int main() {","}"],"correctOrder":[1,0,2]},
            {"type":"inline_fill_code","question":"Completa el código.","template":"std::cout [] \"Listo\";","answers":["<<"]},
            {"type":"drag_fill_code","question":"Coloca la función.","template":"int []() { return 0[] }","choices":["main",";","Main"],"correctOrder":[0,1]}
            ]}]})json");
    });
    const auto dpi=static_cast<int>(GetDpiForWindow(fixture.host));
    MoveWindow(fixture.host,30,30,MulDiv(1100,dpi,96),MulDiv(850,dpi,96),TRUE);
    RECT host_client{};GetClientRect(fixture.host,&host_client);MoveWindow(fixture.view,0,0,host_client.right,host_client.bottom,TRUE);pump(fixture.host);
    const auto original_json=fixture.lesson.raw_json;
    for(std::size_t index=0;index<5;++index){
        require(fixture.text(index,16)=="</> C++"&&accessible_name(fixture.control(index,12))=="Reiniciar respuesta"&&
            accessible_name(fixture.control(index,18))==(index<2?"Copiar texto":"Copiar ejercicio"),
            "A simulated programming response lost its actual language/reset/copy header");
        require(!descendant(fixture.view,id(index,19)),"A simulated puzzle retained an Android-absent settings button");
        if(index<2)require(fixture.text(index,38)=="</> C++"&&accessible_name(fixture.control(index,13))=="Pantalla completa"&&
            accessible_name(fixture.control(index,39))=="Copiar código"&&accessible_name(fixture.control(index,35))=="Ajuste de línea desactivado",
            "A prediction/fill did not retain separate readonly-code and editable-answer Android headers");
        else require(accessible_name(fixture.control(index,13))=="Pantalla completa","A code composition puzzle lost its Android fullscreen action");
        if(index>=3)require(accessible_name(fixture.control(index,35))=="Ajuste de línea desactivado",
            "A compositional code puzzle lost its direct Android wrap button");
        require(!IsWindowEnabled(fixture.control(index,12)),"A fresh simulated response gained a resettable hidden answer");
    }
    const auto output_code=fixture.control(0,1),fill_answer=fixture.control(1,2);
    for(const auto index:{std::size_t{0},std::size_t{1}}) {
        RECT source{},answer_header{},answer{};GetWindowRect(fixture.control(index,1),&source);
        GetWindowRect(fixture.control(index,38),&answer_header);GetWindowRect(fixture.control(index,2),&answer);
        RECT check{};GetWindowRect(fixture.control(index,3),&check);
        require(answer_header.top-source.bottom==MulDiv(48,dpi,96)&&answer.top>answer_header.bottom&&check.top-answer.bottom==MulDiv(18,dpi,96),
            "Separate source/answer surfaces lost their outer margins or added another internal answer bottom padding");
        require(!windows::code_gutter_snapshot(fixture.control(index,2)).active,
            "A plain answer field gained the source console's line-number margin");
    }
    if(caret_only){
    RECT normal_answer{};GetWindowRect(fill_answer,&normal_answer);
    require(normal_answer.bottom-normal_answer.top>MulDiv(32,dpi,96)&&normal_answer.bottom-normal_answer.top<MulDiv(82,dpi,96),
        "An empty answer did not fit one native text line plus Android's vertical padding");
    const auto glyph_pixels=[](HWND field){
        RECT area{};GetClientRect(field,&area);const auto dc=CreateCompatibleDC(nullptr);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;
        info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* memory=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&memory,nullptr,0);
        require(dc&&bitmap&&memory,"Cannot create the native answer clipping raster");const auto previous=SelectObject(dc,bitmap);
        FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));
        SendMessageW(field,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);GdiFlush();
        const auto background=reader_theme::background(field);const auto pixels=static_cast<const std::uint32_t*>(memory);
        std::size_t count=0;
        for(std::size_t at=0;at<static_cast<std::size_t>(area.right)*area.bottom;++at){
            const auto value=pixels[at];
            if(std::abs(static_cast<int>((value>>16)&255)-GetRValue(background))+
                std::abs(static_cast<int>((value>>8)&255)-GetGValue(background))+
                std::abs(static_cast<int>(value&255)-GetBValue(background))>=96)++count;
        }
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return count;
    };
    for(const auto zoom:{75,100,125,150,175,200}){
    update_lesson_preferences(fixture.view,[zoom](state::StateData& state){state.preferences["lesson_text_size_settings"]["global_scale"]=static_cast<std::int64_t>(zoom);});
    for(const auto index:{0u,1u}){
        const auto answer=fixture.control(index,2);
        const auto pane=descendant_class(fixture.view,L"LearnGameAndEngineDev.LessonPane");
        RECT answer_bounds{},viewport{};GetWindowRect(answer,&answer_bounds);GetWindowRect(pane,&viewport);
        // A native RichEdit creates its editing caret only in the visible
        // viewport. Reach each stacked answer through the lesson's actual
        // scrolling path, as a user does before clicking the second card.
        while(answer_bounds.top<viewport.top||answer_bounds.bottom>viewport.bottom){
            const auto previous=answer_bounds;
            SendMessageW(fixture.view,WM_VSCROLL,answer_bounds.top<viewport.top?SB_LINEUP:SB_LINEDOWN,0);
            pump(fixture.host);GetWindowRect(answer,&answer_bounds);GetWindowRect(pane,&viewport);
            if(EqualRect(&previous,&answer_bounds))break;
        }
        require(answer_bounds.top>=viewport.top&&answer_bounds.bottom<=viewport.bottom,
            "The enlarged answer could not be reached through native lesson scrolling");
        SetFocus(answer);pump(fixture.host);
        RECT hit{};SendMessageW(answer,EM_GETRECT,0,reinterpret_cast<LPARAM>(&hit));
        const auto point=MAKELPARAM(hit.left+1,hit.top+1);
        SendMessageW(answer,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(answer,WM_LBUTTONUP,0,point);pump(fixture.host);
        GUITHREADINFO caret{};caret.cbSize=sizeof(caret);
        const bool measurable_caret=GetGUIThreadInfo(GetCurrentThreadId(),&caret)&&caret.hwndCaret==answer&&caret.rcCaret.bottom>caret.rcCaret.top;
        require(measurable_caret,
            "The enlarged answer "+std::to_string(index)+" did not retain a measurable native editing caret");
        RECT content{},client{};SendMessageW(answer,EM_GETRECT,0,reinterpret_cast<LPARAM>(&content));GetClientRect(answer,&client);
        const auto padding=MulDiv(16,static_cast<int>(GetDpiForWindow(answer)),96);
        POINTL first_character{};SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first_character),0);
        int numerator=0,denominator=0;SendMessageW(answer,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        const auto viewport_bounds=rich_edit_viewport(answer);
        // EM_GETRECT reports unzoomed insets. Comparing the caret with that
        // rectangle misses a zoomed bottom inset clipping the actual view.
        require(content.top==MulDiv(padding,denominator,numerator)&&std::abs(first_character.y-padding)<=1&&
            std::abs(viewport_bounds.top-padding)<=1&&std::abs(client.bottom-viewport_bounds.bottom-padding)<=1&&
            caret.rcCaret.top>=first_character.y-1&&caret.rcCaret.bottom<=viewport_bounds.bottom,
            "A zoomed empty answer clipped its actual caret inside Android's vertical padding");
        const auto empty_height=client.bottom;
        for(const auto text:{"RocaAzul",": 'dfgd'","ÁÉí gypq","RocaAzul gypq"}){
        fixture.replace(index,2,text);SetFocus(answer);pump(fixture.host);
        GUITHREADINFO typed_caret{};typed_caret.cbSize=sizeof(typed_caret);
        POINTL typed_origin{};SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&typed_origin),0);
        SendMessageW(answer,EM_GETRECT,0,reinterpret_cast<LPARAM>(&content));GetClientRect(answer,&client);
        const auto typed_viewport=rich_edit_viewport(answer);
        require(GetGUIThreadInfo(GetCurrentThreadId(),&typed_caret)&&typed_caret.hwndCaret==answer&&
            typed_caret.rcCaret.top>=typed_origin.y-1&&typed_caret.rcCaret.bottom<=typed_viewport.bottom&&
            std::abs(client.bottom-typed_viewport.bottom-padding)<=1&&
            std::abs(typed_origin.x-first_character.x)<=1&&std::abs(typed_origin.y-first_character.y)<=1&&fixture.control(index,2)==answer,
            "Typing the first answer moved its line origin, clipped its caret or recreated the editor");
        SetFocus(fixture.view);const auto normal_ink=glyph_pixels(answer);require(normal_ink>8,"The actual answer text was not drawn");
        // A generous temporary viewport must not reveal any part of a glyph
        // that the normal content-sized answer cropped off at its bottom.
        RECT outer{};GetWindowRect(answer,&outer);const auto extra=MulDiv(64,dpi,96);
        SetWindowPos(answer,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top+extra,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        content.bottom+=extra;SendMessageW(answer,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
        require(glyph_pixels(answer)==normal_ink,"The normal answer viewport clipped native glyph pixels visible in a taller viewport");
        SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,0);pump(fixture.host);
        }
        fixture.replace(index,2,"");GetClientRect(answer,&client);
        require(client.bottom==empty_height&&fixture.control(index,2)==answer,
            "Clearing an answer did not restore its original native empty-line height");
        SendMessageW(answer,WM_UNDO,0,0);pump(fixture.host);
        require(fixture.text(index,2)=="RocaAzul gypq","Native line measurement changed the answer Undo history");
        fixture.replace(index,2,"");
    }
    }
    std::cout<<"PASS 75/100/125/150/175/200-percent answer native viewport/caret, stable first-line origin, complete glyph raster and Undo\n";
    return;
    }
    update_lesson_preferences(fixture.view,[](state::StateData& state){state.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{100};});
    const auto inline_host=fixture.control(3,5),drag_host=fixture.control(4,5);
    const auto inline_answer=GetDlgItem(inline_host,100),drag_slot=GetDlgItem(drag_host,100);
    require(inline_answer&&drag_slot&&edit_text(inline_answer).empty()&&edit_text(drag_slot).empty(),
        "Adding code headers revealed the internal inline/drag expected answers");
    fixture.click(3,35);
    require(accessible_name(fixture.control(4,35))=="Ajuste de línea activado"&&
        GetDlgItem(inline_host,100)==inline_answer&&GetDlgItem(drag_host,100)==drag_slot&&
        !(GetWindowLongPtrW(output_code,GWL_STYLE)&ES_AUTOHSCROLL),
        "A direct inline wrap change did not propagate to the same live drag/source views and toolbar states");
    fixture.click(4,35);
    const auto regions=lesson_reading_regions(fixture.view);
    require(std::ranges::none_of(regions,[&](const auto& region){
        return region.target==output_code;
    }),"A simulated header started narrating excluded console code or buttons");
    const auto order_surface=fixture.control(2,5);
    require(std::ranges::none_of(regions,[&](const auto& region){return IsChild(order_surface,region.target);})&&
        std::ranges::any_of(regions,[&](const auto& region){return region.target==fixture.control(2,37)&&region.source_text=="Mantén pulsado un bloque y arrástralo hasta la posición correcta.";}),
        "Code ordering narrated source fragments or omitted the original visible Android instruction");
    fixture.replace(0,2,"Respuesta personal");fixture.replace(1,2,"<<");
    const auto source_before_fullscreen=fixture.text(0,1),answer_before_fullscreen=fixture.text(0,2);
    const auto script_before_fullscreen=reading_script(fixture.view);
    const auto output_answer=fixture.control(0,2);
    fixture.click(0,13);
    require(fixture.control(0,1)==output_code&&fixture.control(0,2)==output_answer&&
        IsWindowVisible(output_code)&&IsWindowVisible(output_answer)&&!IsWindowVisible(fixture.control(0,3))&&
        reading_script(fixture.view)==script_before_fullscreen,"Fullscreen failed to move both original code/answer cards while retaining Check outside and the narration script");
    fixture.escape();
    require(fixture.text(0,1)==source_before_fullscreen&&fixture.text(0,2)==answer_before_fullscreen,
        "Leaving the two-card fullscreen changed the readonly source or written answer");
    SetFocus(output_answer);SendMessageW(output_answer,EM_SETSEL,0,-1);SendMessageW(output_answer,WM_CHAR,L'(',1);pump(fixture.host);
    require(fixture.text(0,2)=="(Respuesta personal)","Output prediction did not share code pair assistance and its selection wrapping");
    SendMessageW(output_answer,WM_UNDO,0,0);pump(fixture.host);
    require(fixture.text(0,2)==answer_before_fullscreen,"Prediction pair Undo did not retain the same original response journal");
    require(IsWindowEnabled(fixture.control(0,12))&&IsWindowEnabled(fixture.control(1,12)),"Typing a simulated answer did not update its real header reset");
    fixture.click(0,12);
    require(fixture.text(0,2).empty()&&fixture.control(1,2)==fill_answer&&fixture.text(1,2)=="<<"&&
        fixture.text(0,1)==fixture.lesson.pages[0].blocks[0].code,
        "Resetting a response modified immutable code, another answer or its HWND");
    fixture.enter(1);require(fixture.text(1,4).starts_with("✓ Correcto")&&fixture.page()==0,"The decorated fill no longer checks with Enter without navigation");
    fixture.click(1,12);require(fixture.text(1,2).empty()&&fixture.text(1,4).empty(),"The fill header did not reuse the existing correct-answer reset");
    const auto order_host=fixture.control(2,5),first_fragment=GetDlgItem(order_host,100),main_fragment=GetDlgItem(order_host,101);
    require(first_fragment&&main_fragment&&!descendant(fixture.view,id(2,20)),"The order card still uses artificial up/down buttons instead of its actual code rows");
    require(!IsWindowEnabled(fixture.control(2,3)),"An untouched order puzzle can be checked before any interaction");
    const auto untouched=read_lesson_state(fixture.view);SendMessageW(fixture.view,WM_COMMAND,MAKEWPARAM(id(2,3),BN_CLICKED),0);pump(fixture.host);
    require(read_lesson_state(fixture.view)==untouched&&fixture.text(2,4).empty(),"Checking an untouched ordering puzzle consumed an attempt or published feedback");
    SendMessageW(main_fragment,WM_SYSKEYDOWN,VK_UP,LPARAM{1}<<29);pump(fixture.host);
    auto ordered=state::get_response(read_lesson_state(fixture.view),curriculum::exercise_key(fixture.lesson,fixture.lesson.pages[0].blocks[2]));
    require(ordered&&ordered->order==std::vector<std::int64_t>{1,0,2}&&IsWindowEnabled(fixture.control(2,12)),"The actual order card did not persist its reordered fragments");
    fixture.click(2,12);ordered=state::get_response(read_lesson_state(fixture.view),curriculum::exercise_key(fixture.lesson,fixture.lesson.pages[0].blocks[2]));
    require(ordered&&ordered->order.empty()&&!IsWindowEnabled(fixture.control(2,12))&&!IsWindowEnabled(fixture.control(2,3))&&fixture.control(2,5)==order_host&&edit_text(first_fragment)=="return 0;","Resetting order did not restore its original unsolved presentation and row identity");
    SendMessageW(inline_answer,EM_SETSEL,0,-1);SendMessageW(inline_answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"<<"));pump(fixture.host);
    fixture.click(3,13);require(fixture.control(3,5)==inline_host&&GetDlgItem(inline_host,100)==inline_answer&&edit_text(inline_answer)=="<<",
        "Inline fullscreen recreated the existing hole/editor or discarded its draft");
    fixture.escape();fixture.click(3,12);require(edit_text(inline_answer).empty(),"The inline header reset did not clear only its own answer");
    const auto bank=GetDlgItem(drag_host,3000),first_card=GetDlgItem(bank,2000),second_card=GetDlgItem(bank,2001);
    require(bank&&first_card&&second_card,"The drag-fill palette or its real available choices are missing");
    RECT first_normal{},second_normal{};GetWindowRect(first_card,&first_normal);GetWindowRect(second_card,&second_normal);
    require(second_normal.top>first_normal.top,"The normal drag-fill palette no longer uses Android's vertical stack");
    const auto drag_script=reading_script(fixture.view);fixture.click(4,13);
    const auto tray=fixture.control(118);RECT tray_bounds{},body_bounds{},first_full{},second_full{},view_bounds{};
    GetWindowRect(tray,&tray_bounds);GetWindowRect(GetParent(drag_host),&body_bounds);GetWindowRect(first_card,&first_full);GetWindowRect(second_card,&second_full);
    GetClientRect(fixture.view,&view_bounds);POINT bottom{0,view_bounds.bottom};ClientToScreen(fixture.view,&bottom);
    require(fixture.control(4,5)==drag_host&&GetDlgItem(drag_host,100)==drag_slot&&GetParent(bank)==tray&&IsWindowVisible(bank)
            &&GetDlgItem(bank,2000)==first_card&&first_full.top==second_full.top&&second_full.left>first_full.left,
        "Drag-fill fullscreen duplicated the palette/cards or failed to arrange the existing choices horizontally");
    require(tray_bounds.bottom==bottom.y&&body_bounds.bottom==tray_bounds.top&&reading_script(fixture.view)==drag_script
            &&!IsWindowVisible(fixture.control(4,3)),"The fullscreen bank is not anchored below its own reserved viewport, or changed the narration/Check policy");
    SendMessageW(first_card,BM_CLICK,0,0);pump(fixture.host);
    require(GetDlgItem(bank,2000)==first_card&&edit_text(drag_slot)=="main",
        "The same floating bank choice stopped writing into the existing authored hole");
    SetFocus(drag_slot);PostMessageW(drag_slot,WM_KEYDOWN,VK_ESCAPE,1);pump(fixture.host);
    require(GetParent(bank)==drag_host&&!IsWindowVisible(tray)&&fixture.control(4,5)==drag_host&&GetDlgItem(bank,2000)==first_card
            &&edit_text(drag_slot)=="main"&&reading_script(fixture.view)==drag_script,
        "Physical Escape failed to return the original vertical bank and its unchanged answer to the lesson");
    RECT second_restored{},third_restored{};GetWindowRect(second_card,&second_restored);GetWindowRect(GetDlgItem(bank,2002),&third_restored);
    require(third_restored.top>second_restored.top,"Leaving fullscreen retained the horizontal palette in normal lesson layout");
    fixture.click(4,12);
    SendMessageW(first_card,BM_CLICK,0,0);pump(fixture.host);
    require(edit_text(drag_slot)=="main"&&IsWindowEnabled(fixture.control(4,12)),"The decorated drag card no longer places a visible choice");
    fixture.click(4,12);require(edit_text(drag_slot).empty()&&fixture.lesson.raw_json==original_json&&fixture.control(4,5)==drag_host,
        "Resetting the drag header changed source curriculum or recreated its controls");
    fixture.non_overlapping_rows();fixture.reopen();
    require(fixture.text(0,1)==fixture.lesson.pages[0].blocks[0].code&&fixture.lesson.raw_json==original_json,
        "Reopening the new simulated headers changed immutable source or authored content");
    std::cout<<"PASS All five simulated C++ response headers, real menus, same-HWND fullscreen, isolated reset, Enter, order/holes/drag and source preservation\n";
}

void lesson_original_math_auto_policy() {
    Fixture fixture({},[](const curriculum::Lesson&){
        auto lesson=windows::parse_lesson_json(R"json({"id":"qa_math_original_auto","title":"6 × 100 = 600","pages":[{"blocks":[
            {"type":"heading","text":"6 × 100 = 600"},
            {"type":"paragraph","text":"6 × 100 = 600"},
            {"type":"note","text":"6 × 100 = 600"},
            {"type":"paragraph","text":"6 × 100 = 600","mathMode":"off"}
        ]}]})json");
        lesson.source_path=std::filesystem::path("math_physics")/"qa_math_original_auto.json";return lesson;
    });
    fixture.close();windows::NarrationWorker backend(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
    std::size_t calls=0;LessonViewHooks hooks;hooks.math_editor_request=[&](std::string_view request){++calls;return backend.math_editor(request);};
    fixture.view=create_lesson_view(fixture.host,fixture.environment,fixture.lesson,std::move(hooks));
    RECT area{};GetClientRect(fixture.host,&area);MoveWindow(fixture.view,0,0,area.right,area.bottom,TRUE);pump(fixture.host);
    require(calls>=4,"Lesson math prose did not delegate its Auto ranges to the original Android renderer");
    for(const auto target:{fixture.control(990),fixture.control(0,0),fixture.control(1,0),fixture.control(2,0)}){
        const auto mapping=native_math_text_mapping(target,"6 × 100 = 600");
        require(mapping&&mapping->source=="6 × 100 = 600"&&mapping->source_to_native.size()==wide(mapping->source).size()+1,
            "A math title, heading, paragraph or note lost the original Auto source-to-native projection");
    }
    require(!native_math_text_mapping(fixture.control(3,0),"6 × 100 = 600")&&fixture.text(3,0)=="6 × 100 = 600",
        "An explicit mathMode off did not preserve the authored literal text");
    for(const auto& region:lesson_reading_regions(fixture.view))if(region.block_index<4&&region.source_text!="Nota")
        require(region.source_text=="6 × 100 = 600","Rendering implicit formulas rewrote the original narration source");
    std::cout<<"PASS lesson titles, headings, paragraphs and notes use original MathAuto with exact source projection and explicit off\n";
}

void fixed_programming_header_and_spoken_labels() {
    for(const auto locale:{std::string("es"),std::string("en"),std::string("fr")}){
        Fixture fixture([&](state::StateData& data,const curriculum::Lesson&){data.preferences["native_windows"]["ui_language"]=locale;},
            [&](const curriculum::Lesson&){
                auto lesson=windows::parse_lesson_json(R"json({"id":"qa_fixed_code_header","title":"Cabeceras","pages":[{"blocks":[
                    {"type":"output_prediction","question":"Predice.","code":"std::cout << 2;","answers":["2\n3"]},
                    {"type":"code","language":"cpp","code":"return 0;"},
                    {"type":"cpp_compiler","initialCode":"int main() {}"},
                    {"type":"multiple_choice","question":"Elige.","options":["A","B"],"correctIndex":0}
                ]}]})json");
                lesson.locale=locale;
                for(int line=0;line<60;++line)lesson.pages[0].blocks[0].code+="\nstd::cout << 2;";
                return lesson;
            });
        const auto regions=lesson_reading_regions(fixture.view);
        const std::array<std::string,3> labels=locale=="es"?std::array<std::string,3>{"Ejercicio de código C más más.","Ejemplo de código C más más.","Editor de código C más más."}
            :locale=="en"?std::array<std::string,3>{"Code exercise in C plus plus.","Example of C plus plus code.","C plus plus code editor."}
            :std::array<std::string,3>{"Exercice de code C plus plus.","Exemple de code C plus plus.","Éditeur de code C plus plus."};
        for(std::size_t i=0;i<labels.size();++i)require(std::ranges::any_of(regions,[&](const auto& region){
            return region.target==fixture.control(i,16)&&region.source_text==labels[i]&&region.passive_label;
        }),"An original Android code heading was omitted, mistranslated or given a fabricated word map");
        require(std::ranges::none_of(regions,[&](const auto& region){return region.target==fixture.control(0,1)||region.target==fixture.control(1,1)||region.target==fixture.control(2,10)||region.target==fixture.control(3,16);}),
            "Heading narration exposed code or added a non-Android multiple-choice heading");
        const auto old=pre_caption_lesson_reading_regions(fixture.view);
        require(std::ranges::none_of(old,[](const auto& region){return region.passive_label;})&&old.size()+3==regions.size(),
            "The exact previous caption-free script cannot be reconstructed for existing bookmarks");
        const auto source=fixture.control(0,1),answer=fixture.control(0,2),caption=fixture.control(0,16),pane=GetParent(source);
        const auto answer_style=GetWindowLongPtrW(answer,GWL_STYLE)&ES_AUTOHSCROLL;
        SetFocus(answer);SendMessageW(answer,EM_SETSEL,0,0);fixture.click(0,35);
        require((GetWindowLongPtrW(answer,GWL_STYLE)&ES_AUTOHSCROLL)==answer_style&&!(GetWindowLongPtrW(source,GWL_STYLE)&ES_AUTOHSCROLL),
            "Wrapping source code changed the independent literal output answer's line policy");
        require(GetFocus()==answer,"The direct source-wrap icon stole focus from the existing answer editor");
        fixture.click(0,35);require(GetFocus()==answer,"Restoring direct wrap moved focus to a toolbar button");
        const auto script=reading_script(fixture.view);fixture.click(0,13);
        require(GetParent(caption)!=pane&&fixture.control(0,1)==source&&fixture.control(0,2)==answer&&GetFocus()==answer,
            "Fullscreen recreated a code/answer HWND or lost its existing editor focus");
        RECT before{},after{},view_bounds{};GetWindowRect(caption,&before);GetWindowRect(fixture.view,&view_bounds);
        require(before.top==view_bounds.top&&!descendant(fixture.view,116),
            "The expanded code header retained an empty exit strip or an unnecessary X button");
        SendMessageW(fixture.view,WM_VSCROLL,SB_BOTTOM,0);pump(fixture.host);GetWindowRect(caption,&after);
        SCROLLINFO scroll{sizeof(scroll),SIF_POS};GetScrollInfo(pane,SB_VERT,&scroll);
        require(scroll.nPos>0&&before.top==after.top&&before.bottom==after.bottom&&IsWindowVisible(caption)&&GetFocus()==answer,
            "Scrolling a long fullscreen exercise moved the fixed source header or stole editing focus");
        require(reading_script(fixture.view)==script,"Fixing the fullscreen header changed narration source/position identity");
        fixture.escape();require(GetParent(caption)==pane&&fixture.control(0,1)==source&&fixture.control(0,2)==answer&&GetFocus()==answer,
            "Closing fullscreen did not restore the original header parent and focused editor");
    }
    std::cout<<"PASS fixed fullscreen programming headers, original ES/EN/FR spoken code labels, excluded source and independent prediction wrap policy\n";
}

void simulated_code_and_choice_cards() {
    Fixture fixture({},[](const curriculum::Lesson&){
        auto model=windows::parse_lesson_json(R"json({"id":"qa_simulated_cards","title":"Tarjetas de ejemplo y respuesta","pages":[{"blocks":[
            {"type":"code","language":"cpp","code":"#include <iostream>\nint main() {\n    std::cout << \"Zona inicial\";\n}\n"},
            {"type":"multiple_choice","id":"qa_choice","question":"¿Qué opción respeta el objetivo?","options":["Primera opción visible.","Segunda opción visible."],"correctIndex":1,"explanation":"Explicación original.","reviewFeedback":"Guía original."},
            {"type":"cpp_compiler","initialCode":"int main() { return 0; }\n"}
            ]}]})json");
        for(unsigned line=0;line<60;++line)model.pages[0].blocks[0].code+="// Línea del ejemplo para comprobar el desplazamiento.\n";
        return model;
    });
    const auto code=fixture.control(0,1),first=fixture.control(1,20),second=fixture.control(1,22),editor=fixture.control(2,10);
    const auto original=fixture.lesson.pages[0].blocks[0].code,raw=fixture.lesson.raw_json;
    require(fixture.text(0,16)=="</> C++"&&fixture.text(1,16)=="</> C++"&&fixture.text(0,1)==original,
            "Simulated card headers changed source text or omitted their original language label");
    require(!descendant(fixture.view,id(0,12))&&accessible_name(fixture.control(0,18))=="Copiar código"&&
            accessible_name(fixture.control(1,18))=="Copiar ejercicio"&&!IsWindowEnabled(fixture.control(1,12)),
            "Immutable code gained a fake reset or the exercise header lost its real copy/reset contract");
    const auto original_regions=lesson_reading_regions(fixture.view);
    require(std::ranges::any_of(original_regions,[&](const auto& region){return region.target==fixture.control(1,21);})&&
            std::ranges::none_of(original_regions,[&](const auto& region){return region.target==code||region.target==fixture.control(1,16);}),
            "Card decoration changed narratable answer targets or started narrating console code/buttons");
    {
        const auto pane=GetParent(code);const auto screen=GetDC(pane),memory=CreateCompatibleDC(screen);RECT client{};GetClientRect(pane,&client);
        const auto bitmap=CreateCompatibleBitmap(screen,client.right,client.bottom);ReleaseDC(pane,screen);
        require(memory&&bitmap,"Create an isolated native card drawing surface");const auto previous=SelectObject(memory,bitmap);
        SendMessageW(pane,WM_ERASEBKGND,reinterpret_cast<WPARAM>(memory),0);
        const auto sample=[&](HWND target,int inset){RECT bounds{};GetWindowRect(target,&bounds);MapWindowPoints(nullptr,pane,reinterpret_cast<POINT*>(&bounds),2);
            return GetPixel(memory,bounds.left-MulDiv(inset,static_cast<int>(GetDpiForWindow(pane)),96),bounds.top+2);};
        const auto header=sample(fixture.control(0,16),7),body=sample(code,9);
        SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);
        const auto colors=reader_theme::palette(pane);
        require(header==reader_theme::background(fixture.control(0,16))&&body==colors.card,"The simulated code card did not paint an actual header and body around its unchanged controls");
    }
    require(!descendant(fixture.view,id(0,19))&&!descendant(fixture.view,id(2,19))&&
            accessible_name(fixture.control(0,35))=="Ajuste de línea desactivado"&&
            accessible_name(fixture.control(2,35))=="Ajuste de línea desactivado",
            "Readonly code and compiler must expose Android's direct wrap action, without an added settings button");
    require(windows::code_gutter_snapshot(code).active&&windows::code_gutter_snapshot(code).logical_lines==1+static_cast<std::size_t>(std::ranges::count(original,'\n')),
        "Readonly source does not reuse the actual logical-line number gutter");
    fixture.click(0,35);
    require(!(GetWindowLongPtrW(code,GWL_STYLE)&ES_AUTOHSCROLL)&&
            !(GetWindowLongPtrW(editor,GWL_STYLE)&ES_AUTOHSCROLL)&&
            accessible_name(fixture.control(2,35))=="Ajuste de línea activado"&&fixture.text(0,1)==original,
            "Direct wrap did not update the actual source surfaces and every course toolbar together");
    fixture.click(2,35);
    const auto script_before_fullscreen=reading_script(fixture.view);
    const auto pane=GetParent(code);SendMessageW(fixture.view,WM_VSCROLL,SB_PAGEDOWN,0);pump(fixture.host);
    SCROLLINFO original_scroll{sizeof(original_scroll),SIF_POS};GetScrollInfo(pane,SB_VERT,&original_scroll);
    RECT original_code_bounds{};GetWindowRect(code,&original_code_bounds);
    require(original_scroll.nPos>0,"Readonly fullscreen regression did not begin below the lesson top");
    fixture.click(0,20);
    require(fixture.control(0,1)==code&&fixture.text(0,1)==original&&!(GetWindowLongPtrW(first,GWL_STYLE)&WS_VISIBLE)&&
            reading_script(fixture.view)==script_before_fullscreen&&(GetWindowLongPtrW(code,GWL_STYLE)&WS_VSCROLL),
            "Readonly fullscreen recreated code, changed the page's real narration script or has no internal scrolling");
    int notifications=0;LessonViewHooks events;events.content_changed=[&](HWND){++notifications;};
    set_lesson_view_hooks(fixture.view,std::move(events));
    require(notifications>0&&reading_script(fixture.view)==script_before_fullscreen,
        "A later content notification during fullscreen lost the page narration projection");
    require(fixture.fullscreen()&&!descendant(fixture.view,116),"Fullscreen retained the page toolbar or added an exit X");
    int before_n=0,before_d=0,after_n=0,after_d=0;
    SendMessageW(code,EM_GETZOOM,reinterpret_cast<WPARAM>(&before_n),reinterpret_cast<LPARAM>(&before_d));
    SendMessageW(code,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(40,20));pump(fixture.host);
    SendMessageW(code,EM_GETZOOM,reinterpret_cast<WPARAM>(&after_n),reinterpret_cast<LPARAM>(&after_d));
    const auto base=before_d?MulDiv(before_n,100,before_d):100;
    require(after_d&&MulDiv(after_n,100,after_d)==2*base&&fixture.text(0,1)==original&&fixture.fullscreen(),
        "Double click failed to zoom the existing readonly code or incorrectly left fullscreen");
    SendMessageW(code,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(40,20));pump(fixture.host);
    SendMessageW(code,EM_GETZOOM,reinterpret_cast<WPARAM>(&after_n),reinterpret_cast<LPARAM>(&after_d));
    require(after_d&&MulDiv(after_n,100,after_d)==base,"Second double click did not restore the original code zoom");
    fixture.escape();
    SCROLLINFO restored_scroll{sizeof(restored_scroll),SIF_POS};GetScrollInfo(pane,SB_VERT,&restored_scroll);
    RECT restored_code_bounds{};GetWindowRect(code,&restored_code_bounds);
    require(restored_scroll.nPos==original_scroll.nPos&&EqualRect(&restored_code_bounds,&original_code_bounds)&&
        reading_script(fixture.view)==script_before_fullscreen,"Leaving fullscreen lost the exact normal-page offset or changed its narration script");
    require(fixture.control(0,1)==code&&fixture.control(1,20)==first&&(GetWindowLongPtrW(first,GWL_STYLE)&WS_VISIBLE),
            "Leaving simulated-code fullscreen replaced controls or lost the exercise");
    const auto source=fixture.text(2,10);fixture.replace(2,10,source+"// borrador que debe conservarse\n");const auto draft=fixture.text(2,10);
    const auto choice_text=fixture.control(1,21);
    SendMessageW(choice_text,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(45,12));SendMessageW(choice_text,WM_LBUTTONUP,0,MAKELPARAM(45,12));pump(fixture.host);
    CHARRANGE choice_selection{};SendMessageW(choice_text,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&choice_selection));
    require(SendMessageW(first,BM_GETCHECK,0,0)==BST_CHECKED&&choice_selection.cpMin==choice_selection.cpMax&&GetFocus()==first,
        "Clicking the actual option text selected text instead of choosing its radio and preserving keyboard focus");
    require(IsWindowEnabled(fixture.control(1,12)),"Selecting an option did not enable its header reset");
    fixture.click(1,3);require(fixture.text(1,4).starts_with("✗ Incorrecto"),"The real incorrect-choice path did not execute");
    const auto after_wrong=read_lesson_state(fixture.view);
    require(state::find(after_wrong,"attempt_reserve_seconds_test","reserve_programming")&&std::get<double>(*state::find(after_wrong,"attempt_reserve_seconds_test","reserve_programming"))<8.,"An evaluated wrong puzzle did not consume a route attempt");
    fixture.click(1,3);
    const auto repeated=read_lesson_state(fixture.view);
    require(*state::find(repeated,"attempt_reserve_seconds_test","reserve_programming")==*state::find(after_wrong,"attempt_reserve_seconds_test","reserve_programming"),
        "Repeating the same wrong answer consumed another attempt");
    fixture.click(1,12);
    require(fixture.text(1,4).empty()&&!SendMessageW(first,BM_GETCHECK,0,0)&&!IsWindowEnabled(fixture.control(1,12))&&
            fixture.control(2,10)==editor&&fixture.text(2,10)==draft,"Header reset changed another editor or retained a stale wrong attempt");
    fixture.click(1,22);fixture.click(1,3);require(fixture.text(1,4).starts_with("✓ Correcto")&&!IsWindowEnabled(second),"Correct choice did not retain the existing lock/verdict policy");
    fixture.click(1,12);require(IsWindowEnabled(first)&&IsWindowEnabled(second)&&fixture.text(1,4).empty(),"Header reset did not reuse the ordinary correct-answer reset policy");
    SendMessageW(editor,WM_UNDO,0,0);pump(fixture.host);require(fixture.text(2,10)==source,"Resetting a simulated puzzle destroyed an independent editor's Undo history");
    fixture.non_overlapping_rows();fixture.scroll_repaint();fixture.reopen();
    require(fixture.text(0,1)==original&&!IsWindowEnabled(fixture.control(1,12))&&fixture.lesson.raw_json==raw,
            "Reopening simulated cards changed curriculum, restored a stale attempt or fabricated a resettable immutable program");
    std::cout<<"PASS Simulated code/choice cards, native popup, exact text/reading targets, fullscreen, wrong/correct reset and independent editor history\n";
}
void code_hole_background_gestures(){
    Fixture fixture({},[](const curriculum::Lesson&){
        auto model=windows::parse_lesson_json(R"({"id":"qa_code_hole_gestures","title":"Gestos.","pages":[{"blocks":[{"type":"inline_fill_code","question":"Completa el operador.","template":"std::cout [] mensaje;","answers":["<<"]},{"type":"paragraph","text":"Lectura posterior."}]},{"blocks":[{"type":"heading","text":"Página siguiente."}]}]})");
        model.pages[0].blocks[0].code_template="std::cout [] \""+std::string(160,'x')+"\";";
        for(unsigned i=0;i<35;++i)model.pages[0].blocks[1].text+="\n\nUn párrafo adicional para desplazar la lección.";
        return model;
    });
    const auto host=fixture.control(0,5),field=GetDlgItem(host,100),pane=GetParent(host);
    const auto original_json=fixture.lesson.raw_json;
    SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"<<"));pump(fixture.host);fixture.click(0,3);
    require(lesson_page_state(fixture.view).can_next,"Background gesture regression did not solve its real inline exercise");
    RECT area{};GetClientRect(host,&area);const POINT blank{area.right-12,area.bottom-3};
    fixture.drag_control(host,blank,{-160,0});
    SCROLLINFO horizontal{sizeof(horizontal),SIF_POS};GetScrollInfo(host,SB_HORZ,&horizontal);
    require(fixture.page()==0&&horizontal.nPos>0&&edit_text(field)=="<<",
        "An overflowing code-hole background changed page instead of scrolling its own horizontal axis");
    fixture.drag_control(host,blank,{0,-100});
    SCROLLINFO vertical{sizeof(vertical),SIF_POS};GetScrollInfo(pane,SB_VERT,&vertical);
    require(vertical.nPos>0&&fixture.page()==0,"A code-hole background with no internal vertical scroll failed to hand that axis to the lesson");
    SendMessageW(fixture.view,WM_VSCROLL,SB_TOP,0);pump(fixture.host);fixture.click(0,35);
    GetClientRect(host,&area);
    fixture.drag_control(host,{area.right-12,area.bottom-3},{-180,0});
    require(fixture.page()==1&&fixture.lesson.raw_json==original_json,
        "A wrapped code-hole background with no horizontal overflow did not reuse normal gated page navigation");
    fixture.select_page(0);require(GetDlgItem(fixture.control(0,5),100)&&edit_text(GetDlgItem(fixture.control(0,5),100))=="<<",
        "Delegated code-hole background gestures lost the existing answer");
    std::cout<<"PASS real inline background gesture arbitration: own horizontal overflow, lesson vertical scrolling, gated page swipe and unchanged answers\n";
}
void edited_composition_owns_enter(){
    Fixture fixture({},[](const curriculum::Lesson&){
        return windows::parse_lesson_json(R"({"id":"qa_edited_composition_enter","title":"Comprobar la edición actual.","pages":[{"blocks":[{"type":"multiple_choice","question":"Elige la segunda.","options":["Primera.","Segunda."],"correctIndex":1},{"type":"inline_fill_code","question":"Completa el operador.","template":"std::cout [] 6;","answers":["<<"]}]}]})");
    });
    fixture.click(0,20);
    const auto field=GetDlgItem(fixture.control(1,5),100);require(field,"The current inline composition field is missing");
    SetFocus(field);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"<<"));pump(fixture.host);
    MSG key{field,WM_KEYDOWN,VK_RETURN,1};require(dispatch_lesson_enter(key),"Enter did not route the current inline composition");pump(fixture.host);
    key={field,WM_KEYUP,VK_RETURN,LPARAM{1}<<31};(void)dispatch_lesson_enter(key);pump(fixture.host);
    require(fixture.text(1,4).starts_with("✓ Correcto")&&fixture.text(0,4).empty()&&fixture.page()==0,
        "Enter after editing an inline field checked a previously selected puzzle or advanced the page");
    const auto saved=fixture.saved();const auto reserve=state::find(saved,"attempt_reserve_seconds_test","reserve_programming");
    require(!reserve||std::get<double>(*reserve)==8.,
        "Enter consumed a heart by evaluating the old wrong selection instead of the current edited composition");
    std::cout<<"PASS an edited inline composition owns Enter over a previous prepared option without evaluating it or consuming a heart\n";
}
void incomplete_puzzles_do_not_consume_attempts(){
    Fixture f({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({"id":"qa_incomplete_cpp_inputs","title":"Respuestas completas.","pages":[{"blocks":[
        {"type":"fill_code","question":"Completa el operador.","code":"std::cout ___ 1;","answers":["<<"]},
        {"type":"output_prediction","question":"Predice la salida.","code":"std::cout << \"Listo\";","answers":["Listo"]},
        {"type":"inline_fill_code","question":"Completa ambos huecos.","template":"std::cout [] << [];","answers":["value","2"]},
        {"type":"drag_fill_code","question":"Completa ambas posiciones.","template":"int [](){ return 0[] }","choices":["main",";"],"correctOrder":[0,1]},
        {"type":"output_prediction","question":"Conserva los blancos literales.","code":"std::cout << \" Listo \\t \";","answers":[" Listo \t "],"outputComparison":"exact"},
        {"type":"fill_code","question":"Completa el literal conservando blancos.","code":"const char* s = \"___\";","answers":[" valor \t "]},
        {"type":"multiple_choice","question":"Elige una opción.","options":["Primera.","Segunda."],"correctIndex":0}
        ]}]})json");});
    const auto queued=[&](std::size_t index,bool incomplete=true){const auto previous_focus=GetFocus();
        SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(id(index,3),BN_CLICKED),0);pump(f.host);
        struct Notice {std::wstring expected;HWND window=nullptr;} notice{
            index==3?L"Completa todos los espacios primero.":index==6?L"Selecciona una respuesta.":L"Completa todos los campos de respuesta."};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM raw)->BOOL{auto& target=*reinterpret_cast<Notice*>(raw);
            if(window_text(candidate)==utf8(target.expected)){target.window=candidate;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&notice));
        if(incomplete){require(notice.window&&(GetWindowLongPtrW(notice.window,GWL_EXSTYLE)&WS_EX_NOACTIVATE)&&GetFocus()==previous_focus,
                "An incomplete answer omitted its original nonblocking notice or stole editor focus");DestroyWindow(notice.window);}
        else require(!notice.window,"A valid literal whitespace answer incorrectly produced an incomplete-answer notice");
    };
    for(const auto index:{0u,1u,2u,3u,6u}){
        require(!IsWindowEnabled(f.control(index,3)),"An untouched programming response exposes an enabled Check action");
        const auto before=read_lesson_state(f.view);const auto script=reading_script(f.view);queued(index);
        require(read_lesson_state(f.view)==before&&reading_script(f.view)==script&&f.text(index,4).empty(),
            "A queued incomplete answer changed progress, narration feedback or the attempt reserve");
    }
    for(const auto index:{0u,1u}){
        f.replace(index,2," \t ");const auto before=read_lesson_state(f.view);queued(index);
        require(read_lesson_state(f.view)==before&&f.text(index,4).empty(),"An unaccepted blank answer was charged or marked reviewed");
        if(index==1)require(!IsWindowEnabled(f.control(index,3)),"An all-blank ordinary prediction enabled Check");
    }
    const auto inline_host=f.control(2,5),first_hole=GetDlgItem(inline_host,100);
    SendMessageW(first_hole,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"value"));pump(f.host);
    require(IsWindowEnabled(f.control(2,3)),"Partial inline input cannot request completeness guidance");
    auto before=read_lesson_state(f.view);queued(2);
    require(read_lesson_state(f.view)==before&&f.text(2,4).empty(),"A partially filled inline puzzle consumed a heart or published incorrect feedback");
    const auto drag_host=f.control(3,5),first_slot=GetDlgItem(drag_host,100);
    SendMessageW(GetDlgItem(GetDlgItem(drag_host,3000),2000),BM_CLICK,0,0);pump(f.host);
    require(first_slot&&!edit_text(first_slot).empty()&&edit_text(first_slot)!="…"&&IsWindowEnabled(f.control(3,3)),"A real first-card placement did not fill its existing slot or enable Check");
    before=read_lesson_state(f.view);queued(3);
    require(read_lesson_state(f.view)==before&&f.text(3,4).empty(),"A partially filled drag puzzle consumed a heart or published incorrect feedback");
    for(const auto index:{4u,5u}){
        f.replace(index,2,index==4?" Listo \t ":" valor \t ");require(IsWindowEnabled(f.control(index,3)),"An explicitly authored answer with significant boundary whitespace was disabled");
        queued(index,false);require(f.text(index,4).starts_with("✓ Correcto"),"Exact/literal boundary whitespace ceased to match its valid authored answer");
    }
    const auto final_state=read_lesson_state(f.view);const auto reserve=state::find(final_state,"attempt_reserve_seconds_test","reserve_programming");
    require(!reserve||std::get<double>(*reserve)==8.,
        "Completeness checks or accepted literal blanks reduced the route attempt reserve");
    std::cout<<"PASS incomplete and queued checks retain progress/attempts/script while authored exact/literal boundary whitespace stays valid\n";
}

void physical_enter_and_attempt_fill(){
    Fixture f({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({"id":"qa_physical_enter_parity","title":"Entrada y comprobación","pages":[{"blocks":[
        {"type":"multiple_choice","question":"Selecciona una opción.","options":["Una respuesta que debe ocupar varias líneas en una ventana estrecha sin desplazar el círculo hacia el centro de todo el párrafo.","La otra respuesta."],"correctIndex":0},
        {"type":"fill_code","question":"Completa el operador.","answers":["<<"]},
        {"type":"cpp_compiler","initialCode":"int main() { return 0; }\n","initialStandardInput":"dato\n"}
        ]},{"blocks":[{"type":"paragraph","text":"Página siguiente."}]}]})json");});
    const auto raw=f.lesson.raw_json;
    const auto physical=[&](HWND target,bool repeat=false){MSG down{target,WM_KEYDOWN,VK_RETURN,repeat?(LPARAM{1}<<30):1};return dispatch_lesson_enter(down);};
    const auto up=[&](HWND target){MSG message{target,WM_KEYUP,VK_RETURN,LPARAM{1}<<31};return dispatch_lesson_enter(message);};
    const auto editor=f.control(2,10),input=f.control(2,15);SetFocus(editor);
    require(GetFocus()==editor&&!physical(editor),"The global Enter gate took a newline away from the actual C++ editor");
    SetFocus(input);require(GetFocus()==input&&!physical(input),"The global Enter gate submitted stdin instead of preserving its multiline editing");
    const auto label=f.control(0,21),radio=f.control(0,20);
    SendMessageW(label,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,12));SendMessageW(label,WM_LBUTTONUP,0,MAKELPARAM(40,12));pump(f.host);
    const auto newline=[&](HWND target){SetFocus(target);const auto before=edit_text(target);SendMessageW(target,EM_SETSEL,static_cast<WPARAM>(-1),-1);
        require(!physical(target),"A stale selected puzzle stole Enter after an explicit click/focus in a multiline editor");
        SendMessageW(target,WM_KEYDOWN,VK_RETURN,1);SendMessageW(target,WM_CHAR,L'\r',1);SendMessageW(target,WM_KEYUP,VK_RETURN,1);pump(f.host);
        require(edit_text(target)==before+'\n'&&f.text(0,4).empty()&&f.page()==0&&GetFocus()==target,
            "Enter after selecting another puzzle failed to insert exactly one newline, moved focus, checked or navigated");};
    newline(editor);newline(input);
    SendMessageW(label,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,12));SendMessageW(label,WM_LBUTTONUP,0,MAKELPARAM(40,12));pump(f.host);
    require(GetFocus()==radio&&physical(radio),"A selected actual choice did not own physical Enter");pump(f.host);
    require(f.text(0,4).starts_with("✓ Correcto")&&f.page()==0,"Enter did not check the deliberately selected choice exactly once");
    SetFocus(nullptr); // Disabling a solved native answer can release focus.
    require(physical(f.view,true)&&up(f.view)&&f.page()==0,"Held Enter or its UP checked or advanced another activity");
    f.replace(1,2,"<<");f.enter(1);require(f.page()==0&&lesson_page_state(f.view).can_next,"The last answer's first Enter advanced instead of only checking");
    SetFocus(f.view);require(physical(f.view),"A later Enter did not accept the already enabled Continue action");pump(f.host);
    require(f.page()==1&&up(f.view)&&f.lesson.raw_json==raw,"The next physical Enter failed to advance once or changed authored content");
    // Render the actual life icon independently of desktop occlusion. Only
    // glyph pixels are inspected; labels/counts do not affect the assertion.
    const auto button=f.control(113);RECT bounds{};GetClientRect(button,&bounds);
    const auto screen=GetDC(button),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);ReleaseDC(button,screen);
    const auto previous=SelectObject(dc,bitmap);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=113;draw.hwndItem=button;draw.hDC=dc;draw.rcItem=bounds;
    set_lesson_attempts_state(f.view,4,8,.5,false);SendMessageW(f.view,WM_DRAWITEM,113,reinterpret_cast<LPARAM>(&draw));GdiFlush();
    const auto colors=reader_theme::palette(button);if(!colors.high_contrast){
        const auto scale=[&](int n){return MulDiv(n,static_cast<int>(GetDpiForWindow(button)),96);};
        const int side=scale(34),left=(bounds.right-side)/2,top=bounds.bottom-side-scale(12),middle=left+side/2;
        int left_accent=0,right_white=0,right_accent=0;
        for(int y=top;y<top+side;++y)for(int x=left;x<left+side;++x){const auto ink=GetPixel(dc,x,y);
            if(x<middle&&ink==colors.accent)++left_accent;
            if(x>=middle&&ink==RGB(255,255,255))++right_white;
            if(x>=middle&&ink==colors.accent)++right_accent;}
        require(left_accent>0&&right_white>0&&right_accent==0,"Life refill does not paint left-to-right with a white unfilled console");
    }
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    std::cout<<"PASS real physical Enter: editor/stdin exclusions, selected puzzle, held-key suppression, separate advance, and horizontal white-empty life icon\n";
}
void premium_attempt_indicator(){
    struct Raster {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;int width=0,height=0;
        Raster(HWND owner,int w,int h):width(w),height(h){
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            const auto screen=GetDC(owner);dc=CreateCompatibleDC(screen);void* bits=nullptr;
            bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);ReleaseDC(owner,screen);
            if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);throw std::runtime_error("Cannot capture the actual Premium indicator");}
            previous=SelectObject(dc,bitmap);pixels=static_cast<std::uint32_t*>(bits);
        }
        ~Raster(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}
        Raster(const Raster&)=delete;Raster& operator=(const Raster&)=delete;
        std::vector<std::uint32_t> snapshot()const{GdiFlush();return {pixels,pixels+static_cast<std::size_t>(width)*height};}
        COLORREF color(const std::vector<std::uint32_t>& image,int x,int y)const{
            const auto raw=image.at(static_cast<std::size_t>(y)*width+x);return RGB((raw>>16)&255,(raw>>8)&255,raw&255);
        }
    };
    for(const auto awareness:{DPI_AWARENESS_CONTEXT_UNAWARE,DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}){
        struct DpiScope{DPI_AWARENESS_CONTEXT previous;~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}}
            dpi_scope{SetThreadDpiAwarenessContext(awareness)};
        require(dpi_scope.previous!=nullptr,"Cannot exercise the real attempt indicator in both DPI contexts");
        for(const auto route:{"programming","math_physics"}){
            Fixture f({},[&](const curriculum::Lesson&){auto model=windows::parse_lesson_json(R"json({"id":"qa_premium_indicator","title":"Intentos","pages":[{"blocks":[{"type":"paragraph","text":"Indicador de intentos."}]}]})json");
                model.source_path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es"/route/"qa_premium_indicator.json";return model;});
            int opened=0;LessonViewHooks hooks;hooks.attempts_requested=[&](HWND){++opened;};set_lesson_view_hooks(f.view,std::move(hooks));
            const auto before=read_lesson_state(f.view);const auto button=f.control(113);RECT bounds{};GetClientRect(button,&bounds);
            const auto scale=[&](int n){return MulDiv(n,static_cast<int>(GetDpiForWindow(button)),96);};
            Raster raster(button,bounds.right,bounds.bottom);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=113;
            draw.hwndItem=button;draw.hDC=raster.dc;draw.rcItem=bounds;
            const auto paint=[&](double progress,bool premium){set_lesson_attempts_state(f.view,4,8,progress,premium);
                SendMessageW(f.view,WM_DRAWITEM,113,reinterpret_cast<LPARAM>(&draw));return raster.snapshot();};
            const auto free_half=paint(.5,false),free_full=paint(1.,false),premium=paint(.25,true);
            const auto colors=reader_theme::palette(button);
            const int side=scale(34),left=(bounds.right-side)/2,top=bounds.bottom-side-scale(12);
            int accent=0,free_percentage=0;
            const auto background=reader_theme::background(button);
            for(int y=top;y<top+side;++y)for(int x=left;x<left+side;++x){
                require(raster.color(premium,x,y)==raster.color(free_full,x,y),
                    "Premium changed the original route glyph/tint or left it partially empty");
                if(raster.color(premium,x,y)==colors.accent)++accent;
            }
            require(accent>0,"Premium lost its original Programming/Mathematics accent fill");
            for(int y=0;y<scale(15);++y)for(int x=bounds.right/2;x<bounds.right;++x){
                require(raster.color(premium,x,y)==background,"Premium still paints the recovery percentage beside infinity");
                if(raster.color(free_half,x,y)!=background)++free_percentage;
            }
            require(free_percentage>0,"The free indicator lost its visible recovery percentage");
            require(paint(.5,false)==free_half,"Returning to free mode changed the original count, percentage or refill drawing");

            // Read the original vector through the production renderer at
            // 100% and 225% raster sizes; sample interiors of its actual path.
            const std::array<std::pair<int,int>,6> points{{{220,280},{80,348},{40,445},{40,502},{80,603},{220,680}}};
            const std::array<double,6> offsets{0.,.20,.42,.55,.78,1.};
            const std::array<COLORREF,6> stops{RGB(118,80,31),RGB(183,134,54),RGB(255,240,187),RGB(225,182,95),RGB(163,115,45),RGB(98,64,22)};
            for(const int resolution:{384,864}){
                Raster vector(button,resolution,resolution);const RECT rectangle{0,0,resolution,resolution};
                FillRect(vector.dc,&rectangle,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                const auto contrast_ink=RGB(19,231,217);draw_icon(vector.dc,rectangle,Icon::infinity_premium,contrast_ink);
                const auto image=vector.snapshot();
                for(const auto [source_x,source_y]:points){
                    const int x=source_x*resolution/960,y=source_y*resolution/960;const auto actual=vector.color(image,x,y);
                    if(colors.high_contrast){require(actual==contrast_ink,"Premium ignored the actual system high-contrast icon tint");continue;}
                    const double t=((y+.5)*960/resolution-260)/440;
                    const auto end=static_cast<std::size_t>(std::upper_bound(offsets.begin(),offsets.end(),t)-offsets.begin());
                    require(end>0&&end<stops.size(),"Original gradient sample is outside the drawable stops");
                    const auto start=end-1;const double fraction=(t-offsets[start])/(offsets[end]-offsets[start]);
                    const auto channel=[&](int a,int b){return static_cast<int>(std::lround(a+(b-a)*fraction));};
                    const auto expected=RGB(channel(GetRValue(stops[start]),GetRValue(stops[end])),channel(GetGValue(stops[start]),GetGValue(stops[end])),channel(GetBValue(stops[start]),GetBValue(stops[end])));
                    require(std::abs(static_cast<int>(GetRValue(actual))-GetRValue(expected))<=5&&
                        std::abs(static_cast<int>(GetGValue(actual))-GetGValue(expected))<=5&&
                        std::abs(static_cast<int>(GetBValue(actual))-GetBValue(expected))<=5,
                        "Premium infinity differs from the original six-stop Android gold gradient");
                }
            }
            (void)paint(.25,true);f.click(113);require(opened==0,"Premium still opens the free-life recovery panel");
            (void)paint(.5,false);f.click(113);require(opened==1,"The free-life recovery panel no longer opens");
            require(read_lesson_state(f.view)==before,"Rendering Premium through the existing indicator API persisted an entitlement or changed student data");
        }
    }
    std::cout<<"PASS Programming/Math free and Premium indicators: original full route accent, no Premium percentage, XML six-stop infinity gradient, DPI/contrast and free-panel semantics\n";
}
}

int main(int argc,char* argv[]) {
    const bool caret_only=argc==2&&std::string_view(argv[1])=="--caret";
    if(argc>1&&!caret_only)return 2;
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    struct OleScope{~OleScope(){OleUninitialize();}} apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr); host.lpszClassName = L"LgaedLessonViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        if(caret_only){simulated_exercise_code_cards(true);native_answer_end_viewport();return 0;}
        long_prediction_cards_keep_independent_bounds();
        long_lesson_bands_keep_independent_hit_targets();
        initial_readonly_source_layout();
        native_page_progress_theme();
        simulated_exercise_code_cards();
        lesson_original_math_auto_policy();
        fixed_programming_header_and_spoken_labels();
        simulated_code_and_choice_cards();
        code_hole_background_gestures();
        edited_composition_owns_enter();
        incomplete_puzzles_do_not_consume_attempts();
        physical_enter_and_attempt_fill();
        premium_attempt_indicator();
        native_solved_answer_style();
        native_answer_end_viewport();
        native_compiler_editor_overflow_bars();
        native_compiler_output_overflow_bars();
        lesson_interface_languages();
        laboratory_interface_parity();
        back_navigation_tests();
        mathematical_feedback_paragraph_spacing();
        Fixture view;
        require(view.page() == 0 && !lesson_page_state(view.view).can_next, "A fresh profile bypassed initial page exercises");
        view.close();
        {
            state::StateStore untouched(view.environment.paths.root);
            require(untouched.load()==view.initial_data && state::can_import_android_profile(untouched.load()),
                "Closing an untouched first page changed the UI-only profile or made it ineligible for import");
        }
        view.open();
        require(view.page() == 0 && !lesson_page_state(view.view).can_next, "An untouched reopen changed the initial bookmark or exercise gate");
        {
            state::StateStore untouched(view.environment.paths.root);
            require(untouched.load()==view.initial_data && state::can_import_android_profile(untouched.load()),
                "Reopening an untouched profile published personal state or changed its explicit UI preference");
        }
        view.viewport_layout(); view.scroll_repaint();
        require(character_format(descendant(view.view,990)).yHeight==480,
            "The lesson title did not retain its native 32sp hierarchy");
        for(std::size_t index=0;index<view.lesson.pages.front().blocks.size();++index){
            const auto& item=view.lesson.pages.front().blocks[index];
            if(item.type==BlockType::paragraph)
                require(character_format(view.control(index,0)).yHeight==270,"Lesson prose remains below the readable 18sp base");
            else if(item.type==BlockType::heading||!item.question.empty())
                require(character_format(view.control(index,0)).yHeight==345,"A question/heading lost its distinct 23sp size");
        }
        view.gesture_navigation(false);
        {
            const int dpi = static_cast<int>(GetDpiForWindow(view.view));
            MoveWindow(view.view,0,0,MulDiv(640,dpi,96),MulDiv(600,dpi,96),TRUE); pump(view.host);
            view.viewport_layout(); view.non_overlapping_rows();
            RECT host_area{}; GetClientRect(view.host,&host_area);
            MoveWindow(view.view,0,0,host_area.right,host_area.bottom,TRUE); pump(view.host);
        }
        for (std::size_t option = 0; option < view.lesson.pages[0].blocks[4].options.size(); ++option) {
            const auto radio = view.control(4,20 + static_cast<int>(option) * 2);
            require(window_text(radio).empty() && accessible_name(radio) == view.lesson.pages[0].blocks[4].options[option],
                    "A choice paints clipped duplicate text or lost its complete accessible label");
        }
        std::cout << "PASS Wheel repaint damage, stable text/geometry, fixed header/footer and unduplicated accessible choices\n" << std::flush;
        view.non_overlapping_rows(); view.solve(4); view.gesture_navigation(true); view.next();
        std::cout << "PASS Untouched profile remains importable after close/reopen; initial choice controls and forward gate\n" << std::flush;

        const auto& first_prediction = view.lesson.pages[1].blocks[3];
        view.replace(3,2,first_prediction.answers.at(0) + ' '); view.enter(3);
        require(view.page() == 1 && view.text(3,4).starts_with("✗ Incorrecto") && !lesson_page_state(view.view).can_next, "Wrong exact prediction did not stay on its page");
        view.reopen();
        require(view.page() == 1 && view.text(3,4).starts_with("✗ Incorrecto"), "Reviewed incorrect feedback did not reopen");
        view.replace(3,2,first_prediction.answers.at(0));
        require(view.text(3,4).empty(), "Editing retained an obsolete incorrect verdict");
        view.reopen(); require(view.text(3,4).empty(), "Unreviewed edited response reopened as Incorrecto");
        view.enter(3);
        require(view.page() == 1 && view.text(3,4).starts_with("✓ Correcto") && !IsWindowEnabled(view.control(3,2)), "Correct response was not locked or Enter navigated");
        require(window_text(view.control(3,3)) == "Correcto ✓" && !IsWindowEnabled(view.control(3,3)) && IsWindowEnabled(view.control(3,12)), "Correct quiz does not preserve its Android verdict and header reset");
        view.click(3,12);
        require(IsWindowEnabled(view.control(3,2)) && view.text(3,2).empty() && view.text(3,4).empty(), "Reset did not restore an editable unanswered quiz");
        view.solve(3); view.solve(4);
        require(view.page() == 1 && lesson_page_state(view.view).can_next, "Last Enter auto-advanced instead of enabling Next");
        view.next(); std::cout << "PASS Exact prediction, reviewed feedback, single-line Enter and explicit reset\n" << std::flush;

        view.solve(3); view.solve(4); view.solve(5); view.non_overlapping_rows(); view.next();
        view.solve(2); view.solve(3); view.non_overlapping_rows(); view.next();
        require(view.page() == 4, "Six-page sequence did not reach the authored compiler activities");
        view.solve(1);
        require(!lesson_page_state(view.view).can_next, "Page 5 concept question was bypassed");
        require(!descendant(view.view,id(3,15)) && !descendant(view.view,id(6,15)), "Unrequested stdin fields are visible");
        RECT caption{}, reset{}, source{};
        GetWindowRect(view.control(3,16), &caption); GetWindowRect(view.control(3,12), &reset);
        GetWindowRect(view.control(3,10), &source);
        require(caption.top == reset.top && reset.left == caption.right && source.top > caption.bottom,
                "Reset control is not beside C++ as specified by the existing lesson");
        SCROLLINFO wheel_before{sizeof(wheel_before), SIF_POS}, wheel_after = wheel_before;
        const auto pane = GetParent(view.control(3,10)); GetScrollInfo(pane, SB_VERT, &wheel_before);
        SendMessageW(view.control(2,0), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        GetScrollInfo(pane, SB_VERT, &wheel_after);
        require(wheel_after.nPos > wheel_before.nPos, "Wheel over read-only prose did not scroll the lesson");
        const auto& original = view.lesson.pages[4].blocks[3];
        const auto& modified = view.lesson.pages[4].blocks[6];
        require(view.text(3,10) == original.initial_code && view.text(6,10) == modified.initial_code, "Original sources lost indentation or final LF");
        view.contextual_editing(3);
        view.editor_gestures(3);
        view.run(3);
        require(view.text(3,14) == "Zona inicial" && view.text(3,4) == "✓ Correcto", "Original program did not produce/check its fresh exact output");
        require(view.text(3,34)=="Ejecución terminada (código 0).","The real compiler did not expose Android's ordinary execution status");
        require(!(GetWindowLongPtrW(view.control(3,14),GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL)),
                "A short one-line program output reserves native scrollbars without overflow");
        RECT console{},verdict{};GetWindowRect(view.control(3,14),&console);GetWindowRect(view.control(3,4),&verdict);
        require(verdict.top-console.bottom==MulDiv(12,static_cast<int>(GetDpiForWindow(view.view)),96),
                "Compiler output added an extra gap before its ordinary feedback padding");
        view.compiler_menus(3);
        const auto changed_source = message_code(modified,"Zona inicial","Caja recibida");
        view.replace(6,10,changed_source);
        require(view.text(3,10) == original.initial_code, "Modification editor changed the independent original draft");
        view.run(6);
        require(view.text(6,14) == "Caja recibida" && view.text(6,4).starts_with("✓ Correcto\n\n"), "Modified compiler lost its explanation or checked old output");
        std::cout << "PASS Real original and modified C++ executions, integrated checking and independent editors\n" << std::flush;

        view.replace(6,10,message_code(modified,"Zona inicial","Zona segura"));
        require(view.text(6,4).empty(), "Editing retained the preceding correct verdict");
        view.run(6);
        require(view.text(6,4).starts_with("✗ Incorrecto\n\nRevisa el mensaje solicitado"), "Authored targeted output review was not selected");
        view.replace(6,10,message_code(modified,"Zona inicial","Salida desconocida")); view.run(6);
        require(view.text(6,4) == "✗ Incorrecto", "An unknown output invented a pedagogical explanation");
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

        require(view.page() == 5 && !lesson_page_state(view.view).can_next, "Final page navigation escaped the lesson");
        const auto& independent = view.lesson.pages[5].blocks[6];
        view.replace(6,10,message_code(independent,"Caja recibida","Zona segura")); view.run(6);
        require(view.text(6,14) == "Zona segura" && view.text(6,4) == "✓ Correcto", "Page 6 did not keep its independent target");
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
        require(legacy.text(6,14) == "Caja heredada" && legacy.text(6,4) == "✗ Incorrecto",
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
        legacy.close();
        std::cout << "PASS Historical v1 state adoption, partial answers, independent added editor, CRLF/Unicode/caret and unrequested input preservation\n" << std::flush;

        Fixture references([](state::StateData& data,const curriculum::Lesson& lesson) {
            state::prepare_lesson_state(data,lesson); state::save_current_page(data,lesson,5);
            for (const auto index : {std::size_t{1},std::size_t{7}}) {
                const auto& block = lesson.pages[4].blocks[index]; state::Response response; response.correct = true;
                if (block.correct_index) response.selected = static_cast<std::int64_t>(*block.correct_index);
                else response.answer = block.answers.at(0);
                state::put_response(data,curriculum::exercise_key(lesson,block),response);
            }
            // A formerly imported "correct" field is not proof of a fresh compiler run.
            state::Response old_verdict; old_verdict.correct = true; old_verdict.answer = "Zona segura";
            state::put_response(data,curriculum::exercise_key(lesson,lesson.pages[5].blocks[6]),old_verdict);
        },[](const curriculum::Lesson& source) {
            return with_relationships(source,reference_links);
        });
        const auto previous_reference = [&] {
            return descendant(references.view,id(0,0)) && descendant(references.view,id(1,0))
                && descendant(references.view,id(2,1)) && descendant(references.view,id(3,0));
        };
        const auto current_reference = [&] {
            return descendant(references.view,id(7,0)) && descendant(references.view,id(8,1));
        };
        require(references.page() == 5 && !previous_reference() && !current_reference(),
                "A page bookmark, conceptual answer or imported correct field exposed an unattempted implementation");
        SendMessageW(references.view,WM_VSCROLL,SB_BOTTOM,0); pump(references.host);
        require(!descendant(references.view,id(7,0)) && !descendant(references.view,id(8,1)),
                "Scrolling created hidden reference text providers before a correct execution");
        require(references.text(5,0).find("Zona segura") != std::string::npos,
                "The requested output was hidden together with its implementation");
        references.run(6); require(!current_reference(),"An incorrect program output unlocked its reference");
        auto invalid_source = references.lesson.pages[5].blocks[6].initial_code;
        const auto invalid_at = invalid_source.find("return 0;"); invalid_source.erase(invalid_at + 8,1);
        references.replace(6,10,invalid_source); references.run(6);
        require(references.text(6,4).empty() && !current_reference(),"A compiler diagnostic unlocked the implementation");
        const auto accepted = message_code(references.lesson.pages[5].blocks[6],"Caja recibida","Zona segura");
        references.replace(6,10,accepted); references.run(6);
        require(current_reference() && !previous_reference(),"Correct output unlocked the wrong activity's reference");
        require(references.text(8,1) == references.lesson.pages[5].blocks[8].code,
                "The unlocked reference did not retain its exact original code");
        references.reopen();
        require(current_reference() && references.text(6,4).empty(),
                "Reopening lost a sealed completion or revived a current compiler verdict");
        references.replace(6,10,accepted + "// edición después del acierto\n");
        require(!current_reference(),"Editing kept an obsolete reference completion available");
        SendMessageW(references.control(6,10),WM_UNDO,0,0);
        pump(references.host);
        require(references.text(6,10) == accepted && !current_reference(),
                "Undo either lost source history or revived a discarded completion ticket");
        references.run(6); require(current_reference(),"A new correct run did not restore its own reference");
        references.select_page(4); references.run(3); references.next();
        require(!previous_reference() && current_reference(),"The original Zona inicial editor unlocked the Caja recibida reference");
        references.select_page(4);
        references.replace(6,10,message_code(references.lesson.pages[4].blocks[6],"Zona inicial","Caja recibida"));
        references.run(6); references.next();
        require(previous_reference() && current_reference(),"A previous-page correct compiler run did not unlock only its linked references");
        references.reopen();
        require(previous_reference() && current_reference() && references.text(6,4).empty(),
                "Cross-page completion proofs did not reopen independently of current output verdicts");
        references.click(6,12);
        require(previous_reference() && !current_reference(),"Reset of one editor affected another activity's reference");
        references.non_overlapping_rows();
        std::cout << "PASS Explicit reference links: no early HWND/accessibility text, exact fresh-output gates, sealed reopening, edit/Undo/reset invalidation and independent cross-page targets\n"
                  << std::flush;

        const auto solved_compiler_page = [](state::StateData& data,const curriculum::Lesson& lesson) {
            state::prepare_lesson_state(data,lesson); state::save_current_page(data,lesson,4);
            for (const auto index : {std::size_t{1},std::size_t{7}}) {
                const auto& block = lesson.pages[4].blocks[index]; state::Response response; response.correct = true;
                if (block.correct_index) response.selected = static_cast<std::int64_t>(*block.correct_index);
                else response.answer = block.answers.at(0);
                state::put_response(data,curriculum::exercise_key(lesson,block),response);
            }
            // Imported markers/answers cannot impersonate an actual execution.
            state::Response obsolete; obsolete.correct = true; obsolete.answer = "Caja recibida";
            state::put_response(data,curriculum::exercise_key(lesson,lesson.pages[4].blocks[6]),obsolete);
        };
        {
            int contents = 0, verdicts = 0, leaving = 0, destroying = 0;
            Fixture preferences(solved_compiler_page);
            LessonViewHooks hooks; hooks.lesson_label = "Lección QA";
            hooks.content_changed = [&](HWND) { ++contents; };
            hooks.feedback_changed = [&](HWND,std::size_t index,bool correct) {
                require(index == 3 && correct,"Feedback hooks changed activity identity or correctness"); ++verdicts;
            };
            hooks.before_page_change = [&](HWND) { ++leaving; };
            hooks.before_destroy = [&](HWND) { ++destroying; };
            set_lesson_view_hooks(preferences.view,hooks);
            require(contents > 0,"The reader did not receive the initial visible content");
            for (const auto& region : lesson_reading_regions(preferences.view)) {
                require(region.target && !region.source_text.empty(),"A reading region has no original visible source");
                require(region.target != preferences.control(3,10) && region.target != preferences.control(3,14)
                        && region.target != preferences.control(3,11),"Compiler code, console or buttons entered the reading source");
            }
            const auto course=preferences.lesson.id.substr(0,preferences.lesson.id.find("__"));
            const auto scale_source = preferences.text(3,10);
            const auto scale_editor = preferences.control(3,10);
            preferences.replace(3,10,scale_source + "// historial previo a escala y ajuste\n");
            CHARRANGE scale_selection{4,9};
            SendMessageW(scale_editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&scale_selection));
            update_lesson_preferences(preferences.view,[&](state::StateData& data) {
                data.preferences["lesson_narration_settings"]["enabled"] = true;
                data.preferences["lesson_narration_resume_bounded_v1"]["qa_slot"] = std::string("qa");
                data.preferences["native_windows"]["code_wrap"] = true;
                data.preferences["appearance_settings"]["route_accent_blue_programming"] = std::int64_t{123};
                data.preferences["lesson_text_size_settings"]["global_scale"] = std::int64_t{150};
                data.preferences["lesson_text_size_settings"]["course_scale_"+course] = std::int64_t{125};
                data.preferences["lesson_text_size_settings"]["course_use_global_"+course] = false;
                data.preferences["code_wrap_settings"]["global_wrap"] = true;
                data.preferences["code_wrap_settings"]["course_wrap_"+course] = false;
                data.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"] = std::string(R"(["std::cout","<<",";"])" );
            });
            int zoom_numerator=0,zoom_denominator=0;const auto source_control=preferences.control(3,10);
            SendMessageW(source_control,EM_GETZOOM,reinterpret_cast<WPARAM>(&zoom_numerator),reinterpret_cast<LPARAM>(&zoom_denominator));
            require(zoom_denominator>0&&zoom_numerator*100==125*zoom_denominator
                && (GetWindowLongPtrW(source_control,GWL_STYLE)&ES_AUTOHSCROLL),
                "A lesson ignored its original per-course text size or code-wrap override");
            require(character_format(source_control).yHeight==270,
                "The source editor still uses the old small type or a live zoom rewrote its base formatting");
            // A touch in the lower part of an enlarged glyph is still text,
            // not blank editor space that may be handed to page scrolling.
            update_lesson_preferences(preferences.view,[&](state::StateData& data){
                data.preferences["lesson_text_size_settings"]["course_scale_"+course]=std::int64_t{150};
            });
            const bool gesture_host_shown=IsWindowVisible(preferences.host);
            if(!gesture_host_shown){ShowWindow(preferences.host,SW_SHOWNOACTIVATE);pump(preferences.host);}
            SetFocus(source_control);pump(preferences.host);
            // The enlarged lesson has substantial content above this editor.
            // Use the actual lesson wheel path to expose its first source line
            // before testing native pointer selection, rather than sending a
            // synthetic drag to a child clipped far outside its viewport.
            for(int wheel=0;wheel<96;++wheel){
                RECT source_bounds{},pane_bounds{};GetWindowRect(source_control,&source_bounds);GetWindowRect(GetParent(source_control),&pane_bounds);
                if(source_bounds.top>=pane_bounds.top&&source_bounds.top+100<pane_bounds.bottom)break;
                const auto delta=source_bounds.top<pane_bounds.top?WHEEL_DELTA:-WHEEL_DELTA;
                SendMessageW(preferences.view,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),0);pump(preferences.host);
            }
            CHARRANGE gesture_cursor{0,0};SendMessageW(source_control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&gesture_cursor));
            POINT zero_scroll{};SendMessageW(source_control,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&zero_scroll));
            POINTL glyph_origin{};SendMessageW(source_control,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&glyph_origin),1);
            const int gesture_dpi=static_cast<int>(GetDpiForWindow(source_control));
            const POINT glyph_probe{glyph_origin.x+1,glyph_origin.y+MulDiv(27,gesture_dpi,96)};
            POINTL glyph_character_point{glyph_probe.x,glyph_probe.y};
            const auto glyph_character=SendMessageW(source_control,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&glyph_character_point));
            RECT glyph_control_bounds{},glyph_pane_bounds{};GetWindowRect(source_control,&glyph_control_bounds);GetWindowRect(GetParent(source_control),&glyph_pane_bounds);
            auto glyph_screen=glyph_probe;ClientToScreen(source_control,&glyph_screen);
            auto glyph_pane_point=glyph_screen;ScreenToClient(GetParent(source_control),&glyph_pane_point);
            // Input below is sent to our own HWNDs. Check clipping and sibling
            // occlusion in that same hierarchy, independent of other apps on
            // the desktop (this QA host deliberately does not steal activation).
            const auto glyph_hit=ChildWindowFromPointEx(GetParent(source_control),glyph_pane_point,
                CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT);
            require(PtInRect(&glyph_pane_bounds,glyph_screen)&&glyph_hit==source_control,
                "The large-text pointer fixture did not expose its real editor glyph inside the visible viewport");
            require(rich_text_hit_test(source_control,glyph_probe),
                "The actual glyph/font/zoom hit test treats the lower part of 150-percent code as empty space");
            CHARRANGE after_hit{};SendMessageW(source_control,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_hit));
            require(after_hit.cpMin==gesture_cursor.cpMin&&after_hit.cpMax==gesture_cursor.cpMax,
                "Observing actual text geometry changed the editor selection");
            // Compare the same sent mouse messages with a plain, visible
            // system RichEdit. Some versions consult physical button state
            // rather than growing a selection from synthetic MK_LBUTTON.
            auto native_bounds=glyph_control_bounds;
            MapWindowPoints(HWND_DESKTOP,GetParent(source_control),reinterpret_cast<POINT*>(&native_bounds),2);
            const auto native_edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",
                static_cast<DWORD>(GetWindowLongPtrW(source_control,GWL_STYLE)),native_bounds.left,native_bounds.top,
                native_bounds.right-native_bounds.left,native_bounds.bottom-native_bounds.top,GetParent(source_control),
                reinterpret_cast<HMENU>(49999),GetModuleHandleW(nullptr),nullptr);
            require(native_edit!=nullptr,"Cannot create a real native-only drag baseline");
            set_rich_text(native_edit,preferences.text(3,10),{},true);SendMessageW(native_edit,EM_SETZOOM,150,100);
            RECT native_format{};SendMessageW(source_control,EM_GETRECT,0,reinterpret_cast<LPARAM>(&native_format));
            SendMessageW(native_edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&native_format));
            SetFocus(native_edit);pump(preferences.host);
            POINTL native_glyph{};SendMessageW(native_edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&native_glyph),1);
            preferences.drag_control(native_edit,{native_glyph.x+1,native_glyph.y+MulDiv(27,gesture_dpi,96)},
                {MulDiv(100,gesture_dpi,96),0});
            CHARRANGE native_selection{};SendMessageW(native_edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&native_selection));
            DestroyWindow(native_edit);SetFocus(source_control);pump(preferences.host);
            SCROLLINFO lesson_scroll_before{sizeof(lesson_scroll_before),SIF_POS};GetScrollInfo(GetParent(source_control),SB_VERT,&lesson_scroll_before);
            POINT editor_scroll_before{};SendMessageW(source_control,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&editor_scroll_before));
            preferences.drag_control(source_control,{glyph_origin.x+1,glyph_origin.y+MulDiv(27,gesture_dpi,96)},
                {MulDiv(100,gesture_dpi,96),0});
            CHARRANGE gesture_selection{};SendMessageW(source_control,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&gesture_selection));
            SCROLLINFO lesson_scroll_after{sizeof(lesson_scroll_after),SIF_POS};GetScrollInfo(GetParent(source_control),SB_VERT,&lesson_scroll_after);
            POINT editor_scroll_after{};SendMessageW(source_control,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&editor_scroll_after));
            std::cout<<"INFO native-only sent drag selection="<<native_selection.cpMin<<','<<native_selection.cpMax
                <<"; assisted editor="<<gesture_selection.cpMin<<','<<gesture_selection.cpMax<<'\n';
            require(gesture_selection.cpMin==native_selection.cpMin&&gesture_selection.cpMax==native_selection.cpMax
                &&gesture_selection.cpMin==glyph_character&&preferences.page()==4
                &&lesson_scroll_before.nPos==lesson_scroll_after.nPos&&editor_scroll_before.x==editor_scroll_after.x
                &&editor_scroll_before.y==editor_scroll_after.y&&preferences.text(3,10)==scale_source+"// historial previo a escala y ajuste\n",
                "A real glyph hit did not preserve native mouse routing, source/cursor and independent scroll ownership");
            if(!gesture_host_shown){ShowWindow(preferences.host,SW_HIDE);pump(preferences.host);}
            SendMessageW(source_control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&scale_selection));
            update_lesson_preferences(preferences.view,[&](state::StateData& data){
                data.preferences["lesson_text_size_settings"]["global_scale"] = std::int64_t{75};
                data.preferences["lesson_text_size_settings"]["course_use_global_"+course] = true;
            });
            SendMessageW(source_control,EM_GETZOOM,reinterpret_cast<WPARAM>(&zoom_numerator),reinterpret_cast<LPARAM>(&zoom_denominator));
            require(zoom_denominator>0&&zoom_numerator*100==75*zoom_denominator&&preferences.control(3,10)==source_control,
                "A live inherited scale did not update the existing editor in place");
            CHARRANGE preserved_selection{};
            SendMessageW(source_control,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&preserved_selection));
            require(preserved_selection.cpMin==scale_selection.cpMin&&preserved_selection.cpMax==scale_selection.cpMax,
                "Changing scale/wrap moved the editor's cursor or selection");
            SendMessageW(source_control,WM_UNDO,0,0);pump(preferences.host);
            require(preferences.text(3,10)==scale_source,"Changing scale/wrap discarded the existing source Undo history");
            const auto shortcuts = descendant_class(preferences.view,L"LearnGameAndEngineDev.EditingBar");
            require(!shortcuts&&state::string_value(read_lesson_state(preferences.view),"keyboard_shortcut_settings","cpp_shortcuts")==R"(["std::cout","<<",";"])",
                "Retiring the Programming bar removed stored shortcuts or recreated it");
            const auto theme = reader_theme::palette(preferences.view);
            require(theme.accent == (theme.high_contrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(228,50,123)),
                "A permitted appearance update did not refresh the lesson's own route accent");
            const auto preference_source = preferences.text(3,10);
            preferences.replace(3,10,preference_source + "// borrador tras preferencias\n");
            const auto preference_state = preferences.saved();
            require(state::get_draft(preference_state,curriculum::draft_key("programming",preferences.lesson,
                        preferences.lesson.pages[4].blocks[3]),"","").source == preference_source + "// borrador tras preferencias\n",
                    "Sharing reader preferences caused a stale StateStore write or lost the editor draft");
            bool rejected = false;
            try { update_lesson_preferences(preferences.view,[](state::StateData& data) {
                data.preferences["learning_progress"]["qa_unauthorized_progress"] = true;
            }); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && !state::find(read_lesson_state(preferences.view),"learning_progress","qa_unauthorized_progress"),
                    "The reader preference API can overwrite student progress");
            rejected = false;
            try { update_lesson_preferences(preferences.view,[](state::StateData& data) {
                data.preferences["appearance_settings"]["route_accent_red_unknown_route"] = std::int64_t{7};
            }); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && !state::find(read_lesson_state(preferences.view),"appearance_settings","route_accent_red_unknown_route"),
                "Appearance settings can mutate a domain/key outside the exact supported RGB contracts");
            rejected=false;
            try { update_lesson_preferences(preferences.view,[](state::StateData& data) {
                data.preferences["keyboard_shortcut_settings"]["unknown_shortcuts"] = std::string("must remain protected");
            }); } catch(const std::invalid_argument&) { rejected=true; }
            require(rejected&&!state::find(read_lesson_state(preferences.view),"keyboard_shortcut_settings","unknown_shortcuts"),
                "Shortcut settings can modify keys outside the exact Android cpp_shortcuts contract");
            update_lesson_preferences(preferences.view,[](state::StateData& data) {
                data.preferences["lesson_narration_resume_bounded_v1"].erase("qa_slot");
                data.preferences["native_windows"]["code_wrap"] = false;
            });
            require(!state::find(read_lesson_state(preferences.view),"lesson_narration_resume_bounded_v1","qa_slot"),
                    "The bounded reader history cannot delete an evicted position through the shared store");
            preferences.run(3); require(verdicts == 1,"A fresh compiler verdict did not reach the reader hook exactly once");
            preferences.reopen();
            require(verdicts == 1 && destroying == 1,"Reopening replayed a feedback submission or missed lifecycle cleanup");
            set_lesson_view_hooks(preferences.view,hooks); preferences.next();
            require(leaving == 1,"Page changes did not release the old reader position before replacing controls");
        }
        std::cout << "PASS Shared reader preference store, protected progress/drafts, bounded-history deletion and visible-source/lifecycle/feedback hooks\n" << std::flush;
        {
            Fixture languages({},[](const curriculum::Lesson& source){auto model=source;model.narration_language="fr";model.pages.front().narration_language="en-GB";return model;});
            require(lesson_page_state(languages.view).narration_language=="en-GB","The page's explicit narration language did not reach the reader boundary");
            languages.lesson.pages.front().narration_language.reset();languages.reopen();
            require(lesson_page_state(languages.view).narration_language=="fr","An absent page language lost the original lesson fallback");
            languages.lesson.narration_language.reset();languages.reopen();
            require(lesson_page_state(languages.view).narration_language.empty(),"Absent authored narration metadata overrode the reader's own preference");
        }
        std::cout << "PASS Authored page narration language, lesson fallback and absent-metadata preference compatibility\n" << std::flush;
        const auto reject_forward_selector = [](Fixture& fixture,std::size_t target) {
            const auto before = fixture.page();
            require(!navigate_lesson_view(fixture.view,target),"The Pages command bypassed a prerequisite");
            pump(fixture.host);
            require(fixture.page() == before,"The Pages command bypassed an explicit compiler prerequisite");
        };
        Fixture dependencies(solved_compiler_page,[](const curriculum::Lesson& source) {
            return with_relationships(source,reference_links,
                R"([{"activity":"block_0005_0006","requires":["block_0004_0002"]}])");
        });
        require(!lesson_page_state(dependencies.view).can_next,"A legacy correct marker unlocked the next independent activity");
        reject_forward_selector(dependencies,5);
        dependencies.run(3);
        require(dependencies.text(3,4) == "✓ Correcto" && !lesson_page_state(dependencies.view).can_next,
                "Successful execution of the original program satisfied another editor's prerequisite");
        dependencies.run(6);
        require(dependencies.text(6,4).starts_with("✗ Incorrecto") && !lesson_page_state(dependencies.view).can_next,
                "An incorrect output unlocked its dependent page");
        const auto prerequisite_source = message_code(dependencies.lesson.pages[4].blocks[6],"Zona inicial","Caja recibida");
        dependencies.replace(6,10,prerequisite_source + "esto no compila\n"); dependencies.run(6);
        require(dependencies.text(6,4).empty() && !lesson_page_state(dependencies.view).can_next,
                "A compiler error unlocked its dependent page");
        dependencies.replace(6,10,prerequisite_source); dependencies.run(6);
        require(lesson_page_state(dependencies.view).can_next,"Fresh correct output did not update the next-page prerequisite");
        const auto prerequisite_editor = dependencies.control(6,10);
        dependencies.replace(6,10,prerequisite_source + "// cambio QA\n");
        require(!lesson_page_state(dependencies.view).can_next,"Source editing did not revoke forward access");
        SendMessageW(prerequisite_editor,WM_UNDO,0,0); pump(dependencies.host);
        require(dependencies.text(6,10) == prerequisite_source && !lesson_page_state(dependencies.view).can_next,
                "Undo resurrected a discarded prerequisite receipt");
        dependencies.run(6); dependencies.next();
        require(descendant(dependencies.view,id(0,0)) && descendant(dependencies.view,id(6,10)),
                "A completed prerequisite did not expose its references and dependent editor");
        const auto dependent_source = message_code(dependencies.lesson.pages[5].blocks[6],"Caja recibida","Zona segura");
        dependencies.replace(6,10,dependent_source); dependencies.run(6);
        require(descendant(dependencies.view,id(8,1)),"The dependent activity could not unlock its own reference");
        dependencies.reopen();
        require(dependencies.page() == 5 && dependencies.text(6,4).empty() && dependencies.text(6,14).empty(),
                "Completed dependencies either lost the bookmark or revived an old console/verdict");
        dependencies.select_page(4); dependencies.click(6,12);
        require(!lesson_page_state(dependencies.view).can_next,"Reset retained an obsolete prerequisite");
        // Simulate an already-stored later bookmark without claiming completion.
        dependencies.close();
        {
            state::StateStore store(dependencies.environment.paths.root); auto data = store.load();
            state::save_current_page(data,dependencies.lesson,5); store.save(data);
        }
        dependencies.open();
        require(dependencies.page() == 5 && descendant(dependencies.view,id(6,18))
                && !descendant(dependencies.view,id(6,10)) && !descendant(dependencies.view,id(6,11))
                && !descendant(dependencies.view,id(6,13)) && !descendant(dependencies.view,id(6,14))
                && !descendant(dependencies.view,id(6,4)) && !descendant(dependencies.view,id(8,1)),
                "An old bookmark exposed a blocked editor, its controls or its own reference");
        require(dependencies.text(5,0).find("Zona segura") != std::string::npos,
                "The blocked implementation hid the visible output objective");
        const auto preserved = dependencies.saved();
        require(state::get_draft(preserved,curriculum::draft_key("programming",dependencies.lesson,
                    dependencies.lesson.pages[5].blocks[6]),"").source == dependent_source,
                "Blocking an old bookmark deleted its independent compiler draft");
        dependencies.select_page(4);
        dependencies.replace(6,10,prerequisite_source); dependencies.run(6); dependencies.next();
        require(dependencies.text(6,10) == dependent_source && dependencies.text(6,14).empty(),
                "Reunlocking lost the dependent draft or reused an old console");
        dependencies.non_overlapping_rows();
        std::cout << "PASS Explicit cross-page activity prerequisites: fresh own output, forward/selector gates, Undo/reset invalidation, safe old bookmarks and preserved independent drafts\n" << std::flush;

        std::string same_page_draft;
        Fixture local_dependencies([&](state::StateData& data,const curriculum::Lesson& lesson) {
            solved_compiler_page(data,lesson);
            state::save_current_page(data,lesson,3);
            for (const auto index : {std::size_t{2},std::size_t{3}}) {
                const auto& block = lesson.pages[3].blocks[index]; state::Response response; response.correct = true;
                if (block.correct_index) response.selected = static_cast<std::int64_t>(*block.correct_index);
                else if (block.type == BlockType::order_code) response.order.assign(block.correct_order.begin(),block.correct_order.end());
                else response.answer = block.answers.at(0);
                state::put_response(data,curriculum::exercise_key(lesson,block),response);
            }
            const auto& target = lesson.pages[4].blocks[6];
            same_page_draft = message_code(target,"Zona inicial","Caja recibida") + "// borrador independiente\n";
            state::Draft draft; draft.source = same_page_draft;
            state::put_draft(data,curriculum::draft_key("programming",lesson,target),draft);
        },[](const curriculum::Lesson& source) {
            const auto activity = source.pages[4].blocks[6].stable_id;
            const auto prerequisite = source.pages[4].blocks[3].stable_id;
            return with_relationships(source,reference_links,"[{\"activity\":\"" + activity
                + "\",\"requires\":[\"" + prerequisite + "\"]}]");
        });
        require(local_dependencies.page() == 3 && lesson_page_state(local_dependencies.view).can_next,
                "A same-page prerequisite prevented entering the page where it must be completed");
        local_dependencies.next();
        require(descendant(local_dependencies.view,id(6,18)) && !descendant(local_dependencies.view,id(6,10)),
                "A same-page dependent editor exposed its prepared implementation early");
        const auto original_editor = local_dependencies.control(3,10);
        const auto original_source = local_dependencies.text(3,10);
        local_dependencies.replace(3,10,original_source + "// historial del editor libre\n");
        local_dependencies.run(3);
        require(local_dependencies.control(3,10) == original_editor && local_dependencies.text(3,4) == "✓ Correcto"
                && local_dependencies.text(6,10) == same_page_draft && local_dependencies.text(6,14).empty(),
                "Additive unlocking recreated the prerequisite editor, verdict/history or dependent draft");
        local_dependencies.run(6);
        require(local_dependencies.text(6,4).starts_with("✓ Correcto"),"The same-page dependent editor cannot execute its preserved draft");
        const auto dependent_editor = local_dependencies.control(6,10);
        local_dependencies.replace(3,10,original_source + "// revocación\n");
        require(local_dependencies.control(3,10) == original_editor && !IsWindow(dependent_editor)
                && !descendant(local_dependencies.view,id(6,10)) && descendant(local_dependencies.view,id(6,18)),
                "Revocation retained the dependent controls or rebuilt an unrelated editor");
        SendMessageW(original_editor,WM_UNDO,0,0); pump(local_dependencies.host);
        require(!descendant(local_dependencies.view,id(6,10)),"Undo reused a previous prerequisite achievement");
        local_dependencies.run(3);
        require(local_dependencies.text(6,10) == same_page_draft && local_dependencies.text(6,4).empty(),
                "Revealing a same-page editor lost its saved source or restored a stale verdict");
        local_dependencies.replace(6,10,same_page_draft + "// edición que debe capturarse\n");
        const auto last_dependent_source = local_dependencies.text(6,10);
        local_dependencies.click(6,11);
        local_dependencies.replace(3,10,original_source);
        require(!descendant(local_dependencies.view,id(6,10)),"A running dependent editor remained available after revocation");
        const auto hidden_state = local_dependencies.saved();
        require(state::get_draft(hidden_state,curriculum::draft_key("programming",local_dependencies.lesson,
                    local_dependencies.lesson.pages[4].blocks[6]),"").source == last_dependent_source,
                "Hiding/cancelling the dependent editor failed to capture its latest source");
        local_dependencies.run(3);
        require(local_dependencies.text(6,10) == last_dependent_source && local_dependencies.text(6,14).empty(),
                "Cancelled dependent execution later published stale output");
        local_dependencies.non_overlapping_rows();
        std::cout << "PASS Same-page prerequisites: additive controls, live Undo/verdict preservation, cancel-on-hide and independent draft recovery\n" << std::flush;

        const auto graph_start = GetTickCount64();
        Fixture graph([](state::StateData& data,const curriculum::Lesson& lesson) {
            // Synthetic receipts exercise graph traversal only; this test does
            // not claim that any C++ program was compiled or heard here.
            for (const auto& current : lesson.pages[0].blocks) {
                auto objective = curriculum::reference_objective_frame(lesson,current);
                objective += "/source/" + std::to_string(current.initial_code.size()) + ':' + current.initial_code;
                objective += "/input/0:";
                const auto key = "reference-proof/v1/" + lesson.locale + '/' + curriculum::draft_key("programming",lesson,current);
                data.preferences["native_windows"][key] = state::sha256(objective);
            }
        },[](const curriculum::Lesson& source) {
            curriculum::Lesson result;
            result.id = "qa_shared_prerequisite_graph"; result.title = "Grafo QA de requisitos compartidos"; result.locale = "es";
            result.state_layout.version = 1; result.state_layout.locale = "es";
            result.state_layout.lineage = state::sha256("qa-shared-prerequisite-layout-v1");
            result.state_layout.legacy_lineage = result.state_layout.lineage;
            curriculum::Page page; page.stable_id = "qa_graph_page";
            curriculum::StatePage state_page; state_page.id = page.stable_id;
            for (std::size_t index = 0; index < 35; ++index) {
                auto current = source.pages[4].blocks[3]; current.stable_id = "qa_graph_" + std::to_string(index);
                current.address = {0,index,"",""};
                state_page.blocks.push_back(current.stable_id); page.blocks.push_back(std::move(current));
                if (index > 1) result.activity_after.push_back({page.blocks[index].stable_id,
                    {page.blocks[index-1].stable_id,page.blocks[index-2].stable_id}});
            }
            result.pages.push_back(std::move(page)); result.state_layout.origin.push_back(state_page);
            result.state_layout.current.push_back(std::move(state_page));
            return result;
        });
        require(GetTickCount64() - graph_start < 30000 && descendant(graph.view,id(34,10)),
                "Shared prerequisite traversal became recursive/exponential or lost a valid transitive receipt");
        const auto revoke_start = GetTickCount64();
        graph.replace(0,10,graph.lesson.pages[0].blocks[0].initial_code + "// invalidación del grafo\n");
        require(GetTickCount64() - revoke_start < 30000 && !descendant(graph.view,id(34,10))
                && descendant(graph.view,id(34,18)) && descendant(graph.view,id(0,10)),
                "Transitive graph invalidation hung or left a dependent editor open");
        native_answer_overflow_bars();
        std::cout << "PASS Shared 35-node prerequisite DAG: bounded traversal and transitive invalidation without recursive rechecking\n"
                  << "Native lesson UI integration: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Native lesson UI integration: " << error.what() << '\n'; return 1;
    }
}
