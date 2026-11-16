#pragma once

#include "core/cpp_expression_repair.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Parse a CMake version's numeric components without lossy floating-point
// comparisons. Missing patch/tweak components are equivalent to zero.
// The authored requirement deliberately excludes release suffixes and ranges.
inline std::optional<std::array<std::uint32_t,4>> cmake_numeric_version(
        std::string_view value) {
    if(value.empty()||value.size()>40)return {};
    std::array<std::uint32_t,4> result{};
    std::size_t parts=0;
    for(;;) {
        if(parts==result.size())return {};
        const auto dot=value.find('.');
        const auto part=value.substr(0,dot);
        if(part.empty())return {};
        const auto [end,error]=std::from_chars(part.data(),
            part.data()+part.size(),result[parts]);
        if(error!=std::errc{}||end!=part.data()+part.size())return {};
        ++parts;
        if(dot==std::string_view::npos)break;
        value.remove_prefix(dot+1);
    }
    if(parts<2)return {};
    return result;
}

// Lesson-root structure requirements do not alter the historical cpp_compiler
// payload, source draft, address, output cases or saved answers. A named target
// can require compiled sources and actual links to other generated targets.
struct CmakeRequiredTarget {
    std::string name, type;
    std::vector<std::string> sources, links_to;
    bool exact_sources = false;
    // These opt-in requirements must be expressed by executed, student-authored
    // CMake commands. Toolchain defaults cannot satisfy them.
    bool require_explicit_cxx20 = false;
    bool require_explicit_extensions_off = false;
    // PUBLIC paths must be received by actual linked consumers. PRIVATE
    // paths must be restricted to the owning library's compilation.
    std::vector<std::string> public_include_roots,private_include_roots;
    // Optional authored subproject location, confirmed by CMake's actual
    // codemodel directoryIndex, not by printed messages or source filenames.
    std::string source_directory;
    // Verify options authored on this target and present in its generated
    // compiler command, rather than flags inherited from the MSVC profile.
    std::vector<std::string> authored_compile_options;
    // Require a nonzero MSVC warning level authored on this target without
    // prescribing one particular level. Legacy exact options remain exact.
    bool require_explicit_msvc_warnings = false;
    bool operator==(const CmakeRequiredTarget&) const = default;
};
// A binary option exercise verifies ON and an independent default-OFF build
// of the same student sources without altering original lesson blocks.
struct CmakeOptionSelectionRequirement {
    std::string name, library_target, disabled_source, disabled_stdout;
    bool operator==(const CmakeOptionSelectionRequirement&) const = default;
};
// Require a genuinely authored variable/list used to set properties on the
// declared consumers. CMake's inherited toolchain values do not suffice.
struct CmakeTargetListRequirement {
    std::vector<std::string> targets;
    bool require_append=false;
    bool operator==(const CmakeTargetListRequirement&) const = default;
};
struct CmakeInitialWarning {
    std::string code, path;
    bool operator==(const CmakeInitialWarning&) const = default;
};
// An automatically captured observation of the original faulty expression.
// It does not turn optional output cases into prerequisites or require a
// separate History action. Source provenance comes from expression_repair.
struct CmakeInitialObservation {
    std::string executable, standard_input, standard_output;
    std::uint32_t exit_code = 1;
    std::optional<CmakeInitialWarning> warning;
    bool operator==(const CmakeInitialObservation&) const = default;
};
inline constexpr std::string_view cmake_initial_observation_objective_version =
    "cmake-original-failure-observation-v1";
struct CmakeGraphRequirement {
    std::string project_name;
    std::vector<CmakeRequiredTarget> targets;
    // Optional, exact numeric cmake_minimum_required(VERSION <min>) from the
    // generated codemodel root directory. Does not imply compiler features.
    std::string minimum_cmake_version;
    std::optional<CmakeOptionSelectionRequirement> option_selection;
    // SHA-256 of every original non-CMake file after any specifically
    // allowed source repair. Generated from the immutable original lesson.
    std::vector<std::pair<std::string,std::string>> source_integrity;
    // Ordered status markers bound to the exact original CMake source file.
    std::vector<std::pair<std::string,std::string>> configure_message_order;
    std::optional<CmakeTargetListRequirement> target_list;
    // Opt-in floor semantics. Historical declarations retain their exact
    // numeric requirement and their original receipt frame when this is false.
    bool minimum_cmake_version_at_least = false;
    // The immutable owner supplies the source and one unique expression.
    // All bytes outside that interval and all other non-CMake files stay
    // protected; equivalent replacement expressions are checked by execution.
    std::optional<CppExpressionRepair> expression_repair{};
    std::optional<CmakeInitialObservation> initial_observation{};
    bool operator==(const CmakeGraphRequirement&) const = default;
};

inline bool cmake_initial_observation_values_valid(const CmakeInitialObservation& spec) {
    const auto name = [](std::string_view value) {
        return !value.empty() && value.size() <= 128 &&
            std::ranges::all_of(value, [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '+';
            });
    };
    const auto bounded = [](std::string_view value) {
        return value.size() <= 65536 && value.find('\0') == value.npos;
    };
    if(!name(spec.executable) || !bounded(spec.standard_input) ||
       !bounded(spec.standard_output) || spec.exit_code == 0 || spec.exit_code > 255)
        return false;
    if(spec.warning) {
        const auto& code = spec.warning->code;
        if(code.size() != 5 || !code.starts_with("C4") ||
           !std::ranges::all_of(std::string_view(code).substr(2), [](unsigned char c) {
               return c >= '0' && c <= '9';
           }) || !cpp_expression_repair_detail::path_valid(spec.warning->path))
            return false;
    }
    return true;
}

inline bool cmake_initial_observation_valid(const CmakeGraphRequirement& graph) {
    if(!graph.initial_observation || !graph.expression_repair ||
       !cpp_expression_repair_valid(*graph.expression_repair) ||
       !cmake_initial_observation_values_valid(*graph.initial_observation)) return false;
    const auto& spec = *graph.initial_observation;
    if(!std::ranges::any_of(graph.targets, [&](const auto& target) {
        return target.name == spec.executable && target.type == "EXECUTABLE";
    })) return false;
    if(spec.warning) {
        if(spec.warning->path != graph.expression_repair->path) return false;
        const auto level = [](std::string_view option) {
            if(option.empty() || (option.front() != '/' && option.front() != '-')) return false;
            option.remove_prefix(1);
            return option == "Wall" || (option.size() == 2 && option.front() == 'W' &&
                option.back() >= '1' && option.back() <= '4');
        };
        if(!std::ranges::any_of(graph.targets, [&](const auto& target) {
            return std::ranges::find(target.sources, spec.warning->path) != target.sources.end() &&
                (target.require_explicit_msvc_warnings ||
                 std::ranges::any_of(target.authored_compile_options, level));
        })) return false;
    }
    return true;
}

inline std::optional<std::pair<std::uint32_t, std::uint32_t>>
cmake_initial_observation_warning_lines(const CmakeGraphRequirement& graph) {
    if(!cmake_initial_observation_valid(graph) || !graph.initial_observation->warning) return {};
    const auto& repair = *graph.expression_repair;
    const auto selected = cpp_expression_repair_detail::select(repair);
    if(!selected) return {};
    const auto line_at = [&](std::size_t position) {
        std::uint32_t line = 1;
        for(std::size_t i = 0; i < position; ++i)
            if(repair.original_source[i] == '\n' ||
               (repair.original_source[i] == '\r' &&
                (i + 1 == repair.original_source.size() || repair.original_source[i + 1] != '\n')))
                ++line;
        return line;
    };
    return std::pair{line_at(selected->source[selected->first].begin),
        line_at(selected->source[selected->last].end - 1)};
}

inline std::string cmake_initial_observation_frame(const CmakeInitialObservation& spec) {
    if(!cmake_initial_observation_values_valid(spec))
        throw std::invalid_argument("Invalid original CMake failure observation");
    std::string result;
    const auto field = [&](std::string_view value) {
        result += std::to_string(value.size()) + ':'; result += value;
    };
    field(cmake_initial_observation_objective_version);
    field(spec.executable); field(spec.standard_input); field(spec.standard_output);
    field(std::to_string(spec.exit_code)); field(spec.warning ? "warning" : "no-warning");
    if(spec.warning) { field(spec.warning->code); field(spec.warning->path); }
    return result;
}

} // namespace lgaed::curriculum
