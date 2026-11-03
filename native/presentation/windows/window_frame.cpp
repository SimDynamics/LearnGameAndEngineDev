#include "presentation/windows/window_frame.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace lgaed::presentation {
namespace {
constexpr UINT_PTR frame_subclass=0x4C474652;
double luminance(COLORREF color) noexcept {
    const auto linear=[](BYTE channel){const auto value=channel/255.;return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};
    return .2126*linear(GetRValue(color))+.7152*linear(GetGValue(color))+.0722*linear(GetBValue(color));
}
bool high_contrast() noexcept {
    HIGHCONTRASTW value{};value.cbSize=sizeof(value);
    if(!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0))return true;
    return (value.dwFlags&HCF_HIGHCONTRASTON)!=0;
}
void native_defaults(HWND window) noexcept {
    const COLORREF value=DWMWA_COLOR_DEFAULT;
    for(const auto attribute:{DWMWA_CAPTION_COLOR,DWMWA_TEXT_COLOR,DWMWA_BORDER_COLOR})
        (void)DwmSetWindowAttribute(window,attribute,&value,sizeof(value));
}
struct Frame { COLORREF accent;bool active; };
void apply(HWND window,const Frame& state) noexcept {
    const auto colors=window_frame_colors(state.accent,state.active,high_contrast());
    const auto caption=DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&colors.caption,sizeof(colors.caption));
    const auto text=DwmSetWindowAttribute(window,DWMWA_TEXT_COLOR,&colors.text,sizeof(colors.text));
    const auto border=DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&colors.border,sizeof(colors.border));
    // An unsupported/failed attribute must not leave half a custom frame with
    // a caption/text combination whose contrast we did not select together.
    if(FAILED(caption)||FAILED(text)||FAILED(border))native_defaults(window);
}
LRESULT CALLBACK frame_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
    auto* state=reinterpret_cast<Frame*>(raw);
    if(message==WM_NCDESTROY){
        RemoveWindowSubclass(window,frame_proc,frame_subclass);delete state;
        return DefSubclassProc(window,message,wp,lp);
    }
    const bool activation=message==WM_ACTIVATE;
    const bool refresh=activation||message==WM_THEMECHANGED||message==WM_SYSCOLORCHANGE||
        message==WM_SETTINGCHANGE||message==WM_DWMCOMPOSITIONCHANGED;
    if(activation)state->active=LOWORD(wp)!=WA_INACTIVE;
    const auto result=DefSubclassProc(window,message,wp,lp);
    DWORD_PTR current=0;
    if(refresh&&GetWindowSubclass(window,frame_proc,frame_subclass,&current)&&current==raw)
        apply(window,*reinterpret_cast<Frame*>(current));
    return result;
}
}
WindowFrameColors window_frame_colors(COLORREF accent,bool active,bool contrast) noexcept {
    if(contrast)return {DWMWA_COLOR_DEFAULT,DWMWA_COLOR_DEFAULT,DWMWA_COLOR_DEFAULT};
    const auto caption=active?(accent&0xFFFFFF):RGB(64,64,64);
    const auto light=luminance(caption);
    const auto black=(light+.05)/.05,white=1.05/(light+.05);
    return {caption,black>=white?RGB(0,0,0):RGB(255,255,255),caption};
}
void update_window_frame(HWND window,COLORREF screen_accent) noexcept {
    if(!IsWindow(window)||(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD))return;
    DWORD_PTR current=0;
    if(GetWindowSubclass(window,frame_proc,frame_subclass,&current)){
        auto& state=*reinterpret_cast<Frame*>(current);state.accent=screen_accent;apply(window,state);return;
    }
    auto* state=new(std::nothrow) Frame{screen_accent,GetActiveWindow()==window};
    if(!state)return;
    if(!SetWindowSubclass(window,frame_proc,frame_subclass,reinterpret_cast<DWORD_PTR>(state))){delete state;return;}
    apply(window,*state);
}
}
