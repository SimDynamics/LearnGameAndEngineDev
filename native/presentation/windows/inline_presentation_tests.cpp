#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
RECT rect(HWND window){RECT r{};GetWindowRect(window,&r);return r;}
LONG baseline(HWND window){Microsoft::WRL::ComPtr<IRichEditOle> ole;
    SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> range;
    check(ole&&SUCCEEDED(ole.As(&document))&&document->Range(0,0,&range)==S_OK,"Cannot measure real RichEdit baseline");
    LONG x=0,y=0;check(range->GetPoint(tomStart|tomAllowOffClient|TA_BASELINE|TA_LEFT,&x,&y)==S_OK,"Native baseline query failed");return y;}
COLORREF ink(HWND window,LONG at){CHARRANGE before{},one{at,at+1};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&one));CHARFORMAT2W f{};f.cbSize=sizeof(f);
    SendMessageW(window,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&f));SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return f.crTextColor;}
void replace(HWND window,std::string_view text){const auto value=wide(text);SendMessageW(window,EM_SETSEL,0,-1);
    SendMessageW(window,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));}
void pump(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
void inline_fields(HWND host){
    lgaed::curriculum::Block model;model.type=lgaed::curriculum::BlockType::inline_fill_code;
    model.code_template="#include <iostream>\nint main() {\n    std::cout [] [];\n    return 0;\n}";
    model.answers={"<<","\"Carga lista\""};int submissions=0;
    CppFillView view(host,model,"",[]{},[&]{++submissions;});
    const auto a=GetDlgItem(view.window(),100),b=GetDlgItem(view.window(),101),prefix=GetDlgItem(view.window(),1004);
    check(a&&b&&prefix,"Missing authored hole or source prefix");
    for(const auto scale:{75,100,125,150,100}){
        view.set_text_scale(scale);const int height=view.height_for_width(820);MoveWindow(view.window(),0,0,820,height,TRUE);pump();
        const auto ar=rect(a),br=rect(b),code=rect(prefix);
        const int dpi=static_cast<int>(GetDpiForWindow(host));
        check(height<=MulDiv(190*scale,dpi,9600),"Five source lines are still padded like forty-pixel form rows");
        check(std::abs(baseline(a)-baseline(prefix))<=1&&std::abs(baseline(b)-baseline(prefix))<=1,"Inline text and source baselines do not agree");
        check(ar.left==code.right&&br.top==ar.top,"Compact source advance or slot row alignment changed");
        check(view.answers()==std::vector<std::string>({"",""}),"Hint leaked into answer state");
        PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);SendMessageW(a,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
        check(paragraph.wAlignment==PFA_CENTER,"Empty and typed inline fields must use native centered paragraphs");
        POINTL caret{};SendMessageW(a,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&caret),0);
        check(std::abs(caret.x-(ar.right-ar.left)/2)<=3,"The empty native insertion point is not centered");
        for(int n=0;n<64;++n){check(GetWindowTextLengthW(a)==0,"A placeholder became editable text");SendMessageW(a,EM_SETSEL,0,-1);}
        CHARRANGE selection{};SendMessageW(a,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        check(selection.cpMin==0&&selection.cpMax==0,"An empty hint selected the synthetic paragraph mark");
    }
    SetFocus(a);SendMessageW(a,WM_CHAR,L'f',1);SendMessageW(a,WM_KEYDOWN,VK_RETURN,1);SendMessageW(a,WM_CHAR,L'\r',1);
    check(submissions==1&&view.answers()[0]=="f","A physical Enter inserted a line or submitted twice");
    replace(a,"<<");replace(b,"\"Carga lista\"");const auto saved=view.response_json();
    check(view.answers()==model.answers,"Typing altered exact authored strings");
    if(!reader_theme::palette().high_contrast){check(ink(a,0)==RGB(231,155,186),"Insertion operator lost automatic syntax color");
        check(ink(b,0)==RGB(122,195,141),"String literal lost automatic green");}
    const auto field_base=reader_theme::background(a);view.set_enabled(false);view.set_completed(true);pump();
    check(!IsWindowEnabled(a)&&view.response_json()==saved,"Completion changed answer or enabled a solved field");
    if(!reader_theme::palette().high_contrast){check(reader_theme::background(a)==reader_theme::completion_background(field_base),"Completion tint was omitted");check(ink(b,0)==RGB(122,195,141),"Completion replaced source syntax colors");}
    view.set_enabled(true);view.reset();check(view.answers()==std::vector<std::string>({"",""}),"Reset did not clear only the real answer");
    check(GetDlgItem(view.window(),100)==a,"Presentation changes recreated the editor");
    std::cout<<"PASS inline geometry, hint, centered insertion, Enter, syntax, solved/reset state\n";
}
void output_spans(HWND host){
    const auto edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY,0,0,800,100,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(edit!=nullptr,"Cannot create rich-text fixture");reader_theme::apply(edit,reader_theme::Surface::card);
    for(const auto prefix:{"La salida es ","The output is ","La sortie est "}){
        const std::string original=std::string(prefix)+"`Carga lista`, con un espacio.";set_rich_text(edit,original);
        check(edit_text(edit)==std::string(prefix)+"Carga lista, con un espacio.","Output styling rewrote visible text");
        if(!reader_theme::palette().high_contrast){const auto start=static_cast<LONG>(wide(prefix).size());
            for(LONG i=0;i<11;++i)check(ink(edit,start+i)==reader_theme::foreground(edit),"A displayed output word inherited C++ identifier/string ink");
            check(ink(edit,start+11)==reader_theme::foreground(edit),"Output styling colored adjacent punctuation");}
    }
    set_rich_text(edit,"Usa `std::cout` para enviar datos.");
    if(!reader_theme::palette().high_contrast)check(ink(edit,4)==RGB(171,141,227),"Ordinary code spans lost their C++ colors");
    DestroyWindow(edit);std::cout<<"PASS output snippets versus code snippets\n";
}
void completion(HWND host){
    check(reader_theme::completion_background(RGB(32,33,38))==RGB(29,78,52),"Shared alpha changed the original catalogue green");
    check(reader_theme::completion_background(RGB(20,20,20))!=reader_theme::completion_background(RGB(48,48,48)),"Completion does not composite over its local surface");
    const auto button=CreateWindowExW(0,L"BUTTON",L"Correcto \u2713",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,150,220,48,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    reader_theme::apply(button,reader_theme::Surface::card);const auto initial=reader_theme::background(button);
    reader_theme::completed_control(button,true);EnableWindow(button,FALSE);
    if(!reader_theme::palette().high_contrast)check(reader_theme::background(button)==reader_theme::completion_background(initial),"Solved disabled action lost shared completion tint");
    reader_theme::completed_control(button,false);check(reader_theme::background(button)==initial,"Reset left a success tint on an unavailable button");DestroyWindow(button);
    OrderCodeView order(host,{"return 0;","std::cout << \"Listo\";"},{1,0},[]{});const auto before=order.order();
    order.set_enabled(false);order.set_completed(true);check(order.order()==before,"Solved styling reordered code");
    order.reset();std::cout<<"PASS shared completion alpha and ordering-card state\n";
}
}
int main(){OleInitialize(nullptr);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto module=LoadLibraryW(L"Msftedit.dll");const auto host=CreateWindowExW(0,L"STATIC",L"Inline presentation QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int result=0;try{check(module&&host,"Cannot initialize native QA");reader_theme::prepare_fonts();reader_theme::apply(host,reader_theme::Surface::canvas);
        inline_fields(host);output_spans(host);completion(host);
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';result=1;}
    if(host)DestroyWindow(host);if(module)FreeLibrary(module);OleUninitialize();return result;
}
