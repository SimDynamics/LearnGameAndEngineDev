#pragma once

#include <windows.h>

namespace lgaed::presentation {
// Native, accessible BUTTON cards with pixel scrolling. Supports the catalog's
// LB_* selection/geometry messages without the list box's 255-pixel row limit.
[[nodiscard]] HWND create_card_list(HWND parent,int id,UINT activate_message);
} // namespace lgaed::presentation
