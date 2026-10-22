#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

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
CHARFORMAT2W character_format(HWND edit,LONG character = 0) {
    CHARRANGE previous{}; SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
    CHARRANGE range{character,character+1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));return format;
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
        if (std::wstring_view(type) == current.name) { current.result = item; return FALSE; }
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
        require(bar != nullptr,"The contextual editor shortcuts are missing");
        const auto source = control(block,10); const auto before = text(block,10);
        SetFocus(source); SendMessageW(bar,WM_TIMER,1,0); pump(host);
        require(GetWindowLongPtrW(bar,GWL_STYLE) & WS_VISIBLE,"Focusing a code editor did not show its shortcuts");
        RECT viewport{}, strip{}, frame{};
        GetWindowRect(GetParent(source),&viewport); GetWindowRect(bar,&strip); GetWindowRect(view,&frame);
        require(viewport.bottom <= strip.top && strip.bottom < frame.bottom,
                "The editing shortcuts cover code or page navigation");
        const auto tab = GetDlgItem(bar,9000); require(tab != nullptr,"Contextual TAB command missing");
        SetFocus(tab); SendMessageW(tab,BM_CLICK,0,0); pump(host);
        require(GetFocus() == source && text(block,10) != before,"Contextual TAB lost the editor or did not use its editing operation");
        SendMessageW(source,WM_UNDO,0,0); pump(host);
        require(text(block,10) == before,"Contextual TAB did not preserve the existing Undo history");
        SetFocus(control(107)); SendMessageW(bar,WM_TIMER,1,0); pump(host);
        require(!(GetWindowLongPtrW(bar,GWL_STYLE) & WS_VISIBLE),"Editor shortcuts remained visible outside code editing");
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
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(id(block,75),0),0); pump(host);
        require(control(block,10) == source && text(block,10) == before,
                "Editor fullscreen recreated the document or changed the code");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(id(block,76),0),0); pump(host);
        require(!(GetWindowLongPtrW(source,GWL_STYLE) & WS_VISIBLE) && lesson_reading_regions(view).empty(),
                "Console fullscreen still exposed hidden lesson/source text to the reader");
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
                            MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_STRING | MIIM_FTYPE;
                            require(GetMenuItemInfoW(menu,static_cast<UINT>(index),TRUE,&item),"Cannot inspect the real popup");
                            if (item.fType & MFT_SEPARATOR) continue;
                            self->native_styles = self->native_styles && (item.fType & MFT_OWNERDRAW) != 0;
                            std::wstring caption(static_cast<std::size_t>(item.cch)+1,L'\0');
                            item.dwTypeData = caption.data(); item.cch = static_cast<UINT>(caption.size());
                            require(GetMenuItemInfoW(menu,static_cast<UINT>(index),TRUE,&item),"Cannot read the real popup caption");
                            caption.resize(item.cch); self->captions.push_back(utf8(caption));
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
                accessible_name(fixture.control(113)) == pick("Progreso de la lección","Lesson progress","Progression de la leçon"),
                "Lesson header labels ignored the independent interface language");
        require(window_text(fixture.control(2,11)) == pick("Compilar, ejecutar y comprobar","Compile, run and check","Compiler, exécuter et vérifier") &&
                window_text(fixture.control(3,11)) == pick("Compilar y ejecutar","Compile and run","Compiler et exécuter") &&
                fixture.text(2,17) == pick("Entrada","Input","Entrée") && fixture.text(2,30) == pick("Salida del programa","Program output","Sortie du programme"),
                "Compiler/check/input/output labels did not follow the interface language");
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
            PopupCapture capture(fixture.view); fixture.click(2,19); capture.verified();
            require(capture.captions.front() == pick("Deshacer","Undo","Annuler"),"The real editor popup ignored the interface language");
        }
        {
            PopupCapture capture(fixture.view); fixture.click(2,31); capture.verified();
            require(capture.captions.front() == pick("Copiar salida","Copy output","Copier la sortie"),"The real console popup ignored the interface language");
        }
        fixture.replace(1,2,"void"); fixture.enter(1);
        require(fixture.text(1,4) == pick("Incorrecto","Incorrect","Incorrect") + "\nGuía curricular intacta.",
                "The wrong verdict was not localized or rewrote authored feedback");
        fixture.replace(1,2,"int"); fixture.enter(1);
        require(fixture.text(1,4) == pick("Correcto","Correct","Correct") + "\nExplicación curricular intacta.",
                "The correct verdict was not localized or rewrote authored feedback");
        const auto source = fixture.control(2,10); const auto original = fixture.text(2,10);
        fixture.replace(2,10,original+"// historial QA\n");
        CHARRANGE selection{4,8}; SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        update_lesson_preferences(fixture.view,[](state::StateData& data) { data.preferences["native_windows"]["ui_language"]=std::string("fr"); });
        CHARRANGE restored{}; SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored));
        require(fixture.control(2,10)==source && restored.cpMin==selection.cpMin && restored.cpMax==selection.cpMax &&
                fixture.text(1,4)=="Correct\nExplicación curricular intacta." && fixture.text(2,17)=="Entrée",
                "Refreshing interface labels recreated editors, moved the selection or changed authored feedback");
        SendMessageW(source,WM_UNDO,0,0); pump(fixture.host);
        require(fixture.text(2,10)==original,"Changing interface language discarded the editor's existing Undo history");
    }
    std::cout << "PASS ES/EN/FR real lesson/editor/console popups, generated verdicts, original curriculum, selection and Undo\n";
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

void simulated_exercise_code_cards() {
    Fixture fixture({},[](const curriculum::Lesson&){
        return windows::parse_lesson_json(R"json({"id":"qa_simulated_exercise_headers","title":"Cabeceras de ejercicios","pages":[{"blocks":[
            {"type":"output_prediction","question":"Predice el mensaje.","code":"std::cout << \"Listo\";","answers":["Listo"]},
            {"type":"fill_code","question":"Completa el operador.","code":"std::cout [] \"Listo\";","answers":["<<"]},
            {"type":"order_code","question":"Ordena las líneas.","fragments":["return 0;","int main() {","}"],"correctOrder":[1,0,2]},
            {"type":"inline_fill_code","question":"Completa el código.","template":"std::cout [] \"Listo\";","answers":["<<"]},
            {"type":"drag_fill_code","question":"Coloca la función.","template":"int []() { return 0[] }","choices":["main",";","Main"],"correctOrder":[0,1]}
            ]}]})json");
    });
    const auto original_json=fixture.lesson.raw_json;
    for(std::size_t index=0;index<5;++index){
        require(fixture.text(index,16)=="</> C++"&&accessible_name(fixture.control(index,12))=="Reiniciar respuesta"&&
            accessible_name(fixture.control(index,18))=="Copiar ejercicio"&&accessible_name(fixture.control(index,13))=="Pantalla completa",
            "A simulated programming response lost its actual language/reset/copy/fullscreen header");
        require(!IsWindowEnabled(fixture.control(index,12)),"A fresh simulated response gained a resettable hidden answer");
    }
    const auto output_code=fixture.control(0,1),output_answer=fixture.control(0,2),fill_answer=fixture.control(1,2);
    const auto inline_host=fixture.control(3,5),drag_host=fixture.control(4,5);
    const auto inline_answer=GetDlgItem(inline_host,100),drag_slot=GetDlgItem(drag_host,100);
    require(inline_answer&&drag_slot&&edit_text(inline_answer).empty()&&edit_text(drag_slot)=="…",
        "Adding code headers revealed the internal inline/drag expected answers");
    const auto regions=lesson_reading_regions(fixture.view);
    require(std::ranges::none_of(regions,[&](const auto& region){
        return region.target==output_code||region.target==fixture.control(0,16)||region.target==fixture.control(1,16);
    }),"A decorative simulated header started narrating excluded console code or buttons");
    {
        PopupCapture popup(fixture.view);fixture.click(0,19);popup.verified();
        require(popup.captions==std::vector<std::string>{"Copiar ejercicio","Reiniciar respuesta","Pantalla completa"},
            "The simulated response menu does not expose its real copy/reset/fullscreen operations");
    }
    fixture.replace(0,2,"Respuesta personal");fixture.replace(1,2,"<<");
    require(IsWindowEnabled(fixture.control(0,12))&&IsWindowEnabled(fixture.control(1,12)),"Typing a simulated answer did not update its real header reset");
    fixture.click(0,13);
    require(fixture.control(0,1)==output_code&&fixture.control(0,2)==output_answer&&fixture.text(0,2)=="Respuesta personal"&&
        (GetWindowLongPtrW(output_answer,GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongPtrW(fixture.control(0,3),GWL_STYLE)&WS_VISIBLE)&&
        !(GetWindowLongPtrW(fixture.control(1,2),GWL_STYLE)&WS_VISIBLE),
        "Simulated response fullscreen recreated fields, hid its answer or retained unrelated/check controls");
    fixture.click(110);fixture.click(0,12);
    require(fixture.text(0,2).empty()&&fixture.control(1,2)==fill_answer&&fixture.text(1,2)=="<<"&&
        fixture.text(0,1)==fixture.lesson.pages[0].blocks[0].code,
        "Resetting a response modified immutable code, another answer or its HWND");
    fixture.enter(1);require(fixture.text(1,4).starts_with("Correcto")&&fixture.page()==0,"The decorated fill no longer checks with Enter without navigation");
    fixture.click(1,12);require(fixture.text(1,2).empty()&&fixture.text(1,4).empty(),"The fill header did not reuse the existing correct-answer reset");
    fixture.click(2,21);require(fixture.text(2,22)=="int main() {"&&IsWindowEnabled(fixture.control(2,12)),"The order header collided with the actual first movement control");
    fixture.click(2,12);require(fixture.text(2,22)=="return 0;"&&!IsWindowEnabled(fixture.control(2,12)),"Resetting order did not restore its original unsolved presentation");
    SendMessageW(inline_answer,EM_SETSEL,0,-1);SendMessageW(inline_answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"<<"));pump(fixture.host);
    fixture.click(3,13);require(fixture.control(3,5)==inline_host&&GetDlgItem(inline_host,100)==inline_answer&&edit_text(inline_answer)=="<<",
        "Inline fullscreen recreated the existing hole/editor or discarded its draft");
    fixture.click(110);fixture.click(3,12);require(edit_text(inline_answer).empty(),"The inline header reset did not clear only its own answer");
    SendMessageW(GetDlgItem(drag_host,2000),BM_CLICK,0,0);SendMessageW(drag_slot,WM_LBUTTONUP,0,0);pump(fixture.host);
    require(edit_text(drag_slot)=="main"&&IsWindowEnabled(fixture.control(4,12)),"The decorated drag card no longer places a visible choice");
    fixture.click(4,12);require(edit_text(drag_slot)=="…"&&fixture.lesson.raw_json==original_json&&fixture.control(4,5)==drag_host,
        "Resetting the drag header changed source curriculum or recreated its controls");
    fixture.non_overlapping_rows();fixture.reopen();
    require(fixture.text(0,1)==fixture.lesson.pages[0].blocks[0].code&&fixture.lesson.raw_json==original_json,
        "Reopening the new simulated headers changed immutable source or authored content");
    std::cout<<"PASS All five simulated C++ response headers, real menus, same-HWND fullscreen, isolated reset, Enter, order/holes/drag and source preservation\n";
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
        require(header==colors.header&&body==colors.card,"The simulated code card did not paint an actual header and body around its unchanged controls");
    }
    {
        PopupCapture popup(fixture.view);fixture.click(0,19);popup.verified();
        require(popup.captions==std::vector<std::string>{"Copiar código","Pantalla completa"},
                "The immutable code menu exposed editing actions or lost its original copy/fullscreen actions");
    }
    fixture.click(0,20);
    require(fixture.control(0,1)==code&&fixture.text(0,1)==original&&!(GetWindowLongPtrW(first,GWL_STYLE)&WS_VISIBLE)&&
            lesson_reading_regions(fixture.view).empty()&&(GetWindowLongPtrW(code,GWL_STYLE)&WS_VSCROLL),
            "Readonly fullscreen recreated code, exposed hidden exercise text or has no real internal scrolling");
    fixture.click(110);
    require(fixture.control(0,1)==code&&fixture.control(1,20)==first&&(GetWindowLongPtrW(first,GWL_STYLE)&WS_VISIBLE),
            "Leaving simulated-code fullscreen replaced controls or lost the exercise");
    const auto source=fixture.text(2,10);fixture.replace(2,10,source+"// borrador que debe conservarse\n");const auto draft=fixture.text(2,10);
    fixture.click(1,20);require(IsWindowEnabled(fixture.control(1,12)),"Selecting an option did not enable its header reset");
    fixture.click(1,3);require(fixture.text(1,4).starts_with("Incorrecto"),"The real incorrect-choice path did not execute");
    fixture.click(1,12);
    require(fixture.text(1,4).empty()&&!SendMessageW(first,BM_GETCHECK,0,0)&&!IsWindowEnabled(fixture.control(1,12))&&
            fixture.control(2,10)==editor&&fixture.text(2,10)==draft,"Header reset changed another editor or retained a stale wrong attempt");
    fixture.click(1,22);fixture.click(1,3);require(fixture.text(1,4).starts_with("Correcto")&&!IsWindowEnabled(second),"Correct choice did not retain the existing lock/verdict policy");
    fixture.click(1,12);require(IsWindowEnabled(first)&&IsWindowEnabled(second)&&fixture.text(1,4).empty(),"Header reset did not reuse the ordinary correct-answer reset policy");
    SendMessageW(editor,WM_UNDO,0,0);pump(fixture.host);require(fixture.text(2,10)==source,"Resetting a simulated puzzle destroyed an independent editor's Undo history");
    fixture.non_overlapping_rows();fixture.scroll_repaint();fixture.reopen();
    require(fixture.text(0,1)==original&&!IsWindowEnabled(fixture.control(1,12))&&fixture.lesson.raw_json==raw,
            "Reopening simulated cards changed curriculum, restored a stale attempt or fabricated a resettable immutable program");
    std::cout<<"PASS Simulated code/choice cards, native popup, exact text/reading targets, fullscreen, wrong/correct reset and independent editor history\n";
}
}

int main() {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr); host.lpszClassName = L"LgaedLessonViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        native_page_progress_theme();
        simulated_exercise_code_cards();
        simulated_code_and_choice_cards();
        lesson_interface_languages();
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
        require(character_format(descendant(view.view,990)).yHeight==640,
            "The lesson title did not retain its native 32-point hierarchy");
        for(std::size_t index=0;index<view.lesson.pages.front().blocks.size();++index){
            const auto& item=view.lesson.pages.front().blocks[index];
            if(item.type==BlockType::paragraph)
                require(character_format(view.control(index,0)).yHeight==360,"Lesson prose remains below the readable 18-point base");
            else if(item.type==BlockType::heading||!item.question.empty())
                require(character_format(view.control(index,0)).yHeight==460,"A question/heading lost its distinct 23-point size");
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
        require(view.page() == 1 && view.text(3,4).starts_with("Incorrecto") && !lesson_page_state(view.view).can_next, "Wrong exact prediction did not stay on its page");
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
        view.contextual_editing(3);
        view.editor_gestures(3);
        view.run(3);
        require(view.text(3,14) == "Zona inicial" && view.text(3,4) == "Correcto", "Original program did not produce/check its fresh exact output");
        view.compiler_menus(3);
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

        require(view.page() == 5 && !lesson_page_state(view.view).can_next, "Final page navigation escaped the lesson");
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
            require(character_format(source_control).yHeight==360,
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
            const auto glyph_hit=WindowFromPoint(glyph_screen);
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
            require(shortcuts&&window_text(descendant(shortcuts,9003))=="std::cout"&&window_text(descendant(shortcuts,9004))=="<<",
                "Saving shortcut preferences did not update the existing contextual bar");
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
        require(dependencies.text(3,4) == "Correcto" && !lesson_page_state(dependencies.view).can_next,
                "Successful execution of the original program satisfied another editor's prerequisite");
        dependencies.run(6);
        require(dependencies.text(6,4).starts_with("Incorrecto") && !lesson_page_state(dependencies.view).can_next,
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
        require(local_dependencies.control(3,10) == original_editor && local_dependencies.text(3,4) == "Correcto"
                && local_dependencies.text(6,10) == same_page_draft && local_dependencies.text(6,14).empty(),
                "Additive unlocking recreated the prerequisite editor, verdict/history or dependent draft");
        local_dependencies.run(6);
        require(local_dependencies.text(6,4).starts_with("Correcto"),"The same-page dependent editor cannot execute its preserved draft");
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
        std::cout << "PASS Shared 35-node prerequisite DAG: bounded traversal and transitive invalidation without recursive rechecking\n"
                  << "Native lesson UI integration: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Native lesson UI integration: " << error.what() << '\n'; return 1;
    }
}
