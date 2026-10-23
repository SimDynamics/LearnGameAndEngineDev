#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <oleacc.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace lgaed::presentation;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
LRESULT CALLBACK owner_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_MEASUREITEM && measure_native_menu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lp))) return TRUE;
    if (message == WM_DRAWITEM && draw_native_menu(*reinterpret_cast<DRAWITEMSTRUCT*>(lp))) return TRUE;
    if (message == WM_MENUCHAR) {
        LRESULT result = 0;
        if (native_menu_character(reinterpret_cast<HMENU>(lp), static_cast<wchar_t>(LOWORD(wp)), result)) return result;
    }
    return DefWindowProcW(window, message, wp, lp);
}
struct Owner {
    HWND window = nullptr;
    Owner() {
        WNDCLASSW type{}; type.lpfnWndProc = owner_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LGAED.NativeMenu.TestOwner";
        if (!RegisterClassW(&type)) check(GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Cannot register the native menu test owner");
        window = CreateWindowExW(0, type.lpszClassName, L"Isolated native menu test", WS_OVERLAPPEDWINDOW,
            20, 20, 640, 480, nullptr, nullptr, type.hInstance, nullptr);
        check(window != nullptr, "Cannot create the native menu test owner");
    }
    ~Owner() { if (window) DestroyWindow(window); }
};
struct Menu {
    HMENU handle = CreatePopupMenu();
    Menu() { check(handle != nullptr, "Cannot create a real native popup menu"); }
    ~Menu() { if (handle) DestroyMenu(handle); }
    void append(UINT flags, UINT_PTR command, const wchar_t* text) {
        check(AppendMenuW(handle, flags, command, text) != FALSE, "Cannot append a real native menu item");
    }
};
MENUITEMINFOW information(HMENU menu, UINT position) {
    MENUITEMINFOW value{}; value.cbSize = sizeof(value);
    value.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_SUBMENU;
    check(GetMenuItemInfoW(menu, position, TRUE, &value) != FALSE, "Cannot inspect the real native menu item"); return value;
}
std::wstring caption(HMENU menu, UINT position) {
    MENUITEMINFOW value{}; value.cbSize = sizeof(value); value.fMask = MIIM_STRING;
    check(GetMenuItemInfoW(menu, position, TRUE, &value) != FALSE, "Cannot measure the original native menu caption");
    std::wstring text(static_cast<std::size_t>(value.cch) + 1, L'\0'); value.dwTypeData = text.data(); value.cch = static_cast<UINT>(text.size());
    check(GetMenuItemInfoW(menu, position, TRUE, &value) != FALSE, "Cannot retrieve the original native menu caption");
    text.resize(value.cch); return text;
}
MEASUREITEMSTRUCT measure(HMENU menu, UINT position) {
    const auto actual = information(menu, position);
    MEASUREITEMSTRUCT value{}; value.CtlType = ODT_MENU; value.itemID = actual.wID; value.itemData = actual.dwItemData;
    check(measure_native_menu(value), "The helper did not measure its own real native menu data"); return value;
}

void ids_states_submenus_and_text_remain_native() {
    Owner owner; Menu menu; const auto submenu = CreatePopupMenu();
    check(submenu != nullptr && AppendMenuW(submenu, MF_STRING, 23, L"Français / Español"), "Cannot create the real submenu");
    menu.append(MF_STRING | MF_CHECKED, 11, L"&Open\tCtrl+O");
    menu.append(MF_STRING | MF_GRAYED, 12, L"Unavailable");
    menu.append(MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), L"&More");
    menu.append(MF_SEPARATOR, 0, nullptr);
    MENUITEMINFOW original_data{}; original_data.cbSize = sizeof(original_data); original_data.fMask = MIIM_DATA;
    original_data.dwItemData = 0x12345678;
    check(SetMenuItemInfoW(menu.handle, 0, TRUE, &original_data), "Cannot attach original application menu data");
    const auto guard = prepare_native_menu(menu.handle, owner.window);
    const auto first = information(menu.handle, 0), disabled = information(menu.handle, 1), more = information(menu.handle, 2);
    check(first.wID == 11 && (first.fState & MFS_CHECKED) && disabled.wID == 12 && (disabled.fState & MFS_DISABLED),
        "The visual adapter changed command IDs or enabled/check states");
    check(more.hSubMenu == submenu && GetMenuItemCount(menu.handle) == 4 && information(submenu, 0).wID == 23,
        "The visual adapter replaced native submenu handles or item order");
    check(caption(menu.handle, 0) == L"&Open\tCtrl+O" && caption(submenu, 0) == L"Français / Español",
        "Owner drawing removed the real Unicode strings or shortcut labels");
    if (!guard.styled()) {
        check(reader_theme::palette(owner.window).high_contrast && !(first.fType & MFT_OWNERDRAW),
            "Only system high contrast may deliberately retain standard menu rendering");
        return;
    }
    check((first.fType & MFT_OWNERDRAW) && (information(submenu, 0).fType & MFT_OWNERDRAW),
        "The adapter did not prepare real native items recursively");
    const auto accessibility = reinterpret_cast<const MSAAMENUINFO*>(first.dwItemData);
    check(accessibility->dwMSAASignature == MSAA_MENU_SIG && accessibility->cchWText == 4 &&
        std::wstring(accessibility->pszWText) == L"Open", "MSAA does not expose a stable readable native menu name");
    const auto child_accessibility = reinterpret_cast<const MSAAMENUINFO*>(information(submenu, 0).dwItemData);
    check(std::wstring(child_accessibility->pszWText) == L"Français / Español", "The submenu's accessible Unicode name changed");
}

void dpi_measurement_and_theme_preserve_drawing_state() {
    Owner owner; Menu menu;
    menu.append(MF_STRING | MF_CHECKED, 1, L"&Short");
    menu.append(MF_STRING, 2, L"A substantially longer native menu option\tCtrl+Shift+P");
    menu.append(MF_SEPARATOR, 0, nullptr);
    reader_theme::set_accent(owner.window, RGB(217, 43, 70));
    const auto guard = prepare_native_menu(menu.handle, owner.window); if (!guard.styled()) return;
    const auto short_size = measure(menu.handle, 0), long_size = measure(menu.handle, 1), separator = measure(menu.handle, 2);
    const auto dpi = GetDpiForWindow(owner.window);
    check(short_size.itemHeight >= static_cast<UINT>(MulDiv(48, static_cast<int>(dpi), 96)) &&
        long_size.itemWidth > short_size.itemWidth && separator.itemHeight < short_size.itemHeight,
        "Native menu padding/font/shortcut measurement is not readable or DPI aware");
    NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);
    check(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi)!=FALSE,"Read actual accessibility menu metrics");
    const auto expected_font=CreateFontW(-std::max<LONG>(std::abs(metrics.lfMenuFont.lfHeight),MulDiv(16,static_cast<int>(dpi),96)),
        0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
    check(expected_font!=nullptr,"Create the Android-equivalent 16sp Roboto reference font");
    const auto measure_dc=GetDC(owner.window);const auto old_measure_font=SelectObject(measure_dc,expected_font);
    RECT expected_label{};DrawTextW(measure_dc,L"&Short",-1,&expected_label,DT_CALCRECT|DT_SINGLELINE);
    SelectObject(measure_dc,old_measure_font);ReleaseDC(owner.window,measure_dc);DeleteObject(expected_font);
    check(short_size.itemWidth==static_cast<UINT>(MulDiv(76,static_cast<int>(dpi),96)+expected_label.right),
        "Native popup labels do not use Android's Roboto16sp measurement");
    const auto screen = GetDC(owner.window); check(screen != nullptr, "Cannot acquire the test owner's drawing context");
    const auto dc = CreateCompatibleDC(screen); const auto bitmap = CreateCompatibleBitmap(screen, 640, 200);
    ReleaseDC(owner.window, screen); check(dc && bitmap, "Cannot allocate the isolated native menu drawing surface");
    const auto previous_bitmap = SelectObject(dc, bitmap);
    const auto previous_text = SetTextColor(dc, RGB(1, 2, 3)); const auto original_font = GetCurrentObject(dc, OBJ_FONT);
    DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_MENU; draw.itemID = 1; draw.hwndItem = reinterpret_cast<HWND>(menu.handle);
    draw.itemData = information(menu.handle, 0).dwItemData; draw.hDC = dc; draw.rcItem = {0, 0, 640, 150};
    check(draw_native_menu(draw), "The native menu helper did not draw its real item");
    check(GetPixel(dc, 2, 2) == RGB(48,50,57),
        "The actual popup background differs from Android's explicit #303239 selector surface");
    check(GetTextColor(dc) == RGB(1, 2, 3) && GetCurrentObject(dc, OBJ_FONT) == original_font,
        "Native menu painting changed the caller's DC/font state");
    draw.itemState = ODS_SELECTED | ODS_CHECKED;
    check(draw_native_menu(draw) && GetPixel(dc, 2, 2) != RGB(48,50,57),
        "The real native highlighted item has no visible selection distinction");
    SetTextColor(dc, previous_text); SelectObject(dc, previous_bitmap); DeleteObject(bitmap); DeleteDC(dc);
}

void keyboard_mnemonics_cycle_and_ignore_disabled_items() {
    Owner owner; Menu menu;
    menu.append(MF_STRING, 1, L"&Open"); menu.append(MF_STRING, 2, L"&Other");
    menu.append(MF_STRING | MF_GRAYED, 3, L"&Only unavailable");
    menu.append(MF_STRING, 4, L"Save && close"); menu.append(MF_STRING, 5, L"&Close");
    const auto guard = prepare_native_menu(menu.handle, owner.window); if (!guard.styled()) return;
    LRESULT result = -1;
    check(native_menu_character(menu.handle, L'o', result) && LOWORD(result) == 0 && HIWORD(result) == MNC_SELECT,
        "Duplicate native menu mnemonics did not select the first enabled match");
    MENUITEMINFOW highlight{}; highlight.cbSize = sizeof(highlight); highlight.fMask = MIIM_STATE; highlight.fState = MFS_HILITE;
    check(SetMenuItemInfoW(menu.handle, 0, TRUE, &highlight), "Cannot highlight the current native menu item");
    check(native_menu_character(menu.handle, L'O', result) && LOWORD(result) == 1 && HIWORD(result) == MNC_SELECT,
        "Duplicate mnemonic cycling did not move after the current native highlight");
    check(native_menu_character(menu.handle, L'c', result) && LOWORD(result) == 4 && HIWORD(result) == MNC_EXECUTE,
        "A unique mnemonic did not execute the original command position");
    check(!native_menu_character(menu.handle, L'&', result), "An escaped ampersand became a keyboard mnemonic");
    const auto routed = SendMessageW(owner.window, WM_MENUCHAR, MAKEWPARAM(L'c', MF_POPUP), reinterpret_cast<LPARAM>(menu.handle));
    check(LOWORD(routed) == 4 && HIWORD(routed) == MNC_EXECUTE,
        "The actual popup owner's WM_MENUCHAR handler lost native keyboard routing");
}

void guard_restores_data_without_losing_live_checks() {
    Owner owner; Menu menu; menu.append(MF_STRING, 51, L"Keep original data");
    MENUITEMINFOW original{}; original.cbSize = sizeof(original); original.fMask = MIIM_DATA; original.dwItemData = 0x76543210;
    check(SetMenuItemInfoW(menu.handle, 0, TRUE, &original), "Cannot install original application item data");
    MENUINFO background{}; background.cbSize = sizeof(background); background.fMask = MIM_BACKGROUND;
    check(GetMenuInfo(menu.handle, &background), "Cannot inspect the initial menu background");
    {
        auto guard = prepare_native_menu(menu.handle, owner.window);
        auto moved = std::move(guard);
        check(!guard.styled() && (moved.styled() || reader_theme::palette(owner.window).high_contrast),
            "Moving the guard invalidated its item-data lifetime");
        CheckMenuItem(menu.handle, 51, MF_BYCOMMAND | MF_CHECKED);
    }
    const auto restored = information(menu.handle, 0);
    MENUINFO restored_background{}; restored_background.cbSize = sizeof(restored_background); restored_background.fMask = MIM_BACKGROUND;
    check(GetMenuInfo(menu.handle, &restored_background) && restored_background.hbrBack == background.hbrBack,
        "Releasing native menu styling left its deleted background brush attached");
    check(!(restored.fType & MFT_OWNERDRAW) && restored.dwItemData == original.dwItemData &&
        (restored.fState & MFS_CHECKED) && restored.wID == 51 && caption(menu.handle, 0) == L"Keep original data",
        "The guard lost original native metadata or reverted a legitimate live check state");
    auto guard = prepare_native_menu(menu.handle, owner.window);
    DestroyMenu(menu.handle); menu.handle = nullptr;
    // Guard destruction after DestroyMenu must not dereference its old HMENU.
}

void foreign_owner_drawing_is_not_hijacked() {
    Owner owner; Menu menu;
    struct ForeignData { MSAAMENUINFO accessible; } foreign{{MSAA_MENU_SIG, 7, const_cast<wchar_t*>(L"Foreign")}};
    menu.append(MF_OWNERDRAW, 71, reinterpret_cast<const wchar_t*>(&foreign));
    const auto before = information(menu.handle, 0); const auto guard = prepare_native_menu(menu.handle, owner.window);
    check(information(menu.handle, 0).dwItemData == before.dwItemData && information(menu.handle, 0).fType == before.fType,
        "Preparing a menu hijacked an existing owner-draw renderer");
    MEASUREITEMSTRUCT measure{}; measure.CtlType = ODT_MENU; measure.itemData = before.dwItemData;
    DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_BUTTON;
    LRESULT result = 99;
    check(!measure_native_menu(measure) && !draw_native_menu(draw) && !native_menu_character(menu.handle, L'f', result) && result == 99,
        "Menu handlers consumed another renderer's controls or messages");
}

void actual_msaa_names_are_available_without_a_custom_accessible_server() {
    Owner owner;
    const auto menubar = CreateMenu(); check(menubar != nullptr, "Cannot create the native accessibility menu");
    Menu popup; popup.append(MF_STRING, 81, L"&Information");
    check(AppendMenuW(menubar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup.handle), L"&Options") && SetMenu(owner.window, menubar),
        "Cannot attach a native menu for MSAA verification");
    popup.handle = nullptr; // The attached menubar owns its submenu.
    {
        const auto guard = prepare_native_menu(menubar, owner.window);
        IAccessible* accessible = nullptr;
        const auto status = AccessibleObjectFromWindow(owner.window, static_cast<DWORD>(OBJID_MENU), IID_IAccessible, reinterpret_cast<void**>(&accessible));
        check(SUCCEEDED(status) && accessible != nullptr, "Windows did not expose its real native menu through MSAA");
        VARIANT child{}; child.vt = VT_I4; child.lVal = 1; BSTR name = nullptr;
        const auto named = accessible->get_accName(child, &name);
        const std::wstring actual = name ? std::wstring(name, SysStringLen(name)) : std::wstring{};
        if (name) SysFreeString(name); accessible->Release();
        check(SUCCEEDED(named) && actual == L"Options", "The actual Windows accessible menu object lost its name");
    }
    SetMenu(owner.window, nullptr); DestroyMenu(menubar);
}
}

int main() {
    const auto initialized = OleInitialize(nullptr);
    if (FAILED(initialized)) { std::cerr << "Cannot initialize MSAA test apartment.\n"; return 1; }
    unsigned passed = 0, failed = 0;
    const auto run = [&](const char* name, auto action) {
        try { action(); ++passed; std::cout << name << ": PASS\n"; }
        catch (const std::exception& error) { ++failed; std::cerr << name << ": FAIL: " << error.what() << '\n'; }
    };
    run("Native menu IDs, strings, states, submenus and MSAA item metadata", ids_states_submenus_and_text_remain_native);
    run("DPI-aware real menu measurement and themed GDI state", dpi_measurement_and_theme_preserve_drawing_state);
    run("Native keyboard mnemonics, duplicate cycling and disabled items", keyboard_mnemonics_cycle_and_ignore_disabled_items);
    run("RAII lifetime, original data and live check preservation", guard_restores_data_without_losing_live_checks);
    run("Existing owner-draw renderers remain independent", foreign_owner_drawing_is_not_hijacked);
    run("Actual Windows MSAA menu names", actual_msaa_names_are_available_without_a_custom_accessible_server);
    OleUninitialize();
    std::cout << passed << " native-menu groups passed; " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}
