#pragma once
#include "core/cpp_project.hpp"
#include "core/named_rule_check.hpp"
#include "core/cmake_graph_check.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {
struct OutputCheck;
struct FreshExecution;
enum class RejectionOperation;
enum class ReportMode;
enum class AssertionMode;
struct OutputEvaluation;
struct Block;
}

namespace lgaed::windows {

// Provide an accurate Win32 policy-denial diagnostic without treating an
// unstarted executable as a failed student program or successful puzzle case.
[[nodiscard]] std::string cpp_launch_failure_message(std::uint32_t error,
    std::string_view executable_utf8, bool build_tool);

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
    bool operator==(const CppPhaseResult&) const = default;
};

// A diagnostic objective compares two fresh compilation-only attempts. A zero
// line interval and empty code preserve diagnostics for an unrelated edit but
// cannot accredit a rejection. No executable is linked or run in this mode.
struct CppDiagnosticRequest {
    std::string control_source_utf8;
    std::string expected_error_code;
    std::uint32_t first_line = 0, last_line = 0;
    std::optional<curriculum::CppProjectDiagnostic> project;
    bool operator==(const CppDiagnosticRequest&) const = default;
};
struct CppCompilerDiagnostic {
    std::string code;
    std::filesystem::path source;
    std::uint32_t line = 0, column = 0;
    bool operator==(const CppCompilerDiagnostic&) const = default;
};
struct CppDiagnosticEvidence {
    CppPhaseResult control;
    std::vector<CppCompilerDiagnostic> errors;
    std::string source_sha256, control_sha256;
    bool expected_rejection = false;
    struct Unit {
        std::string source;
        CppPhaseResult phase;
    };
    std::vector<Unit> control_units, candidate_units;
    CppPhaseResult control_link, candidate_link;
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
    // WASI compiles a portable module, then executes it with a trusted host
    // runtime. Native MSVC remains required for diagnostics and CMake.
    enum class Backend {native_msvc,wasi};
    Backend backend=Backend::native_msvc;
    std::filesystem::path projects_directory;
    std::chrono::milliseconds compile_timeout{60000};
    std::chrono::milliseconds execution_timeout{5000};
    std::size_t output_limit_bytes = 1024 * 1024;
    std::optional<CppDiagnosticRequest> diagnostic;
    // Opt-in, source-pinned diagnostic function review; never enabled by a
    // free laboratory or by a project without this reviewed C++ objective.
    std::optional<curriculum::RejectionOperation> rejection_probe;
    std::optional<curriculum::ReportMode> observation_probe;
    std::optional<curriculum::NamedRuleExecutionProbe> named_rule_probe;
    std::optional<curriculum::AssertionMode> assertion_mode;
    // Ordinary CMake builds and private probes do not need to parse the
    // entire generated target graph. Enable only for structure-aware checks.
    bool capture_cmake_graph = false;
    // The lesson loader supplies this immutable root-authored contract.
    // It travels with the specific request and cannot be graded from text.
    std::optional<curriculum::CmakeGraphRequirement> cmake_graph_check;
};
struct CppRejectionProbe {
    std::string source_sha256;
    CppPhaseResult compilation, execution;
};

struct CppObservationProbe {
    std::string source_sha256, standard_input;
    CppPhaseResult compilation, execution;
};

struct CppAssertionProbe {
    std::string source_sha256, standard_input;
    CppPhaseResult compilation, execution;
};
struct CppNamedRuleProbe {
    std::string source_sha256, project_sha256;
    CppPhaseResult compilation, execution;
};

// Read-only CMake File API evidence, pinned to the submitted project/configuration.
// Build dependencies are distinct from actual linker library fragments. Neither
// a successful configure nor a matching stdout grants a curricular achievement.
struct CppCmakeTargetEvidence {
    std::string name, type, source_directory;
    std::vector<std::string> compiled_sources, build_dependencies, link_libraries, artifacts;
    // Observed author commands from a matching CMake configure, not
    // compiler defaults or a regex against arbitrary source text.
    bool explicit_cxx20 = false, explicit_extensions_off = false;
};
struct CppCmakeGraphEvidence {
    std::string project_fingerprint, configuration_fingerprint, project_name;
    std::vector<CppCmakeTargetEvidence> targets;
    // New fields are appended so historical aggregate initializations stay valid.
    std::string minimum_cmake_version;
    std::vector<std::pair<std::string,std::string>> authored_option_defaults;
};
// This separately compiled result is bound to the OFF configuration fingerprint.
struct CppCmakeOptionProbe {
    std::uint64_t request_id=0,revision=0;
    std::string project_fingerprint,configuration_fingerprint,target_type,error_utf8;
    std::optional<std::string> executable;
    CppPhaseResult compilation,execution,configuration;
    std::optional<CppCmakeGraphEvidence> graph;
};
// Keep platform API names as aliases while the authored root contract lives
// in the curriculum core, independent of the Windows runner.
using CppCmakeRequiredTarget = curriculum::CmakeRequiredTarget;
using CppCmakeGraphRequirement = curriculum::CmakeGraphRequirement;
struct CppCmakeGraphVerdict {
    bool correct = false;
    std::string reason;
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
    std::optional<CppCmakeGraphEvidence> cmake_graph;
    std::optional<CppCmakeOptionProbe> cmake_option_probe;
    std::optional<CppDiagnosticEvidence> diagnostic;
    std::optional<CppRejectionProbe> rejection_probe;
    std::optional<CppNamedRuleProbe> named_rule_probe;
    std::vector<CppObservationProbe> observation_probes;
    std::vector<CppAssertionProbe> assertion_probes;
};
struct CppProjectAvailability {
    bool configured=false,built=false;
    std::string project_fingerprint,configuration_fingerprint,target_type;
    std::filesystem::path workspace,artifact;
};
// Structural evidence must belong to the same fresh compiled + executed
// revision and generated CMake configuration, never an old File API reply.
// It does not by itself grant a saved curriculum achievement.
[[nodiscard]] CppCmakeGraphVerdict evaluate_cmake_graph(
    const CppRunRequest&, const CppRunResult&, const CppCmakeGraphRequirement&);

// Read-only inspection. Never configures, builds, executes or rewrites a draft.
[[nodiscard]] CppProjectAvailability inspect_cpp_project(const CppRunRequest&);

// Blocking adapter: invoke on an owned worker, never the window thread. A stop
// request kills the entire child job before returning. The caller rejects stale
// request/revision results. This is bounded execution, not a security sandbox.
[[nodiscard]] CppRunResult run_cpp(const CppRunRequest& request,
    std::stop_token stop = {});

// Reparse the captured compiler streams and verify both phases and the exact
// request/revision; a cached summary boolean is never execution evidence.
[[nodiscard]] bool expected_compile_diagnostic(const CppRunRequest&, const CppRunResult&);
// The same compilation-only capture can prove that the requested rejection
// was not obtained. Both control and candidate must have fully succeeded in
// the requested compile/link phase; technical/incomplete results have no grade.
[[nodiscard]] bool accepted_compile_diagnostic(const CppRunRequest&, const CppRunResult&);
// Reconstruct the closed lesson objective from this exact visible snapshot.
// Unrelated edits retain diagnostics but leave the accreditation fields empty.
[[nodiscard]] CppDiagnosticRequest prepare_cpp_diagnostic_request(const curriculum::Block&, const CppRunRequest&);

// Keep capture and the shared curriculum evaluator byte-exact. At the Windows
// boundary, also accept CRT text-mode CRLF for authored LF-only output goals.
// Explicit CR, spaces and the presence of a final newline remain significant.
[[nodiscard]] curriculum::OutputEvaluation evaluate_cpp_output_check(
    const curriculum::OutputCheck&, const curriculum::FreshExecution&, std::uint64_t current_revision);

// Capture only the completed phases belonging to this exact request/revision.
// A normal nonzero process code is distinct from cancellation, timeout, launch
// failure, output overflow or an abnormal Windows termination. No UI is used.
[[nodiscard]] curriculum::FreshExecution capture_cpp_execution(
    const CppRunRequest&, const CppRunResult&);

} // namespace lgaed::windows
