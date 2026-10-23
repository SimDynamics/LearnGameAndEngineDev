#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {
struct CmakeConfig {
    std::string target;
    std::map<std::string,std::string> options;
    std::string build_type="Debug";
    bool operator==(const CmakeConfig&)const=default;
};
using CppProjectFiles=std::vector<std::pair<std::string,std::string>>;
[[nodiscard]] bool is_cpp_source_file(std::string_view);
// Portable paths plus Windows device/case/normalization safeguards. This
// validates virtual paths; materialization also checks real reparse points.
void validate_cpp_project_path(std::string_view);
void validate_cpp_project_files(const CppProjectFiles&);
void validate_cpp_project(const CppProjectFiles&,const std::vector<std::string>& sources);
void validate_cmake_config(const CmakeConfig&);
[[nodiscard]] std::map<std::string,std::string> parse_cmake_options(std::string_view);
// Deterministic length framing, not a display string. File text and whitespace
// remain exact; source/header text is omitted only for configuration identity.
[[nodiscard]] std::string cpp_project_frame(const CppProjectFiles&,const std::vector<std::string>&,
    const std::optional<CmakeConfig>&,bool configuration_only=false);
}
