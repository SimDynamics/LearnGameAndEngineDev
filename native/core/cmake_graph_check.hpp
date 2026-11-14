#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    bool operator==(const CmakeRequiredTarget&) const = default;
};
// A binary option exercise verifies ON and an independent default-OFF build
// of the same student sources without altering original lesson blocks.
struct CmakeOptionSelectionRequirement {
    std::string name, library_target, disabled_source, disabled_stdout;
    bool operator==(const CmakeOptionSelectionRequirement&) const = default;
};
struct CmakeGraphRequirement {
    std::string project_name;
    std::vector<CmakeRequiredTarget> targets;
    // Optional, exact numeric cmake_minimum_required(VERSION <min>) from the
    // generated codemodel root directory. Does not imply compiler features.
    std::string minimum_cmake_version;
    std::optional<CmakeOptionSelectionRequirement> option_selection;
    bool operator==(const CmakeGraphRequirement&) const = default;
};

} // namespace lgaed::curriculum
