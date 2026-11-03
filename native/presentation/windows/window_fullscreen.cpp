#include "presentation/windows/window_fullscreen.hpp"
#include <commctrl.h>
#include <new>

namespace lgaed::presentation {
namespace {
constexpr UINT_PTR fullscreen_subclass=0x4c474653;
constexpr LONG_PTR frame_extended=WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE|WS_EX_DLGMODALFRAME;
struct Fullscreen {
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    LONG_PTR style=0;
    LONG_PTR extended=0;
    bool active=false;
};
bool style(HWND root,LONG_PTR value,int field=GWL_STYLE) noexcept {
    SetLastError(ERROR_SUCCESS);
    return SetWindowLongPtrW(root,field,value)!=0||GetLastError()==ERROR_SUCCESS;
}
bool fit(HWND root) noexcept {
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromWindow(root,MONITOR_DEFAULTTONEAREST),&monitor))return false;
    const auto& area=monitor.rcMonitor;
    return SetWindowPos(root,nullptr,area.left,area.top,area.right-area.left,area.bottom-area.top,
        SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED)!=FALSE;
}
bool restore(HWND root,Fullscreen& state) noexcept {
    const bool visible=IsWindowVisible(root)!=FALSE;
    if(!style(root,state.style)||!style(root,state.extended,GWL_EXSTYLE))return false;
    auto placement=state.placement;
    if(!visible)placement.showCmd=SW_HIDE;
    const bool positioned=SetWindowPlacement(root,&placement)!=FALSE;
    const bool framed=SetWindowPos(root,nullptr,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED)!=FALSE;
    if(positioned&&framed)state.active=false;
    return positioned&&framed;
}
LRESULT CALLBACK proc(HWND root,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
    auto* state=reinterpret_cast<Fullscreen*>(raw);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(root,proc,fullscreen_subclass);delete state;return DefSubclassProc(root,message,wp,lp);}
    const bool refit=state->active&&(message==WM_DPICHANGED||message==WM_DISPLAYCHANGE||message==WM_SETTINGCHANGE);
    const auto result=DefSubclassProc(root,message,wp,lp);
    DWORD_PTR current=0;
    if(refit&&GetWindowSubclass(root,proc,fullscreen_subclass,&current)&&current==raw&&reinterpret_cast<Fullscreen*>(current)->active)
        (void)fit(root);
    return result;
}
Fullscreen* attached(HWND root) noexcept {
    DWORD_PTR value=0;
    return IsWindow(root)&&GetWindowSubclass(root,proc,fullscreen_subclass,&value)?reinterpret_cast<Fullscreen*>(value):nullptr;
}
}
bool window_fullscreen(HWND root) noexcept {const auto* state=attached(root);return state&&state->active;}
bool toggle_window_fullscreen(HWND root) noexcept {
    if(!IsWindow(root)||(GetWindowLongPtrW(root,GWL_STYLE)&WS_CHILD))return false;
    auto* state=attached(root);
    if(!state){
        state=new(std::nothrow) Fullscreen;
        if(!state)return false;
        if(!SetWindowSubclass(root,proc,fullscreen_subclass,reinterpret_cast<DWORD_PTR>(state))){delete state;return false;}
    }
    if(state->active)return restore(root,*state);
    state->placement.length=sizeof(state->placement);
    if(!GetWindowPlacement(root,&state->placement))return false;
    state->style=GetWindowLongPtrW(root,GWL_STYLE);
    state->extended=GetWindowLongPtrW(root,GWL_EXSTYLE);
    if(!style(root,state->style&~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW|WS_MAXIMIZE|WS_MINIMIZE)))return false;
    state->active=true;
    if(!style(root,state->extended&~frame_extended,GWL_EXSTYLE)){(void)restore(root,*state);return false;}
    if(fit(root))return true;
    (void)restore(root,*state);
    return false;
}
bool dispatch_window_fullscreen_key(HWND root,const MSG& message) noexcept {
    if(message.message!=WM_KEYDOWN||message.wParam!=VK_F12||!IsWindow(root)||GetAncestor(message.hwnd,GA_ROOT)!=root)return false;
    if(!(message.lParam&(static_cast<LPARAM>(1)<<30)))(void)toggle_window_fullscreen(root);
    return true;
}
}
