#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

enum class BlockType { heading, paragraph, note, code, multiple_choice, output_prediction,
                       fill_code, order_code, cpp_compiler, inline_fill_code, drag_fill_code, math_puzzle };
enum class TextComparison { trimmed, exact, tokens };
enum class AnswerComparison { code, cpp_decimal_literal_value };

struct StatePosition {
    std::size_t page = 0;
    std::size_t block = 0;
    bool operator==(const StatePosition&) const = default;
};
struct StateAddress {
    std::size_t page = 0;
    std::size_t block = 0;
    std::string addition_namespace;
    std::string progress_page;
};
struct StatePage { std::string id; std::vector<std::string> blocks; };
struct StateFamily {
    std::string sha256;
    std::vector<StatePage> origin;
    std::map<std::string, std::string> signatures;
};
struct StateAddition {
    std::string id, sha256;
    StatePosition storage;
    std::string progress_page;
};
struct StateLayout {
    int version = 0;
    std::string locale;
    std::vector<StatePage> origin, current;
    std::map<std::string, StateFamily> family;
    std::vector<StateAddition> additions;
    std::map<std::string, std::string> signatures;
    std::string lineage, legacy_lineage;
    std::string raw_json;
};
struct Emphasis {
    std::string text;
    std::size_t occurrence = 0;
    std::string style = "bold";
};
struct EmphasisRange { std::size_t start = 0, end = 0; std::string style; };
// Offsets address UTF-8 source bytes. Presentation converts them to UTF-16 positions.
[[nodiscard]] std::vector<EmphasisRange> emphasis_ranges(std::string_view text, std::span<const Emphasis> entries);
struct OutputReviewFeedback { std::vector<std::string> outputs; std::string text; };
struct OutputCase {
    std::string standard_input;
    std::vector<std::string> outputs;
    std::string explanation;
    std::vector<OutputReviewFeedback> review_feedback;
    std::optional<std::string> executable;
};
struct OutputCheck {
    TextComparison input_comparison = TextComparison::exact;
    std::vector<OutputCase> cases;
};
struct Block {
    BlockType type{};
    std::string stable_id, id, text, question, code, initial_code, language;
    std::string explanation, review_feedback, raw_json;
    std::string code_template, prompt, math_type;
    std::vector<Emphasis> emphasis;
    std::vector<std::string> options, answers, fragments;
    std::vector<std::string> choices;
    std::vector<std::vector<std::string>> answer_alternatives;
    std::optional<std::size_t> correct_index;
    std::vector<std::size_t> correct_order;
    TextComparison output_comparison = TextComparison::trimmed;
    AnswerComparison answer_comparison = AnswerComparison::code;
    std::optional<OutputCheck> output_check;
    std::optional<std::string> standard_input;
    StateAddress address;
};
struct Page {
    std::string stable_id;
    std::vector<Block> blocks;
    // Explicit authored narration metadata, resolved with the lesson fallback.
    // An absent value means the reader's preference remains authoritative.
    std::optional<std::string> narration_language;
};
// Presentation-only relationships, authored at the lesson root so no historical
// block fingerprint, answer or storage slot changes when a reference is linked.
struct ReferenceAfter { std::string activity; std::vector<std::string> blocks; };
// Compiler dependencies use the same stable owners and completion evidence as
// references. They do not allocate progress slots or change block fingerprints.
struct ActivityAfter { std::string activity; std::vector<std::string> prerequisites; };
struct Lesson {
    std::string id, title, locale, raw_json;
    std::filesystem::path source_path;
    std::vector<Page> pages;
    StateLayout state_layout;
    std::vector<ReferenceAfter> reference_after;
    std::vector<ActivityAfter> activity_after;
    std::optional<std::string> narration_language;
};

void validate_reference_relationships(const Lesson&);
void validate_activity_relationships(const Lesson&);
[[nodiscard]] std::optional<StatePosition> stable_block_position(const Lesson&, std::string_view stable_id);
[[nodiscard]] std::optional<StatePosition> reference_activity(const Lesson&, const Block& reference);
[[nodiscard]] std::vector<StatePosition> activity_prerequisites(const Lesson&, const Block& activity);
// Deterministic framing of the authored objective, independent of prose or an
// imported exercise response. The platform hashes this with the executed draft.
[[nodiscard]] std::string reference_objective_frame(const Lesson&, const Block& activity);

// These keys deliberately retain the Android lesson identity and original slots.
[[nodiscard]] std::string exercise_key(const Lesson&, const Block&);
[[nodiscard]] std::string draft_key(std::string_view route_id, const Lesson&, const Block&);
[[nodiscard]] std::size_t storage_page(const Lesson&, std::size_t display_page);
[[nodiscard]] std::size_t display_page(const Lesson&, std::size_t storage_page);

struct AnswerEvaluation { bool correct = false; std::string feedback; };
[[nodiscard]] AnswerEvaluation evaluate_text_answer(const Block&, std::string_view answer);
[[nodiscard]] AnswerEvaluation evaluate_choice(const Block&, std::size_t option);
[[nodiscard]] AnswerEvaluation evaluate_order(const Block&, std::span<const std::size_t> order);
[[nodiscard]] AnswerEvaluation evaluate_hole_answers(const Block&, std::span<const std::string> answers);
[[nodiscard]] AnswerEvaluation evaluate_drag_answers(const Block&, std::span<const std::size_t> choices);
[[nodiscard]] std::string normalize_cpp_answer(std::string_view answer);
[[nodiscard]] bool uses_single_line_answer(const Block&);

// A failed/stale execution is never a wrong educational answer. No heart ledger is involved.
struct FreshExecution {
    std::uint64_t source_revision = 0;
    bool compilation_succeeded = false;
    bool execution_succeeded = false;
    int exit_code = -1;
    std::string standard_input, standard_output;
    std::optional<std::string> executable;
};
enum class OutputVerdict { unavailable, correct, incorrect };
struct OutputEvaluation { OutputVerdict verdict = OutputVerdict::unavailable; std::string feedback; };
[[nodiscard]] OutputEvaluation evaluate_output_check(
    const OutputCheck&, const FreshExecution&, std::uint64_t current_revision);

} // namespace lgaed::curriculum
