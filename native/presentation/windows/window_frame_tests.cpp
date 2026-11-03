#include "presentation/windows/window_frame.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "core/lesson_state.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
double luminance(COLORREF color){
    const auto linear=[](BYTE channel){const auto value=channel/255.;return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};
    return .2126*linear(GetRValue(color))+.7152*linear(GetGValue(color))+.0722*linear(GetBValue(color));
}
double contrast(COLORREF first,COLORREF second){const auto a=luminance(first),b=luminance(second);return (std::max(a,b)+.05)/(std::min(a,b)+.05);}
bool current_high_contrast(){HIGHCONTRASTW value{};value.cbSize=sizeof(value);
    return !SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)||(value.dwFlags&HCF_HIGHCONTRASTON)!=0;}
struct Window {
    HWND value=nullptr;
    explicit Window(HWND parent=nullptr){value=CreateWindowExW(0,L"STATIC",L"Native frame QA",
        parent?WS_CHILD:WS_OVERLAPPEDWINDOW,20,20,700,500,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(value,"Cannot create an isolated hidden native frame");}
    ~Window(){if(value&&IsWindow(value))DestroyWindow(value);}
};
bool query(HWND window,WindowFrameColors& colors){
    return SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CAPTION_COLOR,&colors.caption,sizeof(colors.caption)))&&
        SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_TEXT_COLOR,&colors.text,sizeof(colors.text)))&&
        SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_BORDER_COLOR,&colors.border,sizeof(colors.border)));
}
void check_colors(const WindowFrameColors& actual,const WindowFrameColors& expected){
    if(actual.caption!=expected.caption||actual.text!=expected.text||actual.border!=expected.border)
        std::cerr<<"DWM colors actual="<<actual.caption<<','<<actual.text<<','<<actual.border
            <<" expected="<<expected.caption<<','<<expected.text<<','<<expected.border<<'\n';
    check(actual.caption==expected.caption&&actual.text==expected.text&&actual.border==expected.border,
        "The real DWM caption/text/border differ from the active/inactive application policy");
}
void policy(){
    for(const auto accent:{RGB(0,0,0),RGB(255,255,255),RGB(255,255,0),RGB(25,55,220),RGB(89,240,252),RGB(128,128,128)}){
        const auto active=window_frame_colors(accent,true,false),inactive=window_frame_colors(accent,false,false);
        check(active.caption==accent&&active.border==accent,"Active chrome did not use the exact application accent");
        check(inactive.caption==window_frame_colors(RGB(3,4,5),false,false).caption,
            "Inactive chrome inherited an application accent instead of a neutral gray");
        check(GetRValue(inactive.caption)==GetGValue(inactive.caption)&&GetGValue(inactive.caption)==GetBValue(inactive.caption),
            "Inactive chrome is not a neutral gray");
        check(contrast(active.caption,active.text)>=4.5&&contrast(inactive.caption,inactive.text)>=4.5,
            "A selected native caption text color lacks 4.5:1 contrast");
        for(const bool enabled:{false,true}){const auto hc=window_frame_colors(accent,enabled,true);
            check(hc.caption==DWMWA_COLOR_DEFAULT&&hc.text==DWMWA_COLOR_DEFAULT&&hc.border==DWMWA_COLOR_DEFAULT,
                "High contrast did not reset every custom DWM color to the Windows default");}
    }
    std::cout<<"PASS native frame policy: exact global accent, neutral inactive gray, contrasting text, high-contrast defaults\n";
}
void native_frame(){
    Window frame;const auto style=GetWindowLongPtrW(frame.value,GWL_STYLE),extended=GetWindowLongPtrW(frame.value,GWL_EXSTYLE);
    RECT before{};GetWindowRect(frame.value,&before);
    lgaed::state::StateData preferences;
    preferences.preferences["appearance_settings"]["accent_red"]=std::int64_t{42};
    preferences.preferences["appearance_settings"]["accent_green"]=std::int64_t{180};
    preferences.preferences["appearance_settings"]["accent_blue"]=std::int64_t{220};
    preferences.preferences["appearance_settings"]["route_accent_red_programming"]=std::int64_t{240};
    const auto original=preferences;
    auto accent=reader_theme::appearance_accent(preferences);
    check(accent!=reader_theme::appearance_accent(preferences,"programming"),
        "Frame QA failed to distinguish the global application accent from the route accent");
    update_window_frame(frame.value,accent);
    SendMessageW(frame.value,WM_ACTIVATE,WA_ACTIVE,0);
    WindowFrameColors actual{};const bool supported=query(frame.value,actual);
    if(supported){
        check_colors(actual,window_frame_colors(accent,true,current_high_contrast()));
        SendMessageW(frame.value,WM_ACTIVATE,WA_INACTIVE,0);
        check(query(frame.value,actual),"DWM stopped exposing colors after deactivation");
        check_colors(actual,window_frame_colors(accent,false,current_high_contrast()));
        // Updating an accent while inactive must preserve inactivity; the next
        // standard activation applies the new color without a new preference.
        accent=RGB(230,120,30);update_window_frame(frame.value,accent);
        check(query(frame.value,actual),"DWM lost colors during an inactive accent refresh");
        check_colors(actual,window_frame_colors(accent,false,current_high_contrast()));
        SendMessageW(frame.value,WM_ACTIVATE,WA_CLICKACTIVE,0);
        check(query(frame.value,actual),"DWM lost colors during click activation");
        check_colors(actual,window_frame_colors(accent,true,current_high_contrast()));
        for(const auto message:{WM_THEMECHANGED,WM_SYSCOLORCHANGE,WM_SETTINGCHANGE,WM_DWMCOMPOSITIONCHANGED}){
            SendMessageW(frame.value,message,0,0);check(query(frame.value,actual),"DWM lost colors during a system-theme refresh");
            check_colors(actual,window_frame_colors(accent,true,current_high_contrast()));
        }
        std::cout<<"PASS real DWM attributes: active/click-active/inactive, live accent and system-theme refresh; current HC="
            <<current_high_contrast()<<'\n';
    }else{
        // A readback limitation is not evidence that the custom colors were
        // exercised. The following checks still cover native styles/lifetime.
        update_window_frame(frame.value,RGB(200,30,30));SendMessageW(frame.value,WM_ACTIVATE,WA_INACTIVE,0);
        std::cout<<"SKIP real caption-color readback: unavailable on this Windows/DWM; compatibility calls ran without errors\n";
    }
    update_window_frame(nullptr,accent);
    Window child(frame.value);const auto child_style=GetWindowLongPtrW(child.value,GWL_STYLE);
    update_window_frame(child.value,accent);
    RECT after{};GetWindowRect(frame.value,&after);
    check(GetWindowLongPtrW(frame.value,GWL_STYLE)==style&&GetWindowLongPtrW(frame.value,GWL_EXSTYLE)==extended&&EqualRect(&before,&after),
        "The frame colors changed native caption/system-menu/minimize/maximize/move/resize geometry or styles");
    check(GetWindowLongPtrW(child.value,GWL_STYLE)==child_style&&IsWindowEnabled(frame.value)&&!IsWindowVisible(frame.value),
        "Frame styling altered an internal child, disabled the root or displayed the hidden QA window");
    check(preferences==original,"Applying the native frame wrote application preferences");
    std::cout<<"PASS hidden native frame lifetime and unchanged system controls, geometry, child pages and preferences\n";
}
}
int main(){
    try{INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        policy();native_frame();native_frame();return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL native window frame: "<<error.what()<<'\n';return 1;}
}
