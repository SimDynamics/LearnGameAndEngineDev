#pragma once
#include "presentation/windows/rich_text.hpp"
#include <windows.h>
#include <optional>
#include <functional>
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
struct NativeMathFragment {
    std::size_t start=0,end=0;
    std::string formula;
    bool literal=false;
};
// Converts the existing mathematical source to native UnicodeMath/TOM2.
// The source-to-native map is derived from actual resulting character positions.
// The caller retains the original source for narration and never persists this
// display-only projection as curriculum or as a student's answer.
bool set_native_math_text(HWND,std::string_view original_source,std::string_view locale="es");
// Prose retains its ordinary font; only explicit \(...\) / \[...\]
// mathematical spans are built up. No guessed mathematical ranges.
bool set_native_math_inline_text(HWND,std::string_view original_source,
    std::span<const curriculum::Emphasis> emphasis = {}, RichTextRole role = RichTextRole::body,std::string_view locale="es");
// Ranges/normalization come from the original Android MathTextRenderer. This
// platform adapter projects only those ranges, retaining their source owners.
bool set_native_math_fragments(HWND,std::string_view original_source,std::span<const NativeMathFragment>,
    RichTextRole role=RichTextRole::body,std::string_view locale="es");
// Complete Android MathTextRenderer policy for lesson text as well as widgets.
// The request is the existing canonical mathematics worker; no new detector is
// implemented by the native presentation layer. Source emphasis remains owned
// by the original text, including when an implicit formula changes its display.
bool set_original_math_text(HWND,std::string_view original_source,std::string_view mode,
    const std::function<std::string(std::string_view)>& request,
    std::span<const curriculum::Emphasis> emphasis={},RichTextRole role=RichTextRole::body,std::string_view locale="es");
[[nodiscard]] std::optional<NativeMathTextMapping> native_math_text_mapping(HWND,std::string_view original_source);
} // namespace lgaed::presentation
