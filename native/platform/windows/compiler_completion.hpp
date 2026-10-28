#pragma once

#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/cpp_runner.hpp"

namespace lgaed::windows {
// A completed curricular objective belongs to the successful captured program,
// never to the editable draft. Only the explicit exercise reset revokes it.
[[nodiscard]] std::string compiler_program_frame(const CppRunRequest&);
[[nodiscard]] bool compiler_puzzle_completed(const state::StateData&, const curriculum::Lesson&,
                                            const curriculum::Block&);
[[nodiscard]] std::string compiler_puzzle_feedback(const state::StateData&, const curriculum::Lesson&,
                                                 const curriculum::Block&);
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
// Evaluates the real execution again before recording a case. Returns true
// only when every authored input/target passed with the same captured program.
[[nodiscard]] bool record_compiler_puzzle_case(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest&, const curriculum::FreshExecution&, std::string_view feedback);
[[nodiscard]] bool record_compiler_diagnostic_case(state::StateData&, const curriculum::Lesson&,
    const curriculum::Block&, const CppRunRequest&, const CppRunResult&, std::string_view feedback);
void invalidate_compiler_puzzle_attempt(state::StateData&, const curriculum::Lesson&, const curriculum::Block&,
                                       const CppRunRequest& current);
void reset_compiler_puzzle(state::StateData&, const curriculum::Lesson&, const curriculum::Block&);
} // namespace lgaed::windows
