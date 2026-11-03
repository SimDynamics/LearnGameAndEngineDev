#include "presentation/windows/window_fullscreen.hpp"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Window {
    HWND value=nullptr;
    explicit Window(HWND parent=nullptr,DWORD extra_style=0){value=CreateWindowExW(0,L"STATIC",L"Fullscreen QA",
        (parent?WS_CHILD:WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN)|extra_style,90,110,720,540,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(value,"Cannot create an isolated fullscreen QA window");}
    ~Window(){if(IsWindow(value))DestroyWindow(value);}
};
RECT rectangle(HWND window){RECT result{};check(GetWindowRect(window,&result),"Cannot inspect a native window rectangle");return result;}
void normal_cycle(){
    Window root,child(root.value),other;
    const auto original=rectangle(root.value),child_original=rectangle(child.value);
    const auto original_style=GetWindowLongPtrW(root.value,GWL_STYLE),extended=GetWindowLongPtrW(root.value,GWL_EXSTYLE);
    const auto fullscreen_extended=extended&~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE|WS_EX_DLGMODALFRAME);
    WINDOWPLACEMENT placement{sizeof(placement)};check(GetWindowPlacement(root.value,&placement),"Cannot record original native placement");
    MONITORINFO monitor{sizeof(monitor)};check(GetMonitorInfoW(MonitorFromWindow(root.value,MONITOR_DEFAULTTONEAREST),&monitor),"Cannot observe the actual monitor bounds");
    const auto focus=GetFocus();MSG key{};key.hwnd=child.value;key.message=WM_KEYDOWN;key.wParam=VK_F12;
    check(dispatch_window_fullscreen_key(root.value,key)&&window_fullscreen(root.value),"F12 from a nested child did not enter fullscreen");
    auto fullscreen_bounds=rectangle(root.value);
    check(!(GetWindowLongPtrW(root.value,GWL_STYLE)&WS_OVERLAPPEDWINDOW)&&EqualRect(&fullscreen_bounds,&monitor.rcMonitor),
        "Fullscreen retained its caption/borders or did not cover the actual monitor");
    if(!IsChild(root.value,child.value)||GetWindowLongPtrW(root.value,GWL_EXSTYLE)!=fullscreen_extended||GetFocus()!=focus||IsWindowVisible(root.value))
        std::cerr<<"Fullscreen native state: child="<<IsChild(root.value,child.value)<<" extended="<<GetWindowLongPtrW(root.value,GWL_EXSTYLE)
            <<" original_extended="<<extended<<" focus="<<GetFocus()<<" original_focus="<<focus<<" visible="<<IsWindowVisible(root.value)<<'\n';
    check(IsChild(root.value,child.value)&&GetWindowLongPtrW(root.value,GWL_EXSTYLE)==fullscreen_extended&&GetFocus()==focus&&!IsWindowVisible(root.value),
        "Fullscreen recreated a child, changed extended styles/focus or displayed hidden QA");
    key.lParam=static_cast<LPARAM>(1)<<30;
    check(dispatch_window_fullscreen_key(root.value,key)&&window_fullscreen(root.value),"A held F12 repeatedly toggled the frame");
    key.hwnd=other.value;key.lParam=0;
    check(!dispatch_window_fullscreen_key(root.value,key)&&window_fullscreen(root.value),"Another root's F12 changed this window");
    key.hwnd=child.value;key.wParam=VK_F11;
    check(!dispatch_window_fullscreen_key(root.value,key)&&window_fullscreen(root.value),"An unrelated key toggled fullscreen");
    key.wParam=VK_F12;key.message=WM_KEYUP;
    check(!dispatch_window_fullscreen_key(root.value,key)&&window_fullscreen(root.value),"F12 keyup toggled fullscreen again");
    key.message=WM_KEYDOWN;
    check(dispatch_window_fullscreen_key(root.value,key)&&!window_fullscreen(root.value),"A second F12 did not restore the frame");
    const auto after=rectangle(root.value),child_after=rectangle(child.value);
    WINDOWPLACEMENT restored{sizeof(restored)};check(GetWindowPlacement(root.value,&restored),"Cannot inspect restored placement");
    check(GetWindowLongPtrW(root.value,GWL_STYLE)==original_style&&GetWindowLongPtrW(root.value,GWL_EXSTYLE)==extended&&
        EqualRect(&original,&after)&&EqualRect(&placement.rcNormalPosition,&restored.rcNormalPosition)&&EqualRect(&child_original,&child_after),
        "The original frame/placement or nested child geometry did not return after fullscreen");
    check(GetFocus()==focus&&!IsWindowVisible(root.value),"Restoring fullscreen stole focus or exposed hidden QA");
    check(!toggle_window_fullscreen(child.value)&&!toggle_window_fullscreen(nullptr),"Fullscreen accepted a child or invalid root");
    std::cout<<"PASS F12 child routing, monitor geometry, repeat/keyup isolation, exact native restoration and unchanged children/focus\n";
}
void document_cycle(){
    Window root;
    const auto edit=CreateWindowExW(0,L"EDIT",L"Borrador conservado",WS_CHILD|ES_MULTILINE,0,0,200,100,root.value,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(edit,"Cannot create the existing-document fullscreen fixture");
    SendMessageW(edit,EM_SETSEL,3,9);const auto modified=SendMessageW(edit,EM_GETMODIFY,0,0);
    for(int cycle=0;cycle<3;++cycle){check(toggle_window_fullscreen(root.value)&&toggle_window_fullscreen(root.value),"A repeated fullscreen round-trip failed");}
    DWORD first=0,last=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    wchar_t text[64]{};GetWindowTextW(edit,text,64);
    check(std::wstring_view(text)==L"Borrador conservado"&&first==3&&last==9&&SendMessageW(edit,EM_GETMODIFY,0,0)==modified,
        "Fullscreen changed the existing document, selection or modified flag");
    check(toggle_window_fullscreen(root.value),"Cannot exercise destruction during fullscreen");
    const auto destroyed=root.value;DestroyWindow(root.value);root.value=nullptr;
    check(!window_fullscreen(destroyed)&&!toggle_window_fullscreen(destroyed),"A destroyed window retained live fullscreen ownership");
    std::cout<<"PASS repeated fullscreen preserves draft/selection and releases its native state on destruction\n";
}
void maximized_cycle(){
    Window root(nullptr,WS_MAXIMIZE);
    check(IsZoomed(root.value)&&!IsWindowVisible(root.value),"The maximized QA fixture is not hidden/maximized");
    const auto before=rectangle(root.value);WINDOWPLACEMENT original{sizeof(original)};
    check(GetWindowPlacement(root.value,&original),"Cannot inspect maximized restoration data");
    check(toggle_window_fullscreen(root.value),"Cannot enter fullscreen from maximized");
    SendMessageW(root.value,WM_DISPLAYCHANGE,0,0);
    check(window_fullscreen(root.value)&&toggle_window_fullscreen(root.value),"Cannot restore maximized fullscreen after display refresh");
    const auto after=rectangle(root.value);WINDOWPLACEMENT restored{sizeof(restored)};
    check(GetWindowPlacement(root.value,&restored),"Cannot inspect restored maximized data");
    check(IsZoomed(root.value)&&EqualRect(&before,&after)&&EqualRect(&original.rcNormalPosition,&restored.rcNormalPosition)&&!IsWindowVisible(root.value),
        "Fullscreen lost the original maximized state, restore rectangle or hidden visibility");
    std::cout<<"PASS maximized fullscreen restores maximization and its original normal rectangle after a display refresh\n";
}
}
int main(){
    try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
        normal_cycle();document_cycle();maximized_cycle();return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL fullscreen: "<<error.what()<<'\n';return 1;}
}
