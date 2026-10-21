#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>

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
    std::string error_utf8;
};

// Blocking adapter: invoke on an owned worker, never the window thread. A stop
// request kills the entire child job before returning. The caller rejects stale
// request/revision results. This is bounded execution, not a security sandbox.
[[nodiscard]] CppRunResult run_cpp(const CppRunRequest& request,
    std::stop_token stop = {});

} // namespace lgaed::windows
