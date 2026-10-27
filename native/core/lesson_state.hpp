#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lgaed::state {

// SharedPreferences names and keys are retained verbatim, including stable historical slots.
using Value = std::variant<std::string, std::int64_t, double, bool, std::vector<std::string>>;
using Preferences = std::map<std::string, Value>;
struct StateData {
    std::map<std::string, Preferences> preferences;
    bool operator==(const StateData&) const = default;
};

struct Response {
    std::string answer;
    bool correct = false;
    std::string raw_json;
    std::int64_t selected = -1;
    std::vector<std::int64_t> order;
    std::string reviewed_answer_identity;
    // In-memory provenance only. Never serialize this flag into the old blob.
    bool historical_choice = false;
};
struct Draft {
    std::string source, input;
    // The original Android sidecar is retained; offsets count UTF-16 code units.
    std::string indentation_json;
    std::int64_t selection_start = 0, selection_end = 0;
    bool has_saved = false;
};

[[nodiscard]] const Value* find(const StateData&, std::string_view domain, std::string_view key);
[[nodiscard]] std::string string_value(const StateData&, std::string_view domain,
                                     std::string_view key, std::string_view fallback = {});
[[nodiscard]] std::int64_t integer_value(const StateData&, std::string_view domain,
                                       std::string_view key, std::int64_t fallback = 0);
[[nodiscard]] std::vector<std::uint8_t> encode(const StateData&);
[[nodiscard]] StateData decode(std::span<const std::uint8_t>);

// Deliberately excludes compilation output and verdicts: these require a fresh execution.
void set_saved_page(StateData&, std::string_view lesson_id, std::size_t historical_page);
[[nodiscard]] std::size_t saved_page(const StateData&, std::string_view lesson_id);

} // namespace lgaed::state
