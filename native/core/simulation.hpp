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
enum class SimulationInputFormat { lines, json };
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
    // For each sorted element, the exact index of its original occurrence.
    // Stable ordering preserves distinct positions for repeated values.
    // When no sort was requested this is the identity permutation.
    std::vector<std::size_t> source_indices;
    std::vector<SimulationGroup> groups;
    std::vector<SimulationFrame> frames;
    std::vector<std::size_t> candidate_frames;
    bool operator==(const SimulationTrace&) const = default;
};

// A native simulation view owns a single validated snapshot of the authored
// contract. Editing or choosing a scenario checks the NEW data and constructs
// only its trace, without revalidating/rebuilding every unchanged authored
// scenario for each interaction. The original specification is private and
// immutable to callers; the public stateless APIs below remain fully strict.
class SimulationSession {
public:
    explicit SimulationSession(SimulationSpec authored);
    SimulationSession(const SimulationSession&) = delete;
    SimulationSession& operator=(const SimulationSession&) = delete;

    [[nodiscard]] const SimulationSpec& specification() const noexcept { return authored_; }
    // Transfer the already-validated initial trace without copying up to 8 MiB.
    // This operation is deliberately single-use.
    [[nodiscard]] SimulationTrace take_initial_trace();
    [[nodiscard]] SimulationTrace build_trace(std::vector<SimulationValue> values) const;
    [[nodiscard]] std::vector<SimulationValue> parse_values(
        std::string_view input, SimulationInputFormat format = SimulationInputFormat::lines) const;
    [[nodiscard]] SimulationTrace parse_trace(
        std::string_view input, SimulationInputFormat format = SimulationInputFormat::lines) const;
private:
    SimulationSpec authored_;
    SimulationTrace initial_;
    bool initial_taken_ = false;
};

// A deterministic model of runs in an ordered copy; it never interprets C++.
// Text sorting compares UTF-8 bytes, without locale/case/accent folding.
// All rejected contracts/inputs throw std::invalid_argument before publication.
void validate_simulation_spec(const SimulationSpec& spec);
[[nodiscard]] SimulationTrace build_simulation_trace(const SimulationSpec& spec,
    std::optional<std::vector<SimulationValue>> inputs = std::nullopt);
// Raw data for exact output and identity; display labels never replace values.
[[nodiscard]] std::string simulation_value_text(const SimulationValue& value);
// Text is quoted/escaped; empty and ASCII-space-only values have visible labels.
[[nodiscard]] std::string simulation_value_display_text(const SimulationValue& value);
// Derived explanations for a counted element. Computed only from a validated
// start/advance frame, its already-observed predecessor and original indices.
// Decision/initial/final frames yield no count statements. This is shared by
// portable model tests and the Windows dynamic narration panel.
[[nodiscard]] std::vector<std::string> simulation_count_observation_changes(
    const SimulationTrace& trace, const SimulationFrame& frame);
[[nodiscard]] std::string simulation_frame_text(const SimulationSpec& spec,
    const SimulationTrace& trace, const SimulationFrame& frame);
// Lines retain their historical literal meaning and optional final LF/CRLF.
// Text JSON is an explicit format (never inferred): [] and [""] are different.
// Integers keep signed decimal lines; JSON is limited to arrays of text values.
// When an editor will immediately display the computed trace, parse and
// validate it once instead of discarding a trace and computing it again.
[[nodiscard]] SimulationTrace simulation_parse_input_trace(
    const SimulationSpec& spec, std::string_view input,
    SimulationInputFormat format = SimulationInputFormat::lines);
[[nodiscard]] std::vector<SimulationValue> simulation_parse_input(
    const SimulationSpec& spec, std::string_view input,
    SimulationInputFormat format = SimulationInputFormat::lines);
// Lines cannot represent an empty text item and reject it instead of losing it.
[[nodiscard]] std::string simulation_input_text(const std::vector<SimulationValue>& values,
    SimulationInputFormat format = SimulationInputFormat::lines);

} // namespace lgaed::curriculum
