#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {
enum class SemanticRole { prose,cpp,string,data,math,output,citation };
enum class SemanticReading { automatic,natural,technical,characters };
struct SemanticSpan {
    std::size_t start=0,end=0;
    std::string text;
    SemanticRole role=SemanticRole::prose;
    SemanticReading reading=SemanticReading::automatic;
    bool operator==(const SemanticSpan&) const=default;
};
// A present descriptor makes the entire field explicit: gaps (and an empty
// spans list) are prose. Source bytes and UTF-16 positions precede any display
// projection, delimiter hiding, prefix composition or line-ending conversion.
struct TextSemantics {
    std::string source;
    std::vector<SemanticSpan> spans;
    bool operator==(const TextSemantics&) const=default;
};
[[nodiscard]] std::string_view semantic_role_name(SemanticRole);
[[nodiscard]] std::string_view semantic_reading_name(SemanticReading);
[[nodiscard]] SemanticRole parse_semantic_role(std::string_view);
[[nodiscard]] SemanticReading parse_semantic_reading(std::string_view);
// Reject invalid UTF-8/scalar boundaries, stale exact selections, overlaps,
// unordered spans, unknown enum values and unbounded payloads.
void validate_text_semantics(const TextSemantics&);
} // namespace lgaed::curriculum
