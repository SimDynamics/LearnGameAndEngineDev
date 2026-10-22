#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace presentation;
void check(bool condition,std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }
HWND control(HWND parent,int id) {
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM pointer) -> BOOL {
        auto& current = *reinterpret_cast<Search*>(pointer);
        if (GetDlgCtrlID(item) == current.id) { current.result = item; return FALSE; } return TRUE;
    },reinterpret_cast<LPARAM>(&search)); return search.result;
}
std::wstring text(HWND control) { wchar_t value[4096]{}; GetWindowTextW(control,value,4096); return value; }
void pump() { MSG message{}; while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
void frames(HWND view,unsigned count) { for (unsigned index = 0; index < count; ++index) { SendMessageW(view,WM_TIMER,1,0); pump(); } }
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) { return DefWindowProcW(window,message,wparam,lparam); }
}
int main() {
    try {
        WNDCLASSW type{}; type.lpfnWndProc = host_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LgaedToolsTests";
        check(RegisterClassW(&type) != 0,"Register tools QA");
        const auto environment = resolve_windows_environment({ProfileId::Codex,L"native-tools-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()),false});
        check(!std::filesystem::exists(environment.paths.root),"Isolated tool profile unexpectedly exists"); initialize_profile_directories(environment.paths);
        const auto host = CreateWindowExW(0,type.lpszClassName,L"Native tools QA",WS_OVERLAPPEDWINDOW,0,0,1000,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host != nullptr,"Create native tools host"); ShowWindow(host,SW_SHOWNOACTIVATE);
        unsigned returned = 0;
        auto calculator = create_calculator_view(host,environment,[&] { ++returned; }); MoveWindow(calculator,0,0,980,800,TRUE); pump();
        auto expression = control(calculator,5202); auto result = control(calculator,5203); auto angle = control(calculator,5204);
        check(expression && result && angle,"Real calculator controls missing");
        SetWindowTextW(expression,L"sen(30)+sqrt(81)"); SendMessageW(angle,CB_SETCURSEL,1,0);
        SendMessageW(expression,WM_KEYDOWN,VK_RETURN,1); SendMessageW(expression,WM_CHAR,L'\r',1); SendMessageW(expression,WM_KEYUP,VK_RETURN,1); pump();
        check(text(result) == L"9.5" && text(expression) == L"sen(30)+sqrt(81)","Native Enter failed or rewrote the original expression");
        check(SendMessageW(control(calculator,5205),LB_GETCOUNT,0,0) == 1,"One Enter added multiple history entries");
        SetWindowTextW(expression,L"1/0"); SendMessageW(control(calculator,5206),BM_CLICK,0,0); pump();
        check(text(result).starts_with(L"Error") || text(result).starts_with(L"Erreur"),"Invalid expression was presented as a valid result");
        check(SendMessageW(control(calculator,5205),LB_GETCOUNT,0,0) == 1,"An error became a successful history result");
        DestroyWindow(calculator); calculator = create_calculator_view(host,environment,[&] { ++returned; }); MoveWindow(calculator,0,0,980,800,TRUE); pump();
        check(SendMessageW(control(calculator,5205),LB_GETCOUNT,0,0) == 1 && SendMessageW(control(calculator,5204),CB_GETCURSEL,0,0) == 1,"Profile-local history/angular unit were not restored");
        SendMessageW(control(calculator,5205),LB_SETCURSEL,0,0);
        SendMessageW(calculator,WM_COMMAND,MAKEWPARAM(5205,LBN_DBLCLK),reinterpret_cast<LPARAM>(control(calculator,5205)));
        check(text(control(calculator,5202)) == L"sen(30)+sqrt(81)","History did not retain the exact original expression");
        MoveWindow(calculator,0,0,640,390,TRUE); SendMessageW(calculator,WM_VSCROLL,SB_BOTTOM,0);
        SCROLLINFO scroll{sizeof(scroll),SIF_ALL}; GetScrollInfo(control(calculator,5222),SB_VERT,&scroll);
        check(scroll.nPos > 0,"Compact calculator hides scientific controls instead of scrolling");
        SendMessageW(control(calculator,5201),BM_CLICK,0,0); check(returned == 1,"Calculator back button is decorative"); DestroyWindow(calculator);
        auto graphics = create_graphics_view(host,environment,[&] { ++returned; }); MoveWindow(graphics,0,0,980,800,TRUE); pump(); frames(graphics,2);
        const auto solid = inspect_graphics_frame(graphics);
        check(solid.presented_frames >= 1 && solid.colored_pixels > 100 && solid.backend.starts_with("Direct3D 11"),"The graphics tool did not render a real Direct3D triangle");
        SendMessageW(GetDlgItem(graphics,5211),BM_SETCHECK,BST_UNCHECKED,0); frames(graphics,1);
        const auto paused = inspect_graphics_frame(graphics); frames(graphics,2);
        check(inspect_graphics_frame(graphics).rotation == paused.rotation,"Rotation pause kept changing the scene");
        SendMessageW(GetDlgItem(graphics,5210),CB_SETCURSEL,1,0);
        SendMessageW(graphics,WM_COMMAND,MAKEWPARAM(5210,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(graphics,5210))); frames(graphics,1);
        const auto wire = inspect_graphics_frame(graphics);
        check(wire.wireframe && wire.colored_pixels > 0 && wire.colored_pixels < solid.colored_pixels,"Wireframe selection did not change actual GPU pixels");
        SendMessageW(GetDlgItem(graphics,5212),BM_CLICK,0,0); frames(graphics,1); const auto reset = inspect_graphics_frame(graphics);
        check(!reset.wireframe && reset.rotation == 0 && reset.colored_pixels > wire.colored_pixels,"Reset did not restore the actual scene");
        SendMessageW(GetDlgItem(graphics,5211),BM_SETCHECK,BST_CHECKED,0); Sleep(30); frames(graphics,1);
        check(inspect_graphics_frame(graphics).rotation > 0,"Rotation control is decorative");
        std::cout << "Direct3D adapter actually used: " << solid.backend << '\n';
        SendMessageW(GetDlgItem(graphics,5201),BM_CLICK,0,0); check(returned == 2,"Graphics back button is decorative");
        DestroyWindow(graphics); DestroyWindow(host); UnregisterClassW(type.lpszClassName,type.hInstance);
        std::cout << "Real native scientific evaluation/history/keyboard and Direct3D readback/rotation/wireframe/reset: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
