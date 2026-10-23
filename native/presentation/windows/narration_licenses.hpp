#pragma once
#include <windows.h>
#include <string_view>
namespace lgaed::presentation {
// Reads the original retained notices; does not access the network or models.
[[nodiscard]] HWND open_narration_licenses(HWND owner,std::string_view locale);
}
