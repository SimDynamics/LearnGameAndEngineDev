#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lgaed::curriculum {

using SimulationValue = std::variant<std::int64_t, std::string>;

enum class SimulationViewMode { array, groups, walkthrough, candidates, summary };
enum class SimulationPhase { initial, start, advance, new_maximum, tie, below_maximum, done, empty };

struct SimulationLabels {
    std::string array, start, end, frequency, maximum, leaders, output;
    bool operator==(const SimulationLabels&) const = default;
};
struct SimulationExplanations {
    std::string initial, start, advance, new_maximum, tie, below_maximum, done, empty;
    bool operator==(const SimulationExplanations&) const = default;
};
struct SimulationScenario {
    std::string id, label;
    std::vector<SimulationValue> values;
    bool operator==(const SimulationScenario&) const = default;
};
struct SimulationSpec {
    std::string title, description;
    int version = 1;
    std::string model = "arrayRunsV1";
    SimulationViewMode view = SimulationViewMode::walkthrough;
    bool text_values = false, sort = true, editable = false;
    std::vector<SimulationValue> values;
    SimulationLabels labels;
    SimulationExplanations explanations;
    std::string output_template;
    std::vector<SimulationScenario> scenarios;
    bool operator==(const SimulationSpec&) const = default;
};
struct SimulationGroup {
    std::size_t begin = 0, end = 0;
    SimulationValue value;
    bool operator==(const SimulationGroup&) const = default;
};
struct SimulationFrame {
    SimulationPhase phase = SimulationPhase::initial;
    // The observed range is [begin,end). end==size never reads past the array.
    std::size_t begin = 0, end = 0, group = 0;
    std::size_t frequency = 0, maximum = 0;
    std::vector<SimulationValue> leaders;
    std::string explanation, output;
    bool operator==(const SimulationFrame&) const = default;
};
struct SimulationTrace {
    std::vector<SimulationValue> original, ordered;
    std::vector<SimulationGroup> groups;
    std::vector<SimulationFrame> frames;
    std::vector<std::size_t> candidate_frames;
    bool operator==(const SimulationTrace&) const = default;
};

// A deterministic model of runs in an ordered copy; it never interprets C++.
// Text sorting compares UTF-8 bytes, without locale/case/accent folding.
// All rejected contracts/inputs throw std::invalid_argument before publication.
void validate_simulation_spec(const SimulationSpec& spec);
[[nodiscard]] SimulationTrace build_simulation_trace(const SimulationSpec& spec,
    std::optional<std::vector<SimulationValue>> inputs = std::nullopt);
[[nodiscard]] std::string simulation_value_text(const SimulationValue& value);
[[nodiscard]] std::string simulation_frame_text(const SimulationSpec& spec,
    const SimulationTrace& trace, const SimulationFrame& frame);
// One value per LF/CRLF line. A final newline is optional. Text preserves spaces;
// integers admit a sign and exterior spaces/TAB, but no expressions or suffixes.
[[nodiscard]] std::vector<SimulationValue> simulation_parse_input(
    const SimulationSpec& spec, std::string_view input);
[[nodiscard]] std::string simulation_input_text(const std::vector<SimulationValue>& values);

} // namespace lgaed::curriculum
