#include "presentation/windows/reader_shortcut_focus.hpp"
#include <commctrl.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using lgaed::presentation::reader_shortcuts::owns_arrow_keys;
unsigned assertions=0;
void require(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
struct Window { HWND value=nullptr;~Window(){if(value)DestroyWindow(value);} };
LRESULT CALLBACK wants_arrows(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_GETDLGCODE)return DLGC_WANTARROWS;
    return DefWindowProcW(window,message,wp,lp);
}
std::wstring text(HWND window){const auto size=GetWindowTextLengthW(window);std::wstring result(static_cast<std::size_t>(size)+1,L'\0');
    result.resize(GetWindowTextW(window,result.data(),static_cast<int>(result.size())));return result;}
CHARRANGE selection(HWND window){CHARRANGE range{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&range));return range;}
}
int main(){
    const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    try{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=wants_arrows;cls.lpszClassName=L"ReaderFocusQaArrowOwner";
        require(RegisterClassW(&cls)!=0,"Cannot register the explicit arrow-owning fixture");
        Window root{CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Reader shortcut focus QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
        require(root.value!=nullptr,"Cannot create native shortcut QA root");
        const auto make=[&](const wchar_t* kind,DWORD style,HWND parent,const wchar_t* value=L"A visible text"){
            const auto window=CreateWindowExW(0,kind,value,WS_CHILD|WS_VISIBLE|style,0,0,360,90,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(window!=nullptr,"Cannot create a real native shortcut control");return window;
        };
        const auto view=make(L"STATIC",0,root.value),pane=make(L"STATIC",0,view),bar=make(L"STATIC",0,view);
        const auto prose=make(MSFTEDIT_CLASS,ES_MULTILINE|ES_READONLY,pane);
        const auto heading=make(MSFTEDIT_CLASS,ES_MULTILINE|ES_READONLY,pane);
        const auto answer=make(MSFTEDIT_CLASS,ES_MULTILINE|ES_WANTRETURN,pane);
        const auto readonly_code=make(MSFTEDIT_CLASS,ES_MULTILINE|ES_READONLY,pane);
        const auto edit=make(L"EDIT",ES_MULTILINE,pane),readonly_edit=make(L"EDIT",ES_MULTILINE|ES_READONLY,pane);
        const auto push=make(L"BUTTON",BS_PUSHBUTTON,view),icon=make(L"BUTTON",BS_OWNERDRAW,view),play=make(L"BUTTON",BS_PUSHBUTTON,bar);
        const auto radio=make(L"BUTTON",BS_AUTORADIOBUTTON,pane),checkbox=make(L"BUTTON",BS_AUTOCHECKBOX,pane);
        const auto list=make(L"LISTBOX",0,pane),combo=make(L"COMBOBOX",CBS_DROPDOWN,pane);
        const auto slider=make(TRACKBAR_CLASS,0,pane),custom=make(cls.lpszClassName,0,pane);
        const auto nested=make(MSFTEDIT_CLASS,ES_MULTILINE|ES_READONLY,custom);
        // Even a mistakenly registered editable target must never yield arrows.
        const auto reading=[&](HWND item){return item==prose||item==heading||item==nested||item==answer;};
        const auto no_reading=[](HWND){return false;};
        const auto owns=[&](HWND target,UINT key){MSG message{};message.hwnd=target;message.message=WM_KEYDOWN;message.wParam=key;message.lParam=1;
            return owns_arrow_keys(target,view,bar,message,reading);};
        for(const auto key:{VK_LEFT,VK_UP,VK_DOWN,VK_RIGHT}){
            require(!owns(prose,key),"Unselected curriculum prose blocked a reader arrow");
            require(!owns(heading,key),"An unselected lesson heading blocked a reader arrow");
            require(!owns(push,key)&&!owns(icon,key),"An ordinary push or icon button blocked a reader arrow");
            for(const auto target:{view,pane,root.value,bar,play})require(!owns(target,key),"A neutral reading surface blocked an arrow");
            for(const auto target:{answer,readonly_code,edit,readonly_edit,radio,checkbox,list,combo,slider,custom,nested})
                require(owns(target,key),"A native input, readonly source or arrow-owning ancestor lost its arrow keys");
            MSG message{};message.hwnd=prose;message.message=WM_KEYDOWN;message.wParam=key;
            require(owns_arrow_keys(prose,view,bar,message,no_reading),"An unregistered readonly text control lost its arrow keys");
        }
        const auto original=text(prose),code_original=text(readonly_code);CHARRANGE selected{2,7};
        SendMessageW(prose,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        for(const auto key:{VK_LEFT,VK_UP,VK_DOWN,VK_RIGHT})require(owns(prose,key),"Selected curriculum text lost native selection navigation");
        const auto retained=selection(prose);require(retained.cpMin==2&&retained.cpMax==7&&text(prose)==original,
            "Checking shortcut ownership modified selected text or its selection");
        SendMessageW(prose,WM_KEYDOWN,VK_RIGHT,1);const auto collapsed=selection(prose);
        require(collapsed.cpMin==7&&collapsed.cpMax==7&&!owns(prose,VK_RIGHT),"Native Right did not collapse selection and then restore reading shortcuts");
        SendMessageW(readonly_code,EM_SETSEL,0,0);
        require(owns(readonly_code,VK_RIGHT)&&text(readonly_code)==code_original,"A collapsed readonly source caret was mistaken for lesson prose");
        SendMessageW(prose,EM_SETREADONLY,FALSE,0);require(owns(prose,VK_LEFT),"A reading surface made editable retained an audio shortcut");
        SendMessageW(prose,EM_SETREADONLY,TRUE,0);require(!owns(prose,VK_LEFT),"Restoring readonly prose did not restore its reading shortcut");
        SetFocus(answer);require(GetFocus()==answer,"Cannot focus the native answer fixture");
        require(owns(GetFocus(),VK_RIGHT)&&!owns(root.value,VK_RIGHT),"Actual input focus was lost when a key targets the root");
        SetFocus(prose);require(GetFocus()==prose&&!owns(GetFocus(),VK_RIGHT),"Actual focused prose still suppresses reader arrows");
        require(text(prose)==original&&text(readonly_code)==code_original,"Focus routing changed original documents");
        std::cout<<"PASS "<<assertions<<" real Win32 focus assertions: four reading arrows, unselected prose/headings, push/icon buttons, selected text, editable/readonly inputs, choices, lists and arrow-owning ancestors\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL reader shortcut focus: "<<failure.what()<<'\n';return 1;}
}
