#pragma once

#include <windows.h>
#include <memory>

namespace lgaed::presentation {

// Keep this UI-thread guard alive throughout TrackPopupMenuEx and until the
// caller destroys its HMENU. It does not own or dispatch the menu's commands.
// If released earlier, original item data/types/backgrounds are restored.
class NativeMenuStyle {
public:
    NativeMenuStyle() noexcept;
    ~NativeMenuStyle();
    NativeMenuStyle(NativeMenuStyle&&) noexcept;
    NativeMenuStyle& operator=(NativeMenuStyle&&) noexcept;
    NativeMenuStyle(const NativeMenuStyle&) = delete;
    NativeMenuStyle& operator=(const NativeMenuStyle&) = delete;
    [[nodiscard]] bool styled() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit NativeMenuStyle(std::unique_ptr<Impl>) noexcept;
    friend NativeMenuStyle prepare_native_menu(HMENU, HWND);
};

// Prepare after all items/submenus have been appended. System high contrast
// deliberately retains native system menus. Existing third-party owner drawing
// is left untouched; no undocumented dark-mode API or system setting is used.
[[nodiscard]] NativeMenuStyle prepare_native_menu(HMENU menu, HWND owner);

// Forward these from the actual popup owner, before other control handlers.
// False means the item/menu belongs to another renderer: use normal processing.
[[nodiscard]] bool measure_native_menu(MEASUREITEMSTRUCT& item);
[[nodiscard]] bool draw_native_menu(const DRAWITEMSTRUCT& item);
[[nodiscard]] bool native_menu_character(HMENU menu, wchar_t character, LRESULT& result);

} // namespace lgaed::presentation
