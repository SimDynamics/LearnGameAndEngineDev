#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/narration_worker.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "native_build_config.hpp"

#include <iostream>
#include <stdexcept>
#include <memory>

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
void dialog_escape(HWND frame,int focused_id) {
    const auto button=control(frame,focused_id);
    check(button&&IsChild(frame,button),"Calculator Escape fixture has no target button");
    SetActiveWindow(frame);SetFocus(button);
    check(GetFocus()==button,"Calculator Escape fixture did not focus the actual button");
    MSG message{};message.hwnd=button;message.message=WM_KEYDOWN;message.wParam=VK_ESCAPE;message.lParam=1;
    check(IsDialogMessageW(frame,&message)!=FALSE,"The Windows dialog manager did not process Escape");
    pump();
}
void frames(HWND view,unsigned count) { for (unsigned index = 0; index < count; ++index) { SendMessageW(view,WM_TIMER,1,0); pump(); } }
void calculated(HWND view){const auto start=GetTickCount64();while(inspect_calculator(view).evaluating&&GetTickCount64()-start<20000){pump();Sleep(5);}pump();check(!inspect_calculator(view).evaluating,"Actual canonical calculator did not return");}
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) { return DefWindowProcW(window,message,wparam,lparam); }
}
int main() {
    try {
        WNDCLASSW type{}; type.lpfnWndProc = host_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LgaedToolsTests";
        check(RegisterClassW(&type) != 0,"Register tools QA");
        const auto environment = resolve_windows_environment({ProfileId::Codex,L"native-tools-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()),false});
        check(!std::filesystem::exists(environment.paths.root),"Isolated tool profile unexpectedly exists"); initialize_profile_directories(environment.paths);
        state::StateStore seed_store(environment.paths.root);auto original_preferences=seed_store.load();original_preferences.preferences["appearance_settings"]["accent_red"]=std::int64_t{60};original_preferences.preferences["appearance_settings"]["accent_green"]=std::int64_t{200};original_preferences.preferences["appearance_settings"]["accent_blue"]=std::int64_t{220};original_preferences.preferences["native_windows"]["calculator/history/v1"]=std::vector<std::string>{"1:2R:2"};seed_store.save(original_preferences);
        const auto host = CreateWindowExW(0,type.lpszClassName,L"Native tools QA",WS_OVERLAPPEDWINDOW,0,0,1000,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host != nullptr,"Create native tools host"); ShowWindow(host,SW_SHOWNOACTIVATE);
        reader_theme::set_accent(host,RGB(228,50,72));
        unsigned returned = 0;
        auto worker=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
        auto calculator = create_calculator_view(host,environment,[&] { ++returned; },worker); MoveWindow(calculator,0,0,980,800,TRUE); pump();
        auto expression = control(calculator,5202); auto result = control(calculator,5203); auto angle = control(calculator,5204);
        check(expression && result && angle,"Real calculator controls missing");
        const auto palette=reader_theme::palette(calculator);check(palette.high_contrast||palette.accent==RGB(60,200,220),"Calculator inherited the lesson route accent instead of original global personalization");
        check(control(calculator,5205)==nullptr,"Android calculator unexpectedly exposes the old Windows-only history panel");
        check(inspect_calculator(calculator).key_order.size()==36,"Basic six-column layout differs from original Android calculator");
        SetWindowTextW(expression,L"\\sin{30}+\\sqrt{81}"); SendMessageW(angle,BM_CLICK,0,0);
        SendMessageW(expression,WM_KEYDOWN,VK_RETURN,1); SendMessageW(expression,WM_CHAR,L'\r',1); SendMessageW(expression,WM_KEYUP,VK_RETURN,1); pump();
        calculated(calculator);check(inspect_calculator(calculator).result=="9.5"&&text(result)==L"9.5","Native Enter did not use the canonical Android/Symja calculator");
        const auto original_draft=inspect_calculator(calculator).draft;
        SendMessageW(control(calculator,5240),BM_CLICK,0,0);check(inspect_calculator(calculator).advanced&&inspect_calculator(calculator).key_order.size()==48,"Advanced tools did not use the exact six-column Android layout");
        const auto before_order=inspect_calculator(calculator).key_order;SendMessageW(control(calculator,5241),BM_CLICK,0,0);SendMessageW(control(calculator,5300),BM_CLICK,0,0);SendMessageW(control(calculator,5301),BM_CLICK,0,0);const auto swapped=inspect_calculator(calculator).key_order;check(swapped[0]==before_order[1]&&swapped[1]==before_order[0],"Key personalization is decorative");SendMessageW(control(calculator,5241),BM_CLICK,0,0);
        DestroyWindow(calculator); calculator = create_calculator_view(host,environment,[&] { ++returned; },worker); MoveWindow(calculator,0,0,980,800,TRUE); pump();
        expression=control(calculator,5202);result=control(calculator,5203);angle=control(calculator,5204);const auto restored=inspect_calculator(calculator);check(restored.degrees&&restored.advanced&&restored.draft==original_draft&&restored.result=="9.5"&&restored.key_order==swapped,"Original Android draft/mode/result/key-order preferences were not restored");
        SetWindowTextW(expression,L"1/0");SendMessageW(control(calculator,5206),BM_CLICK,0,0);calculated(calculator);check(!inspect_calculator(calculator).error.empty()&&inspect_calculator(calculator).result.empty(),"Invalid expression became a valid result");
        SetWindowTextW(expression,L"2+3*4");SendMessageW(control(calculator,5206),BM_CLICK,0,0);SetWindowTextW(expression,L"7");pump();check(inspect_calculator(calculator).result.empty()&&!inspect_calculator(calculator).evaluating,"Stale result survived a new input");
        MoveWindow(calculator,0,0,640,220,TRUE); SendMessageW(calculator,WM_VSCROLL,SB_BOTTOM,0);
        SCROLLINFO scroll{sizeof(scroll),SIF_ALL}; GetScrollInfo(control(calculator,5222),SB_VERT,&scroll);
        check(scroll.nPos > 0,"Compact calculator hides scientific controls instead of scrolling");
        SendMessageW(control(calculator,5201),BM_CLICK,0,0); check(returned == 1,"Calculator back button is decorative"); DestroyWindow(calculator);
        const auto retained_preferences=state::StateStore(environment.paths.root).load();check(retained_preferences.preferences.at("native_windows").at("calculator/history/v1")==original_preferences.preferences.at("native_windows").at("calculator/history/v1"),"Calculator migration erased a pre-existing Windows tool history");
        auto shared_preferences=retained_preferences;unsigned shared_writes=0;std::string pasted;
        CalculatorPreferences access;access.read=[&]{return shared_preferences;};access.update=[&](const auto& mutation){mutation(shared_preferences);++shared_writes;};
        const auto frame=open_calculator_window(host,environment,worker,[&](std::string_view value){pasted=value;},access);pump();check(frame&&IsWindow(frame)&&!IsWindowEnabled(host),"Field calculator is not an independently owned modal interaction");const auto frame_expression=control(frame,5202);const auto frame_view=GetParent(GetParent(frame_expression));check(frame_expression&&IsWindowVisible(control(frame,5248)),"Permitted calculator result return is missing");
        shared_preferences.preferences["learning_progress"]["concurrent_reader_coverage"]=std::string("intact");SetWindowTextW(frame_expression,L"5nCr2");SendMessageW(control(frame,5206),BM_CLICK,0,0);calculated(frame_view);check(inspect_calculator(frame_view).result=="10"&&shared_writes>0&&state::string_value(shared_preferences,"learning_progress","concurrent_reader_coverage")=="intact","Calculator overwrote its owning lesson's latest reader/progress state");SendMessageW(control(frame,5248),BM_CLICK,0,0);pump();check(pasted=="10"&&!IsWindow(frame)&&IsWindowEnabled(host),"Paste did not return the exact value and close/re-enable its original owner");
        const auto cancelled_frame=open_calculator_window(host,environment,worker,[&](std::string_view){throw std::runtime_error("Closing calculator returned an unintended value");},access);
        const auto nested_frame=open_calculator_window(cancelled_frame,environment,worker,[&](std::string_view){throw std::runtime_error("Escape returned an unintended calculator value");},access);
        SendMessageW(nested_frame,WM_COMMAND,MAKEWPARAM(IDOK,0),0);pump();check(IsWindow(nested_frame),"An unrelated dialog command closed the calculator");
        dialog_escape(nested_frame,5204);
        check(!IsWindow(nested_frame)&&IsWindow(cancelled_frame)&&IsWindowEnabled(cancelled_frame)&&IsWindow(host)&&!IsWindowEnabled(host),"Escape from RAD closed the preceding calculator or its host");
        dialog_escape(cancelled_frame,5241);
        check(!IsWindow(cancelled_frame)&&IsWindow(host)&&IsWindowEnabled(host),"Escape from Edit did not close only the focused calculator and restore its owner");
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
        std::cout << "Canonical Android AST/Symja calculation, editable layout, persistent draft/mode/result, stale evaluation cancellation and Direct3D readback: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
