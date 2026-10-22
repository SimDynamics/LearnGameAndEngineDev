#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <oleacc.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::presentation {
namespace {
struct AccessibleMenuData {
    // Official MSAA contract: first member, no virtual table. MENUITEMINFO's
    // dwItemData points here, not into an unrelated application object.
    // https://learn.microsoft.com/en-us/windows/win32/api/oleacc/ns-oleacc-msaamenuinfo
    MSAAMENUINFO accessibility{};
};
static_assert(offsetof(AccessibleMenuData, accessibility) == 0);

struct Resources {
    HWND owner = nullptr;
    UINT dpi = 96;
    HFONT font = nullptr, bold_font = nullptr;
    HBRUSH background = nullptr;
    ~Resources() {
        if (font) DeleteObject(font);
        if (bold_font) DeleteObject(bold_font);
        if (background) DeleteObject(background);
    }
    [[nodiscard]] int scale(int value) const { return std::max(1, MulDiv(value, static_cast<int>(dpi), 96)); }
};
struct Item {
    AccessibleMenuData data;
    Resources* resources = nullptr;
    HMENU menu = nullptr;
    std::wstring text, accessible_text;
    ULONG_PTR original_data = 0;
    UINT original_type = 0;
};
thread_local std::map<ULONG_PTR, Item*> active_items;

ULONG_PTR item_data(const Item& item) { return reinterpret_cast<ULONG_PTR>(&item.data); }
Item* find_item(ULONG_PTR data) {
    const auto found = active_items.find(data);
    return found == active_items.end() ? nullptr : found->second;
}
std::wstring accessible_name(std::wstring_view text) {
    std::wstring result;
    for (std::size_t i = 0; i < text.size() && text[i] != L'\t'; ++i) {
        if (text[i] != L'&') result += text[i];
        else if (i + 1 < text.size() && text[i + 1] == L'&') { result += L'&'; ++i; }
    }
    return result;
}
MENUITEMINFOW item_info(HMENU menu, UINT position) {
    MENUITEMINFOW value{}; value.cbSize = sizeof(value);
    value.fMask = MIIM_ID | MIIM_DATA | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_CHECKMARKS | MIIM_BITMAP;
    if (!GetMenuItemInfoW(menu, position, TRUE, &value)) throw std::runtime_error("Cannot read a native menu item.");
    return value;
}
std::wstring item_text(HMENU menu, UINT position) {
    MENUITEMINFOW value{}; value.cbSize = sizeof(value); value.fMask = MIIM_STRING;
    if (!GetMenuItemInfoW(menu, position, TRUE, &value)) throw std::runtime_error("Cannot read native menu text.");
    if (value.cch > 1000000) throw std::runtime_error("Native menu text exceeds the safety limit.");
    std::wstring result(static_cast<std::size_t>(value.cch) + 1, L'\0');
    value.dwTypeData = result.data(); value.cch = static_cast<UINT>(result.size());
    if (!GetMenuItemInfoW(menu, position, TRUE, &value)) throw std::runtime_error("Cannot read native menu text.");
    result.resize(value.cch); return result;
}
std::optional<UINT> current_position(const Item& item) {
    const auto count = GetMenuItemCount(item.menu);
    for (int index = 0; index < count; ++index) {
        MENUITEMINFOW current{}; current.cbSize = sizeof(current); current.fMask = MIIM_DATA;
        if (GetMenuItemInfoW(item.menu, static_cast<UINT>(index), TRUE, &current) && current.dwItemData == item_data(item))
            return static_cast<UINT>(index);
    }
    return std::nullopt;
}
std::pair<std::wstring_view, std::wstring_view> text_parts(const Item& item) {
    const auto text = std::wstring_view(item.text); const auto tab = text.find(L'\t');
    return {text.substr(0, tab), tab == text.npos ? std::wstring_view{} : text.substr(tab + 1)};
}
COLORREF blend(COLORREF first, COLORREF second) {
    return RGB((GetRValue(first) + 3 * GetRValue(second)) / 4,
        (GetGValue(first) + 3 * GetGValue(second)) / 4, (GetBValue(first) + 3 * GetBValue(second)) / 4);
}
void fill(HDC dc, const RECT& area, COLORREF color) {
    const auto brush = CreateSolidBrush(color); if (!brush) return;
    FillRect(dc, &area, brush); DeleteObject(brush);
}
void line(HDC dc, POINT from, POINT to, COLORREF color, int width) {
    const auto pen = CreatePen(PS_SOLID, width, color); if (!pen) return;
    const auto previous = SelectObject(dc, pen);
    MoveToEx(dc, from.x, from.y, nullptr); LineTo(dc, to.x, to.y);
    SelectObject(dc, previous); DeleteObject(pen);
}
void draw_check(HDC dc, RECT area, bool radio, COLORREF ink, int thickness) {
    if (radio) {
        InflateRect(&area, -(area.right - area.left) / 4, -(area.bottom - area.top) / 4);
        const auto brush = CreateSolidBrush(ink); const auto previous_brush = SelectObject(dc, brush);
        const auto previous_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, area.left, area.top, area.right, area.bottom);
        SelectObject(dc, previous_pen); SelectObject(dc, previous_brush); DeleteObject(brush);
    } else {
        const int width = area.right - area.left, height = area.bottom - area.top;
        const auto pen = CreatePen(PS_SOLID, thickness, ink); const auto previous = SelectObject(dc, pen);
        const POINT points[]{{area.left + width / 6, area.top + height / 2},
            {area.left + width * 2 / 5, area.top + height * 3 / 4}, {area.left + width * 5 / 6, area.top + height / 4}};
        Polyline(dc, points, static_cast<int>(std::size(points)));
        SelectObject(dc, previous); DeleteObject(pen);
    }
}
bool draw_bitmap(HDC dc, HBITMAP bitmap, RECT area, bool disabled) {
    if (!bitmap) return false;
    BITMAP dimensions{};
    if (GetObjectW(bitmap, sizeof(dimensions), &dimensions) != sizeof(dimensions) || dimensions.bmWidth <= 0 || dimensions.bmHeight <= 0)
        return false; // Predefined HBMMENU_* values remain system-owned.
    const int width = std::min(static_cast<int>(area.right - area.left), static_cast<int>(dimensions.bmWidth));
    const int height = std::min(static_cast<int>(area.bottom - area.top), static_cast<int>(dimensions.bmHeight));
    return DrawStateW(dc, nullptr, nullptr, reinterpret_cast<LPARAM>(bitmap), 0,
        area.left + (area.right - area.left - width) / 2, area.top + (area.bottom - area.top - height) / 2,
        width, height, DST_BITMAP | (disabled ? DSS_DISABLED : DSS_NORMAL)) != FALSE;
}
bool mnemonic_matches(std::wstring_view text, wchar_t character) {
    for (std::size_t i = 0; i + 1 < text.size() && text[i] != L'\t'; ++i) {
        if (text[i] != L'&') continue;
        if (text[i + 1] == L'&') { ++i; continue; }
        return CompareStringOrdinal(&text[i + 1], 1, &character, 1, TRUE) == CSTR_EQUAL;
    }
    return false;
}
}

struct NativeMenuStyle::Impl {
    struct Surface { HMENU menu; HBRUSH original; };
    Resources resources;
    std::vector<std::unique_ptr<Item>> items;
    std::vector<Surface> surfaces;
    std::set<HMENU> visited;

    ~Impl() {
        for (const auto& item : items) {
            if (const auto position = current_position(*item)) {
                MENUITEMINFOW restore{}; restore.cbSize = sizeof(restore); restore.fMask = MIIM_DATA | MIIM_FTYPE;
                restore.dwItemData = item->original_data;
                // Preserve any live check/radio/column state while removing only
                // the owner-draw bit installed by this guard.
                MENUITEMINFOW current{}; current.cbSize = sizeof(current); current.fMask = MIIM_FTYPE;
                if (GetMenuItemInfoW(item->menu, *position, TRUE, &current)) {
                    restore.fType = current.fType & ~MFT_OWNERDRAW;
                    SetMenuItemInfoW(item->menu, *position, TRUE, &restore);
                }
            }
            active_items.erase(item_data(*item));
        }
        for (const auto& surface : surfaces) {
            MENUINFO current{}; current.cbSize = sizeof(current); current.fMask = MIM_BACKGROUND;
            if (GetMenuInfo(surface.menu, &current) && current.hbrBack == resources.background) {
                current.hbrBack = surface.original; SetMenuInfo(surface.menu, &current);
            }
        }
    }
    void prepare(HMENU menu, unsigned depth = 0) {
        if (depth > 32 || !visited.insert(menu).second) throw std::runtime_error("Native menu hierarchy is invalid.");
        const auto count = GetMenuItemCount(menu);
        if (count < 0 || items.size() + static_cast<std::size_t>(count) > 10000)
            throw std::runtime_error("Native menu hierarchy exceeds the safety limit.");
        MENUINFO info{}; info.cbSize = sizeof(info); info.fMask = MIM_BACKGROUND;
        if (!GetMenuInfo(menu, &info)) throw std::runtime_error("Cannot read the native menu background.");
        surfaces.push_back({menu, info.hbrBack}); info.hbrBack = resources.background;
        if (!SetMenuInfo(menu, &info)) throw std::runtime_error("Cannot prepare the native menu background.");
        for (int index = 0; index < count; ++index) {
            const auto position = static_cast<UINT>(index); const auto original = item_info(menu, position);
            if (original.hSubMenu) prepare(original.hSubMenu, depth + 1);
            if (original.fType & MFT_OWNERDRAW) continue;
            // A bitmap-only or callback menu belongs to its existing renderer.
            if (original.hbmpItem == HBMMENU_CALLBACK || (original.fType & MFT_BITMAP)) continue;
            auto item = std::make_unique<Item>(); item->resources = &resources; item->menu = menu;
            item->text = item_text(menu, position); item->accessible_text = accessible_name(item->text);
            item->original_data = original.dwItemData; item->original_type = original.fType;
            item->data.accessibility = {MSAA_MENU_SIG, static_cast<DWORD>(item->accessible_text.size()), item->accessible_text.data()};
            MENUITEMINFOW change{}; change.cbSize = sizeof(change); change.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
            change.fType = original.fType | MFT_OWNERDRAW; change.dwItemData = item_data(*item);
            change.dwTypeData = item->text.data(); change.cch = static_cast<UINT>(item->text.size());
            const auto address = item_data(*item);
            // Allocate/register first so an exception cannot strand a native
            // item pointing to freed data. The destructor restores partial work.
            items.push_back(std::move(item)); active_items.emplace(address, items.back().get());
            if (!SetMenuItemInfoW(menu, position, TRUE, &change)) throw std::runtime_error("Cannot prepare a native menu item.");
        }
    }
};

NativeMenuStyle::NativeMenuStyle() noexcept = default;
NativeMenuStyle::~NativeMenuStyle() = default;
NativeMenuStyle::NativeMenuStyle(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
NativeMenuStyle::NativeMenuStyle(NativeMenuStyle&&) noexcept = default;
NativeMenuStyle& NativeMenuStyle::operator=(NativeMenuStyle&&) noexcept = default;
bool NativeMenuStyle::styled() const noexcept { return impl_ && !impl_->items.empty(); }

NativeMenuStyle prepare_native_menu(HMENU menu, HWND owner) {
    if (!IsMenu(menu) || !IsWindow(owner)) throw std::invalid_argument("A native menu needs a live menu and owner.");
    if (reader_theme::palette(owner).high_contrast) return {};
    auto implementation = std::make_unique<NativeMenuStyle::Impl>();
    auto& resources = implementation->resources; resources.owner = owner;
    resources.dpi = GetDpiForWindow(owner); if (!resources.dpi) resources.dpi = 96;
    NONCLIENTMETRICSW metrics{}; metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, resources.dpi))
        throw std::runtime_error("Cannot read the system menu font.");
    auto font = metrics.lfMenuFont;
    font.lfHeight = -std::max<LONG>(std::abs(font.lfHeight), MulDiv(16,static_cast<int>(resources.dpi),72));
    resources.font = CreateFontIndirectW(&font); font.lfWeight = std::max<LONG>(font.lfWeight, FW_BOLD);
    resources.bold_font = CreateFontIndirectW(&font);
    resources.background = CreateSolidBrush(reader_theme::palette(owner).card);
    if (!resources.font || !resources.bold_font || !resources.background) throw std::runtime_error("Cannot create native menu drawing resources.");
    implementation->prepare(menu);
    return NativeMenuStyle(std::move(implementation));
}

bool measure_native_menu(MEASUREITEMSTRUCT& measured) {
    if (measured.CtlType != ODT_MENU) return false;
    const auto item = find_item(measured.itemData); if (!item) return false;
    const auto& resources = *item->resources;
    if (item->original_type & MFT_SEPARATOR) {
        measured.itemWidth = static_cast<UINT>(resources.scale(48)); measured.itemHeight = static_cast<UINT>(resources.scale(12)); return true;
    }
    const auto position = current_position(*item); if (!position) return false;
    const auto current = item_info(item->menu, *position);
    const auto dc = GetDC(resources.owner); if (!dc) return false;
    const auto saved = SaveDC(dc);
    SelectObject(dc, (current.fState & MFS_DEFAULT) ? resources.bold_font : resources.font);
    auto [label, shortcut] = text_parts(*item);
    RECT label_size{}, shortcut_size{};
    DrawTextW(dc, label.data(), static_cast<int>(label.size()), &label_size, DT_CALCRECT | DT_SINGLELINE);
    if (!shortcut.empty()) DrawTextW(dc, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    measured.itemWidth = static_cast<UINT>(resources.scale(12 + 24 + 8 + 20 + 12) + label_size.right +
        (shortcut.empty() ? 0 : resources.scale(24) + shortcut_size.right));
    measured.itemHeight = static_cast<UINT>(std::max(resources.scale(44),
        static_cast<int>(std::max(label_size.bottom, shortcut_size.bottom)) + resources.scale(20)));
    RestoreDC(dc, saved); ReleaseDC(resources.owner, dc); return true;
}

bool draw_native_menu(const DRAWITEMSTRUCT& drawn) {
    if (drawn.CtlType != ODT_MENU || !drawn.hDC) return false;
    const auto item = find_item(drawn.itemData); if (!item) return false;
    const auto& resources = *item->resources;
    const auto position = current_position(*item); if (!position) return false;
    const auto current = item_info(item->menu, *position);
    const auto colors = reader_theme::palette(resources.owner);
    const bool disabled = (drawn.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0 || (current.fState & MFS_DISABLED) != 0;
    const bool selected = (drawn.itemState & ODS_SELECTED) != 0;
    const auto background = colors.high_contrast ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_MENU) :
        (selected ? blend(colors.accent, colors.card) : colors.card);
    const auto ink = colors.high_contrast ? GetSysColor(disabled ? COLOR_GRAYTEXT : (selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT)) :
        (disabled ? colors.muted : colors.text);
    const auto saved = SaveDC(drawn.hDC); fill(drawn.hDC, drawn.rcItem, background);
    if (current.fType & MFT_SEPARATOR) {
        const auto middle = (drawn.rcItem.top + drawn.rcItem.bottom) / 2;
        line(drawn.hDC, {drawn.rcItem.left + resources.scale(12), middle},
            {drawn.rcItem.right - resources.scale(12), middle}, colors.outline, resources.scale(1));
        RestoreDC(drawn.hDC, saved); return true;
    }
    SelectObject(drawn.hDC, (drawn.itemState & ODS_DEFAULT) ? resources.bold_font : resources.font);
    SetBkMode(drawn.hDC, TRANSPARENT); SetTextColor(drawn.hDC, ink);
    const auto [label, shortcut] = text_parts(*item);
    const bool rtl = (current.fType & MFT_RIGHTORDER) != 0 || (GetWindowLongPtrW(resources.owner, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    auto label_area = drawn.rcItem;
    label_area.left += resources.scale(rtl ? 12 + 20 : 12 + 24 + 8);
    label_area.right -= resources.scale(rtl ? 12 + 24 + 8 : 12 + 20);
    if (!shortcut.empty()) {
        RECT shortcut_size{}; DrawTextW(drawn.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        auto shortcut_area = label_area;
        if (rtl) { shortcut_area.right = shortcut_area.left + shortcut_size.right; label_area.left = shortcut_area.right + resources.scale(24); }
        else { shortcut_area.left = shortcut_area.right - shortcut_size.right; label_area.right = shortcut_area.left - resources.scale(24); }
        DrawTextW(drawn.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_area,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (rtl ? DT_LEFT | DT_RTLREADING : DT_RIGHT));
    }
    DrawTextW(drawn.hDC, label.data(), static_cast<int>(label.size()), &label_area,
        DT_SINGLELINE | DT_VCENTER | ((drawn.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0) | (rtl ? DT_RIGHT | DT_RTLREADING : DT_LEFT));
    RECT check_area = drawn.rcItem;
    const int size = resources.scale(18);
    check_area.top += (check_area.bottom - check_area.top - size) / 2; check_area.bottom = check_area.top + size;
    check_area.left = rtl ? check_area.right - resources.scale(12 + 24) : check_area.left + resources.scale(12);
    check_area.right = check_area.left + size;
    const bool checked = (drawn.itemState & ODS_CHECKED) != 0 || (current.fState & MFS_CHECKED) != 0;
    const auto bitmap = checked ? current.hbmpChecked : current.hbmpUnchecked;
    if (!draw_bitmap(drawn.hDC, bitmap ? bitmap : current.hbmpItem, check_area, disabled) && checked)
        draw_check(drawn.hDC, check_area, (current.fType & MFT_RADIOCHECK) != 0,
            disabled || colors.high_contrast ? ink : reader_theme::accent_ink(resources.owner,background), resources.scale(2));
    // Windows retains ownership of submenu opening/arrows and menu scrolling.
    // No replacement popup window, keyboard loop or focus management is added.
    RestoreDC(drawn.hDC, saved); return true;
}

bool native_menu_character(HMENU menu, wchar_t character, LRESULT& result) {
    std::vector<UINT> matches; std::optional<UINT> highlighted;
    const auto count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index) {
        const auto current = item_info(menu, static_cast<UINT>(index));
        const auto item = find_item(current.dwItemData);
        if (!item || (current.fType & MFT_SEPARATOR) || (current.fState & MFS_DISABLED)) continue;
        if (current.fState & MFS_HILITE) highlighted = static_cast<UINT>(index);
        if (mnemonic_matches(item->text, character)) matches.push_back(static_cast<UINT>(index));
    }
    if (matches.empty()) return false;
    const auto next = highlighted ? std::ranges::find_if(matches, [&](UINT position) { return position > *highlighted; }) : matches.begin();
    const auto selected = next == matches.end() ? matches.front() : *next;
    result = MAKELRESULT(selected, matches.size() == 1 ? MNC_EXECUTE : MNC_SELECT); return true;
}

} // namespace lgaed::presentation
