#pragma once
#include "core/cpp_project.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>

namespace lgaed::curriculum {
struct OutputCheck;
struct FreshExecution;
struct OutputEvaluation;
}

namespace lgaed::windows {

enum class CppPhaseStatus {
    not_started, completed, cancelled, timed_out, output_limit, launch_failed
};

struct CppPhaseResult {
    CppPhaseStatus status = CppPhaseStatus::not_started;
    std::optional<std::uint32_t> exit_code;
    // Exact bytes from separate redirected streams, including whitespace/CRLF.
    std::string stdout_bytes;
    std::string stderr_bytes;
    [[nodiscard]] bool succeeded() const noexcept {
        return status == CppPhaseStatus::completed && exit_code == std::uint32_t{0};
    }
};

struct CppRunRequest {
    std::uint64_t request_id = 0;
    std::uint64_t revision = 0;
    std::string source_utf8;
    std::string stdin_utf8;
    curriculum::CppProjectFiles project_files;
    std::vector<std::string> source_files;
    std::optional<curriculum::CmakeConfig> cmake;
    // Stable existing lesson draft identity; no student-visible pathname.
    std::string project_key;
    enum class Action {compile_run,configure,build,execute};
    Action action=Action::compile_run;
    std::filesystem::path projects_directory;
    std::chrono::milliseconds compile_timeout{60000};
    std::chrono::milliseconds execution_timeout{5000};
    std::size_t output_limit_bytes = 1024 * 1024;
};

struct CppRunResult {
    std::uint64_t request_id = 0;
    std::uint64_t revision = 0;
    std::filesystem::path run_directory;
    CppPhaseResult compilation;
    CppPhaseResult execution;
    CppPhaseResult configuration;
    std::string project_fingerprint,configuration_fingerprint,target_type;
    std::optional<std::string> executable;
    std::filesystem::path artifact;
    std::string error_utf8;
};
struct CppProjectAvailability {
    bool configured=false,built=false;
    std::string project_fingerprint,configuration_fingerprint,target_type;
    std::filesystem::path workspace,artifact;
};
// Read-only inspection. Never configures, builds, executes or rewrites a draft.
[[nodiscard]] CppProjectAvailability inspect_cpp_project(const CppRunRequest&);

// Blocking adapter: invoke on an owned worker, never the window thread. A stop
// request kills the entire child job before returning. The caller rejects stale
// request/revision results. This is bounded execution, not a security sandbox.
[[nodiscard]] CppRunResult run_cpp(const CppRunRequest& request,
    std::stop_token stop = {});

// Keep capture and the shared curriculum evaluator byte-exact. At the Windows
// boundary, also accept CRT text-mode CRLF for authored LF-only output goals.
// Explicit CR, spaces and the presence of a final newline remain significant.
[[nodiscard]] curriculum::OutputEvaluation evaluate_cpp_output_check(
    const curriculum::OutputCheck&, const curriculum::FreshExecution&, std::uint64_t current_revision);

} // namespace lgaed::windows
