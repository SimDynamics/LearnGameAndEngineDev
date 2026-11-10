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
#include "platform/windows/compiler_completion.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/profile.hpp"
#include "core/narration_state.hpp"
#include "core/compiler_flow.hpp"
#include "core/lesson_entry_policy.hpp"
#include "core/lesson_page_reach.hpp"
#include "core/view_preferences.hpp"
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
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
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
        const std::function<curriculum::Lesson(const curriculum::Lesson&)>& load_model = {},bool hidden=false)
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"native-lesson-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false})),
        content(std::filesystem::path(build::source_root) / "app/src/main/assets/content") {
        require(!std::filesystem::exists(environment.paths.root), "QA instance unexpectedly already exists");
        initialize_profile_directories(environment.paths);
        // Fixed-coordinate regressions describe the verified published UI.
        // Integrated-asset cases below supply their current model explicitly.
        lesson = test::load_pre_retirement_lesson("lesson_primer_programa_y_salida.json");
        supplied_model = true;
        if (load_model) { lesson = load_model(lesson); supplied_model = true; }
        state::StateStore seed_store(environment.paths.root); initial_data=seed_store.load();
        // Spanish expectations below are explicit test choices, independent of
        // the PC language. Individual language cases override this preference.
        initial_data.preferences["native_windows"]["ui_language"]=std::string("es");
        if (initialize) initialize(initial_data,lesson);
        seed_store.save(initial_data);
        host = CreateWindowExW(WS_EX_CONTROLPARENT|(hidden?WS_EX_NOACTIVATE:0), L"LgaedLessonViewTests", L"Native lesson integration QA",
            WS_OVERLAPPEDWINDOW|(hidden?0:WS_VISIBLE), 30, 30, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
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
        require(text(block,4).starts_with("✓ Correcto"), "Authored response was not accepted through actual controls: "
            + lesson.id + "/page=" + std::to_string(page()) + "/block=" + std::to_string(block) + "/feedback=" + text(block,4));
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
        require(!GetDlgItem(view,101) && !GetDlgItem(view,102) && !GetDlgItem(view,103),
                "Removed previous/next/selector navigation still exists as a hidden or visible control");
        require(!GetDlgItem(view,104),"A duplicate import button still occupies the lesson toolbar");
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
                RECT client{};GetClientRect(pane,&client);
                const auto exposed=CreateRectRgnIndirect(&client),pending=CreateRectRgn(0,0,0,0),child_region=CreateRectRgn(0,0,0,0);
                require(exposed&&pending&&child_region,"Cannot observe the lesson's exposed background region");
                const auto pending_kind=GetUpdateRgn(pane,pending,FALSE);
                // WS_CLIPCHILDREN legitimately leaves no parent update when
                // one long source covers the entire viewport. Require every
                // exposed background pixel, including shaped child corners,
                // to be dirty rather than demanding hidden parent pixels.
                for(auto child=GetWindow(pane,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){
                    RECT bounds{};GetWindowRect(child,&bounds);POINT origin{bounds.left,bounds.top};ScreenToClient(pane,&origin);
                    if(GetWindowRgn(child,child_region)==ERROR)SetRectRgn(child_region,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
                    OffsetRgn(child_region,origin.x,origin.y);CombineRgn(exposed,exposed,child_region,RGN_DIFF);
                }
                CombineRgn(exposed,exposed,pending,RGN_DIFF);RECT missing{};const auto uncovered=GetRgnBox(exposed,&missing);
                DeleteObject(child_region);DeleteObject(pending);DeleteObject(exposed);
                require(pending_kind!=ERROR&&uncovered==NULLREGION,
                        "Moving rows left exposed viewport background outside its repaint region");
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

void lesson_menu_retains_import_pages_and_settings_with_hooks() {
    for (const auto locale : {"es","en","fr"}) {
        Fixture fixture([locale](state::StateData& data,const curriculum::Lesson&) {
            data.preferences["native_windows"]["ui_language"] = std::string(locale);
        },[](const curriculum::Lesson&) {
            auto lesson=windows::parse_lesson_json(R"({"id":"qa_lesson_menu","title":"Contenido para el menú","pages":[
                {"blocks":[{"type":"paragraph","text":"Página sin respuestas para comprobar el menú."}]},
                {"blocks":[{"type":"paragraph","text":"Segunda página conservada."}]}]})");
            for(int line=0;line<80;++line)lesson.pages[0].blocks[0].text+="\nContenido de prueba para desplazar la página.";
            return lesson;
        },true);
        const auto before = fixture.saved();
        int settings_requests = 0,finished = 0;
        HWND settings_owner = nullptr;
        LessonViewHooks hooks;
        hooks.settings_requested = [&](HWND owner) { ++settings_requests; settings_owner = owner; };
        hooks.lesson_completed = [&](HWND owner) { require(owner==fixture.view,"Lesson completion lost its owning view");++finished; };
        set_lesson_view_hooks(fixture.view,hooks);
        ShowWindow(fixture.host,SW_SHOWNOACTIVATE); pump(fixture.host);
        const auto verify_options = [&](bool settings_available) {
            const auto requests=settings_requests;
            PopupCapture capture(fixture.view);
            fixture.click(107);
            require(capture.captions.empty(),"The lesson gear still opened a native dropdown instead of integrated settings");
            require(settings_requests==requests+(settings_available?1:0)&&fixture.saved()==before&&fixture.page()==0,
                "The lesson gear failed to request settings once or changed lesson state");
            const auto options=lesson_options_snapshot(fixture.view);
            require(options.current_page==0&&options.page_count==2&&options.available_pages==std::vector<std::size_t>{0,1}
                &&!options.finish_visible&&!options.finish_enabled,
                "Integrated lesson options lost their current page, available pages or completion gate");
        };
        verify_options(true);
        set_lesson_view_hooks(fixture.view,{});
        verify_options(false);
        set_lesson_view_hooks(fixture.view,hooks);
        SendMessageW(fixture.view,WM_COMMAND,MAKEWPARAM(114,BN_CLICKED),0); pump(fixture.host);
        require(settings_requests == 2 && settings_owner == fixture.view && IsWindow(fixture.view)
            && fixture.saved() == before && fixture.page() == 0,
            "The retained Settings command did not dispatch once through its hook with lesson state intact");
        require(!invoke_lesson_option(fixture.view,LessonOptionAction::finish)
            &&!invoke_lesson_option(fixture.view,static_cast<LessonOptionAction>(9999))&&finished==0,
            "An unavailable or invalid lesson option bypassed its original gate");
        // Import is deliberately tested before scrolling: recording a lesson
        // viewport is personal state and correctly makes import unavailable.
        require(state::can_import_android_profile(fixture.saved()),"Lesson import options require an untouched QA profile");
        require(invoke_lesson_option(fixture.view,LessonOptionAction::import_android_state),
            "The integrated lesson options lost the guarded Android import surface");
        pump(fixture.host);require(IsWindowVisible(fixture.control(201)),"The original Android import path control did not open");
        require(lesson_options_snapshot(fixture.view).available_pages.empty()&&!navigate_lesson_view(fixture.view,1),
            "The integrated page selector bypassed the active import surface");
        fixture.click(206);
        RECT original_text{},bottom_text{},top_text{};GetWindowRect(fixture.control(0,0),&original_text);
        require(invoke_lesson_option(fixture.view,LessonOptionAction::page_bottom),"The integrated lesson options lost page bottom");
        pump(fixture.host);GetWindowRect(fixture.control(0,0),&bottom_text);
        require(invoke_lesson_option(fixture.view,LessonOptionAction::page_top),"The integrated lesson options lost page top");
        pump(fixture.host);GetWindowRect(fixture.control(0,0),&top_text);
        require(bottom_text.top<original_text.top&&top_text.top==original_text.top,
            "The integrated top/bottom actions did not scroll the original lesson viewport");
        require(navigate_lesson_view(fixture.view,1),"The integrated page selector lost an allowed page");pump(fixture.host);
        const auto last=lesson_options_snapshot(fixture.view);
        require(last.current_page==1&&last.page_count==2&&last.available_pages==std::vector<std::size_t>{0,1}
            &&last.finish_visible&&last.finish_enabled,"Final lesson options lost the original completion action");
        require(invoke_lesson_option(fixture.view,LessonOptionAction::finish),"The integrated Finish action did not complete an eligible lesson");
        pump(fixture.host);require(finished==1,"The integrated Finish action lost or duplicated its completion callback");
        ShowWindow(fixture.host,SW_HIDE); pump(fixture.host);
    }
    {
        Fixture blocked({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"({"id":"qa_options_gate","title":"Opciones con requisito","pages":[
            {"blocks":[{"type":"fill_code","id":"required","question":"Completa el tipo.","code":"[]","answers":["int"]}]},
            {"blocks":[{"type":"paragraph","text":"Página posterior bloqueada."}]}]})");},true);
        int finished=0;LessonViewHooks hooks;hooks.lesson_completed=[&](HWND){++finished;};set_lesson_view_hooks(blocked.view,hooks);
        const auto before=blocked.saved();const auto options=lesson_options_snapshot(blocked.view);
        require(options.available_pages==std::vector<std::size_t>{0}&&!navigate_lesson_view(blocked.view,1)
            &&!invoke_lesson_option(blocked.view,LessonOptionAction::finish)&&finished==0&&blocked.saved()==before,
            "Integrated lesson options bypassed an unanswered prerequisite or changed protected state");
    }
    {
        Fixture last({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"({"id":"qa_options_finish_gate","title":"Finalizar sin saltar respuestas","pages":[
            {"blocks":[{"type":"fill_code","id":"required","question":"Completa el tipo.","code":"[]","answers":["int"]}]}]})");},true);
        int finished=0;LessonViewHooks hooks;hooks.lesson_completed=[&](HWND){++finished;};set_lesson_view_hooks(last.view,hooks);
        auto options=lesson_options_snapshot(last.view);
        require(options.finish_visible&&!options.finish_enabled&&!invoke_lesson_option(last.view,LessonOptionAction::finish)&&finished==0,
            "The integrated Finish action ignored an unanswered final-page exercise");
        last.replace(0,2,"int");last.click(0,3);options=lesson_options_snapshot(last.view);
        require(options.finish_visible&&options.finish_enabled&&invoke_lesson_option(last.view,LessonOptionAction::finish),
            "The integrated Finish action did not enable after the real answer was checked");
        pump(last.host);require(finished==1,"Checking the final exercise did not preserve the real completion callback");
    }
    std::cout << "PASS real lesson gear requests integrated settings without a dropdown in es/en/fr; import, allowed pages, top/bottom and gated completion remain available without fabricated progress\n";
}

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
            int requests=0;LessonViewHooks hooks;hooks.settings_requested=[&](HWND owner){require(owner==fixture.view,"Localized lesson settings lost their owner");++requests;};
            set_lesson_view_hooks(fixture.view,hooks);PopupCapture capture(fixture.view);fixture.click(107);
            require(requests==1&&capture.captions.empty(),"The localized lesson gear did not open integrated settings directly");
            set_lesson_view_hooks(fixture.view,{});
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
        {PopupCapture capture(fixture.view);fixture.click(0,36);capture.verified();require(capture.captions==std::vector<std::string>{"MSVC C++23 · x64"},"The laboratory compiler control advertised an unavailable backend");}
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
        require(initial_check.top-initial_host.bottom==MulDiv(12,static_cast<int>(GetDpiForWindow(fixture.view)),96)
                && following.top-initial_check.bottom==MulDiv(36,static_cast<int>(GetDpiForWindow(fixture.view)),96),
            "An unanswered math card changed its primary-action inset or separate bottom margins");
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
            require(check_bounds.top-host_bounds.bottom==MulDiv(12,static_cast<int>(GetDpiForWindow(pane)),96),
                "The mathematical host changed its primary-action inset before Check");
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
    if(fixture.text(1,2)!=lines||height(multiline)<=two_line_height||(bars(multiline)&WS_VSCROLL)){
        CHARRANGE cursor{};SendMessageW(multiline,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&cursor));
        POINT offset{},caret{};SendMessageW(multiline,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        SendMessageW(multiline,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&caret),cursor.cpMax);
        std::cerr<<"Answer Undo geometry: equal="<<(fixture.text(1,2)==lines)<<", bytes="<<fixture.text(1,2).size()<<'/'<<lines.size()
            <<", heights empty/two/current="<<empty_height<<'/'<<two_line_height<<'/'<<height(multiline)
            <<", bars="<<bars(multiline)<<", lines="<<SendMessageW(multiline,EM_GETLINECOUNT,0,0)
            <<", selection="<<cursor.cpMin<<':'<<cursor.cpMax<<", caret="<<caret.x<<','<<caret.y
            <<", scroll="<<offset.x<<','<<offset.y<<'\n';
    }
    require(fixture.text(1,2)==lines&&height(multiline)>two_line_height&&!(bars(multiline)&WS_VSCROLL),
        "Undo lost the original multiline text or its content-sized geometry");
    const auto restored_height=height(multiline);
    SendMessageW(multiline,EM_REDO,0,0);pump(fixture.host);
    require(fixture.text(1,2).empty()&&height(multiline)==empty_height&&!bars(multiline)&&fixture.control(1,2)==multiline,
        "Redo did not shrink the same multiline answer and remove the restored overflow");
    SendMessageW(multiline,WM_UNDO,0,0);pump(fixture.host);
    require(fixture.text(1,2)==lines&&height(multiline)==restored_height&&!(bars(multiline)&WS_VSCROLL),
        "Repeated Undo changed the restored answer's exact content-sized height");
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
        if(fixture.text(0,10)!=large||!(bars(source)&WS_VSCROLL)||(bars(source)&WS_HSCROLL)){
            RECT client{},format{};GetClientRect(source,&client);SendMessageW(source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
            POINT position{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
            std::cerr<<"Compiler Undo overflow: laboratory="<<laboratory<<", source="<<fixture.text(0,10).size()<<'/'<<large.size()
                <<", equal="<<(fixture.text(0,10)==large)<<", bars="<<bars(source)
                <<", lines="<<SendMessageW(source,EM_GETLINECOUNT,0,0)<<", client="<<client.right<<'x'<<client.bottom
                <<", format="<<format.left<<','<<format.top<<','<<format.right<<','<<format.bottom
                <<", scroll="<<position.x<<','<<position.y<<'\n';
        }
        require(fixture.text(0,10)==large&&(bars(source)&WS_VSCROLL)&&!(bars(source)&WS_HSCROLL),
            "Undo lost the source or failed to restore its wrapped vertical overflow");
        // Actual edit after scrolling to the middle of a long source. The
        // lesson's deferred autoheight measurement must not send line 7 to 0.
        SendMessageW(source,WM_VSCROLL,SB_TOP,0);
        SendMessageW(source,EM_LINESCROLL,0,7);
        pump(fixture.host);
        const auto initial_line=SendMessageW(source,EM_GETFIRSTVISIBLELINE,0,0);
        require(initial_line>=5,"The typing regression did not reach a scrolled source viewport");
        const auto insertion=SendMessageW(source,EM_LINEINDEX,initial_line+1,0)+5;
        require(insertion>=0,"The typing regression has no visible line for the caret");
        SendMessageW(source,EM_SETSEL,insertion,insertion);
        POINT before_typing{},after_typing{};
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before_typing));
        const auto before_line=SendMessageW(source,EM_GETFIRSTVISIBLELINE,0,0);
        SendMessageW(source,WM_CHAR,L'z',1);
        pump(fixture.host);
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after_typing));
        const auto after_line=SendMessageW(source,EM_GETFIRSTVISIBLELINE,0,0);
        auto changed=large;changed.insert(static_cast<std::size_t>(insertion),1,'z');
        require(before_line>=5&&before_typing.y>0&&after_typing.y>0&&after_line>=before_line-1,
            "Typing in the middle of a scrolled compiler returned its viewport to line zero");
        require(fixture.text(0,10)==changed,
            "Preserving a scrolled compiler viewport changed or lost typed code");
    }
    std::cout<<"PASS compiler editor/stdin native overflow in lessons and Laboratory, wrap, shrinking, offsets, typing from middle and Undo\n";
}
void native_compiler_short_sources_keep_a_usable_height(){
    const std::string seven_lines="#include <iostream>\n\nint main() {\n    std::cout << \"Texto\";\n    return 0;\n}\n";
    const std::string shorter="int main() {\n}\n";
    bool stable=true;
    for(const int scale:{75,100,150}){
        Fixture fixture([scale](state::StateData& data,const curriculum::Lesson&){appearance::save_global_text_scale(data,scale);},
            [&](const curriculum::Lesson&){auto model=windows::parse_lesson_json(R"json({"id":"cpp_fundamentals__qa__compiler_height","title":"Espacio de edición estable","pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main() {}"}]}]})json");
                model.pages[0].blocks[0].initial_code=seven_lines;return model;});
        const auto source=fixture.control(0,10);const auto dpi=static_cast<int>(GetDpiForWindow(source));
        const auto px=[dpi](int logical){return MulDiv(logical,dpi,96);};
        POINTL first{},second{};SendMessageW(source,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
        const auto second_line=SendMessageW(source,EM_LINEINDEX,1,0);require(second_line>=0,"The seven-line editor fixture lost its actual native line geometry");
        SendMessageW(source,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second),second_line);
        const auto minimum=std::clamp(static_cast<int>(second.y-first.y)*10+px(32),px(220),px(320));
        int original_height=0;
        const auto check=[&](std::string_view stage){RECT bounds{};GetWindowRect(source,&bounds);const auto height=static_cast<int>(bounds.bottom-bounds.top);
            if(!original_height)original_height=height;
            stable=stable&&height>=minimum-1&&height<=px(320)+1&&height==original_height;
            std::cout<<"INFO compiler viewport scale="<<scale<<" stage="<<stage<<" actual="<<height<<" minimum="<<minimum
                <<" initial="<<original_height<<" lines="<<SendMessageW(source,EM_GETLINECOUNT,0,0)<<'\n';};
        check("seven lines");fixture.replace(0,10,shorter);check("shorter source");fixture.replace(0,10,"");check("empty source");
        require(SendMessageW(source,WM_UNDO,0,0),"The source height test lost its real Undo history");pump(fixture.host);
        require(fixture.text(0,10)==shorter,"Undo changed the shortened compiler draft");check("Undo");
        require(SendMessageW(source,EM_REDO,0,0),"The source height test lost its real Redo history");pump(fixture.host);
        require(fixture.text(0,10).empty(),"Redo changed the empty compiler draft");check("Redo");
        fixture.replace(0,10,seven_lines);fixture.click(0,35);check("wrap");
        CHARRANGE selection{4,9};SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,0);pump(fixture.host);check("resize");
        fixture.click(0,20);require(fixture.fullscreen(),"The compiler viewport test did not enter actual fullscreen");fixture.escape();check("fullscreen return");
        CHARRANGE after{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        require(fixture.control(0,10)==source&&fixture.text(0,10)==seven_lines&&after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax,
            "Keeping the compiler usable changed its source HWND, draft or selection");
    }
    require(stable,"A short compiler source collapsed below its ten-line/220dp editing space or changed height while shortening");
    std::cout<<"PASS short compiler sources: seven/three/empty lines, Undo/Redo, wrap, resize and fullscreen return retain a usable ten-line viewport at75/100/150percent zoom\n";
}
void native_compiler_output_overflow_bars(){
    Fixture fixture({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({"id":"qa_compiler_console_overflow","title":"Salida real","pages":[{"blocks":[
        {"type":"cpp_compiler","initialCode":"#include <iostream>\n#include <string>\nint main() { for (int i=0;i<40;++i) std::cout << std::string(400,'x') << '\\n'; }\n"}]}]})json");});
    const auto console=fixture.control(0,14),action=fixture.control(0,11);
    require(reader_theme::native_scrollbar_theme(console),"The compiler console retained a competing nonclient scrollbar painter");
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
        // The answer card's margin and primary action's inset are independent.
        const auto check_gap=MulDiv(18,dpi,96)+MulDiv(12,dpi,96);
        if(answer_header.top-source.bottom!=MulDiv(48,dpi,96)||answer.top<=answer_header.bottom||check.top-answer.bottom!=check_gap)
            std::cerr<<"Answer-card spacing: block="<<index<<", DPI="<<dpi<<", source/header="<<answer_header.top-source.bottom
                <<", header/answer="<<answer.top-answer_header.bottom<<", answer/check="<<check.top-answer.bottom<<'\n';
        require(answer_header.top-source.bottom==MulDiv(48,dpi,96)&&answer.top>answer_header.bottom&&check.top-answer.bottom==check_gap,
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
        const auto sample=[&](HWND target,int x_offset,int y_offset){RECT bounds{};GetWindowRect(target,&bounds);MapWindowPoints(nullptr,pane,reinterpret_cast<POINT*>(&bounds),2);
            const auto dpi=static_cast<int>(GetDpiForWindow(pane));
            const POINT point{bounds.left+MulDiv(x_offset,dpi,96),bounds.top+MulDiv(y_offset,dpi,96)};
            return PtInRect(&client,point)?GetPixel(memory,point.x,point.y):CLR_INVALID;};
        // Readonly code fills the card width. Sample its exposed header/body
        // gap instead of subtracting an inset outside the drawing surface.
        const auto header=sample(fixture.control(0,16),-7,2),body=sample(code,9,-4);
        SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);
        const auto colors=reader_theme::palette(pane);
        require(header!=CLR_INVALID&&body!=CLR_INVALID,"A simulated card paint sample fell outside its drawing surface");
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
        EnumChildWindows(f.view,[](HWND candidate,LPARAM raw)->BOOL{auto& target=*reinterpret_cast<Notice*>(raw);
            if(window_text(candidate)==utf8(target.expected)){target.window=candidate;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&notice));
        if(incomplete){
            require(notice.window,"An incomplete answer omitted its internal nonblocking notice");
            const auto style=GetWindowLongPtrW(notice.window,GWL_STYLE);
            require((style&WS_CHILD)&&!(style&WS_POPUP)&&IsChild(f.view,notice.window),
                "An incomplete answer opened a notice outside its lesson");
            require(GetWindowLongPtrW(notice.window,GWL_EXSTYLE)&WS_EX_NOACTIVATE,
                "An incomplete-answer notice can activate over the current editor");
            require(GetFocus()==previous_focus,"An incomplete-answer notice stole editor focus");
            DestroyWindow(notice.window);
        }
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
        const int side=MulDiv(scale(43),4,5),left=(bounds.right-side)/2,top=scale(12);
        const int middle=left+static_cast<int>(std::lround(side*.5));
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
            const int side=MulDiv(scale(43),4,5),left=(bounds.right-side)/2,top=scale(12);
            const RECT route_glyph{left,top,left+side,top+side};
            const RECT infinity_glyph{MulDiv(scale(8),4,5),-MulDiv(scale(1),4,5),
                MulDiv(scale(8),4,5)+MulDiv(scale(30),4,5),-MulDiv(scale(1),4,5)+MulDiv(scale(26),4,5)};
            int accent=0,free_percentage=0;
            const auto background=reader_theme::background(button);
            for(int y=top;y<top+side;++y)for(int x=left;x<left+side;++x){
                if(PtInRect(&infinity_glyph,POINT{x,y}))continue;
                require(raster.color(premium,x,y)==raster.color(free_full,x,y),
                    "Premium changed the original route glyph/tint or left it partially empty");
                if(raster.color(premium,x,y)==colors.accent)++accent;
            }
            require(accent>0,"Premium lost its original Programming/Mathematics accent fill");
            for(int y=0;y<scale(15);++y)for(int x=bounds.right/2;x<bounds.right;++x){
                // Both original glyphs can cross the midpoint. Test the
                // exposed percentage area, rather than their intended ink.
                if(PtInRect(&infinity_glyph,POINT{x,y})||PtInRect(&route_glyph,POINT{x,y}))continue;
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
void text_sequence_lesson_all_pages() {
    constexpr std::string_view lesson_id = "cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato";
    constexpr std::array<std::string_view,4> retired_ids{
        "ejecucion_original_texto_secuencia_y_formato_consigna","ejecucion_original_texto_secuencia_y_formato_editor",
        "block_0007_0002","block_0007_0003"};
    const auto archived_code=[](const curriculum::Lesson& lesson,std::string_view identity)->const curriculum::Block&{
        const auto found=std::ranges::find(lesson.retired_blocks,identity,&curriculum::Block::stable_id);
        require(found!=lesson.retired_blocks.end()&&found->type==BlockType::code&&found->language=="cpp"&&!found->code.empty(),
            "A retired report reference lost its exact authored C++ program");
        require(!curriculum::stable_block_position(lesson,identity)&&curriculum::active_block_successor(lesson,identity).has_value(),
            "A retired report reference became active or lost its reachable successor");
        return *found;
    };
    const auto load = [](const curriculum::Lesson&) {
        return windows::load_lesson(std::filesystem::path(build::source_root) /
            "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json");
    };
    const auto verify_page = [&](Fixture& fixture,std::size_t page) {
        pump(fixture.host);
        const auto current = lesson_page_state(fixture.view);
        require(IsWindow(fixture.view) && current.lesson_id == lesson_id && current.count == 8 && current.index == page
                && current.page_id == fixture.lesson.pages.at(page).stable_id,
            "The actual text/sequence lesson did not open its requested page");
        const auto regions = lesson_reading_regions(fixture.view);
        // A historical bookmark may point at the comparison page while all
        // of its references remain locked. That page must narrate no answers.
        bool locked_comparison=current.page_id=="page_0007";
        if(locked_comparison)for(std::size_t index=0;index<fixture.lesson.pages.at(page).blocks.size();++index)
            if(!curriculum::reference_activity(fixture.lesson,fixture.lesson.pages.at(page).blocks[index])
                    ||descendant(fixture.view,id(index,0)))locked_comparison=false;
        require(!regions.empty() || locked_comparison,
            "Opening the text/sequence lesson lost its available narration regions");
        // Exercise real RichEdit selection callbacks after construction as well
        // as the formatting callbacks emitted while each page is being built.
        for (const auto& region : regions) {
            wchar_t type[128]{}; GetClassNameW(region.target,type,128);
            if (std::wstring_view(type) != MSFTEDIT_CLASS) continue;
            const auto before = edit_text(region.target);
            CHARRANGE previous{}; SendMessageW(region.target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
            CHARRANGE selected{0,static_cast<LONG>(std::min<std::size_t>(wide(before).size(),8))};
            SendMessageW(region.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected)); pump(fixture.host);
            SendMessageW(region.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous)); pump(fixture.host);
            require(edit_text(region.target) == before,"Selecting lesson text changed its source characters");
        }
        fixture.viewport_layout();
    };
    {
        Fixture fixture({},load);
        require(fixture.lesson.pages.size() == 8,"The text/sequence regression must cover all eight authored pages");
        const auto original_json = fixture.lesson.raw_json;
        require(fixture.lesson.retired_blocks.size()==retired_ids.size(),"The actual report lesson lost one of its four conserved retirements");
        for(const auto identity:retired_ids)require(curriculum::retired_block(fixture.lesson,identity)
            &&!curriculum::stable_block_position(fixture.lesson,identity),"A conserved retired report block reentered the active lesson");
        const auto one_literal=archived_code(fixture.lesson,"block_0007_0002").code;
        const auto three_instructions=archived_code(fixture.lesson,"block_0007_0003").code;
        require(one_literal!=three_instructions&&one_literal.find("\"Informe\\nZona Este\\nFin\"")!=std::string::npos
            &&three_instructions.find("\"Informe\\n\"")!=std::string::npos&&three_instructions.find("\"Zona Este\\n\"")!=std::string::npos,
            "The archived reference programs no longer exercise both independent report constructions");
        for (std::size_t page = 0; page < 8; ++page) {
            std::cout << "QA actual text/sequence lesson page " << page + 1 << "/8\n";
            verify_page(fixture,page);
            fixture.reopen(); verify_page(fixture,page);
            for (std::size_t index = 0; index < fixture.lesson.pages[page].blocks.size(); ++index) {
                const auto& block = fixture.lesson.pages[page].blocks[index];
                if (block.type == BlockType::inline_fill_code) {
                    const auto composition = fixture.control(index,5);
                    for (std::size_t hole = 0; hole < block.answers.size(); ++hole) {
                        const auto field = GetDlgItem(composition,100 + static_cast<int>(hole));
                        require(field != nullptr,"The authored inline answer field is missing");
                        const auto answer = wide(block.answers[hole]);
                        SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(answer.c_str())); pump(fixture.host);
                    }
                    fixture.click(index,3);
                    require(fixture.text(index,4).starts_with("✓ Correcto"),"The authored text/sequence inline answer was rejected");
                } else if (block.type == BlockType::output_prediction) {
                    // Multiline predictions own Enter for newline insertion.
                    // Use their actual Check button rather than the single-line
                    // Enter helper used by the first-program lesson fixtures.
                    fixture.replace(index,2,block.answers.at(0)); fixture.click(index,3);
                    require(fixture.text(index,4).starts_with("✓ Correcto"),"The authored text/sequence prediction was rejected: page="
                        + std::to_string(page) + "/block=" + std::to_string(index) + "/feedback=" + fixture.text(index,4));
                } else if (block.type == BlockType::multiple_choice || block.type == BlockType::fill_code
                        || block.type == BlockType::order_code) {
                    fixture.solve(index);
                }
            }
            if (page == 6) {
                const auto first=curriculum::stable_block_position(fixture.lesson,"block_0006_0002");
                const auto second=curriculum::stable_block_position(fixture.lesson,"block_0006_0006");
                require(first&&second&&first->page==page&&second->page==page
                    &&fixture.lesson.compiler_puzzles_required&&fixture.lesson.activity_after.size()==1
                    &&std::ranges::count(fixture.lesson.pages[page].blocks,BlockType::cpp_compiler,&curriculum::Block::type)==2,
                    "The integrated report page lost its two useful required objectives");
                const auto& first_objective=fixture.lesson.pages[first->page].blocks[first->block];
                const auto& second_objective=fixture.lesson.pages[second->page].blocks[second->block];
                const std::vector<std::string> expected_outputs{"Informe\nZona Este\nFin","Informe\nZona Este\nFin\n"};
                for(const auto* objective:{&first_objective,&second_objective})require(objective->output_check
                    &&objective->output_check->input_comparison==curriculum::TextComparison::exact&&objective->output_check->cases.size()==1
                    &&objective->output_check->cases[0].standard_input.empty()&&objective->output_check->cases[0].outputs==expected_outputs,
                    "A useful report objective lost its exact three-line output or optional final newline");
                require(curriculum::activity_prerequisites(fixture.lesson,second_objective)==std::vector<curriculum::StatePosition>{*first}
                    &&fixture.lesson.reference_after.size()==1&&fixture.lesson.reference_after[0].activity==second_objective.stable_id
                    &&fixture.lesson.reference_after[0].blocks==std::vector<std::string>{"block_0007_0000","block_0007_0001","block_0007_0004"},
                    "The useful report chain or its current comparison references lost their stable dependency owners");
                const auto first_editor=fixture.control(first->block,10);
                const auto hidden_from_reader=[&](std::size_t index){return std::ranges::none_of(lesson_reading_regions(fixture.view),
                    [&](const auto& region){return region.block_index==index;});};
                require(!descendant(fixture.view,id(second->block,10))&&hidden_from_reader(second->block)
                    &&!navigate_lesson_view(fixture.view,7),"An old prediction exposed the later report construction");
                fixture.run(first->block);
                require(fixture.text(first->block,4).starts_with("✗ Incorrecto")&&!descendant(fixture.view,id(second->block,10)),
                    "Executing the unchanged displayed report falsely completed its construction");
                fixture.replace(first->block,10,one_literal+"esto no compila\n");fixture.run(first->block);
                require(fixture.text(first->block,4).empty()&&!descendant(fixture.view,id(second->block,10)),
                    "A compile failure falsely completed the current report objective");
                fixture.replace(first->block,10,one_literal);fixture.run(first->block);
                require(fixture.text(first->block,14)=="Informe\nZona Este\nFin"&&fixture.text(first->block,4).starts_with("✓ Correcto")
                    &&fixture.control(first->block,10)==first_editor&&descendant(fixture.view,id(second->block,10)),
                    "The useful one-literal construction failed or lost its independent editor");
                const auto second_editor=fixture.control(second->block,10);
                fixture.run(second->block);
                require(fixture.text(second->block,4).starts_with("✗ Incorrecto"),"The second useful construction inherited another compiler's success");
                fixture.replace(second->block,10,three_instructions);fixture.run(second->block);
                require(fixture.text(second->block,14)=="Informe\nZona Este\nFin"&&fixture.text(second->block,4).starts_with("✓ Correcto")
                    &&fixture.control(second->block,10)==second_editor&&fixture.text(first->block,10)==one_literal,
                    "The useful three-instruction construction changed another draft or lost its own editor");
                require(!curriculum::stable_block_position(fixture.lesson,"ejecucion_original_texto_secuencia_y_formato_editor"),
                    "Current report navigation reactivated its retired redundant editor");
            } else if (page == 7) {
                require(fixture.lesson.pages[page].stable_id=="page_0007"&&fixture.lesson.pages[page].blocks.size()==3,
                    "The current comparison page lost its three retained prose references");
                const auto regions=lesson_reading_regions(fixture.view);
                for(const auto identity:{"block_0007_0000","block_0007_0001","block_0007_0004"}){
                    const auto at=curriculum::stable_block_position(fixture.lesson,identity);
                    require(at&&at->page==page,"A useful comparison reference lost its stable current position");
                    const auto& reference=fixture.lesson.pages[at->page].blocks[at->block];
                    require(fixture.text(at->block,0)==reference.text&&std::ranges::any_of(regions,[&](const auto& region){
                            return region.block_index==at->block&&region.source_text==reference.text&&region.target==fixture.control(at->block,0);}),
                        "The completed compiler chain failed to reveal its exact retained comparison text and narration");
                    require(!descendant(fixture.view,id(at->block,1))&&!descendant(fixture.view,id(at->block,16)),
                        "The retired comparison programs still created native code cards on the current prose page");
                }
                for(const auto identity:retired_ids)require(std::ranges::none_of(regions,[&](const auto& region){
                    return region.key.find('/'+std::string(identity)+'/')!=std::string::npos;}),
                    "A retired execution or reference program leaked into current comparison narration");
                fixture.reopen();verify_page(fixture,page);
                require(archived_code(fixture.lesson,"block_0007_0002").code==one_literal
                    &&archived_code(fixture.lesson,"block_0007_0003").code==three_instructions,
                    "Reopening the completed comparison page changed either conserved reference program");
            }
            if (page + 1 < 8) fixture.next();
        }
        fixture.select_page(0); verify_page(fixture,0);
        fixture.select_page(7); verify_page(fixture,7);
        require(fixture.lesson.raw_json == original_json,"Opening/navigating the regression lesson changed authored content");
    }
    // Current mandatory objectives redirect a later old bookmark to the first
    // pending useful compiler, retaining each historical draft independently.
    for(std::size_t requested=0;requested<8;++requested){
        Fixture restored([&](state::StateData& data,const curriculum::Lesson& lesson){
            state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,requested);
            for(const auto identity:{"block_0006_0002","block_0006_0006"}){
                const auto at=curriculum::stable_block_position(lesson,identity);require(at.has_value(),"A useful report compiler lost its stable identity");
                const auto& compiler=lesson.pages[at->page].blocks[at->block];state::Draft draft;
                draft.source=compiler.initial_code+"// borrador QA bloqueado\n";
                state::put_draft(data,curriculum::draft_key("programming",lesson,compiler),draft);
            }
        },load);
        const auto expected=std::min(requested,std::size_t{6});
        verify_page(restored,expected);restored.reopen();verify_page(restored,expected);
        const auto later=curriculum::stable_block_position(restored.lesson,"block_0006_0006");
        if(requested>=6)require(!descendant(restored.view,id(later->block,10))&&!navigate_lesson_view(restored.view,7),
            "A restored comparison bookmark bypassed its useful required construction");
        const auto saved=restored.saved();
        for(const auto identity:{"block_0006_0002","block_0006_0006"}){
            const auto at=curriculum::stable_block_position(restored.lesson,identity);const auto& compiler=restored.lesson.pages[at->page].blocks[at->block];
            require(state::get_draft(saved,curriculum::draft_key("programming",restored.lesson,compiler),"").source
                ==compiler.initial_code+"// borrador QA bloqueado\n","Restoring a bookmark discarded a current independent report draft");
        }
    }
    std::cout << "PASS actual text/sequence lesson: fresh opening, eight-page gated navigation, RichEdit selection, narration targets, independent editors and every restored bookmark\n";
}
void text_sequence_application_restoration() {
    wchar_t module[32768]{};
    require(GetModuleFileNameW(nullptr,module,static_cast<DWORD>(std::size(module))) > 0,"Cannot locate the lesson test executable");
    const auto executable = std::filesystem::path(module).parent_path()/L"bin/LearnGameAndEngineDev.exe";
    require(std::filesystem::is_regular_file(executable),"Build the actual application beside the lesson tests before restoration QA");
    const auto lesson = windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json");
    const auto first=curriculum::stable_block_position(lesson,"block_0006_0002");
    const auto second=curriculum::stable_block_position(lesson,"block_0006_0006");
    require(first&&second&&first->page==second->page,"The restoration regression lost its two current useful compilers");
    const auto shell_for = [](DWORD process_id) {
        struct Search { DWORD process_id; HWND shell = nullptr; } search{process_id};
        EnumWindows([](HWND window,LPARAM context)->BOOL {
            auto& search = *reinterpret_cast<Search*>(context); DWORD owner = 0; GetWindowThreadProcessId(window,&owner);
            wchar_t type[128]{}; GetClassNameW(window,type,128);
            if (owner == search.process_id && std::wstring_view(type) == L"LearnGameAndEngineDev.NativeShell") {
                search.shell = window; return FALSE;
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&search));
        return search.shell;
    };
    const auto caption = [](HWND window) {
        require(window != nullptr,"The restored application is missing an expected native text control");
        DWORD_PTR length = 0;
        require(SendMessageTimeoutW(window,WM_GETTEXTLENGTH,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&length) && length < 65536,
            "The restored application is unresponsive while querying native text");
        std::wstring text(static_cast<std::size_t>(length) + 1,L'\0'); DWORD_PTR copied = 0;
        require(SendMessageTimeoutW(window,WM_GETTEXT,text.size(),reinterpret_cast<LPARAM>(text.data()),
                SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&copied),"Cannot read restored native text through Windows message marshalling");
        text.resize(static_cast<std::size_t>(copied)); return utf8(text);
    };
    for (std::size_t page = 0; page < lesson.pages.size(); ++page) {
        const auto token = L"lesson-restore-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(page);
        const auto environment = resolve_windows_environment({ProfileId::Codex,token,false});
        require(!std::filesystem::exists(environment.paths.root),"Application restoration QA must own a new isolated profile");
        initialize_profile_directories(environment.paths);
        {
            state::StateStore store(environment.paths.root); auto data = store.load();
            state::prepare_lesson_state(data,lesson); state::save_current_page(data,lesson,page);
            auto& preferences = data.preferences["native_windows"];
            preferences["ui_language"] = std::string("es"); preferences["content_language"] = std::string("es");
            preferences["catalog/locale"] = std::string("es"); preferences["catalog/route"] = std::string("programming");
            preferences["catalog/course"] = std::string("cpp_fundamentals");
            preferences["catalog/unit"] = std::string("cpp_fundamentals__programas_y_ejecucion"); preferences["navigation/lesson"] = lesson.id;
            for (const auto index : {first->block,second->block}) {
                state::Draft draft; draft.source = lesson.pages[6].blocks[index].initial_code + "// borrador QA bloqueado\n";
                state::put_draft(data,curriculum::draft_key("programming",lesson,lesson.pages[6].blocks[index]),draft);
            }
            store.save(data);
        }
        auto command = L"\"" + executable.wstring() + L"\" --profile Codex --qa-instance " + token;
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_SHOWNOACTIVATE;
        PROCESS_INFORMATION child{};
        require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,
                executable.parent_path().c_str(),&startup,&child),"Cannot launch the actual application for isolated restoration QA");
        struct ChildScope {
            PROCESS_INFORMATION process;
            HWND shell = nullptr;
            ~ChildScope() {
                if (WaitForSingleObject(process.hProcess,0) == WAIT_TIMEOUT) {
                    if (shell) PostMessageW(shell,WM_CLOSE,0,0);
                    // A failing child must not leak a QA window/process into
                    // subsequent tests. This handle is only our created child.
                    if (WaitForSingleObject(process.hProcess,3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess,9);
                }
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
            }
        } owner{child};
        HWND view = nullptr;
        const auto deadline = GetTickCount64() + 20000;
        do {
            require(WaitForSingleObject(child.hProcess,0) == WAIT_TIMEOUT,"The actual application exited while restoring the lesson");
            owner.shell = shell_for(child.dwProcessId);
            if (owner.shell) view = descendant_class(owner.shell,L"LearnGameAndEngineDev.LessonView");
            if (view && GetDlgItem(view,105)
                    && descendant_class(view,L"LearnGameAndEngineDev.ReaderBar")) break;
            WaitForSingleObject(child.hProcess,25);
        } while (GetTickCount64() < deadline);
        require(view && descendant_class(view,L"LearnGameAndEngineDev.ReaderBar"),
            "Automatic restoration did not create the real lesson and its narration reader");
        if(page==0)require(caption(descendant(view,990)) == lesson.title,"Automatic restoration opened a different lesson");
        const auto expected_page=std::min(page,first->page);
        const auto page_label = std::to_string(expected_page + 1) + " de 8";
        require(caption(descendant(view,105)).find(page_label) != std::string::npos,
            "Automatic restoration failed to resolve a bookmark to its pending current objective");
        if (page >= first->page) require(descendant(view,id(first->block,10)) && !descendant(view,id(second->block,10)),
            "The actual executable exposed blocked compiler drafts from an old experiment bookmark");
        if (page == 7) require(!descendant(view,id(2,1)) && !descendant(view,id(3,1)),
            "The actual executable exposed unearned report references from an old comparison bookmark");
        const auto sustained_until = GetTickCount64() + 2200;
        do {
            require(WaitForSingleObject(child.hProcess,0) == WAIT_TIMEOUT && IsWindow(view),
                "The restored lesson closed during normal message-loop permanence QA");
            DWORD_PTR response = 0;
            require(SendMessageTimeoutW(owner.shell,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&response),
                "The restored application's normal Windows message loop stopped responding");
            WaitForSingleObject(child.hProcess,50);
        } while (GetTickCount64() < sustained_until);
        require((page!=0 || caption(descendant(view,990)) == lesson.title)
                && caption(descendant(view,105)).find(page_label) != std::string::npos,
            "Permanence QA changed the restored lesson or page");
        require(PostMessageW(owner.shell,WM_CLOSE,0,0),"Cannot request a normal close of the isolated application");
        require(WaitForSingleObject(child.hProcess,10000) == WAIT_OBJECT_0,"The isolated application did not close normally");
        DWORD exit_code = 1;
        require(GetExitCodeProcess(child.hProcess,&exit_code) && exit_code == 0,"The isolated application did not exit successfully");
        state::StateStore reader(environment.paths.root); const auto saved = reader.load();
        require(state::string_value(saved,"native_windows","navigation/lesson")==lesson.id
                && state::current_page(saved,lesson)==expected_page,
            "Normal application restoration/close changed the active lesson or its page bookmark");
        for (const auto index : {first->block,second->block})
            require(state::get_draft(saved,curriculum::draft_key("programming",lesson,lesson.pages[6].blocks[index]),"").source
                    == lesson.pages[6].blocks[index].initial_code + "// borrador QA bloqueado\n",
                "Normal application restoration/close discarded an independent blocked compiler draft");
        std::cout << "PASS actual executable automatic restoration page " << page + 1 << "/8: responsive >2s, normal close, exit 0, no smoke-test\n";
    }
}
void real_retirement_pages_preserve_all_eight_useful_editors() {
    struct Plan {const char* file;std::size_t page;const char* instruction;const char* editor;};
    const std::array plans{
        Plan{"lesson_primer_programa_y_salida.json",4,"ejecucion_original_consigna","ejecucion_original_editor"},
        Plan{"lesson_texto_secuencia_y_formato.json",6,"ejecucion_original_texto_secuencia_y_formato_consigna","ejecucion_original_texto_secuencia_y_formato_editor"},
        Plan{"lesson_fuente_compilacion_y_diagnostico.json",3,"ejecucion_original_fuente_compilacion_y_diagnostico_consigna","ejecucion_original_fuente_compilacion_y_diagnostico_editor"}
    };
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion";
    std::size_t useful_editors=0;
    for(const auto& plan:plans){
        const auto path=folder/plan.file;const auto original_hash=state::sha256(state::read_bytes(path));
        Fixture f([&](state::StateData& data,const curriculum::Lesson& lesson){
            state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,plan.page);
            for(const auto& p:lesson.pages)for(const auto& block:p.blocks)if(block.type==BlockType::cpp_compiler){
                state::Draft draft;draft.source=block.initial_code+"// borrador QA independiente: "+block.stable_id+"\n";
                state::put_draft(data,curriculum::draft_key("programming",lesson,block),draft);
                state::put_response(data,curriculum::exercise_key(lesson,block),state::Response{"",true});
            }
        },[&](const curriculum::Lesson&){return test::load_pre_retirement_lesson(plan.file);});
        require(f.page()==plan.page,"The original U1 page bookmark changed before retirement");
        const auto original=f.lesson;
        const auto retired_editor=std::ranges::find(original.pages[plan.page].blocks,std::string(plan.editor),&curriculum::Block::stable_id);
        require(retired_editor!=original.pages[plan.page].blocks.end(),"A real Work retirement editor was not found");
        const auto retired_key=curriculum::draft_key("programming",original,*retired_editor);
        const auto before=f.saved();const auto archived_draft=state::get_draft(before,retired_key,"");
        require(!archived_draft.source.empty(),"The retirement QA draft was never stored");
        f.close();const std::array<std::string,2> identities{plan.instruction,plan.editor};
        f.lesson=test::retire_lesson(original,identities);f.supplied_model=true;f.open();
        require(f.page()==plan.page&&f.lesson.compiler_puzzles_required&&!lesson_page_state(f.view).can_next
            &&!navigate_lesson_view(f.view,plan.page+1),"An old marker/correct flag bypassed a useful required editor after retirement");
        const auto regions=lesson_reading_regions(f.view);
        require(std::ranges::none_of(regions,[&](const auto& region){return region.key.find(plan.editor)!=std::string::npos||region.key.find(plan.instruction)!=std::string::npos;}),
            "Retired instructions/editor remain in active narration");
        const auto legacy=retired_lesson_reading_regions(f.view);
        const auto retired_anchor=std::ranges::find_if(legacy,[&](const auto& region){return region.key.find(plan.editor)!=std::string::npos;});
        require(retired_anchor!=legacy.end()&&!retired_anchor->retired_successor.empty()
            &&std::ranges::any_of(regions,[&](const auto& region){return region.anchor.starts_with(retired_anchor->retired_successor);}),
            "A bookmark inside a retired compiler has no reachable active narration successor");
        const auto after=f.saved();
        require(state::get_draft(after,retired_key,"").source==archived_draft.source,"Retirement erased or rewrote the archived compiler draft");
        std::size_t current_editors=0;
        for(const auto& p:f.lesson.pages)for(const auto& block:p.blocks)if(block.type==BlockType::cpp_compiler){
            ++current_editors;
            const auto key=curriculum::draft_key("programming",f.lesson,block);
            require(state::get_draft(after,key,"").source==state::get_draft(before,key,"").source,
                "Retirement merged, moved or rewrote an independent useful compiler draft");
            require(!windows::compiler_puzzle_completed(after,f.lesson,block),"A retired execution or old correct flag accredited a useful editor");
        }
        require(current_editors==2,"A Work U1 retirement plan removed useful practice or retained a redundant compiler");
        useful_editors+=current_editors;
        struct ArchivedTextProbe{std::string marker;bool leaked=false;} probe{"borrador QA independiente: "+std::string(plan.editor)};
        EnumChildWindows(f.view,[](HWND item,LPARAM value)->BOOL{auto& probe=*reinterpret_cast<ArchivedTextProbe*>(value);
            if(window_text(item).find(probe.marker)!=std::string::npos)probe.leaked=true;return !probe.leaked;},reinterpret_cast<LPARAM>(&probe));
        require(!probe.leaked,"A retired compiler draft still has a native visible/accessibility control");
        f.reopen();require(f.page()==plan.page&&state::get_draft(f.saved(),retired_key,"").source==archived_draft.source,
            "Reopening the retired route lost its coherent active bookmark or archived draft");
        require(state::sha256(state::read_bytes(path))==original_hash,"The QA retirement changed an authored lesson asset");
    }
    Fixture final_lesson({},[&](const curriculum::Lesson&){auto model=windows::load_lesson(folder/"lesson_resolver_probar_y_recuperar.json");model.compiler_puzzles_required=true;return model;});
    for(const auto& p:final_lesson.lesson.pages)for(const auto& block:p.blocks)if(block.type==BlockType::cpp_compiler)++useful_editors;
    require(useful_editors==8,"The three retirement plans did not retain exactly the eight useful U1 editors");
    std::cout<<"PASS three actual U1 retirement plans: old bookmarks, archived and independent active drafts, native/narration exclusion and all eight useful editors\n";
}
void integrated_u1_compilers_require_all_eight_real_executions() {
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion";
    struct VerifiedSolution { std::string_view filename,editor,reference; };
    // Work may archive a displayed reference without changing the exercise.
    // Keep this QA plan explicit: a stored expected output is not a program.
    constexpr std::array<VerifiedSolution,8> solutions{{
        {"lesson_primer_programa_y_salida.json","block_0004_0002","block_0005_0002"},
        {"lesson_primer_programa_y_salida.json","block_0005_0006","block_0005_0008"},
        {"lesson_texto_secuencia_y_formato.json","block_0006_0002","block_0007_0002"},
        {"lesson_texto_secuencia_y_formato.json","block_0006_0006","block_0007_0003"},
        {"lesson_fuente_compilacion_y_diagnostico.json","block_0003_0003","block_0006_0002"},
        {"lesson_fuente_compilacion_y_diagnostico.json","block_0003_0008",""},
        {"lesson_resolver_probar_y_recuperar.json","block_0003_0002","block_0005_0002"},
        {"lesson_resolver_probar_y_recuperar.json","block_0005_0006","block_0005_0008"}
    }};
    std::size_t completed=0;
    for(const auto filename:{"lesson_primer_programa_y_salida.json","lesson_texto_secuencia_y_formato.json",
            "lesson_fuente_compilacion_y_diagnostico.json","lesson_resolver_probar_y_recuperar.json"}){
        Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
            state::prepare_lesson_state(data,lesson);bool marked=false;
            // Submitted prior quiz progress isolates the compiler requirement;
            // opaque compiler correctness must never substitute native proof.
            for(std::size_t p=0;p<lesson.pages.size();++p)for(const auto& block:lesson.pages[p].blocks){
                if(block.type==BlockType::cpp_compiler&&!marked){state::save_current_page(data,lesson,p);marked=true;}
                if(block.type==BlockType::heading||block.type==BlockType::paragraph||block.type==BlockType::note||block.type==BlockType::code)continue;
                state::Response old;old.correct=true;old.answer="progreso QA anterior";
                if(block.correct_index)old.selected=static_cast<std::int64_t>(*block.correct_index);
                old.order.assign(block.correct_order.begin(),block.correct_order.end());
                if(block.type==BlockType::output_prediction||block.type==BlockType::fill_code)old.answer=block.answers.at(0);
                if(block.type==BlockType::inline_fill_code||block.type==BlockType::drag_fill_code){
                    winrt::Windows::Data::Json::JsonObject submitted;winrt::Windows::Data::Json::JsonArray values;
                    if(block.type==BlockType::inline_fill_code){for(const auto& answer:block.answers)
                        values.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(answer)));}
                    else for(const auto slot:block.correct_order)
                        values.Append(winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(slot)));
                    submitted.Insert(block.type==BlockType::inline_fill_code?L"answers":L"slots",values);old.raw_json=winrt::to_string(submitted.Stringify());
                }
                state::put_response(data,lesson,block,old);
            }
        },[&](const curriculum::Lesson&){return windows::load_lesson(folder/filename);});
        require(f.lesson.compiler_puzzles_required,"An integrated U1 lesson stopped requiring its useful compilers");
        std::vector<curriculum::StatePosition> compilers;
        for(std::size_t p=0;p<f.lesson.pages.size();++p)for(std::size_t b=0;b<f.lesson.pages[p].blocks.size();++b)
            if(f.lesson.pages[p].blocks[b].type==BlockType::cpp_compiler)compilers.push_back({p,b});
        require(compilers.size()==2,"Integrated U1 lost useful practice or retained a redundant compiler");
        for(const auto position:compilers){
            const auto& compiler=f.lesson.pages[position.page].blocks[position.block];
            require(compiler.output_check&&!windows::compiler_puzzle_completed(f.saved(),f.lesson,compiler),
                "Opaque old compiler correctness accredited an integrated objective");
            if(f.page()!=position.page){require(navigate_lesson_view(f.view,position.page),"A completed earlier objective failed to unlock the next useful page");pump(f.host);}
            require(descendant(f.view,id(position.block,10))&&!lesson_page_state(f.view).can_next,
                "A current useful compiler was hidden or its own pending goal did not block advancement");
            f.run(position.block);
            require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,compiler),"An unchanged initial program falsely completed the requested construction/repair");
            const auto plan=std::ranges::find_if(solutions,[&](const auto& item){return item.filename==filename&&item.editor==compiler.stable_id;});
            require(plan!=solutions.end(),"No verified solution plan for "+f.lesson.id+" / "+compiler.stable_id);
            std::string solution;
            if(plan->reference.empty()){
                solution=compiler.initial_code;const std::string line="std::cout << \"Revision inicial\"\n";
                const auto missing=solution.find(line);require(missing!=solution.npos,"The independent repair no longer contains its intended missing semicolon");
                solution.insert(missing+line.size()-1,";");
            }else{
                const curriculum::Block* reference=nullptr;
                if(const auto active=curriculum::stable_block_position(f.lesson,plan->reference))
                    reference=&f.lesson.pages[active->page].blocks[active->block];
                else if(const auto archived=std::ranges::find(f.lesson.retired_blocks,plan->reference,&curriculum::Block::stable_id);
                        archived!=f.lesson.retired_blocks.end())reference=&*archived;
                require(reference&&reference->type==BlockType::code&&reference->language=="cpp"&&reference->code.find("int main")!=std::string::npos,
                    "Missing authored C++ solution "+f.lesson.id+" / "+compiler.stable_id+" / "+std::string(plan->reference));
                solution=reference->code;
            }
            require(!solution.empty(),"Empty verified solution for "+f.lesson.id+" / "+compiler.stable_id);
            f.replace(position.block,10,solution);f.run(position.block);
            require(f.text(position.block,4).starts_with("✓ Correcto")&&windows::compiler_puzzle_completed(f.saved(),f.lesson,compiler),
                "A fresh current reference execution did not complete its own required objective");
            ++completed;f.reopen();require(f.text(position.block,10)==solution&&f.text(position.block,4).starts_with("✓ Correcto"),
                "Reopening an integrated useful objective lost its independent source or persistent success");
        }
        const auto first=compilers.front(),last=compilers.back();
        require(navigate_lesson_view(f.view,first.page),"Backward navigation to reset the first useful compiler failed");pump(f.host);f.click(first.block,12);
        require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[first.page].blocks[first.block])
            &&windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[last.page].blocks[last.block])
            &&!lesson_page_state(f.view).can_next,"Reset erased another useful objective or failed to restore the first requirement");
    }
    require(completed==8,"The integrated U1 acceptance did not execute all eight useful compiler objectives");
    std::cout<<"PASS all eight integrated useful U1 compilers: mandatory gate, independent real execution, authored solutions, opaque history, reopen and scoped Reset\n";
}
void completed_compilers_keep_navigation_after_active_content() {
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_resolver_probar_y_recuperar.json";
    const auto original_hash=state::sha256(state::read_bytes(path));
    Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
        state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,3);
        data.preferences["navigation_geometry_qa"]["unchanged"]=std::string(" \t original preference\r\n");
        // Only earlier quiz responses isolate entry to the actual construction.
        // Neither compiler receives synthetic completion or program receipts.
        for(std::size_t page=0;page<3;++page)for(const auto& block:lesson.pages[page].blocks){
            if(block.type!=BlockType::multiple_choice&&block.type!=BlockType::fill_code&&block.type!=BlockType::output_prediction)continue;
            state::Response answer;answer.correct=true;
            if(block.correct_index)answer.selected=static_cast<std::int64_t>(*block.correct_index);
            if(!block.answers.empty())answer.answer=block.answers.front();
            state::put_response(data,lesson,block,answer);
        }
    },[&](const curriculum::Lesson&){return windows::load_lesson(path);},true);
    require(!IsWindowVisible(f.host),"Navigation geometry QA exposed or activated the desktop");
    require(f.page()==3,"Navigation geometry QA did not restore the actual U1L5 construction");
    const auto first=curriculum::stable_block_position(f.lesson,"block_0003_0002");
    const auto last=curriculum::stable_block_position(f.lesson,"block_0005_0006");
    require(first&&last&&first->page==3&&last->page==5,"The two useful U1L5 compilers changed ownership");
    const auto reference=[&](std::string_view identity){
        const auto archived=std::ranges::find(f.lesson.retired_blocks,identity,&curriculum::Block::stable_id);
        require(archived!=f.lesson.retired_blocks.end()&&archived->type==BlockType::code&&archived->language=="cpp"
            &&archived->code.find("int main")!=std::string::npos,"Missing preserved authored navigation QA solution");
        return archived->code;
    };
    const auto source_b=reference("block_0005_0002"),source_c=reference("block_0005_0008");
    const auto untouched=f.saved().preferences.at("navigation_geometry_qa");
    const auto verify=[&](std::string_view stage){
        const auto next=f.control(117),pane=GetParent(next);RECT footer{};GetWindowRect(next,&footer);
        require((GetWindowLongPtrW(next,GWL_STYLE)&WS_VISIBLE)&&footer.bottom>footer.top,
            "The end-of-content navigation action disappeared");
        std::size_t rows=0;
        for(auto child=GetWindow(pane,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            if(child==next||!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
            const auto control=GetDlgCtrlID(child);if(control<1000&&control!=990)continue;
            RECT bounds{};GetWindowRect(child,&bounds);if(bounds.bottom<=bounds.top||bounds.right<=bounds.left)continue;
            ++rows;
            require(footer.top>=bounds.bottom,"Navigation moved before active content: page="+std::to_string(f.page()+1)
                +" stage="+std::string(stage)+" control="+std::to_string(control)+" content_bottom="+std::to_string(bounds.bottom)
                +" navigation_top="+std::to_string(footer.top));
        }
        require(rows>0,"Navigation geometry QA did not inspect active content rows");
        require(window_text(next)==(f.page()+1==f.lesson.pages.size()?"Finalizar lección":"Continuar"),
            "Navigation lost its page-specific label");
        require(!IsWindowVisible(f.host)&&f.saved().preferences.at("navigation_geometry_qa")==untouched,
            "Navigation layout exposed the desktop or changed unrelated preferences");
    };
    const auto& first_goal=f.lesson.pages[first->page].blocks[first->block];
    const auto& last_goal=f.lesson.pages[last->page].blocks[last->block];
    verify("initial construction");f.run(first->block);verify("incorrect construction");
    require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,first_goal),"The untouched construction accredited navigation QA");
    f.replace(first->block,10,source_b);f.run(first->block);verify("correct construction reveals note and choice");
    require(windows::compiler_puzzle_completed(f.saved(),f.lesson,first_goal)&&f.text(first->block,4).starts_with("✓ Correcto"),
        "The actual construction did not execute its preserved authored solution");
    require(!lesson_page_state(f.view).can_next,"Compiler completion bypassed the remaining current multiple choice");
    f.solve(4);verify("current choice solved");
    require(lesson_page_state(f.view).can_next,"The completed construction and choice did not unlock Continue");
    f.reopen();verify("construction reopened");
    require(f.text(first->block,10)==source_b&&windows::compiler_puzzle_completed(f.saved(),f.lesson,first_goal),
        "Construction reopen lost its source or persistent achievement");
    f.next();verify("recovery page");
    for(std::size_t index=0;index<f.lesson.pages[f.page()].blocks.size();++index)
        if(f.lesson.pages[f.page()].blocks[index].type==BlockType::multiple_choice){f.solve(index);verify("recovery choice solved");}
    f.next();verify("initial final compiler");
    require(f.page()==5&&!IsWindowEnabled(f.control(117)),"The final compiler was bypassed before its own execution");
    f.run(last->block);verify("incorrect final compiler");
    require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,last_goal)&&!IsWindowEnabled(f.control(117)),"Turno B falsely completed the requested Turno C change");
    f.replace(last->block,10,source_c);f.run(last->block);verify("correct final compiler reveals explanation");
    require(windows::compiler_puzzle_completed(f.saved(),f.lesson,last_goal)&&IsWindowEnabled(f.control(117)),
        "The actual changed-turn execution did not unlock Finish");
    f.reopen();verify("final compiler reopened");
    require(f.text(last->block,10)==source_c&&f.text(last->block,4).starts_with("✓ Correcto")&&IsWindowEnabled(f.control(117)),
        "Reopening lost the final source or correct feedback");
    f.click(last->block,12);verify("final compiler Reset hides explanation");f.reopen();verify("Reset reopened");
    require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,last_goal)&&!IsWindowEnabled(f.control(117))
        &&windows::compiler_puzzle_completed(f.saved(),f.lesson,first_goal)&&f.text(last->block,10)==last_goal.initial_code,
        "Reset changed another compiler achievement or failed to restore the final pending goal");
    require(state::sha256(state::read_bytes(path))==original_hash,"Navigation QA changed an authored lesson asset");
    std::cout<<"PASS actual U1L5 p4/p6: hidden owned host, real authored compile/error/success, revealed note/choice/explanation, Continue/Finish after all content, source/achievements retained on reopen, scoped Reset and original unrelated preferences/assets\n";
}
void retired_reading_regions_are_empty_during_import_and_return_after_cancel() {
    // A presentation-only retirement on a copy of page zero exercises the
    // visible heading path; no authored asset or student's profile is changed.
    Fixture f({},[](const curriculum::Lesson& source){
        const auto paragraph=std::ranges::find(source.pages[0].blocks,BlockType::paragraph,&curriculum::Block::type);
        require(paragraph!=source.pages[0].blocks.end(),"The import retirement fixture has no presentation paragraph");
        const std::array identities{paragraph->stable_id};return test::retire_lesson(source,identities);
    });
    require(f.page()==0&&!retired_lesson_reading_regions(f.view).empty(),"The retirement fixture did not reach a real first-page heading/script");
    state::StateStore storage(f.environment.paths.root);require(state::can_import_android_profile(storage.load()),"Import QA requires an untouched private profile");
    const auto before=read_lesson_state(f.view);
    SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(104,BN_CLICKED),0);pump(f.host);
    require(descendant(f.view,201)&&IsWindowVisible(descendant(f.view,201)),"The actual import panel did not open");
    require(lesson_reading_regions(f.view).empty()&&retired_lesson_reading_regions(f.view).empty()
        &&legacy_lesson_reading_regions(f.view).empty()&&pre_caption_lesson_reading_regions(f.view).empty(),
        "Import exposed a hidden page or archived narration script");
    SendMessageW(descendant(f.view,206),BM_CLICK,0,0);pump(f.host);
    require(!descendant(f.view,201)&&!retired_lesson_reading_regions(f.view).empty()&&read_lesson_state(f.view)==before,
        "Canceling import failed to restore the archived-bookmark projection or changed lesson data");
    std::cout<<"PASS retired reading projection during actual import/cancel: heading is safe, no folder selected, no data imported\n";
}
void multiple_input_compiler_is_one_pending_objective_until_every_case_passes() {
    Fixture f({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({
      "id":"qa_multiple_input_compiler","title":"Dos entradas","compilerPuzzlesRequired":true,"pages":[{"blocks":[
        {"type":"cpp_compiler","question":"Comprueba ambas entradas.","initialCode":"#include <iostream>\nint main(){int n=0;std::cin >> n;std::cout << n*2;}\n","initialStandardInput":"1","outputCheck":{"cases":[{"standardInput":"1","outputs":["2"]},{"standardInput":"2","outputs":["4"]}]}},
        {"type":"paragraph","text":"Después de los dos casos."}
      ]},{"blocks":[{"type":"paragraph","text":"Página final."}]}]})json");});
    f.run(0);
    require(f.text(0,4).empty()&&f.text(0,34).find("Caso correcto. Casos comprobados: 1 de 2.")!=std::string::npos
        &&f.text(0,34).find("Para continuar, comprueba los casos restantes")!=std::string::npos
        &&!lesson_page_state(f.view).can_next&&!descendant(f.view,id(1,0)),
        "A single passing input displayed global Correcto, revealed later content or enabled advance");
    require(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==0&&region.is_exercise&&!region.solved;}),
        "A partial compiler case released its narration gate");
    f.reopen();require(f.text(0,4).empty()&&f.text(0,34).find("1 de 2")!=std::string::npos,
        "Reopening lost independent checked cases or invented a completed objective");
    f.replace(0,15,"2");f.run(0);
    require(f.text(0,4)=="✓ Correcto"&&lesson_page_state(f.view).can_next&&descendant(f.view,id(1,0)),
        "Changing only input failed to combine both actual cases of the same program");
    f.replace(0,15,"un caso no declarado");f.run(0);
    require(f.text(0,4)=="✓ Correcto"&&lesson_page_state(f.view).can_next,
        "Compiling an unlisted input after completion revoked the approved puzzle");
    std::cout<<"PASS one compiler with two real inputs: partial status and reader gate, checked-case persistence, independent inputs and sticky full success\n";
}
void prepare_stderr_visual_profile(std::string_view scenario, std::wstring_view token) {
    const bool u8 = scenario == "u8";
    require((u8 || scenario == "u10") && is_valid_qa_instance(token), "Unsafe stderr visual QA scenario or instance token");
    const auto path = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals" /
        (u8 ? "excepciones_y_recuperacion/lesson_limites_comprobados_y_tipos_de_captura.json"
            : "aleatoriedad_y_reproducibilidad/lesson_combinar_ensayos_generados_y_casos_elegidos.json");
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    const auto owner = curriculum::stable_block_position(lesson, u8 ? "block_0002_0005" : "block_0004_0004");
    const auto target = curriculum::stable_block_position(lesson, u8 ? "block_0002_0008" : "block_0004_0007");
    const auto reference = curriculum::stable_block_position(lesson, u8 ? "block_0003_0005" : "block_0005_0001");
    require(owner && owner->page == (u8 ? 2u : 4u) && owner->block == (u8 ? 5u : 4u)
        && target && target->page == owner->page && target->block == (u8 ? 8u : 7u)
        && reference && reference->page == owner->page + 1 && reference->block == (u8 ? 5u : 1u)
        && lesson.compiler_puzzles_required, "The current stderr QA owner, target, reference or required policy changed");
    const auto& constructor = lesson.pages[owner->page].blocks[owner->block];
    const auto& diagnostic = lesson.pages[target->page].blocks[target->block];
    const auto& source = lesson.pages[reference->page].blocks[reference->block];
    const auto prerequisites = curriculum::activity_prerequisites(lesson, diagnostic);
    const auto source_owner = curriculum::reference_activity(lesson, source);
    require(constructor.type == BlockType::cpp_compiler && constructor.output_check
        && constructor.output_check->cases.size() == 1 && !constructor.initial_code.empty()
        && diagnostic.type == BlockType::cpp_compiler && diagnostic.output_check
        && diagnostic.output_check->cases.size() == 1 && !diagnostic.initial_code.empty()
        && diagnostic.output_check->cases[0].standard_error == curriculum::StandardErrorExpectation::non_blank
        && diagnostic.output_check->cases[0].outputs == std::vector<std::string>{u8 ? "Indice invalido:Fin" : "Fallos elegidos:2\n"}
        && constructor.output_check->cases[0].outputs == std::vector<std::string>{u8 ? "2:Fin\n9:Fin\nIndice invalido:Fin\n" : "Fallos elegidos:0\nFallos generados:0"}
        && curriculum::activity_prerequisites(lesson, constructor).empty()
        && prerequisites.size() == 1 && prerequisites[0].page == owner->page && prerequisites[0].block == owner->block
        && source.type == BlockType::code && source.language == "cpp" && !source.code.empty()
        && source_owner && source_owner->page == owner->page && source_owner->block == owner->block,
        "The current stderr QA source, stdout/stderr contracts or exact owner relationship changed");
    // These actual pages have no earlier compiler gate. Keep earlier quizzes
    // unanswered and let the user execute the same-page constructor genuinely.
    for (std::size_t prior = 0; prior < owner->page; ++prior)
        for (const auto& block : lesson.pages[prior].blocks)
            require(!curriculum::has_compiler_objective(block), "The selected page acquired an earlier mandatory compiler");
    const auto environment = resolve_windows_environment({ProfileId::Codex, std::wstring(token), false});
    require(environment.paths.root.parent_path().filename() == L"qa"
        && environment.paths.root.parent_path().parent_path().filename() == L"Codex"
        && environment.paths.root.filename() == token, "Visual stderr QA resolved outside the isolated Codex QA directory");
    state::require_safe_path(environment.paths.root);
    require(!std::filesystem::exists(environment.paths.root), "Visual stderr QA refuses to reuse or overwrite an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    require(std::filesystem::create_directory(environment.paths.root), "Another process already reserved this visual QA profile");
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    const auto empty = store.load();
    require(empty.preferences.empty(), "The newly reserved stderr QA profile unexpectedly contains previous state");
    state::StateData data;
    state::prepare_lesson_state(data, lesson);
    state::save_current_page(data, lesson, owner->page);
    auto& preferences = data.preferences["native_windows"];
    preferences["ui_language"] = std::string("es");
    preferences["content_language"] = std::string("es");
    preferences["catalog/locale"] = std::string("es");
    preferences["catalog/route"] = std::string("programming");
    preferences["catalog/course"] = std::string("cpp_fundamentals");
    const auto unit_end = lesson.id.rfind("__");
    require(unit_end != std::string::npos, "The selected stderr lesson lost its catalog unit identity");
    preferences["catalog/unit"] = lesson.id.substr(0, unit_end);
    preferences["navigation/lesson"] = lesson.id;
    // Only the earlier owner's authored reference is copied as an explicit QA
    // draft. It still needs a real execution; the stderr editor has no draft.
    state::Draft draft;
    draft.source = source.code;
    draft.input = constructor.standard_input.value_or("");
    const auto owner_key = curriculum::draft_key("programming", lesson, constructor);
    const auto diagnostic_key = curriculum::draft_key("programming", lesson, diagnostic);
    state::put_draft(data, owner_key, draft);
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        require(!state::get_response(data, lesson, block), "Preparing stderr QA invented a quiz response or achievement");
        if (block.type == BlockType::cpp_compiler) {
            require(windows::compiler_puzzle_case_count(data, lesson, block) == 0
                && !windows::compiler_puzzle_completed(data, lesson, block),
                "Preparing stderr QA invented a compiler receipt or achievement");
            if (block.stable_id != constructor.stable_id)
                require(!state::get_draft(data, curriculum::draft_key("programming", lesson, block), "").has_saved,
                    "Preparing stderr QA seeded an unrelated or target compiler draft");
        }
    }
    state::validate_lesson_state(data, lesson);
    store.save(data);
    const auto saved = store.load();
    const auto restored = state::get_draft(saved, owner_key, "");
    const auto untouched = state::get_draft(saved, diagnostic_key, diagnostic.initial_code);
    require(saved == data && state::current_page(saved, lesson) == owner->page
        && restored.has_saved && restored.source == source.code && restored.input == draft.input
        && !untouched.has_saved && untouched.source == diagnostic.initial_code
        && !windows::compiler_puzzle_completed(saved, lesson, constructor)
        && !windows::compiler_puzzle_completed(saved, lesson, diagnostic),
        "The prepared stderr QA changed its page, exact owner draft or uncredited initial target");
    require(state::sha256(state::read_bytes(path)) == asset_hash, "Preparing stderr visual QA changed its authored asset");
    std::cout << "profile=Codex\nqa_instance=" << utf8(token)
        << "\ndata_root=" << utf8(environment.paths.root.native()) << "\nlesson=" << lesson.id
        << "\npage=" << owner->page + 1 << "\npage_index=" << owner->page
        << "\nowner=" << constructor.stable_id << "\nowner_source_reference=" << source.stable_id
        << "\nowner_source_seed=authored-reference-draft-only-not-an-execution\ntarget=" << diagnostic.stable_id
        << "\ntarget_source=untouched-initialCode\ntarget_draft=none\nsynthetic_prerequisites=none"
        << "\nquiz_responses=none\ncompiler_receipts=none\ncompiler_achievements=none\nwindows_created=0\n";
}

void runtime_stderr_objective_keeps_legacy_history_and_requires_both_current_channels() {
    Fixture f({},[](const curriculum::Lesson&){auto model=windows::parse_lesson_json(R"json({
      "id":"qa_runtime_stderr","title":"Dos canales","compilerPuzzlesRequired":true,
      "pages":[{"blocks":[
        {"id":"runtime","type":"cpp_compiler","question":"Observa ambos canales.","initialCode":"#include <iostream>\nint main(){bool mostrarDiagnostico=false;std::cout << \"Indice invalido:Fin\";if(mostrarDiagnostico)std::cerr << \"diagnostico variable de la biblioteca\";}\n","outputCheck":{"cases":[{"standardInput":"","outputs":["Indice invalido:Fin"],"explanation":"Ambos canales observados."}]}},
        {"id":"reference","type":"paragraph","text":"Referencia posterior al objetivo completo."}
      ]},{"blocks":[{"type":"paragraph","text":"Fin de la comprobación."}]}]})json");
        model.state_layout.version=1;model.state_layout.locale="es";
        model.state_layout.lineage=state::sha256("qa-runtime-stderr-layout-v1");model.state_layout.legacy_lineage=model.state_layout.lineage;
        for(const auto& page:model.pages){curriculum::StatePage layout;layout.id=page.stable_id;
            for(const auto& block:page.blocks)layout.blocks.push_back(block.stable_id);
            model.state_layout.origin.push_back(layout);model.state_layout.current.push_back(std::move(layout));}
        model.reference_after.push_back({model.pages[0].blocks[0].stable_id,{model.pages[0].blocks[1].stable_id}});
        curriculum::validate_reference_relationships(model);return model;});
    f.run(0);
    const auto legacy=f.saved();const auto& original=f.lesson.pages[0].blocks[0];
    require(windows::compiler_puzzle_completed(legacy,f.lesson,original)&&lesson_page_state(f.view).can_next,
        "The legacy stdout-only contract did not retain its existing behavior");
    state::Preferences history;
    for(const auto& [key,value]:legacy.preferences.at("native_windows"))
        if(key.starts_with("compiler-puzzle/v1/"))history.emplace(key,value);
    require(!history.empty(),"The native legacy execution did not publish its historical proof");
    f.close();
    f.lesson.pages[0].blocks[0].output_check->cases[0].standard_error=curriculum::StandardErrorExpectation::non_blank;
    f.open();
    const auto retained=[&]{const auto saved=f.saved();for(const auto& [key,value]:history){
        const auto found=state::find(saved,"native_windows",key);
        require(found&&*found==value,"A new stderr requirement overwrote or erased an earlier objective's history");}};
    retained();
    require(f.text(0,10)==original.initial_code&&f.text(0,4).empty()&&!lesson_page_state(f.view).can_next
        &&!descendant(f.view,id(1,0)),"An old stdout achievement accredited the new runtime stderr objective or revealed its reference");
    int submitted=0;bool last_correct=true;LessonViewHooks hooks;
    hooks.feedback_changed=[&](HWND,std::size_t,bool correct){++submitted;last_correct=correct;};set_lesson_view_hooks(f.view,hooks);
    f.run(0);
    require(f.text(0,14)=="Indice invalido:Fin"&&f.text(0,4).starts_with("✗ Incorrecto")
        &&f.text(0,4).find("La salida estándar es correcta; falta el diagnóstico solicitado en stderr.")!=std::string::npos
        &&submitted==1&&!last_correct&&!lesson_page_state(f.view).can_next&&!descendant(f.view,id(1,0)),
        "Stdout without the requested runtime diagnostic became a complete success or lost its partial feedback");
    require(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==0&&region.is_exercise&&!region.solved;}),
        "The partial stdout verdict released the reader gate");
    retained();f.reopen();
    require(f.text(0,10)==original.initial_code&&f.text(0,14).empty()&&!lesson_page_state(f.view).can_next,
        "Reopening a partial stdout observation invented stderr, a completed objective or a fresh console");
    auto changed=original.initial_code;const auto flag=changed.find("false");require(flag!=changed.npos,"Missing QA diagnostic flag");changed.replace(flag,5,"true");
    f.replace(0,10,"#error QA_BUILD_DIAGNOSTIC\nint main(){}\n");f.run(0);
    require(f.text(0,4).empty()&&f.text(0,14).find("QA_BUILD_DIAGNOSTIC")!=std::string::npos
        &&!lesson_page_state(f.view).can_next&&!descendant(f.view,id(1,0)),
        "Build stderr was treated as the requested successful runtime diagnostic");
    retained();f.replace(0,10,changed);f.run(0);
    const auto complete=f.text(0,4);
    require(complete=="✓ Correcto\n\nAmbos canales observados."&&f.text(0,14)=="Indice invalido:Fin"
        &&lesson_page_state(f.view).can_next&&descendant(f.view,id(1,0)),
        "One successful current execution with both channels did not complete the new goal and reveal its reference");
    SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(id(0,79),BN_CLICKED),0);pump(f.host);
    require(f.text(0,14)=="diagnostico variable de la biblioteca","The runtime stderr channel was lost or mixed with build diagnostics");
    retained();f.reopen();
    require(f.text(0,10)==changed&&f.text(0,4)==complete&&f.text(0,14).empty()&&lesson_page_state(f.view).can_next,
        "Reopening lost the full two-channel proof/draft or restored stale console output");
    f.replace(0,10,original.initial_code);f.run(0);
    require(f.text(0,4)==complete&&f.text(0,14)=="Indice invalido:Fin"&&lesson_page_state(f.view).can_next,
        "Rerunning without stderr revoked an already completed puzzle or hid its fresh stdout");
    retained();f.replace(0,10,"#error QA_AFTER_SUCCESS\nint main(){}\n");f.run(0);
    require(f.text(0,4)==complete&&f.text(0,14).find("QA_AFTER_SUCCESS")!=std::string::npos&&lesson_page_state(f.view).can_next,
        "A later build error replaced a completed two-channel verdict or hid diagnostics");
    retained();f.reopen();require(f.text(0,4)==complete&&lesson_page_state(f.view).can_next,
        "The persisted two-channel achievement was not independent of its later edited draft");
    f.click(0,12);require(f.text(0,4).empty()&&!lesson_page_state(f.view).can_next&&!descendant(f.view,id(1,0)),
        "Only explicit Reset must revoke the completed runtime-channel objective");
    std::cout<<"PASS runtime stderr: legacy stdout proof preserved, current two-channel goal, partial reader/reference gates, build rejection, reopen, sticky success and explicit reset\n";
}
void integrated_u8l3_stderr_objective_preserves_history_and_requires_real_runtime_diagnostics() {
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/excepciones_y_recuperacion/lesson_limites_comprobados_y_tipos_de_captura.json";
    const auto asset_hash=state::sha256(state::read_bytes(path));
    Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
        state::prepare_lesson_state(data,lesson);
        // Earlier quiz answers isolate both real compiler objectives. No
        // compiler proof is seeded: each independent project must execute.
        for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
            if(block.type!=BlockType::output_prediction&&block.type!=BlockType::multiple_choice)continue;
            state::Response answer;answer.correct=true;
            if(block.type==BlockType::output_prediction)answer.answer=block.answers.at(0);
            else answer.selected=static_cast<std::int64_t>(*block.correct_index);
            state::put_response(data,curriculum::exercise_key(lesson,block),answer);
        }
        state::save_current_page(data,lesson,2);
    },[&](const curriculum::Lesson&){
        auto model=windows::load_lesson(path);
        const auto diagnostic=curriculum::stable_block_position(model,"block_0002_0008");
        require(diagnostic&&diagnostic->page==2&&diagnostic->block==8&&model.compiler_puzzles_required,
            "The current U8L3 diagnostic objective lost its stable identity or required status");
        auto& goal=model.pages[2].blocks[8];
        require(goal.output_check&&goal.output_check->cases.size()==1
            &&goal.output_check->cases[0].standard_error==curriculum::StandardErrorExpectation::non_blank
            &&goal.output_check->cases[0].outputs==std::vector<std::string>{"Indice invalido:Fin"},
            "The integrated U8L3 objective must require its actual stable stdout and runtime stderr");
        // Reproduce the earlier stdout-only proof at the same real storage
        // address, then reopen with the strengthened current authored goal.
        goal.output_check->cases[0].standard_error=curriculum::StandardErrorExpectation::ignored;
        return model;
    });
    require(f.page()==2&&!descendant(f.view,id(8,10)),
        "The diagnostic editor escaped its independent constructor prerequisite");
    const auto reference=curriculum::stable_block_position(f.lesson,"block_0003_0005");
    require(reference&&f.lesson.pages[reference->page].blocks[reference->block].type==BlockType::code,
        "The integrated U8L3 constructor lost its authored reference program");
    const auto constructor_source=f.lesson.pages[reference->page].blocks[reference->block].code;
    f.replace(5,10,constructor_source);f.run(5);
    require(windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[2].blocks[5])
        &&descendant(f.view,id(8,10)),"The real independent constructor failed to unlock the diagnostic editor");
    const auto original_source=f.lesson.pages[2].blocks[8].initial_code;
    f.run(8);
    const auto legacy=f.saved();
    require(f.text(8,14)=="Indice invalido:Fin"&&windows::compiler_puzzle_completed(legacy,f.lesson,f.lesson.pages[2].blocks[8]),
        "The real original U8L3 stdout-only execution failed to produce its historical proof");
    state::Preferences history;
    for(const auto& [key,value]:legacy.preferences.at("native_windows"))
        if(key.starts_with("compiler-puzzle/v1/"))history.emplace(key,value);
    require(!history.empty(),"The actual compiler executions did not publish historical proofs");
    f.close();f.lesson.pages[2].blocks[8].output_check->cases[0].standard_error=curriculum::StandardErrorExpectation::non_blank;f.open();
    const auto retained=[&]{const auto data=f.saved();for(const auto& [key,value]:history){
        const auto found=state::find(data,"native_windows",key);
        require(found&&*found==value,"Strengthening the integrated U8L3 objective erased historical compiler proof data");}};
    retained();
    require(f.page()==2&&f.text(8,10)==original_source&&f.text(8,4).empty()
        &&!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[2].blocks[8])
        &&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,3),
        "The older stdout-only proof completed the integrated two-channel goal or unlocked its references");
    f.run(8);
    require(f.text(8,14)=="Indice invalido:Fin"&&f.text(8,4).find("falta el diagnóstico solicitado en stderr")!=std::string::npos
        &&!lesson_page_state(f.view).can_next,"The actual false flag failed to retain a partial stdout result");
    SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(id(8,79),BN_CLICKED),0);pump(f.host);
    // Empty channels have no selectable menu item; console_present falls back
    // to the existing stdout rather than displaying an empty alternative.
    require(f.text(8,14)=="Indice invalido:Fin",
        "The false flag fabricated a selectable runtime library diagnostic: "+f.text(8,14));
    require(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){
        return region.block_index==8&&region.is_exercise&&!region.solved;}),
        "The integrated partial result released the diagnostic reader boundary");
    retained();f.reopen();
    require(f.text(8,10)==original_source&&f.text(8,14).empty()&&!lesson_page_state(f.view).can_next,
        "Reopening the integrated partial attempt lost its draft or restored stale console output");
    f.replace(8,10,"#error QA_U8L3_BUILD_DIAGNOSTIC\nint main(){}\n");f.run(8);
    require(f.text(8,4).empty()&&f.text(8,14).find("QA_U8L3_BUILD_DIAGNOSTIC")!=std::string::npos
        &&!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[2].blocks[8])
        &&!lesson_page_state(f.view).can_next,"Actual build diagnostics accredited a successful U8L3 runtime observation");
    auto enabled=original_source;const auto flag=enabled.find("mostrarDiagnostico{false}");
    require(flag!=enabled.npos,"The authored U8L3 diagnostic flag changed unexpectedly");
    enabled.replace(flag,std::string_view("mostrarDiagnostico{false}").size(),"mostrarDiagnostico{true}");
    retained();f.replace(8,10,enabled);f.run(8);
    const auto complete=f.text(8,4);
    require(complete=="✓ Correcto\n\n"+f.lesson.pages[2].blocks[8].output_check->cases[0].explanation
        &&f.text(8,14)=="Indice invalido:Fin"&&lesson_page_state(f.view).can_next,
        "The authored true flag failed to complete both channels with the current success explanation");
    SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(id(8,79),BN_CLICKED),0);pump(f.host);
    require(!f.text(8,14).empty()&&f.text(8,14)!="Indice invalido:Fin",
        "The real rejected vector index did not preserve its separate runtime stderr channel");
    retained();f.reopen();
    require(f.text(8,10)==enabled&&f.text(8,4)==complete&&f.text(8,14).empty()&&lesson_page_state(f.view).can_next,
        "The current U8L3 achievement or independent draft failed to persist without replaying stale output");
    f.next();
    require(f.page()==3&&descendant(f.view,id(6,0))&&descendant(f.view,id(7,1))
        &&std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==7;}),
        "The actual completed diagnostic did not release its own reference code and narration");
    f.select_page(2);f.replace(8,10,original_source);f.run(8);
    require(f.text(8,4)==complete&&f.text(8,14)=="Indice invalido:Fin"&&lesson_page_state(f.view).can_next,
        "Recompiling with the false flag revoked the sticky integrated U8L3 achievement");
    retained();f.reopen();
    require(f.text(8,4)==complete&&f.text(8,10)==original_source,
        "The integrated achievement did not survive reopening with a later independent edit");
    f.click(8,12);
    require(f.text(8,4).empty()&&!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[2].blocks[8])
        &&windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[2].blocks[5])
        &&f.text(5,10)==constructor_source&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,3),
        "Reset failed to revoke only the diagnostic objective and preserve its independent constructor");
    require(state::sha256(state::read_bytes(path))==asset_hash,"The integrated diagnostic QA modified an authored lesson asset");
    std::cout<<"PASS current integrated U8L3: real independent constructor, false/true MSVC executions, exact stable stdout, separate variable stderr, historical proof isolation, build rejection, reader/reference gates, reopen, sticky success and scoped Reset\n";
}
void integrated_u10l4_stderr_objective_requires_its_own_constructor_and_runtime_report() {
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/aleatoriedad_y_reproducibilidad/lesson_combinar_ensayos_generados_y_casos_elegidos.json";
    const auto asset_hash=state::sha256(state::read_bytes(path));
    const auto published=windows::load_lesson(path);
    const auto constructor=curriculum::stable_block_position(published,"block_0004_0004");
    const auto diagnostic=curriculum::stable_block_position(published,"block_0004_0007");
    const auto constructor_reference=curriculum::stable_block_position(published,"block_0005_0001");
    const auto diagnostic_reference=curriculum::stable_block_position(published,"block_0005_0008");
    const auto frontier=curriculum::stable_block_position(published,"block_0005_0006");
    const auto frontier_reference=curriculum::stable_block_position(published,"block_0005_0010");
    require(constructor&&constructor->page==4&&constructor->block==4
        &&diagnostic&&diagnostic->page==4&&diagnostic->block==7
        &&constructor_reference&&constructor_reference->page==5&&constructor_reference->block==1
        &&diagnostic_reference&&diagnostic_reference->page==5&&diagnostic_reference->block==8
        &&frontier&&frontier->page==5&&frontier->block==6
        &&frontier_reference&&frontier_reference->page==5&&frontier_reference->block==10
        &&published.compiler_puzzles_required,"The current U10L4 compiler/reference identities or required status changed");
    const auto& constructor_goal=published.pages[4].blocks[4];
    const auto& diagnostic_goal=published.pages[4].blocks[7];
    require(constructor_goal.output_check&&diagnostic_goal.output_check
        &&diagnostic_goal.output_check->cases.size()==1
        &&diagnostic_goal.output_check->cases[0].standard_error==curriculum::StandardErrorExpectation::non_blank
        &&diagnostic_goal.output_check->cases[0].outputs==std::vector<std::string>{"Fallos elegidos:2\n"},
        "The integrated U10L4 objective lost its authored stable stdout and independent stderr requirement");
    const auto constructor_source=published.pages[5].blocks[1].code;
    const auto diagnostic_source=published.pages[5].blocks[8].code;
    const auto frontier_source=published.pages[5].blocks[10].code;
    require(!constructor_source.empty()&&!diagnostic_source.empty()&&!frontier_source.empty(),
        "The real U10L4 reference programs are missing");
    // Silence only the report in the actual defective reference. Its selected
    // cases, random generator, deliberate > comparison and stdout stay intact.
    auto silent_source=diagnostic_source;
    constexpr std::string_view main_start="int main() {";
    const auto main_offset=silent_source.find(main_start);
    require(main_offset!=silent_source.npos&&silent_source.find("std::cerr")!=silent_source.npos,
        "The authored U10L4 report cannot be isolated without rewriting its program");
    silent_source.insert(main_offset+main_start.size(),"\n    std::cerr.setstate(std::ios_base::failbit);");
    const auto expected_stdout=diagnostic_goal.output_check->cases[0].outputs.front();
    Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
        state::prepare_lesson_state(data,lesson);
        // Isolate the real compiler dependency without inventing any compiler
        // completion: both the constructor and the report must execute below.
        for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
            if(block.type!=BlockType::output_prediction&&block.type!=BlockType::multiple_choice)continue;
            state::Response answer;answer.correct=true;
            if(block.type==BlockType::output_prediction)answer.answer=block.answers.at(0);
            else answer.selected=static_cast<std::int64_t>(*block.correct_index);
            state::put_response(data,curriculum::exercise_key(lesson,block),answer);
        }
        state::save_current_page(data,lesson,4);
    },[&](const curriculum::Lesson&){return published;});
    require(f.page()==4&&!descendant(f.view,id(7,10))&&!lesson_page_state(f.view).can_next,
        "The U10L4 report editor escaped its independent constructor prerequisite");
    f.run(4);
    require(f.text(4,4).starts_with("✗ Incorrecto")&&!descendant(f.view,id(7,10)),
        "Running the incomplete real U10L4 constructor unlocked the dependent report");
    f.replace(4,10,constructor_source);f.run(4);
    require(windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[4])
        &&descendant(f.view,id(7,10))&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,5),
        "The actual U10L4 constructor failed to unlock only its dependent editor");
    f.replace(7,10,silent_source);f.run(7);
    require(f.text(7,14)==expected_stdout&&f.text(7,4).starts_with("✗ Incorrecto")
        &&f.text(7,4).find("falta el diagnóstico solicitado en stderr")!=std::string::npos
        &&!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[7])
        &&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,5),
        "The real U10L4 stdout-only reference completed its two-channel objective or revealed its later references");
    SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(id(7,79),BN_CLICKED),0);pump(f.host);
    require(f.text(7,14)==expected_stdout,"Silencing U10L4 runtime stderr fabricated a selectable report");
    require(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){
        return region.block_index==7&&region.is_exercise&&!region.solved;}),
        "The U10L4 stdout-only result released its reader boundary");
    f.reopen();
    require(f.page()==4&&f.text(7,10)==silent_source&&f.text(7,14).empty()
        &&windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[4])
        &&!lesson_page_state(f.view).can_next,"Reopening lost the independent constructor/draft or invented a complete U10L4 report");
    f.replace(7,10,"#error QA_U10L4_BUILD_DIAGNOSTIC\nint main(){}\n");f.run(7);
    require(f.text(7,4).empty()&&f.text(7,14).find("QA_U10L4_BUILD_DIAGNOSTIC")!=std::string::npos
        &&!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[7])
        &&!lesson_page_state(f.view).can_next,"U10L4 build diagnostics accredited its successful runtime report");
    f.replace(7,10,diagnostic_source);f.run(7);
    const auto complete=f.text(7,4);
    require(complete=="✓ Correcto\n\n"+diagnostic_goal.output_check->cases[0].explanation
        &&f.text(7,14)==expected_stdout&&lesson_page_state(f.view).can_next,
        "The unchanged real U10L4 reference did not complete its current two-channel objective");
    SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(id(7,79),BN_CLICKED),0);pump(f.host);
    const auto report=f.text(7,14);
    constexpr std::string_view summary_prefix="Fallos generados:";
    const auto summary=report.rfind(summary_prefix);
    require(summary!=report.npos,"The authored U10L4 runtime report lost its generated-case summary");
    const auto count=report.substr(summary+summary_prefix.size());
    require(!count.empty()&&std::ranges::all_of(count,[](unsigned char value){return value>='0'&&value<='9';}),
        "The U10L4 runtime report did not retain its variable generated-case count");
    std::size_t reported_cases=0;
    for(std::size_t offset=0;(offset=report.find("Caso:",offset))!=report.npos&&offset<summary;offset+=5)++reported_cases;
    require(std::stoul(count)==reported_cases,"The actual U10L4 summary disagrees with its own reported cases");
    // No exact generated count is prescribed: standard libraries may produce
    // different samples. The lesson owns the stable selected-case stdout.
    f.reopen();
    require(f.text(7,10)==diagnostic_source&&f.text(7,4)==complete&&f.text(7,14).empty()
        &&lesson_page_state(f.view).can_next,"The actual U10L4 report proof/draft did not persist independently of its transient console");
    f.next();
    const auto reference_status=[&](std::string_view phase){const auto saved=f.saved();
        std::cout<<"U10L4 references "<<phase<<": page="<<f.page()
            <<" heading7="<<(descendant(f.view,id(7,0))!=nullptr)
            <<" code8="<<(descendant(f.view,id(8,1))!=nullptr)
            <<" code10="<<(descendant(f.view,id(10,1))!=nullptr)
            <<" report-complete="<<windows::compiler_puzzle_completed(saved,f.lesson,f.lesson.pages[4].blocks[7])
            <<" frontier-complete="<<windows::compiler_puzzle_completed(saved,f.lesson,f.lesson.pages[5].blocks[6])<<'\n';};
    reference_status("before independent frontier");
    require(f.page()==5,"The completed U10L4 report did not unlock the actual next page");
    require(windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[7]),
        "Entering the U10L4 reference page lost its completed two-channel proof");
    require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[5].blocks[6]),
        "Completing the U10L4 report accredited the independent frontier activity");
    // In the authored order this mandatory frontier editor precedes BOTH
    // later references. Its sequential gate still applies to the already
    // completed report's reference; owner completion does not bypass it.
    require(!descendant(f.view,id(7,0)),"The U10L4 report heading bypassed the earlier mandatory frontier editor");
    require(!descendant(f.view,id(8,1)),"The U10L4 report reference bypassed the earlier mandatory frontier editor");
    require(!descendant(f.view,id(10,1)),"The independent U10L4 frontier reference was revealed before its execution");
    f.replace(6,10,frontier_source);f.run(6);
    reference_status("after independent frontier");
    const auto& frontier_goal=f.lesson.pages[5].blocks[6];
    require(windows::compiler_puzzle_completed(f.saved(),f.lesson,frontier_goal),
        "The independent U10L4 frontier reference failed to complete its own published output contract");
    require(frontier_goal.output_check&&f.text(6,14)==frontier_goal.output_check->cases[0].outputs.front(),
        "The real U10L4 frontier execution changed its own authored stdout");
    require(descendant(f.view,id(7,0)),"The completed U10L4 sequential prerequisites did not reveal the report heading");
    require(descendant(f.view,id(8,1)),"The completed U10L4 report did not release its own reference code");
    require(descendant(f.view,id(10,1)),"The independent U10L4 frontier achievement did not release its own reference");
    require(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==8;}),
        "The revealed U10L4 report reference was missing from its narration regions");
    f.select_page(4);f.replace(7,10,silent_source);f.run(7);
    require(f.text(7,4)==complete&&f.text(7,14)==expected_stdout&&lesson_page_state(f.view).can_next,
        "A later silent U10L4 execution revoked the completed two-channel achievement");
    f.reopen();
    require(f.text(7,4)==complete&&f.text(7,10)==silent_source,
        "Reopening lost the U10L4 achievement or overwrote its later independent draft");
    f.click(7,12);
    require(f.text(7,4).empty()&&!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[7])
        &&windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[4])
        &&windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[5].blocks[6])
        &&f.text(4,10)==constructor_source&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,5),
        "Reset failed to revoke only the real U10L4 report while preserving its independent constructor");
    require(state::sha256(state::read_bytes(path))==asset_hash,"The integrated U10L4 QA modified its authored lesson asset");
    std::cout<<"PASS current integrated U10L4: real constructor and defective reference, stdout-only rejection, variable runtime report, build rejection, own reader/reference gates, reopen, sticky success and scoped Reset\n";
}
void u11_grammar_choice_preserves_current_feedback_and_history() {
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json";
    const auto asset_hash=state::sha256(state::read_bytes(path));
    const auto published=windows::load_lesson(path);
    const auto position=curriculum::stable_block_position(published,"block_0002_0002");
    require(position&&position->page==2&&position->block==2,"The U11L11 while choice lost its historical position");
    const auto& choice=published.pages[2].blocks[2];
    require(choice.type==BlockType::multiple_choice&&choice.correct_index==0&&choice.options.size()==3
        &&choice.review_feedback_options.size()==3&&!choice.explanation.empty()
        &&curriculum::activity_prerequisites(published,choice).empty(),
        "The current U11L11 while choice lost its exact options, guides or independent availability");
    const auto identity=curriculum::exercise_key(published,choice);
    const auto anchor=published.id+"/es/"+published.pages[2].stable_id+"/"+choice.stable_id+"/part/4";
    const auto visible=[](std::string value){std::erase(value,'`');return value;};
    const auto initialize=[&](state::StateData& data,const curriculum::Lesson& lesson){
        state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,2);
        data.preferences["u11_choice_qa"]["opaque"]=std::string(" \tunchanged\n");
        state::Response sibling;sibling.answer="QA_SIBLING_DRAFT ";
        sibling.raw_json=R"({"opaque":{"unsubmitted":true}})";
        state::put_response(data,lesson,lesson.pages[2].blocks[1],sibling);
        // This source-bound synthetic bookmark checks storage preservation;
        // no audio synthesis, heard feedback or timed restoration is claimed.
        narration::History history;
        history.entries.push_back({state::sha256("QA earlier explanation source"),anchor,{1,730,1,false},lesson.pages[2].stable_id});
        narration::write_history(data,history);
    };
    Fixture f(initialize,[&](const curriculum::Lesson&){return published;},true);
    const auto original=f.saved();
    const auto history=narration::read_history(original);
    const auto sibling=state::get_response(original,f.lesson,f.lesson.pages[2].blocks[1])->raw_json;
    const auto unchanged=[&](Fixture& current){
        const auto saved=current.saved();
        require(current.page()==2&&curriculum::exercise_key(current.lesson,current.lesson.pages[2].blocks[2])==identity
            &&saved.preferences.at("u11_choice_qa")==original.preferences.at("u11_choice_qa")
            &&narration::read_history(saved)==history
            &&state::get_response(saved,current.lesson,current.lesson.pages[2].blocks[1])->raw_json==sibling
            &&!IsWindowVisible(current.host),
            "U11L11 choice changed its page/identity, unrelated draft, bookmark, preference or desktop");
    };
    const auto feedback_source=[&](Fixture& current,bool correct,std::string_view body){
        const auto expected=std::string(correct?"✓ Correcto\n\n":"✗ Incorrecto\n\n")+std::string(body);
        const auto regions=lesson_reading_regions(current.view);
        const auto found=std::ranges::find_if(regions,[&](const auto& region){return region.target==current.control(2,4);});
        require(found!=regions.end()&&found->feedback&&found->correct==correct&&found->solved==correct
            &&found->source_text==expected&&found->anchor==anchor,
            "U11L11 reader lost the exact guide/explanation source, result boundary or stable feedback anchor");
    };
    const auto no_feedback=[&](Fixture& current){
        require(current.text(2,4).empty(),"An unreviewed U11L11 choice exposed feedback");
        for(const auto& region:lesson_reading_regions(current.view))require(region.target!=current.control(2,4),
            "An unreviewed U11L11 choice exposed feedback to narration");
    };
    no_feedback(f);require(!IsWindowEnabled(f.control(2,3)),"An unanswered U11L11 choice enabled Check");
    int feedback_events=0;
    const auto hooks=[&]{LessonViewHooks result;result.feedback_changed=[&](HWND,std::size_t,bool){++feedback_events;};return result;};
    set_lesson_view_hooks(f.view,hooks());
    for(const std::int64_t selected:{1,2}){
        f.click(2,20+static_cast<int>(selected)*2);no_feedback(f);
        const auto draft=state::get_response(f.saved(),f.lesson,choice);
        require(draft&&draft->selected==selected&&!draft->correct&&!state::response_reviewed(*draft,choice),
            "A native U11L11 choice selection was not retained as an unreviewed draft");
        const auto draft_raw=draft->raw_json;const auto before_draft=feedback_events;f.reopen();set_lesson_view_hooks(f.view,hooks());
        no_feedback(f);require(feedback_events==before_draft&&state::get_response(f.saved(),f.lesson,choice)->raw_json==draft_raw,
            "Reopening U11L11 submitted or rewrote an unreviewed choice draft");
        f.click(2,3);const auto& guide=choice.review_feedback_options[static_cast<std::size_t>(selected)];
        require(f.text(2,4)=="✗ Incorrecto\n\n"+visible(guide)&&f.text(2,4).find(visible(choice.explanation))==std::string::npos,
            "A rejected U11L11 choice lost its selected guide or exposed the successful explanation");
        feedback_source(f,false,guide);
        const auto reviewed=state::get_response(f.saved(),f.lesson,choice);
        require(reviewed&&reviewed->selected==selected&&!reviewed->correct&&state::response_reviewed(*reviewed,choice),
            "U11L11 did not persist the actual selected rejection");
        const auto raw=reviewed->raw_json;const auto before_restore=feedback_events;f.reopen();set_lesson_view_hooks(f.view,hooks());
        require(feedback_events==before_restore&&f.text(2,4)=="✗ Incorrecto\n\n"+visible(guide)
            &&state::get_response(f.saved(),f.lesson,choice)->raw_json==raw,
            "Restoring a reviewed U11L11 choice created a new feedback event or changed its stored attempt");
        feedback_source(f,false,guide);unchanged(f);
    }
    f.click(2,20);no_feedback(f);f.click(2,3);
    const auto success="✓ Correcto\n\n"+visible(choice.explanation);
    require(f.text(2,4)==success&&!IsWindowEnabled(f.control(2,20))&&!IsWindowEnabled(f.control(2,22))
        &&!IsWindowEnabled(f.control(2,24)),"A correct U11L11 choice lost its complete explanation or completion lock");
    feedback_source(f,true,choice.explanation);
    const auto correct=state::get_response(f.saved(),f.lesson,choice);const auto before_correct=feedback_events;
    require(correct&&correct->correct&&correct->selected==0,"The native U11L11 correct choice was not persisted");
    f.reopen();set_lesson_view_hooks(f.view,hooks());
    require(feedback_events==before_correct&&f.text(2,4)==success
        &&state::get_response(f.saved(),f.lesson,choice)->raw_json==correct->raw_json,
        "Reopening a completed U11L11 choice changed its exact response or replayed a submission event");
    feedback_source(f,true,choice.explanation);unchanged(f);
    f.click(2,12);no_feedback(f);
    const auto reset=state::get_response(f.saved(),f.lesson,choice);
    require((!reset||(!reset->correct&&reset->selected<0))&&IsWindowEnabled(f.control(2,20)),
        "Explicit U11L11 Reset retained the choice achievement or locked the first option");
    unchanged(f);
    for(const std::string mode:{"correct","incorrect","draft"}){
        std::string stored;
        Fixture old([&](state::StateData& data,const curriculum::Lesson& lesson){
            initialize(data,lesson);state::Response response;response.selected=mode=="correct"?0:1;response.correct=mode=="correct";
            response.raw_json=R"({"incorrectAttempts":5,"opaque":{"priorExplanation":"QA earlier explanation source","caret":7}})";
            if(mode=="incorrect")state::mark_response_reviewed(response,choice);
            state::put_response(data,lesson,choice,response);
            stored=state::get_response(data,lesson,choice)->raw_json;
        },[&](const curriculum::Lesson&){return published;},true);
        for(unsigned reopen=0;reopen<2;++reopen){
            require(state::get_response(old.saved(),old.lesson,choice)->raw_json==stored,
                "Restoring the editorial U11L11 choice rewrote historical selection, attempts or opaque data");
            if(mode=="draft")no_feedback(old);
            else feedback_source(old,mode=="correct",mode=="correct"?choice.explanation:choice.review_feedback_options[1]);
            unchanged(old);if(!reopen)old.reopen();
        }
    }
    require(state::sha256(state::read_bytes(path))==asset_hash,"U11L11 native choice QA modified its authored lesson asset");
    std::cout<<"PASS current U11L11 p3b3 choice: both selected guides, unsubmitted/reviewed/correct reopen, complete success-only explanation, native result reader sources and stable anchor, historical raw attempts/opaque/bookmark, scoped Reset; no audio or visual acceptance claimed\n";
}
void u11_editorial_preserves_prediction_and_compiler_history() {
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_palabras_y_lecturas_condicionadas.json";
    const auto source_hash=state::sha256(state::read_bytes(path));
    const auto published=windows::load_lesson(path);
    const auto prediction=curriculum::stable_block_position(published,"block_0001_0001");
    const auto goal=curriculum::stable_block_position(published,"block_0004_0003");
    const auto reference=curriculum::stable_block_position(published,"block_0007_0001");
    require(prediction&&prediction->page==1&&prediction->block==1&&goal&&goal->page==4&&goal->block==3&&reference,
        "The audited U11L1 editorial identities or historical positions changed");
    const auto& question=published.pages[1].blocks[1];
    const auto& compiler=published.pages[4].blocks[3];
    require(question.type==BlockType::output_prediction&&compiler.output_check&&compiler.output_check->cases.size()==1,
        "The U11L1 fixture no longer contains the prediction and one six-report objective");
    const auto explanation=compiler.output_check->cases[0].explanation;
    require(!explanation.empty(),"The U11L1 reviewed success explanation is absent");
    auto visible_question=question.question;std::erase(visible_question,'`');
    require(visible_question.find("\\t")!=std::string::npos&&visible_question.find("\\n")!=std::string::npos&&
        visible_question.find("\\\\t")==std::string::npos&&visible_question.find("\\\\n")==std::string::npos&&
        question.code.find("\\tbrisa\\npuente")!=std::string::npos,
        "U11L1 authored escapes disagree: the question and source need exactly one visible backslash per escape");
    for(const std::string mode:{"correct","incorrect","draft"}) {
        std::string historical;
        Fixture f([&](state::StateData& data,const curriculum::Lesson& lesson){
            state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,1);
            const auto& block=lesson.pages[1].blocks[1];state::Response answer;
            answer.answer=mode=="correct"?block.answers.at(0):mode=="incorrect"?"QA_RESPUESTA_DISTINTA":"1:<bri";
            answer.correct=mode=="correct";answer.raw_json=R"({"incorrectAttempts":5,"opaque":{"caret":7,"selection":"escape"}})";
            if(mode=="incorrect")state::mark_response_reviewed(answer,block);
            const auto key=curriculum::exercise_key(lesson,block);state::put_response(data,key,answer);
            historical=state::string_value(data,"learning_progress",key);
            const auto& other=lesson.pages[1].blocks[3];state::Response choice;choice.correct=true;choice.selected=static_cast<std::int64_t>(*other.correct_index);
            state::put_response(data,curriculum::exercise_key(lesson,other),choice);
        },[&](const curriculum::Lesson&){return published;},true);
        const auto preserved=[&]{require(state::string_value(f.saved(),"learning_progress",curriculum::exercise_key(f.lesson,f.lesson.pages[1].blocks[1]))==historical,
            "Restoring the revised U11L1 question changed a saved answer, review, attempts or opaque metadata");};
        std::string anchor;
        for(unsigned reopen=0;reopen<2;++reopen) {
            require(f.page()==1&&f.text(1,0)==visible_question&&f.text(1,1)==question.code,
                "U11L1 native question/source changed the authored escapes or saved page");
            const auto shown=f.text(1,0);
            require(shown.find("\\\\t")==std::string::npos&&shown.find("\\\\n")==std::string::npos,
                "U11L1 RichEdit displayed two backslashes where the C++ source has one");
            bool found=false;
            for(const auto& region:lesson_reading_regions(f.view))if(region.target==f.control(1,0)) {
                found=true;require(region.source_text==question.question&&region.is_exercise&&region.solved==(mode=="correct"),
                    "The revised question lost its original narration source or historical completion boundary");
                if(anchor.empty())anchor=region.key;else require(region.key==anchor,"Reopening U11L1 changed the question's stable reader key");
            }
            require(found&&lesson_page_state(f.view).can_next==(mode=="correct"),"The revised U11L1 prediction changed its reader/page completion gate");
            const auto feedback=f.text(1,4);
            require(mode=="draft"?feedback.empty():feedback.starts_with(mode=="correct"?"✓ Correcto":"✗ Incorrecto"),
                "The revised U11L1 question lost an old verdict or turned an unsubmitted draft into feedback");
            preserved();if(!reopen)f.reopen();
        }
    }
    Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
        state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,4);
        const auto& other=lesson.pages[4].blocks[2];state::Response answer;answer.correct=true;answer.selected=static_cast<std::int64_t>(*other.correct_index);
        state::put_response(data,curriculum::exercise_key(lesson,other),answer);
    },[&](const curriculum::Lesson&){
        auto old=published;
        // Prior to this editorial addition the same one-case objective had no
        // explanation. Only that presentation field is removed from this QA
        // model; all current source, oracles, addresses and dependencies remain.
        old.pages[4].blocks[3].output_check->cases[0].explanation.clear();return old;
    },true);
    const auto initial=f.lesson.pages[4].blocks[3].initial_code;
    const auto solution=published.pages[reference->page].blocks[reference->block].code;
    const auto old_objective=curriculum::reference_objective_frame(f.lesson,f.lesson.pages[4].blocks[3]);
    require(!solution.empty()&&!lesson_page_state(f.view).can_next&&f.text(3,4).empty(),"The pending U11L1 objective revealed a success explanation before execution");
    f.run(3);
    require(!windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[3])&&f.text(3,4).find(explanation)==std::string::npos,
        "The initial incomplete U11L1 program was credited or exposed the new success explanation");
    f.replace(3,10,solution);f.run(3);const auto old_feedback=f.text(3,4);
    require(old_feedback=="✓ Correcto"&&f.text(3,14)==compiler.output_check->cases[0].outputs[0]&&lesson_page_state(f.view).can_next,
        "The real historical six-report objective did not complete without its later editorial explanation");
    state::Preferences proofs;const auto old_saved=f.saved();
    for(const auto& [key,value]:old_saved.preferences.at("native_windows"))if(key.starts_with("compiler-puzzle/v1/"))proofs.emplace(key,value);
    require(!proofs.empty(),"The real U11L1 execution did not capture its historical compiler proof");
    f.close();f.lesson.pages[4].blocks[3].output_check->cases[0].explanation=explanation;f.open();
    require(curriculum::reference_objective_frame(f.lesson,f.lesson.pages[4].blocks[3])==old_objective&&
        windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[4].blocks[3])&&f.text(3,4)==old_feedback&&f.text(3,10)==solution&&lesson_page_state(f.view).can_next,
        "Adding only the U11L1 explanation revoked its previous success, draft or historical feedback");
    f.run(3);
    require(f.text(3,4)==old_feedback,"Recompiling a completed objective rewrote its historical feedback without Reset");
    for(const auto& [key,value]:proofs){const auto data=f.saved();const auto found=state::find(data,"native_windows",key);require(found&&*found==value,"The editorial change rewrote a captured U11L1 compiler proof");}
    f.click(3,12);
    require(f.text(3,4).empty()&&f.text(3,10)==initial&&!lesson_page_state(f.view).can_next&&
        std::ranges::none_of(lesson_reading_regions(f.view),[&](const auto& region){return region.feedback&&region.source_text.find(explanation)!=std::string::npos;}),
        "Reset retained the old objective or exposed its new success-only explanation");
    f.replace(3,10,solution);f.run(3);const auto complete=f.text(3,4);
    require(complete=="✓ Correcto\n\n"+explanation&&f.text(3,14)==compiler.output_check->cases[0].outputs[0]&&lesson_page_state(f.view).can_next,
        "A fresh current U11L1 success did not display its exact reviewed explanation and stdout");
    require(std::ranges::any_of(lesson_reading_regions(f.view),[&](const auto& region){return region.target==f.control(3,4)&&region.feedback&&region.correct&&region.solved&&region.source_text==complete;}),
        "The freshly earned U11L1 success explanation was not exposed to the result reader");
    f.reopen();require(f.text(3,4)==complete&&f.text(3,10)==solution&&f.text(3,14).empty()&&lesson_page_state(f.view).can_next,
        "The current U11L1 explanation/proof failed to persist or replayed stale console output");
    f.next();require(f.page()==5&&descendant(f.view,id(2,1)),"The completed U11L1 objective did not release its own reference code");
    f.select_page(4);f.replace(3,10,"int main(){}\n");f.run(3);
    require(f.text(3,4)==complete&&f.text(3,14).find(compiler.output_check->cases[0].outputs[0])==std::string::npos&&lesson_page_state(f.view).can_next,
        "A later independent U11L1 edit/recompilation revoked the sticky correct explanation");
    f.reopen();require(f.text(3,4)==complete&&f.text(3,10)=="int main(){}\n","Reopening lost U11L1's later draft or persistent current explanation");
    f.click(3,12);require(f.text(3,4).empty()&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,5),
        "Only Reset must revoke the completed U11L1 objective and close its references");
    require(state::sha256(state::read_bytes(path))==source_hash,"U11L1 editorial QA modified the authored lesson asset");
    std::cout<<"PASS current U11L1 editorial: literal escapes/source/reading, three prior prediction states, real six-report MSVC objective, historical feedback/proofs, new success-only explanation, reader/reference gates, sticky/reopen and Reset\n";
}
void use_stable_dependency_fixture_layout(curriculum::Lesson& model) {
    model.state_layout.version=1;model.state_layout.locale="es";
    model.state_layout.lineage=state::sha256(model.id+"/dependent-fixture-v1");model.state_layout.legacy_lineage=model.state_layout.lineage;
    for(const auto& page:model.pages){curriculum::StatePage layout;layout.id=page.stable_id;
        for(const auto& block:page.blocks)layout.blocks.push_back(block.stable_id);
        model.state_layout.origin.push_back(layout);model.state_layout.current.push_back(std::move(layout));}
}
void dependent_puzzle_types_hide_payload_and_preserve_their_historical_responses() {
    const std::array<std::string,7> targets{{
        R"({"type":"output_prediction","question":"QA_SECRETO predicción","code":"std::cout << 7; // QA_SECRETO","answers":["7"],"explanation":"QA_SECRETO explicación"})",
        R"({"type":"fill_code","question":"QA_SECRETO completar","code":"int QA_SECRETO = 7;","answers":["7"],"explanation":"QA_SECRETO explicación"})",
        R"({"type":"multiple_choice","question":"QA_SECRETO elección","options":["QA_SECRETO A","QA_SECRETO B"],"correctIndex":0,"explanation":"QA_SECRETO explicación"})",
        R"({"type":"order_code","question":"QA_SECRETO ordenar","fragments":["QA_SECRETO primero","QA_SECRETO segundo"],"correctOrder":[0,1],"explanation":"QA_SECRETO explicación"})",
        R"({"type":"inline_fill_code","question":"QA_SECRETO hueco","template":"std::cout [] \"QA_SECRETO\";","answers":["<<"],"explanation":"QA_SECRETO explicación"})",
        R"({"type":"drag_fill_code","question":"QA_SECRETO arrastre","template":"int [](){return 0;}","choices":["main","QA_SECRETO"],"correctOrder":[0],"explanation":"QA_SECRETO explicación"})",
        R"({"type":"math_puzzle","schemaVersion":1,"prompt":"QA_SECRETO medida","responses":[{"id":"number","type":"math_numeric_input","label":"QA_SECRETO número","validator":{"type":"numeric","expected":6}}]})"
    }};
    for(std::size_t type=0;type<targets.size();++type){
        Fixture f([&](state::StateData& data,const curriculum::Lesson& lesson){
            const auto& target=lesson.pages[0].blocks[1];state::Response response;response.correct=target.type!=BlockType::math_puzzle;
            response.answer="7";if(target.type==BlockType::multiple_choice)response.selected=0;
            if(target.type==BlockType::order_code)response.order={0,1};
            if(target.type==BlockType::inline_fill_code)response.raw_json=R"({"answers":["<<"],"opaque":"historia"})";
            if(target.type==BlockType::drag_fill_code)response.raw_json=R"({"slots":[0],"opaque":"historia"})";
            if(target.type==BlockType::math_puzzle)response.raw_json=R"({"responses":{},"opaque":"historia matemática"})";
            state::put_response(data,curriculum::exercise_key(lesson,target),response);state::save_current_page(data,lesson,1);
        },[&](const curriculum::Lesson&){auto model=windows::parse_lesson_json(
            "{\"id\":\"qa_dependent_type_"+std::to_string(type)+"\",\"title\":\"Dependencia\",\"pages\":[{\"blocks\":["
            R"({"type":"cpp_compiler","question":"Construye primero.","initialCode":"#include <iostream>\nint main(){std::cout << 1;}\n","outputCheck":{"cases":[{"standardInput":"","outputs":["1"]}]}},)"
            +targets[type]+R"(]},{"blocks":[{"type":"paragraph","text":"Final."}]}]})");
            use_stable_dependency_fixture_layout(model);model.activity_after.push_back({model.pages[0].blocks[1].stable_id,{model.pages[0].blocks[0].stable_id}});return model;});
        const auto response_key=curriculum::exercise_key(f.lesson,f.lesson.pages[0].blocks[1]);
        const auto original=state::string_value(f.initial_data,"learning_progress",response_key);
        const auto preserved=[&]{require(state::string_value(f.saved(),"learning_progress",response_key)==original,
            "Locking/unlocking changed a dependent puzzle's historical response bytes");};
        const auto locked=[&]{require(f.page()==0&&!lesson_page_state(f.view).can_next&&descendant(f.view,id(1,18))
            &&!descendant(f.view,id(1,0))&&!descendant(f.view,id(1,1))&&!descendant(f.view,id(1,2))
            &&!descendant(f.view,id(1,3))&&!descendant(f.view,id(1,4))&&!descendant(f.view,id(1,5)),
            "A pending source exposed a dependent prompt, code, answer, feedback, check control or native compound view");
            require(f.text(1,18).find("QA_SECRETO")==std::string::npos
                &&std::ranges::none_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==1;}),
                "A dependent puzzle leaked its authored payload through the generic placeholder or reader");
            require(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==0&&region.is_exercise&&!region.solved;}),
                "A compiler prerequisite for a puzzle was not a reader gate without the global mandatory flag");preserved();};
        locked();f.run(0);require(descendant(f.view,id(1,3)),"A successful source did not reveal the dependent puzzle");preserved();
        const auto check=f.control(1,3);f.replace(0,10,"#include <iostream>\nint main(){std::cout << 9;}\n");f.run(0);
        require(f.control(1,3)==check,"Editing/recompiling a completed source revoked or recreated the dependent puzzle");preserved();
        f.click(0,12);locked();f.reopen();locked();f.run(0);
        require(descendant(f.view,id(1,3)),"Reset and a fresh source success failed to recover the dependent response");preserved();
    }
    std::cout<<"PASS seven dependent puzzle types: no hidden payload/controls/narration, pending source gate, exact response history, sticky success, Reset and reopen\n";
}
void dependent_puzzle_bookmarks_resolve_transitive_pending_sources() {
    Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
        const auto& target=lesson.pages[2].blocks[0];state::put_response(data,curriculum::exercise_key(lesson,target),state::Response{"2",true});
        state::save_current_page(data,lesson,3);
    },[](const curriculum::Lesson&){auto model=windows::parse_lesson_json(R"json({"id":"qa_dependent_transitive","title":"Cadena",
      "pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"#include <iostream>\nint main(){std::cout << 0;}\n","outputCheck":{"cases":[{"standardInput":"","outputs":["1"]}]}}]},
      {"blocks":[{"type":"cpp_compiler","initialCode":"#include <iostream>\nint main(){std::cout << 2;}\n","outputCheck":{"cases":[{"standardInput":"","outputs":["2"]}]}}]},
      {"blocks":[{"type":"output_prediction","question":"Recupera lo construido.","code":"std::cout << 2;","answers":["2"]}]},
      {"blocks":[{"type":"paragraph","text":"Marcador posterior."}]}]})json");use_stable_dependency_fixture_layout(model);
        model.activity_after.push_back({model.pages[1].blocks[0].stable_id,{model.pages[0].blocks[0].stable_id}});
        model.activity_after.push_back({model.pages[2].blocks[0].stable_id,{model.pages[1].blocks[0].stable_id}});return model;});
    require(f.page()==0&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,2),
        "A later historical bookmark/selector bypassed the first transitive compiler prerequisite");
    f.run(0);require(f.text(0,4).starts_with("✗ Incorrecto")&&!navigate_lesson_view(f.view,3),
        "An incorrect prerequisite permitted a later dependent bookmark");
    f.replace(0,10,"#include <iostream>\nint main(){std::cout << 1;}\n");f.run(0);f.next();
    require(f.page()==1&&!lesson_page_state(f.view).can_next,"The direct source did not remain independently pending after its own prerequisite");
    f.run(0);f.next();require(f.page()==2&&f.text(0,2)=="2"&&f.text(0,4).starts_with("✓ Correcto"),
        "Completing both source programs lost or re-evaluated the historical dependent response");
    f.next();f.select_page(0);f.click(0,12);f.close();
    state::StateStore store(f.environment.paths.root);auto data=store.load();state::save_current_page(data,f.lesson,3);store.save(data);f.open();
    require(f.page()==0,"Reopening with a later marker did not return to the reset transitive source");
    f.replace(0,10,"#include <iostream>\nint main(){std::cout << 1;}\n");f.run(0);f.next();f.next();require(f.text(0,2)=="2"&&f.text(0,4).starts_with("✓ Correcto"),
        "Resetting an earlier source erased the independent downstream success or puzzle response");
    std::cout<<"PASS dependent cross-page bookmarks: transitive roots, wrong/current source gates, selector, completed-response recovery, scoped Reset and reopen\n";
}
void authored_dependent_puzzle_locations_protect_historical_bookmarks() {
    // Relations are QA-only projections of the requested constructions. Work
    // still authors and accepts their integration in the unchanged assets.
    struct Location{const char* file;const char* source;const char* target;};
    const std::array<Location,19> locations{{
        {"objetos_y_estado/lesson_objetos_valores_y_tipos.json","block_0004_0006","block_0005_0006"},
        {"objetos_y_estado/lesson_estado_asignacion_y_copia.json","block_0006_0006","block_0007_0006"},
        {"expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json","block_0002_0002","block_0003_0003"},
        {"repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json","block_0008_0003","block_0009_0004"},
        {"repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json","block_0008_0003","relacionar_aportes_de_cada_orden"},
        {"repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json","block_0008_0003","block_0009_0005"},
        {"repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json","block_0001_0002","block_0002_0001"},
        {"repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json","block_0001_0002","block_0002_0002"},
        {"repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json","block_0001_0002","block_0005_0004"},
        {"funciones_y_contratos/lesson_referencias_para_modificar.json","block_0007_0004","block_0010_0002"},
        {"funciones_y_contratos/lesson_referencias_para_modificar.json","block_0007_0004","block_0011_0005"},
        {"funciones_y_contratos/lesson_referencias_para_modificar.json","block_0012_0003","block_0015_0002"},
        {"funciones_y_contratos/lesson_espacios_de_nombres_y_nombres_calificados.json","block_0007_0006","block_0010_0001"},
        {"funciones_y_contratos/lesson_espacios_de_nombres_y_nombres_calificados.json","block_0007_0006","block_0010_0002"},
        {"errores_y_construccion_gradual/lesson_distinguir_fallos_y_localizar_la_causa.json","block_0005_0004","block_0007_0002"},
        {"errores_y_construccion_gradual/lesson_limpiar_sin_cambiar_el_comportamiento.json","block_0005_0004","block_0007_0002"},
        {"errores_y_construccion_gradual/lesson_sesion_de_pedidos_con_errores_controlados.json","block_0001_0002","block_0002_0001"},
        {"errores_y_construccion_gradual/lesson_sesion_de_pedidos_con_errores_controlados.json","block_0001_0002","block_0002_0002"},
        {"excepciones_y_recuperacion/lesson_transferencia_con_movimientos_controlados.json","block_0001_0003","block_0002_0001"}
    }};
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    for(const auto& location:locations){
        Fixture f([&](state::StateData& data,const curriculum::Lesson& model){
            const auto target=curriculum::stable_block_position(model,location.target);require(target.has_value(),"An authored dependent target moved without updating its QA location");
            const auto& block=model.pages[target->page].blocks[target->block];state::Response response;
            response.raw_json=R"({"opaque":"historia curricular conservada"})";
            if(block.type==BlockType::multiple_choice){response.correct=true;response.selected=static_cast<std::int64_t>(*block.correct_index);}
            else if(block.type==BlockType::output_prediction||block.type==BlockType::fill_code){response.correct=true;response.answer=block.answers.at(0);}
            state::put_response(data,curriculum::exercise_key(model,block),response);state::save_current_page(data,model,target->page);
        },[&](const curriculum::Lesson&){auto model=windows::load_lesson(folder/location.file);
            const auto source=curriculum::stable_block_position(model,location.source),target=curriculum::stable_block_position(model,location.target);
            require(source&&target&&source->page<target->page,"The authored dependency QA source must remain earlier than its requested puzzle");
            const auto& compiler=model.pages[source->page].blocks[source->block];require(compiler.type==BlockType::cpp_compiler&&compiler.output_check,
                "An authored dependency QA source lost its evaluable compiler objective");
            auto relation=std::ranges::find_if(model.activity_after,[&](const auto& item){return item.activity==location.target;});
            if(relation==model.activity_after.end())model.activity_after.push_back({location.target,{location.source}});
            else if(std::ranges::find(relation->prerequisites,std::string(location.source))==relation->prerequisites.end())relation->prerequisites.push_back(location.source);
            return model;});
        const auto source=*curriculum::stable_block_position(f.lesson,location.source),target=*curriculum::stable_block_position(f.lesson,location.target);
        const auto key=curriculum::exercise_key(f.lesson,f.lesson.pages[target.page].blocks[target.block]);
        const auto original=state::string_value(f.initial_data,"learning_progress",key);
        require(f.page()<=source.page&&f.page()<target.page&&!navigate_lesson_view(f.view,target.page),
            "An authored dependent historical bookmark or Pages action bypassed its pending construction");
        const auto marker='/'+std::string(location.target)+'/';
        require(std::ranges::none_of(lesson_reading_regions(f.view),[&](const auto& region){return region.anchor.find(marker)!=std::string::npos;}),
            "An authored dependent puzzle exposed its narration before its construction");
        require(state::string_value(f.saved(),"learning_progress",key)==original,
            "Redirecting an authored dependent bookmark changed its historical response");
        f.reopen();require(f.page()<=source.page&&state::string_value(f.saved(),"learning_progress",key)==original,
            "Reopening an authored dependency lost either the protected entry or its preserved response");
    }
    std::cout<<"PASS 19 authored dependent-puzzle locations: QA-only relations, historical bookmarks/Pages protected, narration excluded and responses preserved across reopen\n";
}

// Independent acceptance manifest transcribed from WIN_BLOQUES_DEPENDIENTES_Y_MARCADORES.
// Do not derive owners from activityAfter: a removed/retargeted authored edge must fail.
struct CurrentDependentPuzzle {
    const char* file;
    const char* target;
    std::vector<std::string> owners;
};
const std::array<CurrentDependentPuzzle,52>& current_dependent_puzzle_manifest() {
    static const std::array<CurrentDependentPuzzle,52> manifest{{
        {"objetos_y_estado/lesson_objetos_valores_y_tipos.json","block_0005_0006",{"block_0004_0006"}},
        {"objetos_y_estado/lesson_estado_asignacion_y_copia.json","block_0007_0006",{"block_0006_0006"}},
        {"expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json","block_0003_0003",{"block_0002_0002"}},
        {"repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json","block_0009_0004",{"block_0008_0003"}},
        {"repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json","relacionar_aportes_de_cada_orden",{"block_0008_0003"}},
        {"repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json","block_0009_0005",{"block_0008_0003"}},
        {"repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json","block_0002_0001",{"block_0001_0002"}},
        {"repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json","block_0002_0002",{"block_0001_0002"}},
        {"repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json","block_0005_0004",{"block_0001_0002"}},
        {"funciones_y_contratos/lesson_referencias_para_modificar.json","block_0010_0002",{"block_0007_0004"}},
        {"funciones_y_contratos/lesson_referencias_para_modificar.json","block_0011_0005",{"block_0007_0004"}},
        {"funciones_y_contratos/lesson_referencias_para_modificar.json","block_0015_0002",{"block_0014_0007"}},
        {"funciones_y_contratos/lesson_espacios_de_nombres_y_nombres_calificados.json","block_0010_0001",{"block_0007_0006"}},
        {"funciones_y_contratos/lesson_espacios_de_nombres_y_nombres_calificados.json","block_0010_0002",{"block_0007_0006"}},
        {"errores_y_construccion_gradual/lesson_distinguir_fallos_y_localizar_la_causa.json","block_0007_0002",{"block_0005_0004"}},
        {"errores_y_construccion_gradual/lesson_limpiar_sin_cambiar_el_comportamiento.json","block_0007_0002",{"block_0005_0004"}},
        {"errores_y_construccion_gradual/lesson_sesion_de_pedidos_con_errores_controlados.json","block_0002_0001",{"block_0001_0002"}},
        {"errores_y_construccion_gradual/lesson_sesion_de_pedidos_con_errores_controlados.json","block_0002_0002",{"block_0001_0002"}},
        {"excepciones_y_recuperacion/lesson_transferencia_con_movimientos_controlados.json","block_0002_0001",{"block_0001_0003"}},
        {"datos_propios_y_estado_valido/lesson_colecciones_de_registros_asociados.json","block_0007_0002",{"block_0006_0005"}},
        {"datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json","block_0015_0004",{"block_0014_0014"}},
        {"datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json","block_0015_0005",{"block_0014_0014"}},
        {"datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json","conteos_igualdad_en_cuatro_vectores",{"block_0005_0002"}},
        {"datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json","block_0002_0004",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json","block_0006_0002",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json","block_0005_0004",{"block_0004_0002"}},
        {"datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json","block_0007_0001",{"block_0006_0006","block_0006_0009"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0003_0002",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0003_0003",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0004_0001",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0004_0002",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0005_0001",{"block_0001_0002"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0011_0002",{"block_0009_0006"}},
        {"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0014_0002",{"block_0009_0006"}},
        {"aleatoriedad_y_reproducibilidad/lesson_producir_enteros_en_un_intervalo.json","block_0006_0001",{"block_0004_0003"}},
        {"aleatoriedad_y_reproducibilidad/lesson_combinar_ensayos_generados_y_casos_elegidos.json","block_0006_0001",{"block_0004_0004"}},
        {"aleatoriedad_y_reproducibilidad/lesson_construir_un_experimento_reproducible.json","block_0004_0001",{"block_0002_0004"}},
        {"aleatoriedad_y_reproducibilidad/lesson_construir_un_experimento_reproducible.json","block_0004_0002",{"block_0002_0004"}},
        {"aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json","block_0004_0002",{"block_0002_0005"}},
        {"aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json","block_0004_0004",{"block_0002_0005"}},
        {"aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json","block_0007_0002",{"block_0005_0005"}},
        {"aleatoriedad_y_reproducibilidad/lesson_seleccionar_sin_repetir_candidatos.json","block_0007_0004",{"block_0005_0005"}},
        {"texto_interpretacion_y_flujos/lesson_palabras_y_lecturas_condicionadas.json","block_0005_0004",{"block_0004_0003"}},
        {"texto_interpretacion_y_flujos/lesson_lineas_y_separadores_pendientes.json","block_0005_0004",{"block_0004_0003"}},
        {"texto_interpretacion_y_flujos/lesson_comparar_lecturas_con_la_anterior.json","block_0004_0002",{"block_0002_0005"}},
        {"texto_interpretacion_y_flujos/lesson_comparar_lecturas_con_la_anterior.json","block_0004_0004",{"block_0002_0005"}},
        {"texto_interpretacion_y_flujos/lesson_comparar_lecturas_con_la_anterior.json","block_0007_0002",{"block_0005_0005"}},
        {"texto_interpretacion_y_flujos/lesson_comparar_lecturas_con_la_anterior.json","block_0007_0004",{"block_0005_0005"}},
        {"texto_interpretacion_y_flujos/lesson_caracteres_y_clasificacion_explicita.json","block_0005_0004",{"block_0004_0003"}},
        {"texto_interpretacion_y_flujos/lesson_estados_del_flujo_y_datos_validos.json","block_0006_0004",{"block_0005_0003"}},
        {"texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json","block_0006_0001",{"block_0004_0004"}},
        {"texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json","block_0005_0003",{"block_0004_0004"}}
    }};
    return manifest;
}
void current_authored_dependent_puzzles(std::size_t partition,std::size_t partitions) {
    require(partitions>0&&partition<partitions,"Invalid current dependent-puzzle partition");
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    const auto& manifest=current_dependent_puzzle_manifest();
    std::map<std::string,curriculum::Lesson> models;
    for(const auto& plan:manifest)if(!models.contains(plan.file))models.emplace(plan.file,windows::load_lesson(folder/plan.file));
    require(models.size()==27,"The independent current dependency manifest must contain exactly27 authored lessons");
    // Compare every actual non-compiler relation in these27 lessons, not just
    // the listed edges. Compiler-only chains keep their independent tests.
    std::size_t edges=0;
    for(const auto& [file,lesson]:models){
        std::map<std::string,std::vector<std::string>> expected,actual;
        for(const auto& plan:manifest)if(file==plan.file)
            require(expected.emplace(plan.target,plan.owners).second,"Duplicate target in the independent current dependency manifest");
        for(const auto& relation:lesson.activity_after){
            const auto at=curriculum::stable_block_position(lesson,relation.activity);
            require(at.has_value(),"An authored dependency no longer resolves to an active target");
            if(lesson.pages[at->page].blocks[at->block].type!=BlockType::cpp_compiler)
                require(actual.emplace(relation.activity,relation.prerequisites).second,"A current authored puzzle repeats its prerequisite relation");
        }
        require(actual==expected,"Current authored dependency manifest differs (missing/changed/extra owner): "+file);
        edges+=actual.size();
    }
    require(edges==52,"The current27-lesson dependency census is not52 puzzle relations");
    const auto visible=[](std::string value){std::erase(value,'`');return value;};
    const auto msaa=[](HWND item,bool require_provider=false,std::string* diagnostics=nullptr){
        std::string text=require_provider?std::string{}:window_text(item);Microsoft::WRL::ComPtr<IAccessible> object;
        const auto provider=AccessibleObjectFromWindow(item,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(object.GetAddressOf()));
        if(diagnostics)*diagnostics="provider="+std::to_string(provider);
        require(!require_provider||SUCCEEDED(provider),"An unlocked authored code source has no actual MSAA provider");
        if(FAILED(provider))return text;
        VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;bool supplied=false;
        for(const bool value:{false,true}){BSTR result=nullptr;
            const auto status=value?object->get_accValue(self,&result):object->get_accName(self,&result);
            if(diagnostics)*diagnostics+=(value?" value=":" name=")+std::to_string(status);
            if(status==S_OK){supplied=true;if(result){text+='\n';text+=utf8(std::wstring_view(result,SysStringLen(result)));}}SysFreeString(result);}
        require(!require_provider||supplied,"An unlocked authored code source has no successful MSAA name/value result");
        for(std::size_t at=0;(at=text.find("\r\n",at))!=std::string::npos;)text.replace(at,2,"\n");
        // RichEdit MSAA values expose paragraph delimiters as solitary CR;
        // canonical authored bytes are checked separately through edit_text.
        std::ranges::replace(text,'\r','\n');
        return text;
    };
    // These receipts are expressly synthetic control-isolation data. No child
    // process is started and no compiled-program/curricular success is claimed.
    const auto synthetic_case=[](state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& compiler,bool correct){
        require(compiler.type==BlockType::cpp_compiler&&compiler.output_check.has_value(),"A registered owner lost its runtime output objective");
        windows::CppRunRequest request;request.revision=1;request.source_utf8=compiler.initial_code;
        request.project_files=compiler.project_files;request.source_files=compiler.source_files;request.cmake=compiler.cmake;
        for(const auto& item:compiler.output_check->cases){
            request.stdin_utf8=item.standard_input;
            curriculum::FreshExecution execution{1,true,true,0,item.standard_input,correct?item.outputs.front():"QA_UNDECLARED_INCORRECT_OUTPUT"};
            execution.executable=item.executable;
            if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="Synthetic QA runtime channel";
            if(!correct)require(windows::evaluate_cpp_output_check(*compiler.output_check,execution,1).verdict==curriculum::OutputVerdict::incorrect,
                "The synthetic incorrect owner unexpectedly matches an authored expected output");
            const auto completed=windows::record_compiler_puzzle_case(data,lesson,compiler,request,execution,"Synthetic QA prerequisite only; no C++ execution");
            if(!correct)require(!completed&&windows::compiler_puzzle_case_count(data,lesson,compiler)==0,
                "An incorrect synthetic owner gained an execution proof");
        }
        require(windows::compiler_puzzle_completed(data,lesson,compiler)==correct,"The synthetic owner isolation state has the wrong completion");
    };
    std::size_t exercised=0;
    for(std::size_t number=partition;number<manifest.size();number+=partitions){
        const auto& plan=manifest[number];const auto& published=models.at(plan.file);
        const auto path=folder/plan.file;const auto hash=state::sha256(state::read_bytes(path));
        const auto target=*curriculum::stable_block_position(published,plan.target);
        const auto& puzzle=published.pages[target.page].blocks[target.block];
        require(puzzle.type!=BlockType::cpp_compiler&&puzzle.type!=BlockType::heading&&puzzle.type!=BlockType::paragraph&&puzzle.type!=BlockType::note&&puzzle.type!=BlockType::code,
            "A current dependent-puzzle target is not an authored exercise");
        std::vector<curriculum::StatePosition> owners;
        for(const auto& identity:plan.owners){const auto at=curriculum::stable_block_position(published,identity);
            require(at&&at->page<target.page,"A registered current owner no longer precedes its dependent puzzle");owners.push_back(*at);}
        const auto first_owner=std::ranges::min(owners,{},&curriculum::StatePosition::page);
        Fixture f([&](state::StateData& data,const curriculum::Lesson& lesson){
            state::prepare_lesson_state(data,lesson);
            for(const auto& page:lesson.pages)for(const auto& compiler:page.blocks){
                if(compiler.type==BlockType::cpp_compiler&&compiler.output_check&&std::ranges::find(plan.owners,compiler.stable_id)==plan.owners.end())
                    synthetic_case(data,lesson,compiler,true);
            }
            state::Response history;history.correct=puzzle.type!=BlockType::math_puzzle;history.answer="historical QA response";
            if(puzzle.correct_index)history.selected=static_cast<std::int64_t>(*puzzle.correct_index);
            if(puzzle.type==BlockType::output_prediction||puzzle.type==BlockType::fill_code)history.answer=puzzle.answers.at(0);
            history.order.assign(puzzle.correct_order.begin(),puzzle.correct_order.end());
            winrt::Windows::Data::Json::JsonObject raw;
            raw.Insert(L"opaque",winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"preserved historical dependency QA"));
            if(puzzle.type==BlockType::math_puzzle)raw.Insert(L"responses",winrt::Windows::Data::Json::JsonObject{});
            if(puzzle.type==BlockType::inline_fill_code||puzzle.type==BlockType::drag_fill_code){
                winrt::Windows::Data::Json::JsonArray answers;
                if(puzzle.type==BlockType::inline_fill_code)for(const auto& answer:puzzle.answers)
                    answers.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(answer)));
                else for(const auto slot:puzzle.correct_order)answers.Append(winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(slot)));
                raw.Insert(puzzle.type==BlockType::inline_fill_code?L"answers":L"slots",answers);
            }
            history.raw_json=winrt::to_string(raw.Stringify());state::put_response(data,lesson,puzzle,history);
            data.preferences["current_dependencies_qa"]["opaque"]=std::string(" \t retained\r\n");
            state::save_current_page(data,lesson,target.page);
        },[&](const curriculum::Lesson&){return published;},true);
        const auto response_key=state::response_storage_key(f.lesson,puzzle);
        const auto response=state::string_value(f.initial_data,"learning_progress",response_key);
        const auto opaque=f.initial_data.preferences.at("current_dependencies_qa");
        const auto preserved=[&]{const auto current=f.saved();
            require(state::string_value(current,"learning_progress",response_key)==response,"Current dependency projection rewrote a historical response: "+std::string(plan.target));
            require(current.preferences.at("current_dependencies_qa")==opaque&&!IsWindowVisible(f.host),"Current dependency QA changed unrelated data or displayed a window");};
        const auto late_reopen=[&](const std::function<void(state::StateData&)>& change){
            f.close();state::StateStore store(f.environment.paths.root);auto data=store.load();change(data);
            state::save_current_page(data,f.lesson,target.page);store.save(data);f.open();
        };
        const auto locked=[&]{
            require(f.page()<=first_owner.page&&f.page()<target.page&&!navigate_lesson_view(f.view,target.page),
                "Current authored historical bookmark/Pages bypassed a pending owner: "+std::string(plan.target));
            require(!lesson_page_state(f.view).can_next,"The pending owner page incorrectly permits continuation");
            const auto anchor='/'+std::string(plan.target)+'/';
            require(std::ranges::none_of(lesson_reading_regions(f.view),[&](const auto& region){return region.anchor.find(anchor)!=std::string::npos||region.key.find(anchor)!=std::string::npos;}),
                "A locked current puzzle leaked into the reader source");
            std::vector<std::string> secrets;
            for(const auto& authored:{puzzle.question,puzzle.code,puzzle.prompt})if(authored.size()>=16){
                const auto candidate=visible(authored);bool shared=false;
                for(const auto& current:f.lesson.pages[f.page()].blocks)for(const auto& allowed:{current.question,current.code,current.prompt,current.text})
                    if(visible(allowed).find(candidate)!=std::string::npos)shared=true;
                if(!shared)secrets.push_back(candidate);
            }
            struct Probe{const std::function<std::string(HWND)>* read;const std::vector<std::string>* secrets;bool leaked=false;};
            const std::function<std::string(HWND)> read=[&](HWND item){return visible(msaa(item));};Probe probe{&read,&secrets};
            EnumChildWindows(f.view,[](HWND item,LPARAM value)->BOOL{auto& probe=*reinterpret_cast<Probe*>(value);const auto text=(*probe.read)(item);
                for(const auto& secret:*probe.secrets)if(text.find(secret)!=std::string::npos){probe.leaked=true;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&probe));
            require(!probe.leaked,"A pending current puzzle leaked its exclusive authored prompt/code through a native MSAA provider");preserved();
        };
        std::string reader_anchor;
        const auto unlocked=[&]{
            require(f.page()==target.page&&descendant(f.view,id(target.block,3)),"Completing all exact current owners did not mount the authored puzzle");
            const auto regions=lesson_reading_regions(f.view);const auto anchor='/'+std::string(plan.target)+'/';
            const auto found=std::ranges::find_if(regions,[&](const auto& region){return region.anchor.find(anchor)!=std::string::npos||region.key.find(anchor)!=std::string::npos;});
            require(found!=regions.end()&&found->block_index==target.block,"An unlocked current puzzle has no authored reader region: "+std::string(plan.file)+"/"+plan.target);
            if(reader_anchor.empty())reader_anchor=found->key;else require(found->key==reader_anchor,"Reopening an unlocked puzzle changed its stable reader key");
            require(accessible_name(f.control(target.block,3))==(puzzle.type==BlockType::math_puzzle?"Comprobar":"Correcto "+utf8(L"\u2713")),"An unlocked current puzzle lacks its native accessible current Check action");
            if(puzzle.type==BlockType::math_puzzle){require(descendant(f.view,id(target.block,5)),"An unlocked current math puzzle has no native compound control");}
            else {
                if(!puzzle.question.empty())require(f.text(target.block,0)==visible(puzzle.question),"Unlocking changed the current authored question");
                if(!puzzle.code.empty()){
                    std::string diagnostics;const auto native=f.text(target.block,1),provided=msaa(f.control(target.block,1),true,&diagnostics);
                    const auto escaped=[](std::string_view text){std::string result;for(const auto c:text){
                        if(c=='\r')result+="\\r";else if(c=='\n')result+="\\n";else if(c=='\t')result+="\\t";
                        else if(c=='\0')result+="\\0";else if(c=='\\')result+="\\\\";else result+=c;}return result;};
                    require(native==puzzle.code&&provided.find(puzzle.code)!=std::string::npos,
                        "An unlocked authored code source was lost by its RichEdit/MSAA provider: "+std::string(plan.file)+"/"+plan.target
                        +" "+diagnostics+" expected["+std::to_string(puzzle.code.size())+"]="+escaped(puzzle.code)
                        +" native["+std::to_string(native.size())+"]="+escaped(native)+" MSAA["+std::to_string(provided.size())+"]="+escaped(provided));
                }
            }
            preserved();
        };
        locked();f.reopen();locked();
        late_reopen([&](state::StateData& data){for(const auto at:owners){const auto& compiler=f.lesson.pages[at.page].blocks[at.block];
            synthetic_case(data,f.lesson,compiler,false);state::Response wrong;wrong.answer="previous incorrect compiler attempt";wrong.correct=false;
            state::put_response(data,f.lesson,compiler,wrong);}});locked();f.reopen();locked();
        for(std::size_t owner=0;owner<owners.size();++owner){
            late_reopen([&](state::StateData& data){const auto at=owners[owner];synthetic_case(data,f.lesson,f.lesson.pages[at.page].blocks[at.block],true);});
            if(owner+1<owners.size())locked();else unlocked();
        }
        f.reopen();unlocked();
        // Editing a genuinely mounted source must preserve its sealed synthetic
        // success and the dependent historical response until explicit Reset.
        const auto edited_owner=owners.front();f.select_page(edited_owner.page);
        const auto edited=f.text(edited_owner.block,10)+"// current dependency QA edit\n";
        const auto before_edit=f.saved();state::Preferences proofs;
        for(const auto& [key,value]:before_edit.preferences.at("native_windows"))if(key.starts_with("compiler-puzzle/v1/"))proofs.emplace(key,value);
        f.replace(edited_owner.block,10,edited);
        require(windows::compiler_puzzle_completed(f.saved(),f.lesson,f.lesson.pages[edited_owner.page].blocks[edited_owner.block]),
            "Editing a completed current owner revoked its sticky achievement");
        late_reopen([](state::StateData&){});unlocked();
        const auto after_edit=f.saved();for(const auto& [key,value]:proofs){const auto current=state::find(after_edit,"native_windows",key);
            require(current&&*current==value,"Editing/reopening changed a sealed current owner proof");}
        const auto draft=state::get_draft(after_edit,curriculum::draft_key("programming",f.lesson,f.lesson.pages[edited_owner.page].blocks[edited_owner.block]),"","");
        require(draft.source==edited,"A current dependency reopen lost the owner's edited draft");
        f.select_page(edited_owner.page);f.click(edited_owner.block,12);
        late_reopen([](state::StateData&){});locked();f.reopen();locked();
        require(state::sha256(state::read_bytes(path))==hash,"Current dependency QA changed its authored JSON asset");
        std::cout<<"PASS current authored dependency "<<number<<' '<<plan.file<<'/'<<plan.target
            <<": pending/incorrect/all exact owners, late restoration/Pages, native reader/MSAA, sticky source edit, scoped Reset and preserved response\n";
        ++exercised;
    }
    require(exercised>0,"Current dependent-puzzle partition contains no activity");
    std::cout<<"PASS current dependency manifest52/27; partition "<<partition<<'/'<<partitions<<" mounted "<<exercised
        <<" real authored puzzles with expressly synthetic owner receipts; no compilation, audio synthesis, visual or curricular acceptance claimed\n";
}

void compiler_puzzle_completion_and_navigation() {
    Fixture f([](state::StateData& data,const curriculum::Lesson& lesson){
        state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,1);
        for(const auto& block:lesson.pages[0].blocks)if(block.type==BlockType::cpp_compiler)
            state::put_response(data,curriculum::exercise_key(lesson,block),state::Response{"",true});
    },[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({
        "id":"qa_required_compiler_puzzles","title":"Puzzles de compilador","compilerPuzzlesRequired":true,
        "pages":[{"blocks":[
          {"type":"cpp_compiler","question":"Muestra uno.","initialCode":"#include <iostream>\nint main(){std::cout << 1;}\n","outputCheck":{"cases":[{"standardInput":"","outputs":["1"],"explanation":"La salida es uno."}]}},
          {"type":"cpp_compiler","question":"Muestra dos.","initialCode":"#include <iostream>\nint main(){std::cout << 2;}\n","outputCheck":{"cases":[{"standardInput":"","outputs":["2"]}]}},
          {"type":"paragraph","text":"Continuación posterior a ambos objetivos."}
        ]},{"blocks":[{"type":"paragraph","text":"Página final."}]}]})json");});
    require(f.page()==0&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,1),
        "An old correct flag/bookmark bypassed a required real compiler objective");
    require(!descendant(f.view,id(1,10))&&!descendant(f.view,id(2,0)),
        "Pending compiler exposed the following editor or lesson content");
    const auto pending=lesson_reading_regions(f.view);
    require(std::ranges::any_of(pending,[](const auto& region){return region.block_index==0&&region.is_exercise&&!region.solved;})
        &&std::ranges::none_of(pending,[](const auto& region){return region.block_index==1||region.block_index==2;}),
        "Pending compiler lacks its narration boundary or exposed later narration");
    const auto continuous_header=[&]{RECT header{},source{},toolbar{},pane{};GetWindowRect(f.control(0,16),&header);GetWindowRect(f.control(0,10),&source);
        GetWindowRect(GetParent(f.control(0,16)),&toolbar);GetWindowRect(GetParent(f.control(0,10)),&pane);
        if(header.bottom!=source.top)std::cerr<<"QA compiler toolbar gap: fullscreen="<<f.fullscreen()<<", dpi="<<GetDpiForWindow(f.view)
            <<", caption="<<header.top<<','<<header.bottom<<", source="<<source.top<<','<<source.bottom
            <<", toolbar-parent="<<toolbar.top<<','<<toolbar.bottom<<", source-pane="<<pane.top<<','<<pane.bottom<<'\n';
        require(header.bottom==source.top,"A black strip separates the real compiler toolbar and editor body");
        const auto saved=f.text(0,10);require(saved==f.lesson.pages[0].blocks[0].initial_code,"Removing the toolbar strip changed the first source line");};
    for(const auto scale:{75,100,150}){
        update_lesson_preferences(f.view,[scale](state::StateData& data){data.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{scale};});
        continuous_header();f.click(0,20);continuous_header();f.escape();continuous_header();
    }
    int submissions=0;LessonViewHooks hooks;hooks.feedback_changed=[&](HWND,std::size_t,bool){++submissions;};set_lesson_view_hooks(f.view,hooks);
    f.replace(0,10,"#include <iostream>\nint main(){std::cout << 9;}\n");f.run(0);
    require(f.text(0,4).starts_with("✗ Incorrecto")&&!lesson_page_state(f.view).can_next,
        "Wrong output completed a required compiler puzzle");
    f.replace(0,10,f.lesson.pages[0].blocks[0].initial_code);f.run(0);
    const auto accepted=f.text(0,4);const auto submitted=submissions;
    require(accepted.starts_with("✓ Correcto")&&descendant(f.view,id(1,10))&&!lesson_page_state(f.view).can_next,
        "First objective did not independently reveal the second required editor");
    f.replace(0,10,"int main( { // conserva este borrador de diagnóstico\n");
    require(f.text(0,4)==accepted&&descendant(f.view,id(1,10)),"Editing revoked a completed objective or its feedback");
    f.run(0);
    require(f.text(0,4)==accepted&&submissions==submitted&&!f.text(0,14).empty(),
        "Compiling after success replaced the achievement, replayed evaluation or hid the actual diagnostic");
    const auto edited=f.text(0,10);f.reopen();
    require(f.text(0,10)==edited&&f.text(0,4)==accepted&&descendant(f.view,id(1,10)),
        "Reopen did not preserve the current draft independently of the captured successful program");
    update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["native_windows"]["ui_language"]=std::string("en");});
    f.reopen();require(f.text(0,4)=="✓ Correct\n\nLa salida es uno.",
        "Reopening an approved Spanish goal in English damaged the verdict prefix or its explanation");
    update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["native_windows"]["ui_language"]=std::string("es");});
    require(f.text(0,4)==accepted,"Changing only interface language lost the approved compiler feedback");
    f.run(1);require(f.text(1,4)=="✓ Correcto"&&lesson_page_state(f.view).can_next&&descendant(f.view,id(2,0)),
        "Two independent compiler completions did not open the subsequent content/page");
    f.next();f.reopen();require(f.page()==1,"A real completed compiler objective did not preserve a later bookmark");
    f.select_page(0);f.click(0,12);
    require(f.text(0,4).empty()&&!descendant(f.view,id(1,10))&&!lesson_page_state(f.view).can_next,
        "Explicit reset failed to revoke its achievement and required advance");
    f.run(0);require(f.text(1,4)=="✓ Correcto"&&lesson_page_state(f.view).can_next,
        "Reset of the first puzzle erased the independent second achievement");
    std::cout<<"PASS required compiler puzzles: old markers rejected, independent execution, sticky success, free diagnostics, reopen, reset and narration gates\n";
}
}

namespace {
struct ScrollMotionTrace {
    HWND view;
    std::map<HWND,bool> targets;
    unsigned requests=0,rectangles=0,bars=0,sizes=0,notifications=0;
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
        auto& self=*reinterpret_cast<ScrollMotionTrace*>(raw);
        if(self.targets.at(item)){
            if(message==EM_REQUESTRESIZE)++self.requests;
            if(message==EM_SETRECT||message==EM_SETRECTNP)++self.rectangles;
            if(message==EM_SHOWSCROLLBAR)++self.bars;
            if(message==WM_SIZE)++self.sizes;
        }
        if(message==WM_NOTIFY&&lp&&reinterpret_cast<NMHDR*>(lp)->code==EN_REQUESTRESIZE)++self.notifications;
        return DefSubclassProc(item,message,wp,lp);
    }
    explicit ScrollMotionTrace(HWND owner):view(owner){
        targets.emplace(view,false);
        EnumChildWindows(view,[](HWND item,LPARAM raw)->BOOL{
            auto& self=*reinterpret_cast<ScrollMotionTrace*>(raw);wchar_t name[128]{};GetClassNameW(item,name,128);
            // Overflow bands intentionally clip their own host rectangle.
            // They may resize; measured content controls must only translate.
            self.targets.emplace(item,std::wstring_view(name)!=L"LearnGameAndEngineDev.LessonPane");return TRUE;
        },reinterpret_cast<LPARAM>(this));
        for(const auto& [item,content]:targets){(void)content;
            require(SetWindowSubclass(item,proc,0x53434d4f,reinterpret_cast<DWORD_PTR>(this)),"Cannot observe actual scroll-motion messages");}
    }
    ~ScrollMotionTrace(){for(const auto& [item,content]:targets){(void)content;
        if(IsWindow(item))RemoveWindowSubclass(item,proc,0x53434d4f);}}
    void quiet()const{
        require(requests==0&&rectangles==0&&bars==0&&sizes==0&&notifications==0,
            "Pure scrolling remeasured native controls: EM_REQUESTRESIZE="+std::to_string(requests)
            +" EM_SETRECT/NP="+std::to_string(rectangles)+" EM_SHOWSCROLLBAR="+std::to_string(bars)
            +" WM_SIZE="+std::to_string(sizes)+" EN_REQUESTRESIZE="+std::to_string(notifications));
    }
};

std::vector<HWND> scroll_descendants(HWND owner){
    std::vector<HWND> result;EnumChildWindows(owner,[](HWND item,LPARAM raw)->BOOL{
        reinterpret_cast<std::vector<HWND>*>(raw)->push_back(item);return TRUE;
    },reinterpret_cast<LPARAM>(&result));std::ranges::sort(result);return result;
}
RECT screen_client(HWND item){RECT result{};GetClientRect(item,&result);MapWindowPoints(item,nullptr,reinterpret_cast<POINT*>(&result),2);return result;}
bool scroll_visible_clip(HWND item,HWND pane,RECT& clipped){
    if(!IsWindowVisible(item))return false;
    GetWindowRect(item,&clipped);const auto viewport=screen_client(pane);
    if(!IntersectRect(&clipped,&clipped,&viewport))return false;
    for(auto parent=GetParent(item);parent&&parent!=pane;parent=GetParent(parent)){
        const auto bounds=screen_client(parent);if(!IntersectRect(&clipped,&clipped,&bounds))return false;
    }
    return clipped.bottom>clipped.top&&clipped.right>clipped.left;
}
void require_clean_visible_scroll_frame(HWND pane){
    auto windows=scroll_descendants(pane);windows.push_back(pane);
    for(const auto item:windows){RECT clipped{};if(!scroll_visible_clip(item,pane,clipped))continue;
        MapWindowPoints(nullptr,item,reinterpret_cast<POINT*>(&clipped),2);
        const auto update=CreateRectRgn(0,0,0,0),visible=CreateRectRgnIndirect(&clipped);
        require(update&&visible,"Cannot observe the actual viewport update region");
        const auto kind=GetUpdateRgn(item,update,FALSE);
        CombineRgn(update,update,visible,RGN_AND);RECT remaining{};const auto pending=GetRgnBox(update,&remaining);
        DeleteObject(visible);DeleteObject(update);
        require(kind!=ERROR&&pending==NULLREGION,
            "A returned drag frame still has dirty visible client pixels: control="+std::to_string(GetDlgCtrlID(item)));
    }
}
void pure_scroll_motion_keeps_native_document(bool tall){
    Fixture f([](state::StateData& data,const curriculum::Lesson&){
        data.preferences["scroll_motion_qa"]["unrelated"]=std::string(" \t conservar\r\n");
    },[&](const curriculum::Lesson&){
        curriculum::Lesson lesson;lesson.id=tall?"qa_scroll_motion_overflow_bands":"qa_scroll_motion";
        lesson.locale="es";lesson.title="Movimiento de la página";curriculum::Page page;page.stable_id="page_motion";
        curriculum::Block editor;editor.type=BlockType::cpp_compiler;editor.stable_id="editor";
        editor.initial_code="int main() {\n";
        for(int line=0;line<70;++line)editor.initial_code+="    // línea "+std::to_string(line)+" "+std::string(260,'x')+'\n';
        editor.initial_code+="    return 0;\n}\n";editor.address={0,0,{},{}};page.blocks.push_back(std::move(editor));
        curriculum::Block answer;answer.type=BlockType::fill_code;answer.stable_id="checked_answer";
        answer.question="Completa el operador.";answer.answers={"<<"};answer.explanation="El operador conserva la salida.";
        answer.address={0,1,{},{}};page.blocks.push_back(std::move(answer));
        for(int block=0;block<(tall?24:5);++block){curriculum::Block paragraph;paragraph.type=BlockType::paragraph;
            paragraph.stable_id="paragraph_"+std::to_string(block);paragraph.address={0,page.blocks.size(),{},{}};
            for(int line=0;line<(tall?100:20);++line)paragraph.text+="Contenido conservado durante el movimiento de la página.\n";
            page.blocks.push_back(std::move(paragraph));}
        lesson.pages.push_back(std::move(page));return lesson;
    },true);
    // Enter deliberately ignores controls under a hidden host. Establish a
    // visible QA surface before accepting the prerequisite through its actual
    // Check button; this regression concerns movement, not keyboard dispatch.
    ShowWindow(f.host,SW_SHOWNOACTIVATE);pump(f.host);
    f.replace(0,10,f.text(0,10)+"// borrador QA sin compilación\n");
    f.replace(1,2,"<<");f.click(1,3);
    const auto accepted=state::get_response(f.saved(),f.lesson,f.lesson.pages[0].blocks[1]);
    require(f.text(1,4).starts_with("✓ Correcto")&&accepted&&accepted->correct,
        "The visible scroll QA did not accept its answer through the actual Check button");
    const auto pane=descendant_class(f.view,L"LearnGameAndEngineDev.LessonPane"),editor=f.control(0,10);
    require(pane&&editor,"Scroll-motion fixture has no real pane/editor");
    SetFocus(editor);CHARRANGE selection{300,315};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    POINT desired{80,40};SendMessageW(editor,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&desired));pump(f.host);
    POINT inner{};SendMessageW(editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&inner));
    require(inner.x>0&&inner.y>0,"The scroll-motion fixture did not establish actual two-axis editor scrolling");
    SendMessageW(f.view,WM_VSCROLL,tall?SB_BOTTOM:SB_TOP,0);pump(f.host);
    if(tall){SendMessageW(pane,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(8*WHEEL_DELTA)),0);pump(f.host);}
    SCROLLINFO initial{sizeof(initial),SIF_ALL};GetScrollInfo(pane,SB_VERT,&initial);
    require(initial.nMax-static_cast<int>(initial.nPage)>500,"The scroll-motion fixture has no movable document");
    if(tall){
        require(initial.nMax>32767&&initial.nPos>32767,"The tall scroll fixture did not reach a genuine late overflow band");
        require(GetParent(f.control(f.lesson.pages[0].blocks.size()-1,0))!=pane,
            "The late tall-page paragraph did not use a rebased overflow host");
    }
    const auto state_before=f.saved();
    const auto script=reading_script(f.view);
    const auto handles=scroll_descendants(f.view);
    struct Content{HWND item;std::string text;SIZE dimensions;bool row;};std::vector<Content> content;
    for(const auto item:handles){wchar_t type[128]{};GetClassNameW(item,type,128);
        if(std::wstring_view(type)==L"LearnGameAndEngineDev.LessonPane")continue;
        RECT bounds{};GetClientRect(item,&bounds);wchar_t parent_type[128]{};GetClassNameW(GetParent(item),parent_type,128);
        content.push_back({item,window_text(item),{bounds.right,bounds.bottom},
            std::wstring_view(parent_type)==L"LearnGameAndEngineDev.LessonPane"});
    }
    std::map<HWND,RECT> document_positions;
    const auto unchanged=[&](int position){
        require(scroll_descendants(f.view)==handles,"Pure scrolling recreated or removed a lesson control");
        const auto viewport=screen_client(pane);
        for(const auto& item:content){RECT bounds{};GetClientRect(item.item,&bounds);
            require(bounds.right==item.dimensions.cx&&bounds.bottom==item.dimensions.cy&&window_text(item.item)==item.text,
                "Pure scrolling changed a control's dimensions or exact text");
            RECT clipped{};
            if(item.row&&scroll_visible_clip(item.item,pane,clipped)){
                RECT logical{};GetWindowRect(item.item,&logical);OffsetRect(&logical,-viewport.left,position-viewport.top);
                const auto [found,inserted]=document_positions.emplace(item.item,logical);
                require(inserted||EqualRect(&found->second,&logical),"Translating the viewport changed a row's logical document geometry");
            }
        }
        CHARRANGE current{};POINT offset{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&current));
        SendMessageW(editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        require(current.cpMin==selection.cpMin&&current.cpMax==selection.cpMax&&offset.x==inner.x&&offset.y==inner.y,
            "Page scrolling changed the editor's selection or internal scroll position");
        require(f.page()==0&&reading_script(f.view)==script,"Pure vertical movement changed the page or its narration sources");
    };
    unchanged(initial.nPos);RedrawWindow(pane,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    {
        ScrollMotionTrace trace(f.view);
        const auto position=[&]{SCROLLINFO value{sizeof(value),SIF_ALL};GetScrollInfo(pane,SB_VERT,&value);return value;};
        const auto drag=[&](int direction){
            const auto start_position=position();RECT area{};GetClientRect(pane,&area);
            const int scale=static_cast<int>(GetDpiForWindow(pane));
            const POINT start{MulDiv(8,scale,96),area.bottom/2};
            SendMessageW(pane,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
            int previous=start_position.nPos;
            for(int step=1;step<=24;++step){
                const auto distance=MulDiv(18+step*6,scale,96)*direction;
                SendMessageW(pane,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(start.x,start.y-distance));
                const auto current=position();const auto maximum=std::max(0,current.nMax-static_cast<int>(current.nPage)+1);
                const auto expected=std::clamp(start_position.nPos+distance,0,maximum);
                require(current.nPos==expected&&current.nPos!=previous,
                    "A real vertical drag did not expose every continuous requested scroll position");
                trace.quiet();unchanged(current.nPos);require_clean_visible_scroll_frame(pane);previous=current.nPos;
            }
            const auto end_distance=MulDiv(18+24*6,scale,96)*direction;
            SendMessageW(pane,WM_LBUTTONUP,0,MAKELPARAM(start.x,start.y-end_distance));
            require(position().nPos==previous,"Releasing a vertical drag changed its final position");trace.quiet();
        };
        drag(1);drag(-1);
        for(const auto delta:{-WHEEL_DELTA,-WHEEL_DELTA,WHEEL_DELTA,-WHEEL_DELTA}){
            const auto before=position();SendMessageW(pane,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),0);
            const auto after=position();require((delta<0&&after.nPos>before.nPos)||(delta>0&&after.nPos<before.nPos),
                "The actual mouse wheel did not move only the current viewport");
            trace.quiet();unchanged(after.nPos);pump(f.host);trace.quiet();
        }
    }
    SCROLLINFO final_position{sizeof(final_position),SIF_ALL};GetScrollInfo(pane,SB_VERT,&final_position);
    const auto final_state=f.saved();
    const auto without_scroll=[&](state::StateData value){value.preferences["native_windows"].erase(
        "scroll/"+f.lesson.id+'/'+f.lesson.pages[0].stable_id);return value;};
    require(without_scroll(final_state)==without_scroll(state_before),
        "Pure movement changed preferences, drafts, answers, attempts or achievements beyond the page offset");
    const auto original_source=f.text(0,10),original_feedback=f.text(1,4);f.reopen();
    const auto restored_pane=descendant_class(f.view,L"LearnGameAndEngineDev.LessonPane");SCROLLINFO restored{sizeof(restored),SIF_POS};
    GetScrollInfo(restored_pane,SB_VERT,&restored);const auto dpi=static_cast<int>(GetDpiForWindow(restored_pane));
    const auto saved_dp=MulDiv(final_position.nPos,96,dpi),expected_pixels=MulDiv(saved_dp,dpi,96);
    require(restored.nPos==expected_pixels&&f.text(0,10)==original_source&&f.text(1,4)==original_feedback
        &&without_scroll(f.saved())==without_scroll(final_state),
        "Reopening lost the real final page offset, code draft, accepted response or unrelated preferences");
    std::cout<<"PASS pure scroll motion "<<(tall?"over32767 rebased bands":"ordinary page")
        <<": 48 actual drag moves plus wheel, no native content measurement/resize, exact positions and geometry, synchronous clean drag frames, same HWNDs/text/selection/internal scroll, preserved state and reopen; no timing threshold\n";
}

void compiler_pointer_scroll_ownership(){
    const std::string initial=[&]{std::string code="int main() {\n";
        for(int row=0;row<70;++row)code+="    // línea "+std::to_string(row)+"\n";
        return code+"    // "+std::string(260,'x')+"\n    return 0;\n}\n";}();
    Fixture f([](state::StateData& data,const curriculum::Lesson&){
        data.preferences["compiler_pointer_qa"]["opaque"]=std::string(" \t conservar\r\n");
    },[&](const curriculum::Lesson&){
        curriculum::Lesson lesson;lesson.id="qa_compiler_pointer_scroll";lesson.locale="es";lesson.title="Desplazamiento del compilador";
        curriculum::Page page;page.stable_id="page";curriculum::Block editor;
        editor.type=BlockType::cpp_compiler;editor.stable_id="source";editor.initial_code=initial;editor.address={0,0,{},{}};page.blocks.push_back(editor);
        for(int n=0;n<5;++n){curriculum::Block prose;prose.type=BlockType::paragraph;prose.stable_id="tail_"+std::to_string(n);
            prose.address={0,page.blocks.size(),{},{}};for(int row=0;row<15;++row)prose.text+="Contenido que permite desplazar la lección.\n";
            page.blocks.push_back(std::move(prose));}
        lesson.pages.push_back(std::move(page));return lesson;
    },true);
    const auto editor=f.control(0,10),pane=descendant_class(f.view,L"LearnGameAndEngineDev.LessonPane");
    require(editor&&pane&&!IsWindowVisible(f.host),"Compiler pointer QA must use a complete hidden isolated view");
    f.replace(0,10,initial+"// historial conservado\n");const auto source=f.text(0,10);
    const auto outer=[&]{SCROLLINFO value{sizeof(value),SIF_POS};require(GetScrollInfo(pane,SB_VERT,&value),"Missing real lesson scrollbar");return value.nPos;};
    const auto inner=[&]{POINT value{};SendMessageW(editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&value));return value;};
    const auto diagnostic=[&](std::string_view stage){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(editor,SB_VERT,&info);
        const auto position=inner();std::cout<<"INFO compiler pointer "<<stage<<" inner="<<position.x<<','<<position.y
            <<" range="<<info.nMin<<','<<info.nMax<<" page="<<info.nPage<<" pos="<<info.nPos<<" outer="<<outer()<<'\n';};
    const auto wheel=[&](int delta){SendMessageW(editor,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),0);pump(f.host);};
    const auto blank=[&]{RECT bounds{};GetClientRect(editor,&bounds);
        for(int y=4;y<bounds.bottom-4;y+=6)for(int x=bounds.right-24;x>bounds.right/2;x-=12)
            if(!rich_text_hit_test(editor,{x,y}))return POINT{x,y};
        throw std::runtime_error("The compiler fixture has no genuine blank-space hit");};
    const CHARRANGE selection{4,12};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    SendMessageW(editor,WM_VSCROLL,SB_TOP,0);SendMessageW(f.view,WM_VSCROLL,SB_TOP,0);pump(f.host);
    const auto before=inner();wheel(-WHEEL_DELTA);
    require(inner().y>before.y&&outer()==0,"A scrollable compiler failed to consume its own usable wheel direction");
    SendMessageW(editor,WM_VSCROLL,SB_BOTTOM,0);pump(f.host);const auto bottom=inner();
    require(bottom.y>0,"The actual source editor has no vertical overflow");diagnostic("before bottom wheel");wheel(-WHEEL_DELTA);diagnostic("after bottom wheel");
    require(outer()>0&&inner().y==bottom.y,"The mouse wheel stopped at the compiler bottom instead of reaching the lesson");
    const auto whole_notch=outer();SendMessageW(f.view,WM_VSCROLL,SB_TOP,0);pump(f.host);wheel(-WHEEL_DELTA/2);
    require(outer()==0&&inner().y==bottom.y,"A partial compiler wheel notch escaped its accumulator");wheel(-WHEEL_DELTA/2);
    require(outer()==whole_notch&&inner().y==bottom.y,"Two half wheel notches lost motion when handed from compiler to lesson");
    const auto parent_before=outer();wheel(WHEEL_DELTA);
    require(inner().y<bottom.y&&outer()==parent_before,"The compiler gave away a wheel direction that could still move its own contents");
    SendMessageW(editor,WM_VSCROLL,SB_TOP,0);pump(f.host);wheel(WHEEL_DELTA);
    require(outer()<parent_before&&inner().y==0,"The mouse wheel stopped at the compiler top instead of reaching the lesson");
    // Blank space starts a pan even when the learner has selected code. At an
    // internal edge its remaining movement belongs to the parent viewport.
    SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    SendMessageW(editor,WM_VSCROLL,SB_TOP,0);SendMessageW(f.view,WM_VSCROLL,SB_TOP,0);pump(f.host);
    f.drag_control(editor,blank(),{0,-60});
    CHARRANGE retained{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&retained));
    require(inner().y>0&&outer()==0&&retained.cpMin==selection.cpMin&&retained.cpMax==selection.cpMax,
        "A selected blank-space drag could not scroll the compiler's usable inner viewport");
    SendMessageW(editor,WM_VSCROLL,SB_BOTTOM,0);SendMessageW(f.view,WM_VSCROLL,SB_TOP,0);pump(f.host);
    const auto drag_bottom=inner();f.drag_control(editor,blank(),{0,-72});
    diagnostic("after selected bottom blank drag");
    SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&retained));
    require(outer()>0&&inner().y==drag_bottom.y&&retained.cpMin==selection.cpMin&&retained.cpMax==selection.cpMax
        &&f.text(0,10)==source,"A blank-space compiler drag blocked at the edge or changed source/selection");
    // A real glyph stays with native selection, rather than claiming a pan.
    SendMessageW(editor,WM_VSCROLL,SB_TOP,0);SendMessageW(f.view,WM_VSCROLL,SB_TOP,0);pump(f.host);
    POINTL glyph{};SendMessageW(editor,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&glyph),5);const POINT hit{glyph.x+1,glyph.y+4};
    require(rich_text_hit_test(editor,hit),"The compiler selection test did not start on a real glyph");
    const auto old_outer=outer();const auto old_inner=inner();f.drag_control(editor,hit,{80,0});
    require(outer()==old_outer&&inner().x==old_inner.x&&inner().y==old_inner.y&&f.text(0,10)==source,
        "A text hit was hijacked by blank-space scrolling");
    require(SendMessageW(editor,WM_UNDO,0,0),"Pointer routing removed the source Undo transaction");pump(f.host);
    require(f.text(0,10)==initial,"A pointer gesture edited code or corrupted Undo");
    require(SendMessageW(editor,EM_REDO,0,0),"Pointer routing removed the source Redo transaction");pump(f.host);
    require(f.text(0,10)==source,"Redo failed after compiler wheel/pan/selection routing");
    // An autosized short source has no inner overflow at all.
    f.replace(0,10,"int main() {}\n");SendMessageW(f.view,WM_VSCROLL,SB_TOP,0);pump(f.host);wheel(-WHEEL_DELTA);
    require(outer()>0&&inner().y==0,"The fitted compiler swallowed vertical lesson scrolling");
    const auto saved_state=f.saved();require(saved_state.preferences.at("compiler_pointer_qa")==f.initial_data.preferences.at("compiler_pointer_qa")
        &&!state::get_response(saved_state,f.lesson,f.lesson.pages[0].blocks[0]),"Pointer scrolling changed unrelated preferences or manufactured an exercise result");
    f.reopen();require(f.text(0,10)=="int main() {}\n"&&!IsWindowVisible(f.host),"Reopening lost the source draft or exposed a personal window");
    std::cout<<"PASS actual compiler pointer routing: internal wheel while usable, top/bottom handoff, selected blank-space pan, native text hit, fitted editor, unchanged source/Undo/Redo and isolated reopen\n";
}
#include "presentation/windows/u12_file_roundtrip_view_test.hpp"
#include "presentation/windows/u12_file_append_view_test.hpp"
#include "presentation/windows/u12_file_repairs_view_test.hpp"
#include "presentation/windows/u12_composed_feedback_view_test.hpp"
struct NarrationLayoutTrace {
    unsigned geometry=0,formatting=0,erases=0;
    std::vector<HWND> targets;
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto& trace=*reinterpret_cast<NarrationLayoutTrace*>(raw);
        if(message==WM_SIZE||message==EM_SETRECT||message==EM_SETRECTNP||message==EM_SETMARGINS)++trace.geometry;
        if(message==WM_SETFONT||message==EM_SETZOOM||message==EM_SETTARGETDEVICE||message==EM_SETCHARFORMAT)++trace.formatting;
        if(message==WM_ERASEBKGND)++trace.erases;
        return DefSubclassProc(item,message,wp,lp);
    }
    explicit NarrationLayoutTrace(std::initializer_list<HWND> items):targets(items) {
        for(const auto item:targets)require(SetWindowSubclass(item,proc,0x4e415252,reinterpret_cast<DWORD_PTR>(this)),"Cannot observe native narration repaints");
    }
    ~NarrationLayoutTrace(){for(const auto item:targets)if(IsWindow(item))RemoveWindowSubclass(item,proc,0x4e415252);}
    void clear(){geometry=formatting=erases=0;}
    std::string counts()const{return "geometry="+std::to_string(geometry)+" formatting="+std::to_string(formatting)+" erases="+std::to_string(erases);}
};
void narration_checkpoints_do_not_reformat_or_reflow_the_page() {
    Fixture f({},[](const curriculum::Lesson&){return windows::parse_lesson_json(R"json({"id":"cpp_fundamentals__qa__narration_repaint","title":"Narración y vista estable","pages":[{"blocks":[
        {"type":"paragraph","text":"Uno dos tres. Conserva el contenido al guardar la lectura."},
        {"type":"code","code":"#include <iostream>\nint main() {\n    std::cout << \"Texto de ejemplo\";\n}\n"},
        {"type":"cpp_compiler","initialCode":"#include <iostream>\nint main() {\n    std::cout << \"Texto del editor\";\n}\n"}
    ]}]})json");});
    const auto paragraph=f.control(0,0),example=f.control(1,1),editor=f.control(2,10),pane=GetParent(paragraph);
    const auto initial=f.text(2,10);f.replace(2,10,initial+"// conservar Undo de la edición\n");
    const auto source=f.text(2,10),example_source=f.text(1,1);CHARRANGE selection{4,12};
    SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump(f.host);
    const auto before_scroll=[&]{POINT value{};SendMessageW(editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&value));return value;}();
    NarrationLayoutTrace trace{pane,paragraph,example,editor};
    const auto quiet=[&](std::string_view stage,const std::function<void(state::StateData&)>& change){
        trace.clear();update_lesson_preferences(f.view,change);pump(f.host);
        require(trace.geometry==0&&trace.formatting==0&&trace.erases==0,
            std::string("A nonvisual narration update repainted/reformatted the page: ")+std::string(stage)+" "+trace.counts());
        CHARRANGE after{};POINT scroll{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));SendMessageW(editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        require(after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&scroll.x==before_scroll.x&&scroll.y==before_scroll.y&&
            f.text(2,10)==source&&f.text(1,1)==example_source,"A narration checkpoint moved the editor or changed its source/selection");
    };
    quiet("begin",[](state::StateData& data){auto history=narration::begin(narration::read_history(data),"qa-narration-key","qa-owner",{}, {},"qa-page");narration::write_history(data,history);});
    for(const auto milliseconds:{1201,2402,3603})quiet("checkpoint",[&](state::StateData& data){
        auto history=narration::checkpoint(narration::read_history(data),"qa-narration-key","qa-owner",{0,milliseconds,0,false});narration::write_history(data,history);});
    quiet("rate/voice/flow",[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=1.5;
        data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_alex");data.preferences["learning_flow_settings"]["qa_flow_choice"]=true;});
    quiet("another course",[](state::StateData& data){appearance::save_course_text_scale(data,"unrelated_course",false,125);
        appearance::save_course_wrap(data,"unrelated_course",true);});
    quiet("same effective scale",[](state::StateData& data){appearance::save_course_text_scale(data,"cpp_fundamentals",true,125);});
    quiet("retired editing bar shortcuts",[](state::StateData& data){data.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=std::string(R"(["std::cout","<<",";"])");});
    const auto state=f.saved();const auto history=narration::read_history(state);
    require(history.entries.size()==1&&history.entries[0].position.milliseconds==3603&&
        std::get<double>(*state::find(state,"lesson_narration_settings","rate"))==1.5,"A quiet narration update was not persisted");
    bool rejected=false;const auto before_rejection=f.saved();
    try{update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["learning_progress"]["qa_forbidden"]=true;});}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&f.saved()==before_rejection,"Avoiding redraw weakened the protection of student progress");
    trace.clear();update_lesson_preferences(f.view,[](state::StateData& data){appearance::save_global_text_scale(data,150);});pump(f.host);
    int numerator=0,denominator=0;SendMessageW(editor,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
    require(trace.formatting>0&&denominator>0&&numerator*100==denominator*150,"A real scale preference no longer reformats the live editor");
    trace.clear();update_lesson_preferences(f.view,[](state::StateData& data){appearance::save_course_wrap(data,"cpp_fundamentals",true);});pump(f.host);
    require(!(GetWindowLongPtrW(editor,GWL_STYLE)&ES_AUTOHSCROLL)&&trace.formatting>0,"A real wrap preference no longer updates the live editor");
    update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["appearance_settings"]["route_accent_blue_programming"]=std::int64_t{123};});pump(f.host);
    const auto colors=reader_theme::palette(f.view);require(colors.high_contrast||colors.accent==RGB(228,50,123),"A real route accent preference no longer repaints its current controls");
    update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["native_windows"]["ui_language"]=std::string("en");});pump(f.host);
    require(accessible_name(f.control(110))=="Back"&&f.text(2,10)==source&&f.text(1,1)==example_source,
        "Refreshing the interface language lost labels or changed authored code");
    update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["native_windows"]["ui_language"]=std::string("es");});pump(f.host);
    require(accessible_name(f.control(110))=="Volver","Returning to Spanish did not refresh the live interface");
    require(f.text(2,10)==source&&SendMessageW(editor,WM_UNDO,0,0)&&f.text(2,10)==initial,
        "Quiet checkpoint persistence or later real view updates discarded source Undo");
    require(SendMessageW(editor,EM_REDO,0,0)&&f.text(2,10)==source,"Checkpoint persistence discarded source Redo");
    std::cout<<"PASS narration checkpoints: persisted begin/1200ms history/rate/voice/flow without page geometry, font, zoom or erase work; real scale/wrap/accent, protected progress, source/selection/Undo/Redo retained\n";
}
}

void current_inline_feedback_uses_authored_controls_and_restores_each_attempt() {
    struct Plan { const char* filename; std::size_t page,block; std::vector<std::string> incorrect;
        std::size_t ordinary_prefix,compiler_prefix=0; };
    const Plan plans[]{
        {"objetos_y_estado/lesson_enteros_y_aproximaciones.json",7,6,{"int","longitud_metros","partes"},12,2},
        {"objetos_y_estado/lesson_enteros_y_aproximaciones.json",3,3,{"double","int"},6},
        {"objetos_y_estado/lesson_declarar_e_inicializar.json",2,4,{"12","3"},3},
        {"objetos_y_estado/lesson_estado_asignacion_y_copia.json",5,4,{"+","-"},10},
        {"objetos_y_estado/lesson_nombres_con_intencion.json",3,4,{"recibidos","sobres_recibidos"},5},
        {"objetos_y_estado/lesson_conversiones_y_deduccion.json",3,5,{"int"},6},
        {"objetos_y_estado/lesson_cadenas_y_texto.json",3,4,{"\"Sector\"","\"Este\""},6},
        {"objetos_y_estado/lesson_caracteres_y_valores_logicos.json",2,3,{"'Y'","'Y'"},4},
        {"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json",4,1,{"<<","\"Carga\""},12},
    };
    const auto ordinary=[](BlockType type){return type==BlockType::multiple_choice||type==BlockType::output_prediction||
        type==BlockType::fill_code||type==BlockType::order_code||type==BlockType::inline_fill_code||type==BlockType::drag_fill_code;};
    const auto entry=[&](const state::StateData& data,const curriculum::Lesson& lesson,std::size_t requested){
        std::vector<curriculum::EntryRequirement> requirements;
        const auto proofs=curriculum::compiler_completion_snapshot(lesson,[&](const auto& block){
            return windows::compiler_puzzle_completed(data,lesson,block);});
        for(std::size_t page=0;page<lesson.pages.size();++page)for(const auto& block:lesson.pages[page].blocks){
            if(curriculum::compiler_requires_completion(block))requirements.push_back({page,proofs.at(block.stable_id)
                ?curriculum::EntryCompletion::complete:curriculum::EntryCompletion::incomplete});
            else if(ordinary(block.type)){
                const auto response=state::get_response(data,lesson,block);
                requirements.push_back({page,response&&response->correct?curriculum::EntryCompletion::complete:curriculum::EntryCompletion::incomplete});
            }else if(page<requested)require(block.type!=BlockType::math_puzzle,
                "The nine audited inline prefixes acquired an unreviewed mathematical prerequisite");
        }
        return curriculum::lesson_entry_decision(state::clamp_reachable_page(data,lesson,requested),lesson.pages.size(),requirements);
    };
    std::size_t alternatives=0,synthetic_ordinary=0,synthetic_compilers=0;
    for(const auto& plan:plans){
        const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/plan.filename;
        const auto hash=state::sha256(state::read_bytes(path));
        Fixture f([&](state::StateData& data,const curriculum::Lesson& lesson){
            using namespace winrt::Windows::Data::Json;
            state::prepare_lesson_state(data,lesson);const auto empty=data;
            require(entry(empty,lesson,plan.page).page<plan.page,
                "The old address-only inline fixture unexpectedly bypassed its uncompleted prefix");
            std::vector<std::string> answers,compilers;
            // QA isolation only: evaluate the canonical ordinary prefix and
            // disclose the two model compiler receipts. The target and every
            // subsequent page stay untouched. Arrival is not listening/study.
            for(std::size_t page=0;page<=plan.page;++page){
                const auto decision=entry(data,lesson,page);
                require(decision.page==page&&!decision.pending_math&&state::can_reach_page(data,lesson,page),
                    "The inline QA setup attempted to skip an unresolved activity or an unreached page");
                state::save_current_page(data,lesson,page);
                if(page==plan.page)break;
                for(const auto& block:lesson.pages[page].blocks){
                    if(block.type==BlockType::cpp_compiler){
                        require(block.output_check&&!block.output_check->cases.empty()&&!block.compile_check,
                            "The audited inline prefix needs a new compiler prerequisite policy");
                        windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
                        request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
                        for(const auto& item:block.output_check->cases){
                            require(item.exit_code==0&&!item.outputs.empty(),"The inline prefix acquired another process-result contract");
                            request.stdin_utf8=item.standard_input;
                            curriculum::FreshExecution result{1,true,true,0,item.standard_input,item.outputs.front()};result.executable=item.executable;
                            if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)result.standard_error="Synthetic QA prerequisite only";
                            (void)windows::record_compiler_puzzle_case(data,lesson,block,request,result,"Synthetic inline QA prerequisite only");
                        }
                        require(windows::compiler_puzzle_completed(data,lesson,block),"The synthetic inline prefix did not satisfy its owned compiler gate");
                        compilers.push_back(block.stable_id);continue;
                    }
                    if(!ordinary(block.type))continue;
                    require(!state::get_response(data,lesson,block),"The fresh inline QA setup refuses to replace an existing response");
                    state::Response response;curriculum::AnswerEvaluation result;
                    if(block.type==BlockType::multiple_choice){
                        require(block.correct_index&&*block.correct_index<block.options.size(),"Missing canonical inline-prefix choice");
                        response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];
                        result=curriculum::evaluate_choice(block,*block.correct_index);
                    }else if(block.type==BlockType::order_code){
                        response.order.assign(block.correct_order.begin(),block.correct_order.end());result=curriculum::evaluate_order(block,block.correct_order);
                    }else if(block.type==BlockType::inline_fill_code||block.type==BlockType::drag_fill_code){
                        JsonObject raw;JsonArray values;
                        if(block.type==BlockType::inline_fill_code){
                            result=curriculum::evaluate_hole_answers(block,block.answers);
                            for(const auto& answer:block.answers)values.Append(JsonValue::CreateStringValue(winrt::to_hstring(answer)));
                        }else{
                            result=curriculum::evaluate_drag_answers(block,block.correct_order);
                            for(const auto slot:block.correct_order)values.Append(JsonValue::CreateNumberValue(static_cast<double>(slot)));
                        }
                        raw.Insert(block.type==BlockType::inline_fill_code?L"answers":L"slots",values);response.raw_json=winrt::to_string(raw.Stringify());
                    }else{
                        require(!block.answers.empty(),"Missing canonical inline-prefix text answer");response.answer=block.answers.front();
                        result=curriculum::evaluate_text_answer(block,response.answer);
                    }
                    require(result.correct,"The synthetic inline prefix failed its actual authored evaluator");
                    response.correct=true;state::mark_response_reviewed(response,block);state::put_response(data,lesson,block,response);answers.push_back(block.stable_id);
                }
            }
            require(answers.size()==plan.ordinary_prefix&&compilers.size()==plan.compiler_prefix,
                "The audited inline fixture's exact synthetic prefix inventory changed");
            state::validate_lesson_state(data,lesson);synthetic_ordinary+=answers.size();synthetic_compilers+=compilers.size();
            data.preferences["inline_feedback_qa"]["synthetic_ordinary_prefix_only"]=answers;
            data.preferences["inline_feedback_qa"]["synthetic_compiler_prefix_only"]=compilers;
            data.preferences["inline_feedback_qa"]["unrelated"]=std::string(" \t preserve\r\n");
        },[&](const curriculum::Lesson&){return windows::load_lesson(path);},true);
        const auto& block=f.lesson.pages.at(plan.page).blocks.at(plan.block);
        require(block.type==BlockType::inline_fill_code&&!block.review_feedback.empty()&&!block.explanation.empty(),
            "A registered inline feedback location no longer identifies its authored contract");
        require(!state::get_response(f.initial_data,f.lesson,block)&&!IsWindowVisible(f.host)&&
            GetAncestor(GetForegroundWindow(),GA_ROOT)!=f.host,
            "Inline QA seeded its target or activated its hidden host during setup");
        require(plan.incorrect.size()==block.answers.size()&&!curriculum::evaluate_hole_answers(block,plan.incorrect).correct,
            "The focal incorrect answer must exercise the authored evaluator");
        const auto unchanged=f.saved().preferences.at("inline_feedback_qa");
        const auto field=[&](std::size_t index){const auto value=GetDlgItem(f.control(plan.block,5),100+static_cast<int>(index));
            require(value!=nullptr,"A registered inline hole is missing");return value;};
        const auto replace=[&](const std::vector<std::string>& values){
            for(std::size_t index=0;index<values.size();++index){const auto target=field(index);require(IsWindowEnabled(target),"An editable inline hole is locked");
                const auto text=wide(values[index]);SendMessageW(target,EM_SETSEL,0,-1);
                SendMessageW(target,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));pump(f.host);
                require(edit_text(target)==values[index],"An inline field changed the exact submitted characters");}
        };
        const auto verify_fields=[&](const std::vector<std::string>& values,bool enabled){
            for(std::size_t index=0;index<values.size();++index)require(edit_text(field(index))==values[index]
                &&(IsWindowEnabled(field(index))!=FALSE)==enabled,"Reopening lost an inline answer or its completion lock");
        };
        const auto feedback_narration=[&](bool correct,std::string_view expected){
            std::string script;for(const auto& region:lesson_reading_regions(f.view))if(region.target==f.control(plan.block,4))script+=region.source_text;
            require(script==std::string(correct?"✓ Correcto\n\n":"✗ Incorrecto\n\n")+std::string(expected),
                "The inline narration source omitted current feedback or added hidden solution text");
        };
        // These registered prose fields use backticks solely as inline-code
        // delimiters. RichEdit hides them for display; the narration assertion
        // above independently requires the complete original authored source.
        const auto visible=[](std::string value){std::erase(value,'`');return value;};
        require(f.page()==plan.page&&f.text(plan.block,4).empty()&&!IsWindowEnabled(f.control(plan.block,3)),
            "An unanswered inline exercise displayed feedback or enabled an empty submission");
        for(const auto& region:lesson_reading_regions(f.view))require(region.target!=f.control(plan.block,4),
            "An unanswered inline exercise exposed feedback to narration");
        verify_fields(std::vector<std::string>(block.answers.size()),true);
        replace(plan.incorrect);f.reopen();verify_fields(plan.incorrect,true);
        const auto draft=state::get_response(f.saved(),f.lesson,block);
        require(draft&&!draft->correct&&!state::response_reviewed(*draft,block)&&f.text(plan.block,4).empty(),
            "Reopening an unsubmitted inline draft invented an evaluated attempt");
        f.click(plan.block,3);const auto wrong=f.text(plan.block,4);
        require(wrong=="✗ Incorrecto\n\n"+visible(block.review_feedback)
            &&wrong.find(visible(block.explanation))==std::string::npos,"An incorrect inline attempt lost its authored guide or exposed the solution");
        feedback_narration(false,block.review_feedback);const auto reviewed=state::get_response(f.saved(),f.lesson,block);
        require(reviewed&&!reviewed->correct&&state::response_reviewed(*reviewed,block),"The rejected inline attempt was not persisted as reviewed");
        f.reopen();verify_fields(plan.incorrect,true);require(f.text(plan.block,4)==wrong,"Restoration changed the exact authored rejection feedback");
        feedback_narration(false,block.review_feedback);
        replace(block.answers);require(f.text(plan.block,4).empty(),"A corrected inline draft retained feedback for a different answer");
        f.click(plan.block,3);const auto correct=f.text(plan.block,4);
        require(correct=="✓ Correcto\n\n"+visible(block.explanation),
            "The accepted inline answer lost its complete current explanation");
        feedback_narration(true,block.explanation);verify_fields(block.answers,false);
        f.reopen();verify_fields(block.answers,false);require(f.text(plan.block,4)==correct,"Restoration changed current success feedback");
        feedback_narration(true,block.explanation);
        f.click(plan.block,12);verify_fields(std::vector<std::string>(block.answers.size()),true);
        require(f.text(plan.block,4).empty(),"Explicit inline Reset retained the previous verdict");
        for(std::size_t index=0;index<block.answer_alternatives.size();++index)for(const auto& option:block.answer_alternatives[index]){
            if(option==block.answers[index])continue;auto answer=block.answers;answer[index]=option;
            require(curriculum::evaluate_hole_answers(block,answer).correct,"A published inline alternative became invalid");
            replace(answer);f.click(plan.block,3);verify_fields(answer,false);
            require(f.text(plan.block,4)==correct,"An admitted inline alternative lost the same complete explanation");
            f.reopen();verify_fields(answer,false);require(f.text(plan.block,4)==correct,"Reopening changed a valid alternative's completion");
            f.click(plan.block,12);++alternatives;
        }
        const auto final=f.saved();
        for(std::size_t page=0;page<f.lesson.pages.size();++page)for(const auto& current:f.lesson.pages[page].blocks){
            if(page<plan.page&&ordinary(current.type)){
                const auto key=state::response_storage_key(f.lesson,current);
                require(state::string_value(final,"learning_progress",key)==state::string_value(f.initial_data,"learning_progress",key),
                    "Inline control QA changed an exact synthetic prerequisite response");
            }else if(ordinary(current.type)&& &current!=&block)require(!state::get_response(final,f.lesson,current),
                "Inline control QA manufactured an answer outside its prefix and actual target");
            if(current.type==BlockType::cpp_compiler)require(windows::compiler_puzzle_completed(final,f.lesson,current)==(page<plan.page),
                "Inline control QA changed an independent compiler gate or credited a later editor");
        }
        for(const auto& [key,value]:f.initial_data.preferences.at("native_windows"))if(key.starts_with("compiler-puzzle/v1/"))
            require(state::find(final,"native_windows",key)&&*state::find(final,"native_windows",key)==value,
                "Inline control QA rewrote one of its disclosed model compiler receipts");
        require(!IsWindowVisible(f.host)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=f.host&&final.preferences.at("inline_feedback_qa")==unchanged
            &&hash==state::sha256(state::read_bytes(path)),"Inline QA changed a curricular asset, unrelated state or the user's desktop");
        std::cout<<"PASS actual inline feedback "<<plan.filename<<" p"<<plan.page+1<<" b"<<plan.block+1
            <<": draft/rejection/correction/success/Reset/reopen and exact feedback narration sources\n";
    }
    require(synthetic_ordinary==64&&synthetic_compilers==2,"The nine inline controls used another synthetic prerequisite scope");
    std::cout<<"QA setup only: 64 canonical ordinary prerequisites and two synthetic compiler objectives, with sequential admitted arrivals; no compiler executions or prior listening claimed.\n";
    std::cout<<"PASS nine registered inline controls and "<<alternatives<<" additional authored alternatives; no synthesis or human listening claimed\n";
}

int main(int argc,char* argv[]) {
#ifdef LGAED_INLINE_FEEDBACK_ONLY
    if(argc!=2||std::string_view(argv[1])!="--inline-feedback")return 2;
#endif
    if (argc > 1 && std::string_view(argv[1]) == "--prepare-stderr-visual") {
        const bool shape = argc == 4 && (std::string_view(argv[2]) == "u8" || std::string_view(argv[2]) == "u10");
        const auto raw = shape ? std::string_view(argv[3]) : std::string_view{};
        const std::wstring token(raw.begin(), raw.end());
        if (!shape || !is_valid_qa_instance(token)) {
            std::cerr << "Usage: lesson_view_tests --prepare-stderr-visual <u8|u10> <safe-new-QA-token>\n";
            return 2;
        }
        // This preparation branch must not initialize OLE, register a window
        // class or create native controls. Interactive verification is separate.
        try { prepare_stderr_visual_profile(argv[2], token); return 0; }
        catch (const std::exception& error) { std::cerr << "Stderr visual QA preparation failed: " << error.what() << '\n'; return 1; }
    }
    const bool scroll_repaint_only=argc==2&&std::string_view(argv[1])=="--scroll-repaint";
    const bool scroll_motion_only=argc==2&&std::string_view(argv[1])=="--scroll-motion";
    const bool compiler_scroll_only=argc==2&&std::string_view(argv[1])=="--compiler-scroll";
    const bool u12_file_roundtrip_only=argc==2&&std::string_view(argv[1])=="--u12-file-roundtrip";
    const bool u12_file_append_only=argc==2&&std::string_view(argv[1])=="--u12-file-append";
    const bool u12_file_repairs_only=argc==2&&std::string_view(argv[1])=="--u12-file-repairs";
    const bool u12_composed_feedback_only=argc==4&&std::string_view(argv[1])=="--u12-composed-feedback";
    const bool caret_only=argc==2&&std::string_view(argv[1])=="--caret";
    const bool text_sequence_only=argc==2&&std::string_view(argv[1])=="--texto-secuencia";
    const bool restoration_only=argc==2&&std::string_view(argv[1])=="--texto-restauracion";
    const bool compiler_puzzles_only=argc==2&&std::string_view(argv[1])=="--compiler-puzzles";
    const bool u11_editorial_only=argc==2&&std::string_view(argv[1])=="--u11-editorial";
    const bool stderr_objective_only=argc==2&&std::string_view(argv[1])=="--stderr-objective";
    const bool dependent_puzzles_only=argc==2&&std::string_view(argv[1])=="--dependent-puzzles";
    const bool current_dependent_puzzles_only=(argc==2||argc==4)&&std::string_view(argv[1])=="--current-dependent-puzzles";
    const bool editor_overflow_only=argc==2&&std::string_view(argv[1])=="--editor-overflow";
    const bool answer_overflow_only=argc==2&&std::string_view(argv[1])=="--answer-overflow";
    const bool narration_repaint_only=argc==2&&std::string_view(argv[1])=="--narration-repaint";
    const bool compiler_height_only=argc==2&&std::string_view(argv[1])=="--compiler-height";
    const bool navigation_geometry_only=argc==2&&std::string_view(argv[1])=="--navigation-geometry";
    const bool inline_feedback_only=argc==2&&std::string_view(argv[1])=="--inline-feedback";
    const bool lesson_menu_only=argc==2&&std::string_view(argv[1])=="--lesson-menu";
    if(argc>1&&!scroll_repaint_only&&!scroll_motion_only&&!compiler_scroll_only&&!u12_file_roundtrip_only&&!u12_file_append_only&&!u12_file_repairs_only&&!u12_composed_feedback_only&&!caret_only&&!text_sequence_only&&!restoration_only&&!compiler_puzzles_only&&!u11_editorial_only&&!stderr_objective_only&&!dependent_puzzles_only&&!current_dependent_puzzles_only&&!editor_overflow_only&&!answer_overflow_only&&!narration_repaint_only&&!compiler_height_only&&!navigation_geometry_only&&!inline_feedback_only&&!lesson_menu_only)return 2;
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    struct OleScope{~OleScope(){OleUninitialize();}} apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr); host.lpszClassName = L"LgaedLessonViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        if(lesson_menu_only){lesson_menu_retains_import_pages_and_settings_with_hooks();return 0;}
        if(inline_feedback_only){current_inline_feedback_uses_authored_controls_and_restores_each_attempt();return 0;}
        if(navigation_geometry_only){completed_compilers_keep_navigation_after_active_content();return 0;}
        if(scroll_repaint_only){simulated_code_and_choice_cards();Fixture fixture;fixture.viewport_layout();fixture.scroll_repaint();return 0;}
        if(scroll_motion_only){pure_scroll_motion_keeps_native_document(false);pure_scroll_motion_keeps_native_document(true);return 0;}
        if(compiler_scroll_only){compiler_pointer_scroll_ownership();return 0;}
        if(u12_file_roundtrip_only){current_u12_file_roundtrip_view();return 0;}
        if(u12_file_append_only){current_u12_file_append_view();return 0;}
        if(u12_file_repairs_only){current_u12_file_repairs_view();return 0;}
        if(u12_composed_feedback_only){u12_composed_feedback_view(std::filesystem::path(argv[2]),std::filesystem::path(argv[3]));return 0;}
        if(compiler_height_only){native_compiler_short_sources_keep_a_usable_height();return 0;}
        if(narration_repaint_only){narration_checkpoints_do_not_reformat_or_reflow_the_page();return 0;}
        if(answer_overflow_only){native_answer_overflow_bars();return 0;}
        if(editor_overflow_only){native_compiler_editor_overflow_bars();return 0;}
        if(restoration_only){text_sequence_application_restoration();return 0;}
        if(u11_editorial_only){u11_editorial_preserves_prediction_and_compiler_history();u11_grammar_choice_preserves_current_feedback_and_history();return 0;}
        if(stderr_objective_only){runtime_stderr_objective_keeps_legacy_history_and_requires_both_current_channels();integrated_u8l3_stderr_objective_preserves_history_and_requires_real_runtime_diagnostics();integrated_u10l4_stderr_objective_requires_its_own_constructor_and_runtime_report();return 0;}
        if(dependent_puzzles_only){dependent_puzzle_types_hide_payload_and_preserve_their_historical_responses();dependent_puzzle_bookmarks_resolve_transitive_pending_sources();authored_dependent_puzzle_locations_protect_historical_bookmarks();return 0;}
        if(current_dependent_puzzles_only){current_authored_dependent_puzzles(argc==4?std::stoul(argv[2]):0,argc==4?std::stoul(argv[3]):1);return 0;}
        if(compiler_puzzles_only){real_retirement_pages_preserve_all_eight_useful_editors();integrated_u1_compilers_require_all_eight_real_executions();retired_reading_regions_are_empty_during_import_and_return_after_cancel();multiple_input_compiler_is_one_pending_objective_until_every_case_passes();runtime_stderr_objective_keeps_legacy_history_and_requires_both_current_channels();integrated_u8l3_stderr_objective_preserves_history_and_requires_real_runtime_diagnostics();integrated_u10l4_stderr_objective_requires_its_own_constructor_and_runtime_report();compiler_puzzle_completion_and_navigation();return 0;}
        if(caret_only){simulated_exercise_code_cards(true);native_answer_end_viewport();return 0;}
        if(text_sequence_only){text_sequence_lesson_all_pages();return 0;}
        runtime_stderr_objective_keeps_legacy_history_and_requires_both_current_channels();
        integrated_u8l3_stderr_objective_preserves_history_and_requires_real_runtime_diagnostics();
        integrated_u10l4_stderr_objective_requires_its_own_constructor_and_runtime_report();
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
        native_compiler_short_sources_keep_a_usable_height();
        native_compiler_output_overflow_bars();
        lesson_menu_retains_import_pages_and_settings_with_hooks();
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
        require(caption.top == reset.top && reset.left == caption.right && source.top == caption.bottom,
                "Reset control is not beside C++ as specified by the existing lesson");
        SCROLLINFO wheel_before{sizeof(wheel_before), SIF_POS}, wheel_after = wheel_before;
        const auto pane = GetParent(view.control(3,10)); GetScrollInfo(pane, SB_VERT, &wheel_before);
        SendMessageW(view.control(2,0), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        GetScrollInfo(pane, SB_VERT, &wheel_after);
        require(wheel_after.nPos > wheel_before.nPos, "Wheel over read-only prose did not scroll the lesson");
        const auto& original = view.lesson.pages[4].blocks[3];
        const auto& modified = view.lesson.pages[4].blocks[6];
        require(view.text(3,10) == original.initial_code && !descendant(view.view,id(6,10)),
                "The original source changed or its dependent editor opened before the required execution");
        view.contextual_editing(3);
        view.editor_gestures(3);
        view.run(3);
        require(view.text(3,14) == "Zona inicial" && view.text(3,4) == "✓ Correcto", "Original program did not produce/check its fresh exact output");
        require(view.text(3,10) == original.initial_code && view.text(6,10) == modified.initial_code,
                "Unlocking the modification editor changed either exact authored source");
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

        view.click(6,12); // Only explicit reset starts another assessed attempt.
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
        view.replace(3,10,original_draft);
        require(descendant(view.view,id(6,10)),"Editing a completed prerequisite revoked the dependent editor");
        view.run(3);
        view.replace(6,10,modified_draft); view.run(6);
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
        require(view.text(3,4).starts_with("✓ Correcto") && view.text(6,4).starts_with("✓ Correcto"), "Reopening lost the independently captured compiler achievements");
        view.select_page(3); view.next();
        require(view.text(6,10) == modified_draft && view.text(6,4).starts_with("✓ Correcto"), "Page transition lost a compiler achievement or draft");
        view.solve(7); view.non_overlapping_rows(); view.next();
        std::cout << "PASS Compilation diagnostics, edit/reset invalidation, targeted reviews and persistent independent drafts\n" << std::flush;

        require(view.page() == 5 && !lesson_page_state(view.view).can_next, "Final page navigation escaped the lesson");
        const auto& independent = view.lesson.pages[5].blocks[6];
        view.replace(6,10,message_code(independent,"Caja recibida","Zona segura")); view.run(6);
        require(view.text(6,14) == "Zona segura" && view.text(6,4) == "✓ Correcto", "Page 6 did not keep its independent target");
        view.reopen();
        require(view.page() == 5 && view.text(6,10).find("Zona segura") != std::string::npos && view.text(6,4)=="✓ Correcto", "Final reopen lost position/source or the completed objective");
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
        require(!descendant(legacy.view,id(6,10)),"An imported draft impersonated a completed prerequisite");
        legacy.run(3);
        require(state::get_draft(read_lesson_state(legacy.view),curriculum::draft_key("programming",legacy.lesson,
                    legacy.lesson.pages[4].blocks[6]),"").source == legacy_source,
                "A fresh prerequisite execution changed the independent historical draft");
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
        require(current_reference() && references.text(6,4).starts_with("✓ Correcto"),
                "Reopening lost a sealed completion or its approved feedback");
        references.replace(6,10,accepted + "// edición después del acierto\n");
        require(current_reference(),"Editing revoked a completed objective's reference");
        SendMessageW(references.control(6,10),WM_UNDO,0,0);
        pump(references.host);
        require(references.text(6,10) == accepted && current_reference(),
                "Undo lost source history or revoked an accepted completion");
        references.run(6); require(current_reference(),"A new correct run did not restore its own reference");
        references.select_page(4); references.run(3); references.next();
        require(!previous_reference() && current_reference(),"The original Zona inicial editor unlocked the Caja recibida reference");
        references.select_page(4);
        references.replace(6,10,message_code(references.lesson.pages[4].blocks[6],"Zona inicial","Caja recibida"));
        references.run(6); references.next();
        require(previous_reference() && current_reference(),"A previous-page correct compiler run did not unlock only its linked references");
        references.reopen();
        require(previous_reference() && current_reference() && references.text(6,4).starts_with("✓ Correcto"),
                "Cross-page completion proofs did not reopen with approved feedback");
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
                require(index == (verdicts == 0 ? 3u : 6u) && correct,"Feedback hooks changed activity identity or correctness"); ++verdicts;
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
            require(!lesson_page_state(preferences.view).can_next,"An original execution substituted for the modification prerequisite");
            preferences.replace(6,10,message_code(preferences.lesson.pages[4].blocks[6],"Zona inicial","Caja recibida"));
            preferences.run(6);
            require(verdicts == 2 && lesson_page_state(preferences.view).can_next,
                    "The modification's own fresh execution did not reach its hook or unlock the next page");
            preferences.reopen();
            require(verdicts == 2 && destroying == 1,"Reopening replayed a feedback submission or missed lifecycle cleanup");
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
        require(lesson_page_state(dependencies.view).can_next,"Source editing revoked a completed prerequisite");
        SendMessageW(prerequisite_editor,WM_UNDO,0,0); pump(dependencies.host);
        require(dependencies.text(6,10) == prerequisite_source && lesson_page_state(dependencies.view).can_next,
                "Undo changed a completed prerequisite's achievement");
        dependencies.run(6); dependencies.next();
        require(descendant(dependencies.view,id(0,0)) && descendant(dependencies.view,id(6,10)),
                "A completed prerequisite did not expose its references and dependent editor");
        const auto dependent_source = message_code(dependencies.lesson.pages[5].blocks[6],"Caja recibida","Zona segura");
        dependencies.replace(6,10,dependent_source); dependencies.run(6);
        require(descendant(dependencies.view,id(8,1)),"The dependent activity could not unlock its own reference");
        dependencies.reopen();
        require(dependencies.page() == 5 && dependencies.text(6,4).starts_with("✓ Correcto") && dependencies.text(6,14).empty(),
                "Completed dependencies lost the bookmark/achievement or revived an old console");
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
        require(local_dependencies.control(6,10)==dependent_editor,"Editing revoked a completed prerequisite before reset");
        local_dependencies.click(3,12);
        require(local_dependencies.control(3,10) == original_editor && !IsWindow(dependent_editor)
                && !descendant(local_dependencies.view,id(6,10)) && descendant(local_dependencies.view,id(6,18)),
                "Revocation retained the dependent controls or rebuilt an unrelated editor");
        SendMessageW(original_editor,WM_UNDO,0,0); pump(local_dependencies.host);
        require(!descendant(local_dependencies.view,id(6,10)),"Undo reused a previous prerequisite achievement");
        local_dependencies.run(3);
        require(local_dependencies.text(6,10) == same_page_draft && local_dependencies.text(6,4).starts_with("✓ Correcto"),
                "Revealing a same-page editor lost its independent draft or achievement");
        local_dependencies.replace(6,10,same_page_draft + "// edición que debe capturarse\n");
        const auto last_dependent_source = local_dependencies.text(6,10);
        local_dependencies.click(6,11);
        local_dependencies.click(3,12);
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
