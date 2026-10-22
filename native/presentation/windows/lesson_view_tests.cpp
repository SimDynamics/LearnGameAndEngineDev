#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/code_editor.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
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
}

int main() {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW host{}; host.lpfnWndProc = host_proc; host.hInstance = GetModuleHandleW(nullptr); host.lpszClassName = L"LgaedLessonViewTests";
    if (!RegisterClassW(&host)) return 2;
    try {
        Fixture view;
        require(view.page() == 0 && !lesson_page_state(view.view).can_next, "A fresh profile bypassed initial page exercises");
        view.close();
        {
            state::StateStore untouched(view.environment.paths.root);
            require(untouched.empty(), "Closing an untouched first page made a fresh profile ineligible for import");
        }
        view.open();
        require(view.page() == 0 && !lesson_page_state(view.view).can_next, "An untouched reopen changed the initial bookmark or exercise gate");
        {
            state::StateStore untouched(view.environment.paths.root);
            require(untouched.empty(), "Reopening an untouched profile published personal state");
        }
        view.viewport_layout(); view.scroll_repaint();
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
            update_lesson_preferences(preferences.view,[](state::StateData& data) {
                data.preferences["lesson_narration_settings"]["enabled"] = true;
                data.preferences["lesson_narration_resume_bounded_v1"]["qa_slot"] = std::string("qa");
                data.preferences["native_windows"]["code_wrap"] = true;
            });
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
