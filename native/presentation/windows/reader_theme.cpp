#include "presentation/windows/reader_theme.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>

namespace lgaed::presentation::reader_theme {
namespace {
constexpr wchar_t surface_property[] = L"LGAED.ReaderSurface";
constexpr wchar_t primary_property[] = L"LGAED.ReaderPrimaryButton";
constexpr UINT_PTR radio_subclass_id = 0x4C475244;
constexpr UINT_PTR combo_subclass_id = 0x4C47434F;
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
bool rich_edit(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return _wcsnicmp(name, L"RichEdit", 8) == 0;
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
bool combo_box(HWND window) {wchar_t name[32]{};GetClassNameW(window,name,32);return _wcsicmp(name,L"COMBOBOX")==0
    &&(GetWindowLongPtrW(window,GWL_STYLE)&3)==CBS_DROPDOWNLIST;}
LRESULT CALLBACK combo_subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,combo_subclass,combo_subclass_id);return DefSubclassProc(window,message,wp,lp);}
    if(message==WM_MOUSEWHEEL&&!SendMessageW(window,CB_GETDROPPEDSTATE,0,0))return SendMessageW(GetParent(window),message,wp,lp);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT){PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wp);const auto saved=SaveDC(dc);
        RECT bounds{};GetClientRect(window,&bounds);FillRect(dc,&bounds,brush(Surface::card));const auto colors=palette();const auto dpi=GetDpiForWindow(window);const int inset=MulDiv(18,static_cast<int>(dpi),96);
        auto text_area=bounds;text_area.left+=inset;text_area.right-=inset*3;
        const auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(window)?colors.text:colors.muted);
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(window,text.data(),static_cast<int>(text.size()))));
        auto measured=text_area;DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);text_area.top+=(bounds.bottom-(measured.bottom-measured.top))/2;
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&text_area,DT_WORDBREAK|DT_NOPREFIX);
        const auto x=bounds.right-inset*2,y=bounds.bottom/2;const int size=std::max(2,inset/3);POINT arrow[]{{x-size,y-size/2},{x+size,y-size/2},{x,y+size/2}};
        const auto pen=CreatePen(PS_SOLID,1,colors.muted),fill=CreateSolidBrush(colors.muted);const auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,fill);Polygon(dc,arrow,3);SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);DeleteObject(fill);
        if(GetFocus()==window){auto focus=bounds;InflateRect(&focus,-3,-3);DrawFocusRect(dc,&focus);}RestoreDC(dc,saved);if(message==WM_PAINT)EndPaint(window,&paint);return 0;}
    const auto result=DefSubclassProc(window,message,wp,lp);if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==CB_SETCURSEL||message==WM_ENABLE||message==WM_SETTEXT)InvalidateRect(window,nullptr,FALSE);return result;
}
void paint_radio(HWND window, HDC dc) {
    if (!dc) return;
    const auto saved = SaveDC(dc);
    RECT client{}; GetClientRect(window, &client);
    FillRect(dc, &client, brush(static_cast<Surface>(surface_index(window))));
    const auto colors = palette();
    const bool disabled = !IsWindowEnabled(window);
    const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const auto ink = disabled ? (colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.muted) : colors.text;
    const auto edge = disabled ? ink : (checked ? colors.accent : colors.muted);
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
    Ellipse(dc,glyph.left,glyph.top,glyph.right,glyph.bottom);
    if (checked) {
        const auto center = CreateSolidBrush(disabled ? ink : colors.accent);
        SelectObject(dc,center); SelectObject(dc,GetStockObject(NULL_PEN));
        const auto inset = std::max(1,diameter / 4);
        Ellipse(dc,glyph.left + inset,glyph.top + inset,glyph.right - inset,glyph.bottom - inset);
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
    if (!text.empty()) DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | ((ui_state & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));
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
    if (!radio_button(window)) return DefSubclassProc(window,message,wparam,lparam);
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
        RGB(222,223,225), RGB(158,163,173), RGB(77,211,224), RGB(70,74,83), false};
}
HBRUSH brush(Surface surface) { static Brushes brushes; return brushes.get(surface); }
bool applied(HWND window) { return window && GetPropW(window, surface_property) != nullptr; }
COLORREF background(HWND window) {
    return applied(window) ? color(palette(), static_cast<Surface>(surface_index(window))) : GetSysColor(COLOR_WINDOW);
}
COLORREF foreground(HWND window) { return applied(window) ? palette().text : GetSysColor(COLOR_WINDOWTEXT); }
void apply(HWND window, Surface surface) {
    if (!window) return;
    SetPropW(window, surface_property, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(surface) + 1));
    if (rich_edit(window)) SendMessageW(window, EM_SETBKGNDCOLOR, 0, background(window));
    if (push_button(window)) {
        const auto original = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE, (original & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW);
    }
    if (radio_button(window)) SetWindowSubclass(window,radio_subclass,radio_subclass_id,0);
    if (combo_box(window)) {SetWindowSubclass(window,combo_subclass,combo_subclass_id,0);COMBOBOXINFO info{sizeof(info)};if(GetComboBoxInfo(window,&info)&&info.hwndList)SetPropW(info.hwndList,surface_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(Surface::card)+1));}
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
    const auto colors = palette();
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = GetPropW(item.hwndItem, primary_property) != nullptr;
    auto fill = primary && !disabled ? colors.accent : colors.field;
    auto ink = primary && !disabled ? colors.canvas : colors.text;
    if (colors.high_contrast) {
        fill = primary && !disabled ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE);
        ink = primary && !disabled ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_BTNTEXT);
    } else if (pressed && !disabled) {
        fill = primary ? RGB(67,204,216) : RGB(53,57,65);
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
    DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &bounds,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        auto focus = item.rcItem; InflateRect(&focus, -4, -4); DrawFocusRect(item.hDC, &focus);
    }
    RestoreDC(item.hDC, saved);
    return true;
}
} // namespace lgaed::presentation::reader_theme
