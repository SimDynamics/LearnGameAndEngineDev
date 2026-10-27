#pragma once
#include "core/cpp_project.hpp"

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
// Archived blocks retain their complete authored contract and original address.
// They are absent from pages/current and therefore never become a new activity.
struct StateRetirement { std::string id, page, next, raw_json, sha256; };
struct Emphasis {
    std::string text;
    std::size_t occurrence = 0;
    std::string style = "bold";
};
struct EmphasisRange { std::size_t start = 0, end = 0; std::string style; };
// Offsets address UTF-8 source bytes. Presentation converts them to UTF-16 positions.
[[nodiscard]] std::vector<EmphasisRange> emphasis_ranges(std::string_view text, std::span<const Emphasis> entries);
struct OutputReviewFeedback { std::vector<std::string> outputs; std::string text; };
enum class StandardErrorExpectation { ignored, non_blank };
struct OutputCase {
    std::string standard_input;
    std::vector<std::string> outputs;
    std::string explanation;
    std::vector<OutputReviewFeedback> review_feedback;
    std::optional<std::string> executable;
    StandardErrorExpectation standard_error = StandardErrorExpectation::ignored;
};
struct OutputCheck {
    TextComparison input_comparison = TextComparison::exact;
    std::vector<OutputCase> cases;
};
// One historical grouped ordering refined into independently movable lines.
// The complete original block remains provenance, never another active puzzle.
struct OrderRefinement {
    std::string id, raw_json, sha256, representation_signature;
    std::vector<std::string> original_fragments;
    std::vector<std::size_t> original_correct_order;
    std::vector<std::vector<std::size_t>> groups;
};
// One explicitly reviewed option revision. The complete old block owns the
// historical selection; current attempts use a separate representation.
struct ChoiceRevision {
    std::string id, raw_json, sha256, representation_signature;
    std::vector<std::string> original_options, original_review_feedback_options;
    std::string original_question, original_explanation, original_review_feedback;
    std::optional<std::size_t> original_correct_index;
};
// An exact reviewed compiler contract expansion retains its complete original
// and independent execution proofs; old success never proves the new checks.
struct CompilerReferenceRevision {
    std::string id, raw_json, sha256, current_payload;
};
struct CompilerRevision {
    std::string id, raw_json, sha256, previous_payload, current_payload;
    OutputCheck previous_output_check;
    std::vector<CompilerReferenceRevision> references;
};
struct Block {
    BlockType type{};
    std::string stable_id, id, text, question, code, initial_code, language;
    std::string explanation, review_feedback, raw_json;
    std::string code_template, prompt, math_type;
    std::vector<Emphasis> emphasis;
    std::vector<std::string> options, answers, fragments;
    std::vector<std::string> choices;
    std::vector<std::string> review_feedback_options;
    std::vector<std::vector<std::string>> answer_alternatives;
    std::optional<std::size_t> correct_index;
    std::vector<std::size_t> correct_order;
    // Reviewed, source-bound independent fragments may exchange only their
    // canonical positions. This is supplied by the native loader, not JSON,
    // and does not change the stored representation or historical answer key.
    std::vector<std::vector<std::size_t>> order_equivalence_groups;
    std::optional<OrderRefinement> order_refinement;
    std::optional<ChoiceRevision> choice_revision;
    std::optional<CompilerRevision> compiler_revision;
    TextComparison output_comparison = TextComparison::trimmed;
    AnswerComparison answer_comparison = AnswerComparison::code;
    std::optional<OutputCheck> output_check;
    std::optional<std::string> standard_input;
    CppProjectFiles project_files;
    std::vector<std::string> source_files;
    std::optional<CmakeConfig> cmake;
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
// Compiler and puzzle dependencies use the same stable compiler owners and
// completion evidence as references. They allocate no new progress slots.
struct ActivityAfter { std::string activity; std::vector<std::string> prerequisites; };
// A before-retirement reading snapshot is archival provenance, never a second
// active lesson. Full pages retain wording as well as the old visibility graph.
struct StateRetirementHistory {
    std::vector<std::string> retired;
    std::vector<StatePage> current;
    std::vector<Page> pages;
    bool compiler_puzzles_required = false;
    std::vector<ReferenceAfter> reference_after;
    std::vector<ActivityAfter> activity_after;
    std::string raw_json, sha256;
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
    std::vector<StateRetirement> retired;
    std::vector<StateRetirementHistory> retirement_history;
    std::vector<OrderRefinement> order_refinements;
    std::vector<ChoiceRevision> choice_revisions;
    std::vector<CompilerRevision> compiler_revisions;
};
struct Lesson {
    std::string id, title, locale, raw_json;
    std::filesystem::path source_path;
    std::vector<Page> pages;
    StateLayout state_layout;
    std::vector<ReferenceAfter> reference_after;
    std::vector<ActivityAfter> activity_after;
    std::optional<std::string> narration_language;
    std::vector<Block> retired_blocks;
    bool compiler_puzzles_required = false;
};

void validate_reference_relationships(const Lesson&);
void validate_activity_relationships(const Lesson&);
[[nodiscard]] std::optional<StatePosition> stable_block_position(const Lesson&, std::string_view stable_id);
[[nodiscard]] bool retired_block(const Lesson&, std::string_view stable_id);
// Resolve an archived reader anchor without rewriting its historical bookmark.
[[nodiscard]] std::optional<StatePosition> active_block_successor(const Lesson&, std::string_view stable_id);
[[nodiscard]] std::optional<StatePosition> reference_activity(const Lesson&, const Block& reference);
[[nodiscard]] std::vector<StatePosition> activity_prerequisites(const Lesson&, const Block& activity);
// A source compiler remains a reader/advance gate for a dependent puzzle,
// including paths through other compilers. Isolated legacy compiler chains do
// not become mandatory, and historical progress counters retain their owners.
[[nodiscard]] bool compiler_is_puzzle_prerequisite(const Lesson&, const Block& compiler);
// Deterministic framing of the authored objective, independent of prose or an
// imported exercise response. The platform hashes this with the executed draft.
[[nodiscard]] std::string reference_objective_frame(const Lesson&, const Block& activity);
// Fragments and their canonical permutation only; prose and view placement do
// not change an ordering draft's representation. The platform hashes this frame.
[[nodiscard]] std::string order_representation_frame(const Block&);
[[nodiscard]] std::string choice_representation_frame(const Block&);

// These keys deliberately retain the Android lesson identity and original slots.
[[nodiscard]] std::string exercise_key(const Lesson&, const Block&);
[[nodiscard]] std::string draft_key(std::string_view route_id, const Lesson&, const Block&);
[[nodiscard]] std::size_t storage_page(const Lesson&, std::size_t display_page);
[[nodiscard]] std::size_t display_page(const Lesson&, std::size_t storage_page);

struct AnswerEvaluation { bool correct = false; std::string feedback; };
[[nodiscard]] std::string incorrect_feedback(const Block&,std::optional<std::size_t> selected={});
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
    // Runtime stderr only. Build diagnostics never enter this field.
    std::string standard_error;
};
enum class OutputVerdict { unavailable, correct, incorrect };
struct OutputEvaluation {
    OutputVerdict verdict = OutputVerdict::unavailable;
    std::string feedback;
    bool stdout_correct = false;
    bool stderr_correct = false;
};
[[nodiscard]] OutputEvaluation evaluate_output_check(
    const OutputCheck&, const FreshExecution&, std::uint64_t current_revision);

} // namespace lgaed::curriculum
