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

namespace {
using namespace lgaed;using namespace presentation;using namespace winrt::Windows::Data::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){return DefWindowProcW(window,message,wp,lp);}
}
int main(){try{
    WNDCLASSW type{};type.lpfnWndProc=host_proc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"LgaedCanonicalMathEditorTests";check(RegisterClassW(&type)!=0,"Register editor QA");
    const auto host=CreateWindowExW(0,type.lpszClassName,L"Canonical Math QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,type.hInstance,nullptr);check(host!=nullptr,"Create editor QA");ShowWindow(host,SW_SHOWNOACTIVATE);
    auto worker=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
    const auto dispatch=[worker](std::string_view request){const auto value=JsonObject::Parse(wide(request));return value.GetNamedString(L"command",L"")==L"math_keyboard"?worker->math_keyboard(request):worker->math_editor(request);};
    unsigned changed=0,submitted=0;MathExpressionEditorAccess access;access.request=dispatch;access.changed=[&]{++changed;};access.submit=[&]{++submitted;};access.expected_template="\\frac{1}{2}";
    const auto editor=create_math_expression_editor(host,101,std::move(access));MoveWindow(editor,12,12,750,180,TRUE);SetFocus(editor);
    check(set_math_expression_latex(editor,"\\frac{1234}{56}+\\sqrt{x}"),"Import exact original AST");const auto initial=inspect_math_expression_editor(editor);
    check(initial.complete&&initial.expression_json.find("fraction")!=std::string::npos&&initial.measured_height>44&&initial.carets.size()>10,"Structured fraction/root was flattened or has no canonical caret map");
    check(std::ranges::any_of(initial.carets,[](const auto& caret){return caret.path_json.find("numerator")!=std::string::npos&&caret.leaf_text_offset==2;}),"Interior numeric glyph caret missing");
    const auto numerator=std::ranges::find_if(initial.carets,[](const auto& caret){return caret.path_json.find("numerator")!=std::string::npos&&caret.leaf_text_offset==2;});
    const auto point=MAKELPARAM(numerator->bounds.left,(numerator->bounds.top+numerator->bounds.bottom)/2);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(editor,WM_LBUTTONUP,0,point);const auto placed=inspect_math_expression_editor(editor);SendMessageW(editor,WM_CHAR,L'9',1);const auto edited=inspect_math_expression_editor(editor);if(edited.latex.find("12934")==std::string::npos)throw std::runtime_error("Interior glyph edit: "+edited.latex+"; actual cursor "+placed.cursor_json);
    const auto saved=edited.draft_json;check(perform_math_expression_key(editor,"Fraction"),"Insert canonical fraction");check(restore_math_expression_draft(editor,saved)&&inspect_math_expression_editor(editor).latex==edited.latex,"Canonical draft did not restore AST/cursor");
    const auto before_enter=inspect_math_expression_editor(editor).latex;SendMessageW(editor,WM_KEYDOWN,VK_RETURN,1);SendMessageW(editor,WM_CHAR,L'\r',1);check(submitted==1&&inspect_math_expression_editor(editor).latex==before_enter,"Enter inserted a newline or submitted twice");
    check(set_math_expression_latex(editor,"123456"),"Prepare precise numeric selection");const auto digits=inspect_math_expression_editor(editor);const auto first=std::ranges::find_if(digits.carets,[](const auto& caret){return caret.path_json=="[]"&&caret.leaf_text_offset==2;});const auto last=std::ranges::find_if(digits.carets,[](const auto& caret){return caret.path_json=="[]"&&caret.leaf_text_offset==4;});check(first!=digits.carets.end()&&last!=digits.carets.end(),"Partial numeral carets missing");const auto start_point=MAKELPARAM(first->bounds.left,(first->bounds.top+first->bounds.bottom)/2),end_point=MAKELPARAM(last->bounds.left,(last->bounds.top+last->bounds.bottom)/2);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,start_point);SendMessageW(editor,WM_MOUSEMOVE,MK_LBUTTON,end_point);SendMessageW(editor,WM_LBUTTONUP,0,end_point);SendMessageW(editor,WM_CHAR,L'9',1);check(inspect_math_expression_editor(editor).latex=="12956","Partial numeral drag did not use canonical bounded replacement");
    check(set_math_expression_latex(editor,"\\frac{1}{2}"),"Reset fraction hit fixture");MoveWindow(editor,12,12,750,180,TRUE);const auto outside=MAKELPARAM(700,3);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,outside);SendMessageW(editor,WM_LBUTTONUP,0,outside);const auto cursor=JsonObject::Parse(wide(inspect_math_expression_editor(editor).cursor_json));check(cursor.GetNamedArray(L"path").Size()==0&&cursor.GetNamedNumber(L"offset")==1,"Whitespace outside tall parent entered an unrelated child");
    check(set_math_expression_latex(editor,"\\frac{12}{}"),"Prepare exact-font fraction geometry");const auto fraction_geometry=inspect_math_expression_editor(editor);const auto number=std::ranges::find(fraction_geometry.text_positions,std::string("12"),&MathExpressionTextPosition::text);check(number!=fraction_geometry.text_positions.end()&&fraction_geometry.horizontal_rules.size()==1,"Actual fraction ink geometry missing");const auto gap=fraction_geometry.horizontal_rules.front()-number->baseline;check(gap>0&&gap<=18&&fraction_geometry.measured_height<=80,"Fraction font metrics expanded beyond original Android serif geometry");
    show_math_keyboard(editor,dispatch,R"({"inputMode":"structured","structured":true,"keyboard":"ALGEBRA","validator":{"type":"expression","expected":"\\frac{1}{2}"}})","es");pump();const auto bar=math_keyboard_window(host);check(bar&&IsWindowVisible(bar)&&GetFocus()==editor,"Shared mathematical keyboard stole focus or failed to open");
    const auto second=create_math_expression_editor(host,102,{dispatch});MoveWindow(second,12,210,750,80,TRUE);SetFocus(second);show_math_keyboard(second,dispatch,R"({"inputMode":"structured","structured":true,"keyboard":"FRACTION"})","fr");check(math_keyboard_window(host)==bar&&GetFocus()==second,"Each field created a new keyboard or changed focus");DestroyWindow(second);pump();check(!IsWindowVisible(bar),"Destroyed target left an orphan mathematical keyboard");
    set_math_expression_read_only(editor,true);const auto before=inspect_math_expression_editor(editor).latex;SendMessageW(editor,WM_CHAR,L'7',1);check(inspect_math_expression_editor(editor).latex==before,"Read-only result accepted an edit");check(changed>=3,"Canonical edits were not observable");
    DestroyWindow(editor);DestroyWindow(host);UnregisterClassW(type.lpszClassName,type.hInstance);std::cout<<"Canonical AST layout, interior caret, exterior parent hit, draft, Enter, shared keyboard lifecycle and readonly: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}}
