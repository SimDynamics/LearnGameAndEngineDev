#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/internal_surface_test_support.hpp"
#include "presentation/windows/graphics_view.hpp"
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
    SetActiveWindow(GetAncestor(frame,GA_ROOT));SetFocus(button);
    check(GetFocus()==button,"Calculator Escape fixture did not focus the actual button");
    MSG message{};message.hwnd=button;message.message=WM_KEYDOWN;message.wParam=VK_ESCAPE;message.lParam=1;
    check(IsDialogMessageW(frame,&message)!=FALSE,"The Windows dialog manager did not process Escape");
    pump();
}
void calculated(HWND view){const auto start=GetTickCount64();while(inspect_calculator(view).evaluating&&GetTickCount64()-start<20000){pump();Sleep(5);}pump();check(!inspect_calculator(view).evaluating,"Actual canonical calculator did not return");}
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) { return DefWindowProcW(window,message,wparam,lparam); }
}
int main(int argc,char** argv) {
    const bool calculator_only=argc==2&&std::string_view(argv[1])=="--calculator-window";
    if(argc>1&&!calculator_only)return 2;
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
        RECT result_heading{},result_surface{};GetWindowRect(control(calculator,5244),&result_heading);GetWindowRect(result,&result_surface);
        check(result_heading.top==result_surface.top&&result_heading.bottom==result_surface.bottom&&result_heading.right<=result_surface.left,
            "Calculator result no longer shares the original Android result-label row");
        const auto original_draft=inspect_calculator(calculator).draft;
        SendMessageW(control(calculator,5240),BM_CLICK,0,0);check(inspect_calculator(calculator).advanced&&inspect_calculator(calculator).key_order.size()==48,"Advanced tools did not use the exact six-column Android layout");
        const auto before_order=inspect_calculator(calculator).key_order;SendMessageW(control(calculator,5241),BM_CLICK,0,0);
        MoveWindow(calculator,0,0,640,220,TRUE);SendMessageW(calculator,WM_VSCROLL,SB_TOP,0);const auto keypad=control(calculator,5222);RECT keypad_area{},scroll_down_area{},scroll_up_area{};GetClientRect(keypad,&keypad_area);MapWindowPoints(keypad,nullptr,reinterpret_cast<POINT*>(&keypad_area),2);GetWindowRect(control(calculator,5249),&scroll_up_area);GetWindowRect(control(calculator,5250),&scroll_down_area);
        check(scroll_down_area.right==keypad_area.right&&scroll_up_area.right==scroll_down_area.left,"Calculator edit scrolling controls are not aligned at the original right edge");
        SendMessageW(control(calculator,5250),BM_CLICK,0,0);SCROLLINFO edit_scroll{sizeof(edit_scroll),SIF_POS};GetScrollInfo(keypad,SB_VERT,&edit_scroll);
        check(edit_scroll.nPos==MulDiv(74,static_cast<int>(GetDpiForWindow(calculator)),96),"Calculator edit scrolling did not advance exactly two landscape key rows");
        SendMessageW(control(calculator,5249),BM_CLICK,0,0);MoveWindow(calculator,0,0,980,800,TRUE);
        SendMessageW(control(calculator,5300),BM_CLICK,0,0);SendMessageW(control(calculator,5301),BM_CLICK,0,0);const auto swapped=inspect_calculator(calculator).key_order;check(swapped[0]==before_order[1]&&swapped[1]==before_order[0],"Key personalization is decorative");SendMessageW(control(calculator,5241),BM_CLICK,0,0);
        DestroyWindow(calculator); calculator = create_calculator_view(host,environment,[&] { ++returned; },worker); MoveWindow(calculator,0,0,980,800,TRUE); pump();
        expression=control(calculator,5202);result=control(calculator,5203);angle=control(calculator,5204);const auto restored=inspect_calculator(calculator);check(restored.degrees&&restored.advanced&&restored.draft==original_draft&&restored.result=="9.5"&&restored.key_order==swapped,"Original Android draft/mode/result/key-order preferences were not restored");
        SetWindowTextW(expression,L"1/0");SendMessageW(control(calculator,5206),BM_CLICK,0,0);calculated(calculator);check(!inspect_calculator(calculator).error.empty()&&inspect_calculator(calculator).result.empty(),"Invalid expression became a valid result");
        SetWindowTextW(expression,L"2+3*4");SendMessageW(control(calculator,5206),BM_CLICK,0,0);SetWindowTextW(expression,L"7");pump();check(inspect_calculator(calculator).result.empty()&&!inspect_calculator(calculator).evaluating,"Stale result survived a new input");
        MoveWindow(calculator,0,0,640,220,TRUE); SendMessageW(calculator,WM_VSCROLL,SB_BOTTOM,0);
        SCROLLINFO scroll{sizeof(scroll),SIF_ALL}; GetScrollInfo(control(calculator,5222),SB_VERT,&scroll);
        check(scroll.nPos > 0,"Compact calculator hides scientific controls instead of scrolling");
        const auto before_dpi=inspect_calculator(calculator);const auto before_scroll=scroll.nPos;SendMessageW(calculator,WM_DPICHANGED_AFTERPARENT,0,0);GetScrollInfo(control(calculator,5222),SB_VERT,&scroll);
        const auto after_dpi=inspect_calculator(calculator);check(after_dpi.draft==before_dpi.draft&&after_dpi.result==before_dpi.result&&after_dpi.degrees==before_dpi.degrees&&after_dpi.advanced==before_dpi.advanced&&after_dpi.key_order==before_dpi.key_order&&scroll.nPos==before_scroll,"A calculator display notification reset its expression, modes, key order or viewport");
        SendMessageW(control(calculator,5201),BM_CLICK,0,0); check(returned == 1,"Calculator back button is decorative"); DestroyWindow(calculator);
        const auto retained_preferences=state::StateStore(environment.paths.root).load();check(retained_preferences.preferences.at("native_windows").at("calculator/history/v1")==original_preferences.preferences.at("native_windows").at("calculator/history/v1"),"Calculator migration erased a pre-existing Windows tool history");
        auto shared_preferences=retained_preferences;unsigned shared_writes=0;std::string pasted;
        CalculatorPreferences access;access.read=[&]{return shared_preferences;};access.update=[&](const auto& mutation){mutation(shared_preferences);++shared_writes;};
        const auto frame=open_calculator_window(host,environment,worker,[&](std::string_view value){pasted=value;},access);pump();internal_surface_test::require_internal_surface(host,frame);const auto frame_expression=control(frame,5202);const auto frame_view=GetParent(GetParent(frame_expression));check(frame_expression&&IsWindowVisible(control(frame,5248)),"Permitted calculator result return is missing");
        shared_preferences.preferences["learning_progress"]["concurrent_reader_coverage"]=std::string("intact");SetWindowTextW(frame_expression,L"5nCr2");SendMessageW(control(frame,5206),BM_CLICK,0,0);calculated(frame_view);check(inspect_calculator(frame_view).result=="10"&&shared_writes>0&&state::string_value(shared_preferences,"learning_progress","concurrent_reader_coverage")=="intact","Calculator overwrote its owning lesson's latest reader/progress state");SendMessageW(control(frame,5248),BM_CLICK,0,0);pump();check(pasted=="10"&&!IsWindow(frame)&&IsWindowEnabled(host),"Paste did not return the exact value and close/re-enable its original owner");
        const auto cancelled_frame=open_calculator_window(host,environment,worker,[&](std::string_view){throw std::runtime_error("Closing calculator returned an unintended value");},access);
        const auto nested_frame=open_calculator_window(cancelled_frame,environment,worker,[&](std::string_view){throw std::runtime_error("Escape returned an unintended calculator value");},access);
        const auto display_draft=inspect_calculator(GetParent(GetParent(control(nested_frame,5202)))).draft;RECT suggested{};GetWindowRect(nested_frame,&suggested);suggested.right=suggested.left+820;suggested.bottom=suggested.top+540;const auto native_dpi=GetDpiForWindow(nested_frame);SendMessageW(nested_frame,WM_DPICHANGED,MAKEWPARAM(native_dpi,native_dpi),reinterpret_cast<LPARAM>(&suggested));RECT actual{};GetWindowRect(nested_frame,&actual);
        internal_surface_test::require_internal_surface(host,nested_frame);
        check(inspect_calculator(GetParent(GetParent(control(nested_frame,5202)))).draft==display_draft&&!IsWindowVisible(cancelled_frame),"Internal calculator DPI layout lost its draft or exposed the preceding page");
        RECT root_bounds{};GetWindowRect(host,&root_bounds);MoveWindow(host,root_bounds.left,root_bounds.top,760,520,TRUE);layout_internal_windows(host);pump();
        internal_surface_test::require_internal_surface(host,nested_frame);
        MoveWindow(host,root_bounds.left,root_bounds.top,root_bounds.right-root_bounds.left,root_bounds.bottom-root_bounds.top,TRUE);layout_internal_windows(host);pump();
        SendMessageW(nested_frame,WM_COMMAND,MAKEWPARAM(IDOK,0),0);pump();check(IsWindow(nested_frame),"An unrelated dialog command closed the calculator");
        dialog_escape(nested_frame,5204);
        check(!IsWindow(nested_frame)&&IsWindow(cancelled_frame)&&IsWindowVisible(cancelled_frame)&&IsWindowEnabled(host),"Escape from RAD closed the preceding calculator or disabled its main root");
        internal_surface_test::require_internal_surface(host,cancelled_frame);
        dialog_escape(cancelled_frame,5241);
        check(!IsWindow(cancelled_frame)&&IsWindow(host)&&IsWindowEnabled(host),"Escape from Edit did not close only the focused calculator and restore its owner");
        if(calculator_only){DestroyWindow(host);UnregisterClassW(type.lpszClassName,type.hInstance);std::cout<<"PASS internal calculator pages: one visible app root, client bounds/resize/DPI, nested Back/Escape, unchanged drafts and exact result return\n";return 0;}
        auto graphics=create_graphics_view(host,environment,[&]{++returned;});MoveWindow(graphics,0,0,980,800,TRUE);pump();
        const auto scene=inspect_graphics_lab(graphics);const auto frame_compatibility=inspect_graphics_frame(graphics);
        check(scene.objects==14&&!scene.benchmark&&scene.presented_frames>0&&scene.colored_pixels>100&&scene.error.empty(),"The public graphics wrapper did not open the original fourteen-body physics laboratory");
        check(frame_compatibility.backend==scene.backend&&frame_compatibility.presented_frames>=scene.presented_frames,"The public graphics inspection bypassed its real renderer");
        check(control(graphics,5260)&&control(graphics,5261)&&control(graphics,5262)&&control(graphics,5263)&&!control(graphics,5210)&&!control(graphics,5211),"The graphics wrapper retained the obsolete triangle controls or omitted the real laboratory controls");
        SendMessageW(control(graphics,5201),BM_CLICK,0,0);check(returned==2,"The physical graphics laboratory back action is disconnected");
        DestroyWindow(graphics); DestroyWindow(host); UnregisterClassW(type.lpszClassName,type.hInstance);
        std::cout << "Canonical Android AST/Symja calculation, editable layout, persistent draft/mode/result, stale evaluation cancellation and physical graphics laboratory integration: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
