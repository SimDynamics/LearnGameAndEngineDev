#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "core/lesson_state.hpp"
#include "native_build_config.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <windowsx.h>
#include <oleacc.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace lgaed::presentation::reader_theme {
namespace {
constexpr wchar_t surface_property[] = L"LGAED.ReaderSurface";
constexpr wchar_t completed_property[] = L"LGAED.ReaderCompleted";
constexpr wchar_t primary_property[] = L"LGAED.ReaderPrimaryButton";
constexpr wchar_t icon_property[] = L"LGAED.ReaderIconButton";
constexpr wchar_t premium_property[] = L"LGAED.ReaderPremiumButton";
constexpr wchar_t switch_property[] = L"LGAED.ReaderSwitchButton";
constexpr wchar_t accent_property[] = L"LGAED.ReaderAccent";
constexpr wchar_t text_property[] = L"LGAED.ReaderTextColor";
constexpr wchar_t scroll_paint_pending_property[] = L"LGAED.ReaderScrollbarPaintPending";
constexpr wchar_t scroll_theme_policy_property[] = L"LGAED.ReaderScrollbarThemePolicy";
constexpr UINT_PTR radio_subclass_id = 0x4C475244;
constexpr UINT_PTR combo_subclass_id = 0x4C47434F;
constexpr UINT_PTR scroll_subclass_id = 0x4C475342;
constexpr UINT_PTR progress_subclass_id = 0x4C475052;
constexpr UINT_PTR focus_subclass_id = 0x4C474643;
constexpr UINT_PTR slider_subclass_id = 0x4C47534C;
thread_local bool keyboard_focus = false;
constexpr std::size_t surface_count=static_cast<std::size_t>(Surface::compiler_output)+1;
unsigned surface_index(HWND window) {
    const auto value = reinterpret_cast<ULONG_PTR>(GetPropW(window, surface_property));
    return value >= 1 && value <= surface_count ? static_cast<unsigned>(value) - 1 : 0;
}
COLORREF color(const Palette& colors, Surface surface) {
    switch (surface) {
    case Surface::canvas: return colors.canvas;
    case Surface::header: return colors.header;
    case Surface::card: return colors.card;
    case Surface::field: return colors.field;
    case Surface::screen: return colors.high_contrast ? colors.canvas : RGB(16,17,20);
    case Surface::catalog_card: return colors.high_contrast ? colors.card : RGB(32,33,38);
    case Surface::card_header: return colors.high_contrast ? colors.header : RGB(35,35,35);
    case Surface::inset: return colors.high_contrast ? colors.field : RGB(20,20,20);
    case Surface::low: return colors.high_contrast ? colors.field : RGB(25,25,25);
    case Surface::note: return colors.high_contrast ? colors.card : RGB(30,30,30);
    case Surface::popup: return colors.high_contrast ? colors.card : RGB(48,50,57);
    case Surface::compiler_header: return colors.high_contrast ? colors.header : RGB(37,40,46);
    case Surface::compiler_editor: return colors.high_contrast ? colors.field : RGB(28,31,36);
    case Surface::settings_defaults: return colors.high_contrast ? colors.field : RGB(24,25,29);
    case Surface::compiler_output: return colors.high_contrast ? colors.field : RGB(5,6,7);
    }
    return colors.canvas;
}
LRESULT CALLBACK focus_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(message==WM_NCDESTROY){
        RemoveWindowSubclass(window,focus_subclass,focus_subclass_id);
        RemovePropW(window,completed_property);RemovePropW(window,icon_property);RemovePropW(window,premium_property);RemovePropW(window,switch_property);
        return DefSubclassProc(window,message,wp,lp);
    }
    if(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_POINTERDOWN)keyboard_focus=false;
    else if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN)keyboard_focus=true;
    else if(message==WM_SETFOCUS&&(GetKeyState(VK_TAB)&0x8000))keyboard_focus=true;
    const auto result=DefSubclassProc(window,message,wp,lp);
    if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_LBUTTONDOWN||message==WM_KEYDOWN||message==WM_UPDATEUISTATE)
        InvalidateRect(window,nullptr,FALSE);
    return result;
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
bool trackbar(HWND window){wchar_t name[64]{};GetClassNameW(window,name,64);return _wcsicmp(name,TRACKBAR_CLASSW)==0;}
void paint_slider(HWND window,HDC dc){
    if(!dc)return;
    RECT bounds{},channel{},thumb{};GetClientRect(window,&bounds);
    const auto colors=palette(window);const auto bg=CreateSolidBrush(background(window));FillRect(dc,&bounds,bg);DeleteObject(bg);
    SendMessageW(window,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    SendMessageW(window,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    const auto dpi=static_cast<int>(GetDpiForWindow(window));const auto px=[&](int n){return std::max(1,MulDiv(n,dpi,96));};
    const bool vertical=(GetWindowLongPtrW(window,GWL_STYLE)&TBS_VERT)!=0;
    const int center=vertical?(thumb.top+thumb.bottom)/2:(thumb.left+thumb.right)/2;
    RECT track=channel;
    if(vertical){const int x=(thumb.left+thumb.right)/2;track.left=x-px(1);track.right=x+px(1);}
    else{const int y=(thumb.top+thumb.bottom)/2;track.top=y-px(1);track.bottom=y+px(1);}
    const auto fill=[&](const RECT& area,COLORREF color){const auto brush=CreateSolidBrush(color);FillRect(dc,&area,brush);DeleteObject(brush);};
    fill(track,colors.outline);auto completed=track;
    if(vertical)completed.top=center;else completed.right=center;
    const auto ink=IsWindowEnabled(window)?colors.accent:colors.muted;fill(completed,ink);
    if(!(GetWindowLongPtrW(window,GWL_STYLE)&TBS_NOTHUMB)){
        const int radius=px(6),x=(thumb.left+thumb.right)/2,y=(thumb.top+thumb.bottom)/2;
        const auto previous=SelectObject(dc,GetStockObject(NULL_PEN));const auto brush=CreateSolidBrush(ink);const auto old=SelectObject(dc,brush);
        Ellipse(dc,x-radius,y-radius,x+radius,y+radius);SelectObject(dc,old);SelectObject(dc,previous);DeleteObject(brush);
    }
    paint_keyboard_focus(window,dc,bounds,colors.muted);
}
LRESULT CALLBACK slider_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,slider_subclass,slider_subclass_id);return DefSubclassProc(window,message,wp,lp);}
    if(palette(window).high_contrast)return DefSubclassProc(window,message,wp,lp);
    if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);paint_slider(window,dc);EndPaint(window,&paint);return 0;}
    if(message==WM_PRINTCLIENT){paint_slider(window,reinterpret_cast<HDC>(wp));return 0;}
    if(message==WM_ERASEBKGND)return 1;
    const auto result=DefSubclassProc(window,message,wp,lp);
    if(message==TBM_SETPOS||message==TBM_SETRANGE||message==TBM_SETRANGEMAX||message==TBM_SETRANGEMIN||message==WM_ENABLE
        ||message==WM_SIZE||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_MOUSEMOVE||message==WM_KEYDOWN||message==WM_LBUTTONUP)
        InvalidateRect(window,nullptr,FALSE);
    return result;
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
bool draw_success_label(HDC dc,const std::wstring& text,RECT bounds,COLORREF ink) {
    if(!text.ends_with(L" \u2713")||text.find_first_of(L"\r\n&")!=std::wstring::npos)return false;
    // The caption remains "Correcto \u2713" for accessibility. Paint only its
    // terminal mark as geometry so a missing glyph in the selected UI font
    // cannot hide the solved state or consume the separating space.
    const auto label=text.substr(0,text.size()-2);
    TEXTMETRICW metrics{};SIZE label_size{},space{};
    if(!GetTextMetricsW(dc,&metrics)||!GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&label_size)
        ||!GetTextExtentPoint32W(dc,L" ",1,&space))return false;
    const int side=std::max(1L,metrics.tmAscent*3/4),gap=std::max(1L,space.cx);
    const int room=std::max(0L,bounds.right-bounds.left);
    if(room<=side+gap||bounds.bottom<=bounds.top)return false;
    const int label_width=std::min(static_cast<int>(label_size.cx),room-side-gap);
    const int left=bounds.left+(room-label_width-gap-side)/2;
    const int text_top=bounds.top+(bounds.bottom-bounds.top-metrics.tmHeight)/2;
    const auto pen=CreatePen(PS_SOLID,std::max(1L,metrics.tmHeight/10),ink);
    if(!pen)return false;
    RECT label_bounds{left,bounds.top,left+label_width,bounds.bottom};
    DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&label_bounds,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    const int x=left+label_width+gap,y=text_top+(metrics.tmAscent-side)/2;
    const POINT points[]{{x,y+side/2},{x+side/3,y+side*4/5},{x+side,y+side/8}};
    const auto previous=SelectObject(dc,pen);Polyline(dc,points,3);SelectObject(dc,previous);DeleteObject(pen);
    return true;
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
        RECT bounds{};GetClientRect(window,&bounds);FillRect(dc,&bounds,brush(static_cast<Surface>(surface_index(window))));const auto colors=palette(window);const auto dpi=GetDpiForWindow(window);const int inset=MulDiv(18,static_cast<int>(dpi),96);
        auto text_area=bounds;text_area.left+=inset;text_area.right-=inset*3;
        const auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(window)?foreground(window):colors.muted);
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
        paint_keyboard_focus(window,dc,bounds,colors.muted);RestoreDC(dc,saved);if(message==WM_PAINT)EndPaint(window,&paint);return 0;}
    const auto result=DefSubclassProc(window,message,wp,lp);if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==CB_SETCURSEL||message==WM_ENABLE||message==WM_SETTEXT)InvalidateRect(window,nullptr,FALSE);return result;
}
bool paint_switch(HWND window,HDC dc){
    if(!dc)return false;
    const auto colors=palette(window);if(colors.high_contrast)return false;
    RECT client{};GetClientRect(window,&client);const int dpi=static_cast<int>(GetDpiForWindow(window));
    const auto px=[&](int value){return std::max(1,MulDiv(value,dpi?dpi:96,96));};
    const int padding=px(3),width=std::min(px(36),static_cast<int>(client.right)-padding*2),height=std::min(px(20),static_cast<int>(client.bottom));
    if(width<height||height<1)return false;
    const RECT glyph{client.right-padding-width,(client.bottom-height)/2,client.right-padding,(client.bottom+height)/2};
    constexpr int samples=4;const auto memory=CreateCompatibleDC(dc);
    BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);format.bmiHeader.biWidth=width*samples;format.bmiHeader.biHeight=-height*samples;
    format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(dc,&format,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!memory||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);return false;}
    const auto saved=SaveDC(dc);FillRect(dc,&client,brush(static_cast<Surface>(surface_index(window))));
    const auto previous_bitmap=SelectObject(memory,bitmap);const RECT sample{0,0,width*samples,height*samples};
    FillRect(memory,&sample,brush(static_cast<Surface>(surface_index(window))));
    const bool checked=SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED;
    const auto blend=[](COLORREF ink,COLORREF bg,unsigned alpha){const auto c=[alpha](unsigned a,unsigned b){return (a*alpha+b*(255-alpha)+127)/255;};return RGB(c(GetRValue(ink),GetRValue(bg)),c(GetGValue(ink),GetGValue(bg)),c(GetBValue(ink),GetBValue(bg)));};
    // Exact AppearanceUi.applySwitch state colours: disabled checked controls
    // retain their checked tint because the Android list keys on checked only.
    const auto thumb_ink=checked?colors.accent:RGB(145,148,158);
    const auto track_ink=blend(checked?colors.accent:RGB(120,124,134),background(window),checked?135:90);
    const auto pen=SelectObject(memory,GetStockObject(NULL_PEN));const int track_height=std::min(height,px(14))*samples;
    const int top=(height*samples-track_height)/2;const auto track=CreateSolidBrush(track_ink),thumb=CreateSolidBrush(thumb_ink);
    const auto previous_brush=SelectObject(memory,track);RoundRect(memory,0,top,width*samples,top+track_height,track_height,track_height);
    SelectObject(memory,thumb);const int x=checked?(width-height)*samples:0;Ellipse(memory,x,0,x+height*samples,height*samples);
    SelectObject(memory,previous_brush);SelectObject(memory,pen);DeleteObject(track);DeleteObject(thumb);GdiFlush();
    const auto* source=static_cast<const std::uint32_t*>(pixels);std::vector<std::uint32_t> reduced(static_cast<std::size_t>(width)*height);
    constexpr unsigned count=samples*samples;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){unsigned red=0,green=0,blue=0;
        for(int yy=0;yy<samples;++yy)for(int xx=0;xx<samples;++xx){const auto value=source[(y*samples+yy)*width*samples+x*samples+xx];red+=(value>>16)&255;green+=(value>>8)&255;blue+=value&255;}
        reduced[static_cast<std::size_t>(y)*width+x]=((red+count/2)/count<<16)|((green+count/2)/count<<8)|(blue+count/2)/count;}
    format.bmiHeader.biWidth=width;format.bmiHeader.biHeight=-height;SetDIBitsToDevice(dc,glyph.left,glyph.top,width,height,0,0,0,height,reduced.data(),&format,DIB_RGB_COLORS);
    SelectObject(memory,previous_bitmap);DeleteObject(bitmap);DeleteDC(memory);
    const auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,IsWindowEnabled(window)?foreground(window):colors.muted);std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');text.resize(GetWindowTextW(window,text.data(),static_cast<int>(text.size())));
    auto label=client;label.left+=padding;label.right=glyph.left-px(8);const auto ui=SendMessageW(window,WM_QUERYUISTATE,0,0);UINT flags=DT_LEFT|((ui&UISF_HIDEACCEL)?DT_HIDEPREFIX:0);
    if(GetWindowLongPtrW(window,GWL_STYLE)&BS_MULTILINE){auto measured=label;DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,flags|DT_CALCRECT|DT_WORDBREAK);label.top+=std::max<LONG>(0,(label.bottom-label.top-(measured.bottom-measured.top))/2);flags|=DT_WORDBREAK;}else flags|=DT_SINGLELINE|DT_VCENTER;
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&label,flags);if(!(ui&UISF_HIDEFOCUS))paint_keyboard_focus(window,dc,client,colors.muted);RestoreDC(dc,saved);return true;
}
void paint_radio(HWND window, HDC dc) {
    if (!dc) return;
    if(checkbox_button(window)&&GetPropW(window,switch_property)&&paint_switch(window,dc))return;
    const auto saved = SaveDC(dc);
    RECT client{}; GetClientRect(window, &client);
    FillRect(dc, &client, brush(static_cast<Surface>(surface_index(window))));
    const auto colors = palette(window);
    const bool disabled = !IsWindowEnabled(window);
    const auto state = SendMessageW(window,BM_GETCHECK,0,0);
    const bool checked = state == BST_CHECKED;
    const bool checkbox = checkbox_button(window);
    const auto ink = disabled ? (colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.muted) : foreground(window);
    // Android AppearanceUi keys the tint on checked state, not enabled state.
    // A solved, disabled choice therefore keeps the route's red/green circle.
    const auto unchecked = colors.high_contrast ? colors.muted : checkbox ? colors.accent : RGB(120,124,134);
    const auto edge = colors.high_contrast && disabled ? ink : (checked ? colors.accent : unchecked);
    const auto dpi = GetDpiForWindow(window);
    const auto scale = [dpi](int value) { return std::max(1,MulDiv(value,static_cast<int>(dpi ? dpi : 96),96)); };
    const auto padding = scale(3);
    const auto width = static_cast<int>(client.right - client.left);
    const auto height = static_cast<int>(client.bottom - client.top);
    const auto diameter = std::min({scale(20),std::max(1,width - 2 * padding),std::max(1,height - 2 * padding)});
    const bool right = (GetWindowLongPtrW(window,GWL_STYLE) & BS_LEFTTEXT) != 0;
    const auto left = right ? client.right - padding - diameter : padding;
    const auto top = (client.bottom - diameter) / 2;
    RECT glyph{left,top,left + diameter,top + diameter};
    // Supersample only the small glyph. Text keeps ClearType and native radio
    // behavior, while the circle no longer has a pixelated GDI outline.
    constexpr int supersampling=4;
    const auto glyph_dc=CreateCompatibleDC(dc);
    BITMAPINFO sample_format{};sample_format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    sample_format.bmiHeader.biWidth=diameter*supersampling;sample_format.bmiHeader.biHeight=-diameter*supersampling;
    sample_format.bmiHeader.biPlanes=1;sample_format.bmiHeader.biBitCount=32;sample_format.bmiHeader.biCompression=BI_RGB;
    void* sample_pixels=nullptr;
    const auto bitmap=CreateDIBSection(dc,&sample_format,DIB_RGB_COLORS,&sample_pixels,nullptr,0);
    const auto old_bitmap=SelectObject(glyph_dc,bitmap);
    const RECT sample{0,0,diameter*supersampling,diameter*supersampling};
    FillRect(glyph_dc,&sample,brush(static_cast<Surface>(surface_index(window))));
    const auto border = CreatePen(PS_SOLID,scale(2)*supersampling,edge);
    const auto previous_pen = SelectObject(glyph_dc,border);
    const auto previous_brush = SelectObject(glyph_dc,brush(static_cast<Surface>(surface_index(window))));
    const int sample_end=diameter*supersampling-supersampling;
    if (checkbox) RoundRect(glyph_dc,supersampling,supersampling,sample_end,sample_end,scale(3)*supersampling,scale(3)*supersampling);
    else Ellipse(glyph_dc,supersampling,supersampling,sample_end,sample_end);
    if (checked || state == BST_INDETERMINATE) {
        const auto center = CreateSolidBrush(colors.high_contrast && disabled ? ink : colors.accent);
        SelectObject(glyph_dc,center); SelectObject(glyph_dc,GetStockObject(NULL_PEN));
        const auto inset = std::max(supersampling,diameter*supersampling / 4);
        if (checkbox) {
            RoundRect(glyph_dc,supersampling,supersampling,sample_end,sample_end,scale(3)*supersampling,scale(3)*supersampling);
            const auto check = CreatePen(PS_SOLID,scale(2)*supersampling,accent_foreground(window));
            SelectObject(glyph_dc,check);const int size=diameter*supersampling;
            if (state == BST_INDETERMINATE) { MoveToEx(glyph_dc,inset,size/2,nullptr); LineTo(glyph_dc,size-inset,size/2); }
            else { MoveToEx(glyph_dc,size/5,size/2,nullptr);LineTo(glyph_dc,size*2/5,size*3/4);LineTo(glyph_dc,size*4/5,size/4); }
            SelectObject(glyph_dc,border);DeleteObject(check);
        } else Ellipse(glyph_dc,inset,inset,diameter*supersampling-inset,diameter*supersampling-inset);
        SelectObject(glyph_dc,previous_brush); DeleteObject(center);
    } else SelectObject(glyph_dc,previous_brush);
    SelectObject(glyph_dc,previous_pen); DeleteObject(border);
    if(sample_pixels){
        // HALFTONE StretchBlt may quantize even opaque stroke colors according
        // to the display's palette. Average exact RGB coverage instead, so the
        // selected circle retains the same route tint as its Android drawable.
        GdiFlush();const auto* samples=static_cast<const std::uint32_t*>(sample_pixels);
        std::vector<std::uint32_t> reduced(static_cast<std::size_t>(diameter)*diameter);
        constexpr unsigned sample_count=supersampling*supersampling;
        for(int y=0;y<diameter;++y)for(int x=0;x<diameter;++x){
            unsigned red=0,green=0,blue=0;
            for(int sy=0;sy<supersampling;++sy)for(int sx=0;sx<supersampling;++sx){
                const auto value=samples[(y*supersampling+sy)*diameter*supersampling+x*supersampling+sx];
                red+=(value>>16)&255;green+=(value>>8)&255;blue+=value&255;
            }
            reduced[static_cast<std::size_t>(y)*diameter+x]=((red+sample_count/2)/sample_count<<16)
                |((green+sample_count/2)/sample_count<<8)|(blue+sample_count/2)/sample_count;
        }
        auto reduced_format=sample_format;reduced_format.bmiHeader.biWidth=diameter;reduced_format.bmiHeader.biHeight=-diameter;
        SetDIBitsToDevice(dc,glyph.left,glyph.top,diameter,diameter,0,0,0,diameter,reduced.data(),&reduced_format,DIB_RGB_COLORS);
    }
    SelectObject(glyph_dc,old_bitmap);DeleteObject(bitmap);DeleteDC(glyph_dc);
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
    if((ui_state&UISF_HIDEFOCUS)==0)paint_keyboard_focus(window,dc,text.empty()?glyph:client,colors.muted);
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
    std::array<COLORREF,surface_count> colors{};
    std::array<HBRUSH,surface_count> handles{};
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
UINT scroll_paint_message(){
    static const UINT message=RegisterWindowMessageW(L"LGAED.ReaderTheme.ScrollbarPaint");
    return message;
}
void sync_scrollbar_theme(HWND window){
    if(!applied(window)||!rich_edit(window))return;
    const bool native=palette(window).high_contrast;
    const ULONG_PTR wanted=native?2:1;
    const auto previous=reinterpret_cast<ULONG_PTR>(GetPropW(window,scroll_theme_policy_property));
    if(previous==wanted)return;
    // SetWindowTheme sends WM_THEMECHANGED synchronously. Publish the policy
    // first so its nested nonclient paint cannot enter this transition again.
    if(!SetPropW(window,scroll_theme_policy_property,reinterpret_cast<HANDLE>(wanted)))return;
    if(native&&!previous)return;
    // RichEdit already uses our source and scrollbar skin. Its visual-style
    // hover animation otherwise paints a second, light scrollbar over it even
    // after the native tracking loop has finished. Keep USER's actual bars,
    // hit testing and accessibility; restore Windows styling in high contrast.
    if(FAILED(SetWindowTheme(window,native?nullptr:L"",native?nullptr:L""))){
        if(previous)SetPropW(window,scroll_theme_policy_property,reinterpret_cast<HANDLE>(previous));
        else RemovePropW(window,scroll_theme_policy_property);
    }
}
void queue_scrollbar_paint(HWND window){
    if(!IsWindow(window)||!applied(window)||palette(window).high_contrast||GetPropW(window,scroll_paint_pending_property))return;
    const auto message=scroll_paint_message();if(!message)return;
    if(SetPropW(window,scroll_paint_pending_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)))&&
        !PostMessageW(window,message,0,0))RemovePropW(window,scroll_paint_pending_property);
}
LRESULT CALLBACK scroll_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    constexpr wchar_t hidden_frame_property[]=L"lgaed.scrollbar-frame-hidden";
    if(message==WM_NCDESTROY){RemovePropW(window,hidden_frame_property);RemovePropW(window,scroll_paint_pending_property);RemovePropW(window,scroll_theme_policy_property);RemoveWindowSubclass(window,scroll_subclass,scroll_subclass_id);return DefSubclassProc(window,message,wp,lp);}
    if(const auto repaint=scroll_paint_message();repaint&&message==repaint){
        if(RemovePropW(window,scroll_paint_pending_property))refresh_scrollbars(window);
        return 0;
    }
    const auto hidden=reinterpret_cast<ULONG_PTR>(GetPropW(window,hidden_frame_property));
    const auto mask=(hidden&1?WS_VSCROLL:0)|(hidden&2?WS_HSCROLL:0);
    if(mask&&message==WM_STYLECHANGING&&wp==static_cast<WPARAM>(GWL_STYLE)&&lp)
        reinterpret_cast<STYLESTRUCT*>(lp)->styleNew&=~mask;
    if(mask&&(message==WM_NCCALCSIZE||message==WM_NCPAINT)) {
        // SetScrollInfo can add WS_*SCROLL and send WM_NCPAINT synchronously,
        // even with redraw=FALSE, before the caller can hide the scrollbar.
        // Clear only explicitly hidden axes before USER reserves/paints them.
        const auto style=GetWindowLongPtrW(window,GWL_STYLE);
        if(style&mask)SetWindowLongPtrW(window,GWL_STYLE,style&~static_cast<LONG_PTR>(mask));
    }
    const auto result=DefSubclassProc(window,message,wp,lp);
    // During native thumb tracking USER paints again after our nested scroll
    // callback returns. A window message reaches the same modal loop after
    // that paint; coalesce it per HWND instead of polling or changing input.
    if((message==WM_HSCROLL||message==WM_VSCROLL)&&!lp)queue_scrollbar_paint(window);
    // USER also redraws the scrollbar when updating/leaving the cursor's hot
    // state and releasing capture, without WM_NCPAINT or a nonclient mouse-up. Repaint
    // after those callbacks too, before the native message returns.
    // There is no timer, input interception or parallel scroll position.
    if(message==WM_NCPAINT||message==WM_PAINT||message==WM_SIZE||message==WM_VSCROLL||message==WM_HSCROLL||
        message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||message==WM_NCMOUSEMOVE||message==WM_NCMOUSELEAVE||message==WM_NCLBUTTONUP||
        message==WM_NCLBUTTONDOWN||message==WM_SETCURSOR||message==WM_CAPTURECHANGED||message==WM_THEMECHANGED||message==WM_SETTINGCHANGE||
        message==WM_STYLECHANGED||message==WM_DPICHANGED_AFTERPARENT||message==WM_SETFONT||
        message==EM_SETSCROLLPOS||message==EM_LINESCROLL||message==EM_SETTARGETDEVICE||
        message==LB_ADDSTRING||message==LB_DELETESTRING||message==LB_RESETCONTENT||
        message==LB_SETITEMHEIGHT||message==LB_SETTOPINDEX){
        refresh_scrollbars(window);
    }else if(message==WM_PRINT&&(lp&PRF_NONCLIENT))paint_scrollbars(window,reinterpret_cast<HDC>(wp));
    return result;
}
}

void prepare_fonts() {
    struct PrivateFonts {
        std::vector<std::wstring> loaded;
        PrivateFonts(){
            // Original AOSP files, byte-identical to the reference LDPlayer:
            // https://android.googlesource.com/platform/external/noto-fonts/+/refs/heads/main/notoserif/
            // Their original copyright/OFL notice is bundled beside them.
            // Android's monospace family is the unmodified Apache-2.0 font at
            // https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/data/fonts/
            // Its separate original notice is DroidSansMono-NOTICE.txt.
            std::wstring module(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
            if(length&&length<module.size())module.resize(length);else module.clear();
            for(const auto name:{L"NotoSerif-Regular.ttf",L"NotoSerif-Italic.ttf",L"DroidSansMono.ttf"}){
                const auto installed=std::filesystem::path(module).parent_path()/L"fonts"/name;
                const auto source=std::filesystem::path(build::source_root)/L"native/assets/fonts"/name;
                for(const auto& candidate:{installed,source}){
                    if(std::filesystem::is_regular_file(candidate)&&AddFontResourceExW(candidate.c_str(),FR_PRIVATE,nullptr)>0){loaded.push_back(candidate.native());break;}
                }
            }
            wchar_t directory[MAX_PATH]{};const auto count=GetWindowsDirectoryW(directory,MAX_PATH);
            if(count==0||count>=MAX_PATH)return;
            // Some Windows installations register "Roboto" against the Bold
            // file only. Loading its existing Regular/Medium/Bold faces with
            // FR_PRIVATE fixes weight selection without touching that registry.
            for(const auto name:{L"Roboto-Regular.ttf",L"Roboto-Medium.ttf",L"Roboto-Bold_2.ttf"}){
                const auto path=std::wstring(directory)+L"\\Fonts\\"+name;
                if(AddFontResourceExW(path.c_str(),FR_PRIVATE,nullptr)>0)loaded.push_back(path);
            }
        }
        ~PrivateFonts(){for(const auto& path:loaded)RemoveFontResourceExW(path.c_str(),FR_PRIVATE,nullptr);}
    };
    static const PrivateFonts fonts;(void)fonts;
}
const wchar_t* math_font_face(){prepare_fonts();return L"Noto Serif";}
const wchar_t* code_font_face(){prepare_fonts();return L"Droid Sans Mono";}
Palette palette() {
    prepare_fonts();
    HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
    const bool high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (high_contrast) {
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_WINDOWTEXT), true};
    }
    return {RGB(16,16,16), RGB(23,23,23), RGB(28,28,28), RGB(37,37,37),
        RGB(218,221,228), RGB(170,174,183), RGB(89,240,252), RGB(74,74,74), false};
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
    // Same enabled Button ink selection as Android AppearanceUi.applyButton.
    const auto brightness=(GetRValue(colors.accent)*299+GetGValue(colors.accent)*587+GetBValue(colors.accent)*114)/1000;
    return brightness>=160?RGB(0,0,0):RGB(255,255,255);
}
COLORREF accent_ink(HWND context,COLORREF surface) {
    const auto colors=palette(context);if(colors.high_contrast)return GetSysColor(COLOR_WINDOWTEXT);
    if(contrast_ratio(colors.accent,surface)>=3.)return colors.accent;
    return contrast_ratio(colors.text,surface)>=contrast_ratio(colors.canvas,surface)?colors.text:colors.canvas;
}
COLORREF unread_color(COLORREF background){
    const int preferred=luminance(background)<.35?122:80;
    const auto gray=[](int channel){return RGB(channel,channel,channel);};
    if(contrast_ratio(gray(preferred),background)>=4.5)return gray(preferred);
    int selected=preferred,distance=256;
    for(int channel=0;channel<=255;++channel)if(contrast_ratio(gray(channel),background)>=4.5&&std::abs(channel-preferred)<distance){selected=channel;distance=std::abs(channel-preferred);}
    return gray(selected);
}
HBRUSH brush(Surface surface) { static Brushes brushes; return brushes.get(surface); }
bool applied(HWND window) { return window && GetPropW(window, surface_property) != nullptr; }
COLORREF completion_background(COLORREF surface) {
    // Alpha 56/255 over the actual local surface, not a layered child HWND.
    // On catalogue RGB(32,33,38) this rounds to the historic RGB(29,78,52).
    constexpr unsigned alpha=56;
    const auto over=[](unsigned source,unsigned destination){return (source*alpha+destination*(255-alpha)+127)/255;};
    return RGB(over(18,GetRValue(surface)),over(238,GetGValue(surface)),over(102,GetBValue(surface)));
}
COLORREF background(HWND window) {
    const auto colors=palette(window);
    const auto surface=applied(window)?color(colors,static_cast<Surface>(surface_index(window))):GetSysColor(COLOR_WINDOW);
    return !colors.high_contrast&&GetPropW(window,completed_property)?completion_background(surface):surface;
}
void completed_control(HWND window,bool completed) {
    if(!IsWindow(window))return;
    if((GetPropW(window,completed_property)!=nullptr)==completed)return;
    if(completed)SetPropW(window,completed_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    else RemovePropW(window,completed_property);
    if(rich_edit(window))SendMessageW(window,EM_SETBKGNDCOLOR,0,background(window));
    InvalidateRect(window,nullptr,FALSE);
}
COLORREF foreground(HWND window) {
    const auto colors=palette(window);
    if(!colors.high_contrast)if(const auto value=reinterpret_cast<ULONG_PTR>(GetPropW(window,text_property)))return static_cast<COLORREF>(value-1);
    return applied(window)?colors.text:GetSysColor(COLOR_WINDOWTEXT);
}
void paint_scrollbars(HWND window,HDC dc){
    if(!dc||!IsWindow(window)||!applied(window))return;
    const auto colors=palette(window);if(colors.high_contrast)return;
    RECT outer{};GetWindowRect(window,&outer);const auto surface=background(window);
    const auto saved=SaveDC(dc);const auto dpi=static_cast<int>(GetDpiForWindow(window));
    const auto scale=[&](int value){return std::max(1,MulDiv(value,dpi,96));};
    const auto fill=[&](RECT bounds,COLORREF ink){const auto handle=CreateSolidBrush(ink);if(handle){FillRect(dc,&bounds,handle);DeleteObject(handle);}};
    RECT vertical{},horizontal{};bool has_vertical=false,has_horizontal=false;
    const auto hidden=reinterpret_cast<ULONG_PTR>(GetPropW(window,L"lgaed.scrollbar-frame-hidden"));
    for(const bool sideways:{false,true}){
        if(hidden&(sideways?2:1))continue;
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
    sync_scrollbar_theme(window);
    if(const auto dc=GetWindowDC(window)){paint_scrollbars(window,dc);ReleaseDC(window,dc);}
}
void set_scrollbar_frame_hidden(HWND window,bool vertical,bool horizontal){
    if(!IsWindow(window))return;
    const ULONG_PTR hidden=(vertical?1:0)|(horizontal?2:0);
    if(hidden)SetPropW(window,L"lgaed.scrollbar-frame-hidden",reinterpret_cast<HANDLE>(hidden));
    else RemovePropW(window,L"lgaed.scrollbar-frame-hidden");
    SetWindowSubclass(window,scroll_subclass,scroll_subclass_id,0);
    if(vertical)ShowScrollBar(window,SB_VERT,FALSE);
    if(horizontal)ShowScrollBar(window,SB_HORZ,FALSE);
}
void apply(HWND window, Surface surface) {
    if (!window) return;
    SetPropW(window, surface_property, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(surface) + 1));
    SetWindowSubclass(window,focus_subclass,focus_subclass_id,0);
    if(progress_bar(window))SetWindowSubclass(window,progress_subclass,progress_subclass_id,0);
    if(trackbar(window))SetWindowSubclass(window,slider_subclass,slider_subclass_id,0);
    if((GetWindowLongPtrW(window,GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL))||rich_edit(window))
        SetWindowSubclass(window,scroll_subclass,scroll_subclass_id,0);
    if (rich_edit(window)) {
        sync_scrollbar_theme(window);
        SendMessageW(window, EM_SETBKGNDCOLOR, 0, background(window));
        if(!(GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY))use_plain_text_input(window);
    }
    if (push_button(window)) {
        const auto original = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE, (original & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW);
    }
    if (radio_button(window) || checkbox_button(window)) SetWindowSubclass(window,radio_subclass,radio_subclass_id,0);
    if (combo_box(window)) {SetWindowSubclass(window,combo_subclass,combo_subclass_id,0);COMBOBOXINFO info{sizeof(info)};if(GetComboBoxInfo(window,&info)&&info.hwndList){const auto popup=surface==Surface::catalog_card?Surface::popup:Surface::card;SetPropW(info.hwndList,surface_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(popup)+1));SetWindowSubclass(info.hwndList,scroll_subclass,scroll_subclass_id,0);}}
    InvalidateRect(window, nullptr, TRUE);
}
void primary_button(HWND button, bool primary) {
    if (primary) SetPropW(button, primary_property, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    else RemovePropW(button, primary_property);
    InvalidateRect(button, nullptr, FALSE);
}
void icon_button(HWND button,bool icon) {
    if(!IsWindow(button))return;
    if(icon)SetPropW(button,icon_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    else RemovePropW(button,icon_property);
    InvalidateRect(button,nullptr,FALSE);
}
void premium_button(HWND button,bool premium) {
    if(!IsWindow(button))return;
    if(premium)SetPropW(button,premium_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    else RemovePropW(button,premium_property);
    InvalidateRect(button,nullptr,FALSE);
}
void switch_button(HWND button,bool is_switch){
    if(!IsWindow(button))return;
    if(is_switch&&checkbox_button(button)){SetPropW(button,switch_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));SetWindowSubclass(button,radio_subclass,radio_subclass_id,0);}
    else RemovePropW(button,switch_property);
    InvalidateRect(button,nullptr,FALSE);
}
void set_text_color(HWND window,std::optional<COLORREF> color) {
    if(!IsWindow(window))return;
    if(color)SetPropW(window,text_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(*color&0xFFFFFF)+1));
    else RemovePropW(window,text_property);
    InvalidateRect(window,nullptr,FALSE);
}
bool keyboard_focus_visible(HWND window) {
    const auto focus=GetFocus();
    return keyboard_focus&&focus&&(focus==window||IsChild(window,focus));
}
void paint_keyboard_focus(HWND window,HDC dc,RECT bounds,COLORREF ink) {
    if(!dc||!keyboard_focus_visible(window))return;
    const int dpi=static_cast<int>(GetDpiForWindow(window));
    const int inset=std::max(1,MulDiv(2,dpi,96));InflateRect(&bounds,-inset,-inset);
    const auto pen=CreatePen(PS_SOLID,std::max(1,MulDiv(1,dpi,96)),ink);
    const auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,inset*4,inset*4);
    SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);
}
bool control_color(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) {
    if (message != WM_CTLCOLOREDIT && message != WM_CTLCOLORSTATIC && message != WM_CTLCOLORBTN
        && message != WM_CTLCOLORLISTBOX && message != WM_CTLCOLORDLG) return false;
    const auto window = reinterpret_cast<HWND>(lparam);
    if (!applied(window)) return false;
    const auto dc = reinterpret_cast<HDC>(wparam);
    SetTextColor(dc, foreground(window)); SetBkColor(dc, background(window)); SetBkMode(dc, OPAQUE);
    if(GetPropW(window,completed_property)&&!palette(window).high_contrast){
        SetDCBrushColor(dc,background(window));result=reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }else result = reinterpret_cast<LRESULT>(brush(static_cast<Surface>(surface_index(window))));
    return true;
}
bool draw_button(const DRAWITEMSTRUCT& item) {
    if (item.CtlType != ODT_BUTTON || !applied(item.hwndItem) || !push_button(item.hwndItem)) return false;
    const auto colors = palette(item.hwndItem);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool icon = GetPropW(item.hwndItem,icon_property) != nullptr;
    const bool premium = GetPropW(item.hwndItem,premium_property) != nullptr;
    const bool completed = GetPropW(item.hwndItem,completed_property) != nullptr;
    const auto parent=GetParent(item.hwndItem);
    const auto parent_fill=applied(parent)?background(parent):colors.canvas;
    const auto icon_fill=background(item.hwndItem);
    auto fill = icon ? icon_fill : disabled ? RGB(72,72,72) : colors.accent;
    auto ink = icon ? colors.text : disabled ? colors.muted : accent_foreground(item.hwndItem);
    if (colors.high_contrast) {
        fill = icon ? icon_fill : !disabled ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE);
        ink = !icon && !disabled ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_BTNTEXT);
    } else if (pressed && !disabled) {
        const auto base=icon?icon_fill:colors.accent;
        fill=RGB((GetRValue(base)*9+255)/10,(GetGValue(base)*9+255)/10,(GetBValue(base)*9+255)/10);
    }
    if (disabled) ink = colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.muted;
    // Completed is distinct from disabled/busy. Keep native disabled click
    // semantics, but do not paint a solved exercise as an unavailable action.
    if(completed&&!icon&&!colors.high_contrast){
        fill=background(item.hwndItem);ink=RGB(105,220,145);
        if(contrast_ratio(ink,fill)<4.5)ink=colors.text;
    }
    const auto saved = SaveDC(item.hDC);
    const int dpi=static_cast<int>(GetDpiForWindow(item.hwndItem));
    const auto px=[&](int value){return std::max(1,MulDiv(value,dpi,96));};
    const auto outside=CreateSolidBrush(icon?icon_fill:parent_fill);FillRect(item.hDC,&item.rcItem,outside);DeleteObject(outside);
    if(!icon || pressed){
        const auto body = CreateSolidBrush(fill);
        const auto old_brush = SelectObject(item.hDC, body);
        const auto old_pen = SelectObject(item.hDC,GetStockObject(NULL_PEN));
        const auto radius=px(icon?24:4)*2;
        RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,radius,radius);
        SelectObject(item.hDC,old_pen);SelectObject(item.hDC,old_brush);DeleteObject(body);
    }
    // The Android "Premium activated" status keeps its gold drawable even
    // when it is non-clickable; disabling a native read-only status must not
    // turn the entitlement into an ordinary grey action.
    if(premium&&!colors.high_contrast){
        // Exact three stops, 45-degree direction, radius and stroke from
        // Android's bg_premium_gold.xml. No single-color approximation.
        const int width=item.rcItem.right-item.rcItem.left,height=item.rcItem.bottom-item.rcItem.top;
        if(width>0&&height>0){
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
            info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            void* pixels=nullptr;const auto bitmap=CreateDIBSection(item.hDC,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            if(bitmap&&pixels){
                auto* data=static_cast<std::uint32_t*>(pixels);
                const double denominator=static_cast<double>(width)*width+static_cast<double>(height)*height;
                for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                    const double t=std::clamp((static_cast<double>(x)*width+static_cast<double>(height-y)*height)/denominator,0.,1.);
                    const auto a=t<.5?RGB(110,83,16):RGB(212,175,55),b=t<.5?RGB(212,175,55):RGB(140,106,18);
                    const double fraction=t<.5?t*2:(t-.5)*2;
                    const auto channel=[&](int start,int end){return static_cast<std::uint32_t>(std::lround(start+(end-start)*fraction));};
                    data[y*width+x]=(channel(GetRValue(a),GetRValue(b))<<16)|(channel(GetGValue(a),GetGValue(b))<<8)|channel(GetBValue(a),GetBValue(b));
                }
                const auto memory=CreateCompatibleDC(item.hDC);const auto before=SelectObject(memory,bitmap);
                const auto clip=CreateRoundRectRgn(item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,px(36),px(36));
                const auto saved_clip=SaveDC(item.hDC);ExtSelectClipRgn(item.hDC,clip,RGN_AND);
                BitBlt(item.hDC,item.rcItem.left,item.rcItem.top,width,height,memory,0,0,SRCCOPY);
                RestoreDC(item.hDC,saved_clip);DeleteObject(clip);SelectObject(memory,before);DeleteDC(memory);
                const auto stroke=CreatePen(PS_SOLID,px(1),RGB(242,220,138));const auto pen_before=SelectObject(item.hDC,stroke);
                const auto brush_before=SelectObject(item.hDC,GetStockObject(NULL_BRUSH));
                RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,px(36),px(36));
                SelectObject(item.hDC,pen_before);SelectObject(item.hDC,brush_before);DeleteObject(stroke);
            }
            if(bitmap)DeleteObject(bitmap);
        }
        ink=RGB(32,22,0);
    }
    const auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    SelectObject(item.hDC, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(item.hDC, ink); SetBkMode(item.hDC, TRANSPARENT);
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item.hwndItem)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(item.hwndItem, text.data(), static_cast<int>(text.size()))));
    auto bounds = item.rcItem;
    const bool action=GetPropW(item.hwndItem,primary_property)!=nullptr;
    InflateRect(&bounds,action?-px(12):-7,action?-px(4):-3);
    if (pressed) OffsetRect(&bounds, 1, 1);
    if(!icon&&!draw_success_label(item.hDC,text,bounds,ink)){
        UINT text_flags=DT_CENTER | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
        if(GetWindowLongPtrW(item.hwndItem,GWL_STYLE)&BS_MULTILINE){
            auto measured=bounds;DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&measured,text_flags | DT_WORDBREAK | DT_CALCRECT);
            bounds.top+=std::max<LONG>(0,(bounds.bottom-bounds.top-(measured.bottom-measured.top))/2);text_flags|=DT_WORDBREAK;
        }else text_flags|=DT_VCENTER | DT_SINGLELINE;
        DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&bounds,text_flags);
    }
    if((item.itemState&ODS_FOCUS)&&(item.itemState&ODS_NOFOCUSRECT)==0)
        paint_keyboard_focus(item.hwndItem,item.hDC,item.rcItem,ink);
    RestoreDC(item.hDC, saved);
    return true;
}
} // namespace lgaed::presentation::reader_theme
