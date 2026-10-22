#pragma once
#include "presentation/windows/rich_text.hpp"
#include <windows.h>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace lgaed::presentation {
struct NativeMathTextMapping {
    std::string source;
    std::wstring native_text;
    std::vector<std::size_t> source_to_native;
    bool built_up = false;
};
// Converts the existing mathematical source to native UnicodeMath/TOM2.
// The source-to-native map is derived from actual resulting character positions.
// The caller retains the original source for narration and never persists this
// display-only projection as curriculum or as a student's answer.
bool set_native_math_text(HWND,std::string_view original_source);
// Prose retains its ordinary font; only explicit \(...\) / \[...\]
// mathematical spans are built up. No guessed mathematical ranges.
bool set_native_math_inline_text(HWND,std::string_view original_source,
    std::span<const curriculum::Emphasis> emphasis = {}, RichTextRole role = RichTextRole::body);
[[nodiscard]] std::optional<NativeMathTextMapping> native_math_text_mapping(HWND,std::string_view original_source);
} // namespace lgaed::presentation
