#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/narration_worker.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <iostream>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <array>
#include <cmath>

namespace {
using namespace lgaed;using namespace presentation;using namespace winrt::Windows::Data::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){return DefWindowProcW(window,message,wp,lp);}
void surface_geometry(HWND host,const std::function<std::string(std::string_view)>& dispatch){
    std::vector<std::wstring> actions;MathExpressionEditorAccess calculator;calculator.request=[&](std::string_view request){actions.emplace_back(JsonObject::Parse(wide(request)).GetNamedString(L"action",L""));return dispatch(request);};
    MathExpressionEditorAccess puzzle=calculator;puzzle.minimum_height=56;puzzle.horizontal_padding=12;puzzle.vertical_padding=10;
    MathExpressionEditorAccess output=calculator;output.minimum_height=32;
    const auto one=create_math_expression_editor(host,103,calculator),two=create_math_expression_editor(host,104,puzzle),three=create_math_expression_editor(host,105,output);
    for(const auto item:{one,two,three}){MoveWindow(item,0,0,700,80,TRUE);check(set_math_expression_latex(item,"x"),"Prepare original per-surface mathematical geometry");}
    set_math_expression_read_only(three,true);
    const auto first=inspect_math_expression_editor(one),second=inspect_math_expression_editor(two),third=inspect_math_expression_editor(three);const auto dpi=static_cast<int>(GetDpiForWindow(host));
    check(first.measured_height==MulDiv(44,dpi,96)&&second.measured_height==MulDiv(56,dpi,96)&&third.measured_height==MulDiv(32,dpi,96),"Puzzle, calculator and result editors lost their original Android minimum heights");
    check(second.measured_width-first.measured_width==MulDiv(8,dpi,96)&&first.latex==second.latex&&first.latex==third.latex,"Per-surface editor padding changed its expression or calculator spacing");
    check(set_math_expression_latex(one,"\\frac{1234}{56}"),"Prepare canonical hardware slot navigation");SetFocus(one);actions.clear();
    SendMessageW(one,WM_KEYDOWN,VK_UP,1);SendMessageW(one,WM_KEYDOWN,VK_UP,1);SendMessageW(one,WM_KEYDOWN,VK_DOWN,1);SendMessageW(one,WM_KEYDOWN,VK_TAB,1);
    check(actions==std::vector<std::wstring>{L"previousSlot",L"previousSlot",L"nextSlot",L"nextSlot"}&&GetFocus()==one&&inspect_math_expression_editor(one).latex=="\\frac{1234}{56}","Hardware Up/Down/Tab used leaf steps, changed the expression or abandoned the mathematical editor");
    DestroyWindow(one);DestroyWindow(two);DestroyWindow(three);
}
void blocked_clipboard_cut(HWND editor){
    check(set_math_expression_latex(editor,"\\frac{1234}{56}")&&perform_math_expression_key(editor,"selectAll"),"Prepare a selected canonical expression for a failed native cut");
    const auto original=inspect_math_expression_editor(editor);
    struct ClipboardOwner{
        HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::atomic_bool opened=false;std::jthread thread;
        ClipboardOwner(){if(!ready||!release){if(ready)CloseHandle(ready);if(release)CloseHandle(release);throw std::runtime_error("Create the isolated clipboard contention fixture");}thread=std::jthread([this]{opened=OpenClipboard(nullptr)!=FALSE;SetEvent(ready);WaitForSingleObject(release,10000);if(opened)CloseClipboard();});if(WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0||!opened){SetEvent(release);thread.join();CloseHandle(ready);CloseHandle(release);throw std::runtime_error("A separate native thread could not own the clipboard for the contention regression");}}
        ~ClipboardOwner(){SetEvent(release);if(thread.joinable())thread.join();CloseHandle(ready);CloseHandle(release);}
    } owner;
    struct KeyboardState{BYTE original[256]{};KeyboardState(){check(GetKeyboardState(original)!=FALSE,"Read the test thread's keyboard state");auto state=std::to_array(original);state[VK_CONTROL]=0x80;check(SetKeyboardState(state.data())!=FALSE,"Prepare only the test thread's Ctrl modifier");}~KeyboardState(){SetKeyboardState(original);}} modifier;
    SendMessageW(editor,WM_KEYDOWN,'X',1);const auto after=inspect_math_expression_editor(editor);
    check(after.draft_json==original.draft_json&&after.latex==original.latex&&after.cursor_json==original.cursor_json,"A failed clipboard copy consumed the canonical cut selection or changed its expression/cursor");
}
}
int main(){try{
    WNDCLASSW type{};type.lpfnWndProc=host_proc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"LgaedCanonicalMathEditorTests";check(RegisterClassW(&type)!=0,"Register editor QA");
    const auto host=CreateWindowExW(0,type.lpszClassName,L"Canonical Math QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,type.hInstance,nullptr);check(host!=nullptr,"Create editor QA");ShowWindow(host,SW_SHOWNOACTIVATE);
    auto worker=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
    const auto dispatch=[worker](std::string_view request){const auto value=JsonObject::Parse(wide(request));return value.GetNamedString(L"command",L"")==L"math_keyboard"?worker->math_keyboard(request):worker->math_editor(request);};
    surface_geometry(host,dispatch);
    unsigned changed=0,submitted=0;MathExpressionEditorAccess access;access.request=dispatch;access.changed=[&]{++changed;};access.submit=[&]{++submitted;};access.expected_template="\\frac{1}{2}";
    const auto editor=create_math_expression_editor(host,101,std::move(access));MoveWindow(editor,12,12,750,180,TRUE);SetFocus(editor);
    check(set_math_expression_latex(editor,"\\frac{1234}{56}+\\sqrt{x}"),"Import exact original AST");const auto initial=inspect_math_expression_editor(editor);
    check(initial.complete&&initial.expression_json.find("fraction")!=std::string::npos&&initial.measured_height>44&&initial.carets.size()>10,"Structured fraction/root was flattened or has no canonical caret map");
    check(std::ranges::any_of(initial.carets,[](const auto& caret){return caret.path_json.find("numerator")!=std::string::npos&&caret.leaf_text_offset==2;}),"Interior numeric glyph caret missing");
    const auto numerator=std::ranges::find_if(initial.carets,[](const auto& caret){return caret.path_json.find("numerator")!=std::string::npos&&caret.leaf_text_offset==2;});
    const auto point=MAKELPARAM(numerator->bounds.left,(numerator->bounds.top+numerator->bounds.bottom)/2);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(editor,WM_LBUTTONUP,0,point);const auto placed=inspect_math_expression_editor(editor);SendMessageW(editor,WM_CHAR,L'9',1);const auto edited=inspect_math_expression_editor(editor);if(edited.latex.find("12934")==std::string::npos)throw std::runtime_error("Interior glyph edit: "+edited.latex+"; actual cursor "+placed.cursor_json);
    const auto saved=edited.draft_json;check(perform_math_expression_key(editor,"Fraction"),"Insert canonical fraction");check(restore_math_expression_draft(editor,saved)&&inspect_math_expression_editor(editor).latex==edited.latex,"Canonical draft did not restore AST/cursor");
    const auto before_enter=inspect_math_expression_editor(editor).latex;SendMessageW(editor,WM_KEYDOWN,VK_RETURN,1);SendMessageW(editor,WM_CHAR,L'\r',1);check(submitted==1&&inspect_math_expression_editor(editor).latex==before_enter,"Enter inserted a newline or submitted twice");
    blocked_clipboard_cut(editor);
    check(set_math_expression_latex(editor,"123456"),"Prepare precise numeric selection");const auto digits=inspect_math_expression_editor(editor);const auto first=std::ranges::find_if(digits.carets,[](const auto& caret){return caret.path_json=="[]"&&caret.leaf_text_offset==2;});const auto last=std::ranges::find_if(digits.carets,[](const auto& caret){return caret.path_json=="[]"&&caret.leaf_text_offset==4;});check(first!=digits.carets.end()&&last!=digits.carets.end(),"Partial numeral carets missing");const auto start_point=MAKELPARAM(first->bounds.left,(first->bounds.top+first->bounds.bottom)/2),end_point=MAKELPARAM(last->bounds.left,(last->bounds.top+last->bounds.bottom)/2);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,start_point);SendMessageW(editor,WM_MOUSEMOVE,MK_LBUTTON,end_point);SendMessageW(editor,WM_LBUTTONUP,0,end_point);SendMessageW(editor,WM_CHAR,L'9',1);check(inspect_math_expression_editor(editor).latex=="12956","Partial numeral drag did not use canonical bounded replacement");
    check(set_math_expression_latex(editor,"\\frac{1}{2}"),"Reset fraction hit fixture");MoveWindow(editor,12,12,750,180,TRUE);const auto outside=MAKELPARAM(700,3);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,outside);SendMessageW(editor,WM_LBUTTONUP,0,outside);const auto cursor=JsonObject::Parse(wide(inspect_math_expression_editor(editor).cursor_json));check(cursor.GetNamedArray(L"path").Size()==0&&cursor.GetNamedNumber(L"offset")==1,"Whitespace outside tall parent entered an unrelated child");
    check(set_math_expression_latex(editor,"2+3"),"Prepare a complete fitting mathematical expression");const auto short_draft=inspect_math_expression_editor(editor);POINT short_caret{},after_wheel{};check(GetCaretPos(&short_caret),"The focused mathematical field has no actual native caret");
    SendMessageW(editor,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);check(GetCaretPos(&after_wheel)&&after_wheel.x==short_caret.x&&after_wheel.y==short_caret.y&&inspect_math_expression_editor(editor).draft_json==short_draft.draft_json,
        "A wheel moved a fully visible mathematical expression offscreen or changed its canonical draft/cursor");
    MoveWindow(editor,12,12,240,180,TRUE);check(set_math_expression_latex(editor,"123456789012345678901234567890123456789012345678901234567890"),"Prepare a mathematical expression with actual horizontal overflow");
    const auto long_draft=inspect_math_expression_editor(editor);check(long_draft.measured_width>240,"The horizontal scroll regression fixture unexpectedly fits its viewport");
    SendMessageW(editor,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA*30)),0);POINT left_caret{};check(GetCaretPos(&left_caret),"The long expression lost its native caret");
    SendMessageW(editor,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA*30)),0);RECT viewport{};GetClientRect(editor,&viewport);check(GetCaretPos(&after_wheel)&&after_wheel.x>=viewport.right-MulDiv(32,static_cast<int>(GetDpiForWindow(editor)),96)&&after_wheel.x<=viewport.right&&left_caret.x>after_wheel.x&&inspect_math_expression_editor(editor).draft_json==long_draft.draft_json,
        "Mathematical wheel scrolling exceeded the actual horizontal overflow or changed the expression/cursor");MoveWindow(editor,12,12,750,180,TRUE);
    check(set_math_expression_latex(editor,"\\frac{12}{}"),"Prepare exact-font fraction geometry");const auto fraction_geometry=inspect_math_expression_editor(editor);const auto number=std::ranges::find(fraction_geometry.text_positions,std::string("12"),&MathExpressionTextPosition::text);check(number!=fraction_geometry.text_positions.end()&&fraction_geometry.horizontal_rules.size()==1,"Actual fraction ink geometry missing");const auto gap=fraction_geometry.horizontal_rules.front()-number->baseline;check(gap>0&&gap<=18&&fraction_geometry.measured_height<=80,"Fraction font metrics expanded beyond original Android serif geometry");
    show_math_keyboard(editor,dispatch,R"({"inputMode":"structured","structured":true,"keyboard":"ALGEBRA","validator":{"type":"expression","expected":"\\frac{1}{2}"}})","es");pump();const auto bar=math_keyboard_window(host);check(bar&&IsWindowVisible(bar)&&GetFocus()==editor,"Shared mathematical keyboard stole focus or failed to open");
    const auto keyboard_draft=inspect_math_expression_editor(editor);RECT bar_bounds{},host_bounds{};GetWindowRect(bar,&bar_bounds);GetWindowRect(host,&host_bounds);const auto dpi=static_cast<int>(GetDpiForWindow(host));const auto compact_height=static_cast<int>((MulDiv(48,dpi,96)-2*MulDiv(6,dpi,96))*.80f);check(bar_bounds.bottom-bar_bounds.top==compact_height,"Landscape mathematical shortcuts do not use the original material-inset compact-height calculation");
    const auto previous_font=reinterpret_cast<HFONT>(SendMessageW(editor,WM_GETFONT,0,0));const auto large_font=CreateFontW(-MulDiv(27,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");const auto normal_font=CreateFontW(-MulDiv(18,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
    SendMessageW(editor,WM_SETFONT,reinterpret_cast<WPARAM>(large_font),TRUE);MoveWindow(host,host_bounds.left,host_bounds.top,600,900,TRUE);pump();LOGFONTW keyboard_font{};check(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(bar,WM_GETFONT,0,0)),sizeof(keyboard_font),&keyboard_font)>0&&std::abs(keyboard_font.lfHeight)==static_cast<int>(std::lround(MulDiv(27,dpi,96)*14.f/18)),"Portrait shortcut text does not track its source editor font through the original14/18 ratio");GetWindowRect(bar,&bar_bounds);check(bar_bounds.bottom-bar_bounds.top>=MulDiv(48,dpi,96),"Portrait mathematical previews lost their original minimum/measured height");
    check(inspect_math_expression_editor(editor).draft_json==keyboard_draft.draft_json&&inspect_math_expression_editor(editor).cursor_json==keyboard_draft.cursor_json&&GetFocus()==editor,"Responsive mathematical shortcuts changed the expression, complex cursor or editing focus");SendMessageW(editor,WM_SETFONT,reinterpret_cast<WPARAM>(normal_font),TRUE);SendMessageW(editor,WM_SETFONT,reinterpret_cast<WPARAM>(previous_font),TRUE);MoveWindow(host,host_bounds.left,host_bounds.top,host_bounds.right-host_bounds.left,host_bounds.bottom-host_bounds.top,TRUE);DeleteObject(large_font);DeleteObject(normal_font);pump();
    const auto second=create_math_expression_editor(host,102,{dispatch});MoveWindow(second,12,210,750,80,TRUE);SetFocus(second);show_math_keyboard(second,dispatch,R"({"inputMode":"structured","structured":true,"keyboard":"FRACTION"})","fr");check(math_keyboard_window(host)==bar&&GetFocus()==second,"Each field created a new keyboard or changed focus");DestroyWindow(second);pump();check(!IsWindowVisible(bar),"Destroyed target left an orphan mathematical keyboard");
    set_math_expression_read_only(editor,true);const auto before=inspect_math_expression_editor(editor).latex;SendMessageW(editor,WM_CHAR,L'7',1);check(inspect_math_expression_editor(editor).latex==before,"Read-only result accepted an edit");check(changed>=3,"Canonical edits were not observable");
    DestroyWindow(editor);DestroyWindow(host);UnregisterClassW(type.lpszClassName,type.hInstance);std::cout<<"Canonical AST layout, interior caret, exterior parent hit, draft, Enter, shared keyboard lifecycle and readonly: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}}
