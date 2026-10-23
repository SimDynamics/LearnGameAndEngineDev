#pragma once

#include <windows.h>

namespace lgaed::presentation {
// Native, accessible BUTTON cards with pixel scrolling. Supports the catalog's
// LB_* selection/geometry messages without the list box's 255-pixel row limit.
[[nodiscard]] HWND create_card_list(HWND parent,int id,UINT activate_message);
// A catalog can share the same viewport with its existing sibling heading
// controls. The parent positions those controls at their document Y minus the
// offset delivered by scroll_message; card identities remain unchanged.
void set_card_list_header(HWND list,int height,UINT scroll_message);
[[nodiscard]] int card_list_scroll_offset(HWND list);
void set_card_list_scroll_offset(HWND list,int offset);
} // namespace lgaed::presentation
