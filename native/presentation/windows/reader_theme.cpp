#include "presentation/windows/reader_theme.hpp"
#include "core/lesson_state.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <windowsx.h>
#include <oleacc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <cmath>
#include <string>

namespace lgaed::presentation::reader_theme {
namespace {
constexpr wchar_t surface_property[] = L"LGAED.ReaderSurface";
constexpr wchar_t primary_property[] = L"LGAED.ReaderPrimaryButton";
constexpr wchar_t accent_property[] = L"LGAED.ReaderAccent";
constexpr UINT_PTR radio_subclass_id = 0x4C475244;
constexpr UINT_PTR combo_subclass_id = 0x4C47434F;
constexpr UINT_PTR scroll_subclass_id = 0x4C475342;
constexpr UINT_PTR progress_subclass_id = 0x4C475052;
unsigned surface_index(HWND window) {
    const auto value = reinterpret_cast<ULONG_PTR>(GetPropW(window, surface_property));
    return value >= 1 && value <= 4 ? static_cast<unsigned>(value) - 1 : 0;
}
COLORREF color(const Palette& colors, Surface surface) {
    switch (surface) {
    case Surface::canvas: return colors.canvas;
    case Surface::header: return colors.header;
    case Surface::card: return colors.card;
    case Surface::field: return colors.field;
    }
    return colors.canvas;
}
double luminance(COLORREF value) {
    const auto linear=[](BYTE c){const auto s=c/255.;return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
    return .2126*linear(GetRValue(value))+.7152*linear(GetGValue(value))+.0722*linear(GetBValue(value));
}
double contrast_ratio(COLORREF first,COLORREF second) {
    const auto a=luminance(first),b=luminance(second);return (std::max(a,b)+.05)/(std::min(a,b)+.05);
}
bool rich_edit(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return _wcsnicmp(name, L"RichEdit", 8) == 0;
}
bool progress_bar(HWND window) {
    wchar_t name[64]{};GetClassNameW(window,name,static_cast<int>(std::size(name)));
    return _wcsicmp(name,PROGRESS_CLASSW)==0;
}
void paint_progress(HWND window,HDC dc) {
    if(!dc)return;
    RECT bounds{};GetClientRect(window,&bounds);const auto colors=palette(window);
    const auto fill=[&](const RECT& area,COLORREF ink){
        const auto handle=CreateSolidBrush(ink);if(handle){FillRect(dc,&area,handle);DeleteObject(handle);}
    };
    fill(bounds,colors.outline);
    // Keep the native control as the sole owner of its position and range.
    // PBM_SETBARCOLOR is ignored by visual styles; only its painting differs.
    PBRANGE range{};SendMessageW(window,PBM_GETRANGE,FALSE,reinterpret_cast<LPARAM>(&range));
    const auto position=static_cast<int>(SendMessageW(window,PBM_GETPOS,0,0));
    const auto total=static_cast<std::int64_t>(range.iHigh)-range.iLow;
    if(total<=0)return;
    const auto completed=std::clamp(static_cast<std::int64_t>(position)-range.iLow,std::int64_t{0},total);
    auto filled=bounds;
    if(GetWindowLongPtrW(window,GWL_STYLE)&PBS_VERTICAL)
        filled.top=filled.bottom-static_cast<LONG>((static_cast<std::int64_t>(bounds.bottom-bounds.top)*completed)/total);
    else filled.right=filled.left+static_cast<LONG>((static_cast<std::int64_t>(bounds.right-bounds.left)*completed)/total);
    if(!IsRectEmpty(&filled))fill(filled,colors.accent);
}
LRESULT CALLBACK progress_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,progress_subclass,progress_subclass_id);return DefSubclassProc(window,message,wp,lp);}
    // Marquee has no determinate position; retain its native animation. High
    // contrast likewise retains the complete system rendering and semantics.
    if(palette(window).high_contrast||(GetWindowLongPtrW(window,GWL_STYLE)&PBS_MARQUEE))
        return DefSubclassProc(window,message,wp,lp);
    if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);paint_progress(window,dc);EndPaint(window,&paint);return 0;}
    if(message==WM_PRINTCLIENT){paint_progress(window,reinterpret_cast<HDC>(wp));return 0;}
    if(message==WM_ERASEBKGND){paint_progress(window,reinterpret_cast<HDC>(wp));return TRUE;}
    const auto result=DefSubclassProc(window,message,wp,lp);
    if(message==PBM_SETRANGE||message==PBM_SETRANGE32||message==PBM_SETPOS||message==PBM_DELTAPOS||message==PBM_STEPIT
        ||message==PBM_SETBARCOLOR||message==PBM_SETBKCOLOR||message==PBM_SETSTATE||message==WM_ENABLE
        ||message==WM_THEMECHANGED||message==WM_SETTINGCHANGE||message==WM_STYLECHANGED)
        InvalidateRect(window,nullptr,FALSE);
    return result;
}
bool push_button(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window, name, static_cast<int>(std::size(name)));
    if (_wcsicmp(name, L"BUTTON") != 0) return false;
    const auto type = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
    return type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW;
}
bool radio_button(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window, name, static_cast<int>(std::size(name)));
    if (_wcsicmp(name, L"BUTTON") != 0) return false;
    const auto type = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
    return type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
}
bool checkbox_button(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window,name,static_cast<int>(std::size(name)));
    if (_wcsicmp(name,L"BUTTON") != 0) return false;
    const auto type = GetWindowLongPtrW(window,GWL_STYLE) & BS_TYPEMASK;
    return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE;
}
bool combo_box(HWND window) {wchar_t name[32]{};GetClassNameW(window,name,32);return _wcsicmp(name,L"COMBOBOX")==0
    &&(GetWindowLongPtrW(window,GWL_STYLE)&3)==CBS_DROPDOWNLIST;}
LRESULT CALLBACK combo_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,combo_subclass,combo_subclass_id);return DefSubclassProc(window,message,wp,lp);}
    if(message==WM_MOUSEWHEEL&&!SendMessageW(window,CB_GETDROPPEDSTATE,0,0))return SendMessageW(GetParent(window),message,wp,lp);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT){PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wp);const auto saved=SaveDC(dc);
        RECT bounds{};GetClientRect(window,&bounds);FillRect(dc,&bounds,brush(Surface::card));const auto colors=palette(window);const auto dpi=GetDpiForWindow(window);const int inset=MulDiv(18,static_cast<int>(dpi),96);
        auto text_area=bounds;text_area.left+=inset;text_area.right-=inset*3;
        const auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(window)?colors.text:colors.muted);
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(window,text.data(),static_cast<int>(text.size()))));
        auto measured=text_area;DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);text_area.top+=(bounds.bottom-(measured.bottom-measured.top))/2;
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&text_area,DT_WORDBREAK|DT_NOPREFIX);
        const auto x=bounds.right-inset*2,y=bounds.bottom/2;const int size=std::max(2,inset/3);POINT arrow[]{{x-size,y-size/2},{x+size,y-size/2},{x,y+size/2}};
        const HPEN pen=CreatePen(PS_SOLID,1,colors.muted);
        const HBRUSH fill=CreateSolidBrush(colors.muted);
        const HGDIOBJ old_pen=SelectObject(dc,pen);
        const HGDIOBJ old_brush=SelectObject(dc,fill);
        Polygon(dc,arrow,3);
        SelectObject(dc,old_pen);SelectObject(dc,old_brush);
        DeleteObject(pen);DeleteObject(fill);
        if(GetFocus()==window){auto focus=bounds;InflateRect(&focus,-3,-3);DrawFocusRect(dc,&focus);}RestoreDC(dc,saved);if(message==WM_PAINT)EndPaint(window,&paint);return 0;}
    const auto result=DefSubclassProc(window,message,wp,lp);if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==CB_SETCURSEL||message==WM_ENABLE||message==WM_SETTEXT)InvalidateRect(window,nullptr,FALSE);return result;
}
void paint_radio(HWND window, HDC dc) {
    if (!dc) return;
    const auto saved = SaveDC(dc);
    RECT client{}; GetClientRect(window, &client);
    FillRect(dc, &client, brush(static_cast<Surface>(surface_index(window))));
    const auto colors = palette(window);
    const bool disabled = !IsWindowEnabled(window);
    const auto state = SendMessageW(window,BM_GETCHECK,0,0);
    const bool checked = state == BST_CHECKED;
    const bool checkbox = checkbox_button(window);
    const auto ink = disabled ? (colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.muted) : colors.text;
    const auto edge = disabled ? ink : (checked ? accent_ink(window,background(window)) : colors.muted);
    const auto dpi = GetDpiForWindow(window);
    const auto scale = [dpi](int value) { return std::max(1,MulDiv(value,static_cast<int>(dpi ? dpi : 96),96)); };
    const auto padding = scale(3);
    const auto width = static_cast<int>(client.right - client.left);
    const auto height = static_cast<int>(client.bottom - client.top);
    const auto diameter = std::min({scale(16),std::max(1,width - 2 * padding),std::max(1,height - 2 * padding)});
    const bool right = (GetWindowLongPtrW(window,GWL_STYLE) & BS_LEFTTEXT) != 0;
    const auto left = right ? client.right - padding - diameter : padding;
    const auto top = (client.bottom - diameter) / 2;
    RECT glyph{left,top,left + diameter,top + diameter};
    const auto border = CreatePen(PS_SOLID,scale(2),edge);
    const auto previous_pen = SelectObject(dc,border);
    const auto previous_brush = SelectObject(dc,brush(static_cast<Surface>(surface_index(window))));
    if (checkbox) RoundRect(dc,glyph.left,glyph.top,glyph.right,glyph.bottom,scale(3),scale(3));
    else Ellipse(dc,glyph.left,glyph.top,glyph.right,glyph.bottom);
    if (checked || state == BST_INDETERMINATE) {
        const auto center = CreateSolidBrush(disabled ? ink : colors.accent);
        SelectObject(dc,center); SelectObject(dc,GetStockObject(NULL_PEN));
        const auto inset = std::max(1,diameter / 4);
        if (checkbox) {
            RoundRect(dc,glyph.left,glyph.top,glyph.right,glyph.bottom,scale(3),scale(3));
            const auto check = CreatePen(PS_SOLID,scale(2),disabled ? colors.canvas : accent_foreground(window));
            SelectObject(dc,check);
            if (state == BST_INDETERMINATE) { MoveToEx(dc,glyph.left+inset,glyph.top+diameter/2,nullptr); LineTo(dc,glyph.right-inset,glyph.top+diameter/2); }
            else { MoveToEx(dc,glyph.left+diameter/5,glyph.top+diameter/2,nullptr);LineTo(dc,glyph.left+diameter*2/5,glyph.top+diameter*3/4);LineTo(dc,glyph.left+diameter*4/5,glyph.top+diameter/4); }
            SelectObject(dc,border);DeleteObject(check);
        } else Ellipse(dc,glyph.left + inset,glyph.top + inset,glyph.right - inset,glyph.bottom - inset);
        SelectObject(dc,previous_brush); DeleteObject(center);
    } else SelectObject(dc,previous_brush);
    SelectObject(dc,previous_pen); DeleteObject(border);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));
    SelectObject(dc,font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,ink);
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1,L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(window,text.data(),static_cast<int>(text.size()))));
    auto text_rect = client;
    if (right) text_rect.right = glyph.left - padding;
    else text_rect.left = glyph.right + padding;
    const auto ui_state = SendMessageW(window,WM_QUERYUISTATE,0,0);
    if (!text.empty()) {
        UINT flags=DT_LEFT | ((ui_state & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0);
        if(GetWindowLongPtrW(window,GWL_STYLE)&BS_MULTILINE){auto measured=text_rect;
            DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,flags|DT_WORDBREAK|DT_CALCRECT);
            text_rect.top+=std::max<LONG>(0,(text_rect.bottom-text_rect.top-(measured.bottom-measured.top))/2);flags|=DT_WORDBREAK;
        }else flags|=DT_VCENTER|DT_SINGLELINE;
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&text_rect,flags);
    }
    if (GetFocus() == window && (ui_state & UISF_HIDEFOCUS) == 0) {
        auto focus = text.empty() ? glyph : client;
        InflateRect(&focus,text.empty() ? padding : -1,text.empty() ? padding : -1);
        DrawFocusRect(dc,&focus);
    }
    RestoreDC(dc,saved);
}
LRESULT CALLBACK radio_subclass(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window,radio_subclass,radio_subclass_id);
        return DefSubclassProc(window,message,wparam,lparam);
    }
    if (!radio_button(window) && !checkbox_button(window)) return DefSubclassProc(window,message,wparam,lparam);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window,&paint);
        paint_radio(window,dc); EndPaint(window,&paint); return 0;
    }
    case WM_PRINTCLIENT: paint_radio(window,reinterpret_cast<HDC>(wparam)); return 0;
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window,&client);
        FillRect(reinterpret_cast<HDC>(wparam),&client,brush(static_cast<Surface>(surface_index(window)))); return TRUE;
    }
    default: {
        const auto result = DefSubclassProc(window,message,wparam,lparam);
        if (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_SETFOCUS || message == WM_KILLFOCUS
            || message == WM_ENABLE || message == WM_SETTEXT || message == WM_UPDATEUISTATE
            || message == WM_THEMECHANGED || message == WM_SETTINGCHANGE || message == WM_DPICHANGED_AFTERPARENT)
            InvalidateRect(window,nullptr,FALSE);
        return result;
    }
    }
}
struct Brushes {
    std::array<COLORREF,4> colors{};
    std::array<HBRUSH,4> handles{};
    ~Brushes() { for (const auto handle : handles) if (handle) DeleteObject(handle); }
    HBRUSH get(Surface surface) {
        const auto index = static_cast<std::size_t>(surface);
        const auto value = color(palette(), surface);
        if (!handles[index] || colors[index] != value) {
            if (handles[index]) DeleteObject(handles[index]);
            handles[index] = CreateSolidBrush(value); colors[index] = value;
        }
        return handles[index];
    }
};
LRESULT CALLBACK scroll_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,scroll_subclass,scroll_subclass_id);return DefSubclassProc(window,message,wp,lp);}
    const auto result=DefSubclassProc(window,message,wp,lp);
    // Painting follows native processing, including a native thumb's tracking
    // notifications. There is no timer, input interception or parallel offset.
    if(message==WM_NCPAINT||message==WM_PAINT||message==WM_SIZE||message==WM_VSCROLL||message==WM_HSCROLL||
        message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||message==WM_NCMOUSEMOVE||message==WM_NCLBUTTONUP||
        message==WM_NCLBUTTONDOWN||message==WM_THEMECHANGED||message==WM_SETTINGCHANGE||
        message==WM_STYLECHANGED||message==WM_DPICHANGED_AFTERPARENT||message==WM_SETFONT||
        message==EM_SETSCROLLPOS||message==EM_LINESCROLL||message==EM_SETTARGETDEVICE||
        message==LB_ADDSTRING||message==LB_DELETESTRING||message==LB_RESETCONTENT||
        message==LB_SETITEMHEIGHT||message==LB_SETTOPINDEX){
        refresh_scrollbars(window);
    }else if(message==WM_PRINT&&(lp&PRF_NONCLIENT))paint_scrollbars(window,reinterpret_cast<HDC>(wp));
    return result;
}
}

Palette palette() {
    HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
    const bool high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (high_contrast) {
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_WINDOWTEXT), true};
    }
    return {RGB(30,31,33), RGB(37,40,45), RGB(40,41,45), RGB(38,41,46),
        RGB(222,223,225), RGB(158,163,173), RGB(89,240,252), RGB(70,74,83), false};
}
Palette palette(HWND context) {
    auto result = palette();
    if (result.high_contrast) return result;
    for (auto window = context; window; window = GetParent(window))
        if (const auto value = reinterpret_cast<ULONG_PTR>(GetPropW(window,accent_property))) {
            result.accent = static_cast<COLORREF>(value - 1); break;
        }
    return result;
}
void set_accent(HWND window,std::optional<COLORREF> accent) {
    if (!IsWindow(window)) return;
    if (accent) SetPropW(window,accent_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(*accent & 0xFFFFFF) + 1));
    else RemovePropW(window,accent_property);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_ALLCHILDREN);
}
COLORREF appearance_accent(const state::StateData& data,std::string_view route) {
    const auto component = [&](std::string_view key,int fallback) {
        return static_cast<BYTE>(std::clamp<std::int64_t>(state::integer_value(data,"appearance_settings",key,fallback),0,255));
    };
    const auto global = RGB(component("accent_red",89),component("accent_green",240),component("accent_blue",252));
    const auto fallback = route == "programming" ? RGB(228,50,72) : route == "math_physics" ? RGB(108,209,92) : global;
    if (route.empty()) return global;
    return RGB(component("route_accent_red_" + std::string(route),GetRValue(fallback)),
        component("route_accent_green_" + std::string(route),GetGValue(fallback)),
        component("route_accent_blue_" + std::string(route),GetBValue(fallback)));
}
COLORREF accent_foreground(HWND context) {
    const auto colors=palette(context);if(colors.high_contrast)return GetSysColor(COLOR_HIGHLIGHTTEXT);
    return contrast_ratio(colors.accent,colors.text)>=contrast_ratio(colors.accent,colors.canvas)?colors.text:colors.canvas;
}
COLORREF accent_ink(HWND context,COLORREF surface) {
    const auto colors=palette(context);if(colors.high_contrast)return GetSysColor(COLOR_WINDOWTEXT);
    if(contrast_ratio(colors.accent,surface)>=3.)return colors.accent;
    return contrast_ratio(colors.text,surface)>=contrast_ratio(colors.canvas,surface)?colors.text:colors.canvas;
}
HBRUSH brush(Surface surface) { static Brushes brushes; return brushes.get(surface); }
bool applied(HWND window) { return window && GetPropW(window, surface_property) != nullptr; }
COLORREF background(HWND window) {
    return applied(window) ? color(palette(window), static_cast<Surface>(surface_index(window))) : GetSysColor(COLOR_WINDOW);
}
COLORREF foreground(HWND window) { return applied(window) ? palette(window).text : GetSysColor(COLOR_WINDOWTEXT); }
void paint_scrollbars(HWND window,HDC dc){
    if(!dc||!IsWindow(window)||!applied(window))return;
    const auto colors=palette(window);if(colors.high_contrast)return;
    RECT outer{};GetWindowRect(window,&outer);const auto surface=background(window);
    const auto saved=SaveDC(dc);const auto dpi=static_cast<int>(GetDpiForWindow(window));
    const auto scale=[&](int value){return std::max(1,MulDiv(value,dpi,96));};
    const auto fill=[&](RECT bounds,COLORREF ink){const auto handle=CreateSolidBrush(ink);if(handle){FillRect(dc,&bounds,handle);DeleteObject(handle);}};
    RECT vertical{},horizontal{};bool has_vertical=false,has_horizontal=false;
    for(const bool sideways:{false,true}){
        SCROLLBARINFO info{};info.cbSize=sizeof(info);
        if(!GetScrollBarInfo(window,sideways?OBJID_HSCROLL:OBJID_VSCROLL,&info)||
            (info.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)))continue;
        auto bounds=info.rcScrollBar;OffsetRect(&bounds,-outer.left,-outer.top);
        if(IsRectEmpty(&bounds))continue;
        if(sideways){horizontal=bounds;has_horizontal=true;}else{vertical=bounds;has_vertical=true;}
        const auto clipped=SaveDC(dc);IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);
        fill(bounds,surface);
        const auto extent=sideways?bounds.right-bounds.left:bounds.bottom-bounds.top;
        const auto arrow=std::min<LONG>(info.dxyLineButton,extent/2);
        for(const bool end:{false,true}){
            const int state_index=end?5:1;const auto ink=(info.rgstate[state_index]&STATE_SYSTEM_UNAVAILABLE)?colors.outline:colors.muted;
            const auto center=sideways?POINT{end?bounds.right-arrow/2:bounds.left+arrow/2,(bounds.top+bounds.bottom)/2}:
                POINT{(bounds.left+bounds.right)/2,end?bounds.bottom-arrow/2:bounds.top+arrow/2};
            const int direction=end?1:-1,radius=scale(2);POINT points[3];
            if(sideways){points[0]={center.x-direction*radius,center.y-radius};points[1]={center.x+direction*radius,center.y};points[2]={center.x-direction*radius,center.y+radius};}
            else{points[0]={center.x-radius,center.y-direction*radius};points[1]={center.x,center.y+direction*radius};points[2]={center.x+radius,center.y-direction*radius};}
            const auto pen=CreatePen(PS_SOLID,scale(1),ink);const auto before=SelectObject(dc,pen);Polyline(dc,points,3);SelectObject(dc,before);DeleteObject(pen);
        }
        if(!(info.rgstate[3]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_UNAVAILABLE))&&info.xyThumbBottom>info.xyThumbTop){
            auto thumb=bounds;const auto inset=std::max<LONG>(scale(2),((sideways?bounds.bottom-bounds.top:bounds.right-bounds.left)-scale(5))/2);
            if(sideways){thumb.left+=info.xyThumbTop;thumb.right=bounds.left+info.xyThumbBottom;thumb.top+=inset;thumb.bottom-=inset;}
            else{thumb.top+=info.xyThumbTop;thumb.bottom=bounds.top+info.xyThumbBottom;thumb.left+=inset;thumb.right-=inset;}
            fill(thumb,(info.rgstate[3]&STATE_SYSTEM_PRESSED)?accent_ink(window,surface):colors.muted);
        }
        RestoreDC(dc,clipped);
    }
    if(has_vertical&&has_horizontal){const RECT corner{vertical.left,horizontal.top,vertical.right,horizontal.bottom};fill(corner,surface);}
    RestoreDC(dc,saved);
}
void refresh_scrollbars(HWND window){
    if(!IsWindow(window)||!applied(window))return;
    // Empty custom viewports can lose WS_*SCROLL before apply() runs.
    // Once they acquire a range, retain the same nonclient paint hook too.
    SetWindowSubclass(window,scroll_subclass,scroll_subclass_id,0);
    if(const auto dc=GetWindowDC(window)){paint_scrollbars(window,dc);ReleaseDC(window,dc);}
}
void apply(HWND window, Surface surface) {
    if (!window) return;
    SetPropW(window, surface_property, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(surface) + 1));
    if(progress_bar(window))SetWindowSubclass(window,progress_subclass,progress_subclass_id,0);
    if((GetWindowLongPtrW(window,GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL))||rich_edit(window))
        SetWindowSubclass(window,scroll_subclass,scroll_subclass_id,0);
    if (rich_edit(window)) SendMessageW(window, EM_SETBKGNDCOLOR, 0, background(window));
    if (push_button(window)) {
        const auto original = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE, (original & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW);
    }
    if (radio_button(window) || checkbox_button(window)) SetWindowSubclass(window,radio_subclass,radio_subclass_id,0);
    if (combo_box(window)) {SetWindowSubclass(window,combo_subclass,combo_subclass_id,0);COMBOBOXINFO info{sizeof(info)};if(GetComboBoxInfo(window,&info)&&info.hwndList){SetPropW(info.hwndList,surface_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(Surface::card)+1));SetWindowSubclass(info.hwndList,scroll_subclass,scroll_subclass_id,0);}}
    InvalidateRect(window, nullptr, TRUE);
}
void primary_button(HWND button, bool primary) {
    if (primary) SetPropW(button, primary_property, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    else RemovePropW(button, primary_property);
    InvalidateRect(button, nullptr, FALSE);
}
bool control_color(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) {
    if (message != WM_CTLCOLOREDIT && message != WM_CTLCOLORSTATIC && message != WM_CTLCOLORBTN
        && message != WM_CTLCOLORLISTBOX && message != WM_CTLCOLORDLG) return false;
    const auto window = reinterpret_cast<HWND>(lparam);
    if (!applied(window)) return false;
    const auto dc = reinterpret_cast<HDC>(wparam);
    SetTextColor(dc, foreground(window)); SetBkColor(dc, background(window)); SetBkMode(dc, OPAQUE);
    result = reinterpret_cast<LRESULT>(brush(static_cast<Surface>(surface_index(window))));
    return true;
}
bool draw_button(const DRAWITEMSTRUCT& item) {
    if (item.CtlType != ODT_BUTTON || !applied(item.hwndItem) || !push_button(item.hwndItem)) return false;
    const auto colors = palette(item.hwndItem);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = GetPropW(item.hwndItem, primary_property) != nullptr;
    auto fill = primary && !disabled ? colors.accent : colors.field;
    auto ink = primary && !disabled ? accent_foreground(item.hwndItem) : colors.text;
    if (colors.high_contrast) {
        fill = primary && !disabled ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE);
        ink = primary && !disabled ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_BTNTEXT);
    } else if (pressed && !disabled) {
        fill = primary ? colors.accent : RGB(53,57,65);
    }
    if (disabled) ink = colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.muted;
    const auto saved = SaveDC(item.hDC);
    const auto parent = GetParent(item.hwndItem);
    FillRect(item.hDC, &item.rcItem, brush(applied(parent) ? static_cast<Surface>(surface_index(parent)) : Surface::canvas));
    const auto body = CreateSolidBrush(fill);
    const auto border = CreatePen(PS_SOLID, 1, primary && !disabled ? colors.accent : colors.outline);
    const auto old_brush = SelectObject(item.hDC, body);
    const auto old_pen = SelectObject(item.hDC, border);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 10, 10);
    SelectObject(item.hDC, old_pen); SelectObject(item.hDC, old_brush);
    DeleteObject(border); DeleteObject(body);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    SelectObject(item.hDC, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(item.hDC, ink); SetBkMode(item.hDC, TRANSPARENT);
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item.hwndItem)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(item.hwndItem, text.data(), static_cast<int>(text.size()))));
    auto bounds = item.rcItem; InflateRect(&bounds, -7, -3);
    if (pressed) OffsetRect(&bounds, 1, 1);
    UINT text_flags=DT_CENTER | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
    if(GetWindowLongPtrW(item.hwndItem,GWL_STYLE)&BS_MULTILINE){
        auto measured=bounds;DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&measured,text_flags | DT_WORDBREAK | DT_CALCRECT);
        bounds.top+=std::max<LONG>(0,(bounds.bottom-bounds.top-(measured.bottom-measured.top))/2);text_flags|=DT_WORDBREAK;
    }else text_flags|=DT_VCENTER | DT_SINGLELINE;
    DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&bounds,text_flags);
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        auto focus = item.rcItem; InflateRect(&focus, -4, -4); DrawFocusRect(item.hDC, &focus);
    }
    RestoreDC(item.hDC, saved);
    return true;
}
} // namespace lgaed::presentation::reader_theme
