#pragma once

#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/cpp_runner.hpp"

#include <optional>
#include <string>

namespace lgaed::windows {
class CmakeSelectiveRecoveryTrace;
// An execution objective that requires the published project, not merely
// coincidentally matching stdout/stderr from a changed source.
// Optional presentation feedback is cleared on entry and filled only by a
// named-rule rejection. It describes this captured request, never a live
// editor draft, and is not evidence of correctness or a persisted receipt.
[[nodiscard]] bool compiler_runtime_request_allowed(const curriculum::Block&,
    const CppRunRequest&, std::string* rejection_reason = nullptr);
// The reader and receipt writer share the same private-check verdicts.
// This predicate alone neither authenticates a capture nor grants a receipt.
[[nodiscard]] bool compiler_independent_checks_satisfied(const curriculum::Block&,
    const curriculum::FreshExecution&);
// The native evaluator and Win32 reader share the same curricular gate.
[[nodiscard]] bool compiler_selective_history_required(const curriculum::Lesson&,
    const curriculum::Block&);
// A completed curricular objective belongs to the successful captured program,
// never to the editable draft. Only the explicit exercise reset revokes it.
[[nodiscard]] std::string compiler_program_frame(const CppRunRequest&);
[[nodiscard]] bool compiler_puzzle_completed(const state::StateData&, const curriculum::Lesson&,
                                            const curriculum::Block&);
[[nodiscard]] std::string compiler_puzzle_feedback(const state::StateData&, const curriculum::Lesson&,
                                                 const curriculum::Block&);
// A reviewed rejection is a resumable presentation of one actual evaluated
// attempt, independent of an older earned achievement. It is restored only for
// the exact current program, stdin, target and objective, never as console data.
[[nodiscard]] std::string compiler_puzzle_review(const state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest& current);
struct CompilerFeedback { std::string text; bool correct = false; std::string source_path; };
// Resolve earned success and its authored origin without a live editor request.
// Historical reading uses its own block version and never guesses a case path
// from the feedback text. This read does not alter any execution receipt.
[[nodiscard]] std::optional<CompilerFeedback> compiler_puzzle_success_feedback(const state::StateData&,
    const curriculum::Lesson&, const curriculum::Block&);
// The matching latest rejection takes presentation precedence over historical
// success. Reading this choice never alters or revokes a completion receipt.
[[nodiscard]] std::optional<CompilerFeedback> compiler_puzzle_current_feedback(const state::StateData&,
    const curriculum::Lesson&, const curriculum::Block&, const CppRunRequest& current);
[[nodiscard]] bool record_compiler_puzzle_review(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest& captured, const curriculum::FreshExecution&,
    std::string_view feedback);
[[nodiscard]] bool record_compiler_diagnostic_review(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest& captured, const CppRunResult&,
    std::string_view feedback);
void clear_compiler_puzzle_review(state::StateData&, const curriculum::Lesson&, const curriculum::Block&);
[[nodiscard]] std::size_t compiler_puzzle_case_count(const state::StateData&, const curriculum::Lesson&,
                                                  const curriculum::Block&);
[[nodiscard]] std::size_t compiler_puzzle_historical_case_count(const state::StateData&, const curriculum::Lesson&,
                                                             const curriculum::Block&);
[[nodiscard]] bool compiler_puzzle_historical_completed(const state::StateData&, const curriculum::Lesson&,
                                                      const curriculum::Block&);
[[nodiscard]] std::string compiler_puzzle_historical_feedback(const state::StateData&, const curriculum::Lesson&,
                                                           const curriculum::Block&);
// Used only inside the state-store's staged adoption. Preserves historical
// achievements, code/drafts, answers and other objectives/languages. Published
// case revisions reuse only identical cases from the same captured program;
// an aggregate receipt never proves newly isolated inputs. Authenticated
// permutations and replacements are adopted in editorial order, and
// a current first-case seal requires that case's own authenticated execution.
void prepare_compiler_puzzle_revisions(state::StateData&, const curriculum::Lesson&);
void validate_compiler_puzzle_state(const state::StateData&, const curriculum::Lesson&, const curriculum::Block&);
// Offline incident recovery only. The caller must authenticate both exact
// U3L4 publications and operate on a staged copy, never live profile state.
// Rebind only the existing ordinary receipts of the four displaced owners.
// Conflicting, incomplete or unknown receipt groups are rejected unchanged.
void stage_u3l4_storage_receipts(state::StateData&,
    const curriculum::Lesson& rejected,const curriculum::Block& prior,
    const curriculum::Lesson& restored,const curriculum::Block& current);

// Evaluates every real execution again, including after an earned achievement.
// An incorrect/unavailable run returns false without changing that achievement;
// a correct first authored case earns a sealed completion for that exact program.
// Remaining cases may be checked voluntarily, without changing the earned seal.
// Legacy all-case achievements keep their original seals and identities.
[[nodiscard]] bool record_compiler_puzzle_case(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest&, const curriculum::FreshExecution&, std::string_view feedback);
// A client observation is a prerequisite, never a completed checker objective.
// Capture its token with the request before the checker is started.
[[nodiscard]] std::string compiler_integer_battery_prerequisite(const state::StateData&,
    const curriculum::Lesson&, const curriculum::Block&, const CppRunRequest&);
[[nodiscard]] bool record_compiler_integer_battery_client(state::StateData&,
    const curriculum::Lesson&, const curriculum::Block&, const CppRunRequest&,
    const curriculum::FreshExecution&);
// Automatic observation of the authentic faulty CMake source. Build warnings
// and the subsequent normal failing run remain separate, artifact-bound facts.
// The final repaired request must capture this prerequisite before it starts.
[[nodiscard]] std::string compiler_cmake_initial_observation_prerequisite(const state::StateData&,
    const curriculum::Lesson&, const curriculum::Block&, const CppRunRequest&);
[[nodiscard]] bool record_compiler_cmake_initial_observation(state::StateData&,
    const curriculum::Lesson&, const curriculum::Block&, const CppRunRequest&,
    const CppRunResult&);
/* For the exact U3 recovery owner, a saved stdout proof remains historical;
   a new independent sequence receipt requires authentic record, comparisons,
   selective restore, both builds and both runs. Never infer it from stdout. */
[[nodiscard]] bool record_compiler_selective_history_completion(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest&, const curriculum::FreshExecution&,
    const CmakeSelectiveRecoveryTrace&);

[[nodiscard]] bool record_compiler_diagnostic_case(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest&, const CppRunResult&, std::string_view feedback);
void invalidate_compiler_puzzle_attempt(state::StateData&, const curriculum::Lesson&, const curriculum::Block&,
                                       const CppRunRequest& current);
void reset_compiler_puzzle(state::StateData&, const curriculum::Lesson&, const curriculum::Block&);
} // namespace lgaed::windows
