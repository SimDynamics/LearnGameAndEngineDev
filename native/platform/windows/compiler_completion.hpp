#pragma once

#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/cpp_runner.hpp"

#include <optional>

namespace lgaed::windows {
class CmakeSelectiveRecoveryTrace;
// An execution objective that requires the published project, not merely
// coincidentally matching stdout/stderr from a changed source.
[[nodiscard]] bool compiler_runtime_request_allowed(const curriculum::Block&,
    const CppRunRequest&);
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
// Used only inside the state-store's staged adoption. Does not alter original
// receipts, code/drafts, answers or any other objective/language.
void prepare_compiler_puzzle_revisions(state::StateData&, const curriculum::Lesson&);
void validate_compiler_puzzle_state(const state::StateData&, const curriculum::Lesson&, const curriculum::Block&);
// Evaluates every real execution again, including after an earned achievement.
// An incorrect/unavailable run returns false without changing that achievement;
// a correct first authored case earns a sealed completion for that exact program.
// Remaining cases may be checked voluntarily, without changing the earned seal.
// Legacy all-case achievements keep their original seals and identities.
[[nodiscard]] bool record_compiler_puzzle_case(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest&, const curriculum::FreshExecution&, std::string_view feedback);
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
