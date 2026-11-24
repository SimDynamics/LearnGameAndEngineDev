#pragma once

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

// Authored private executions of an unchanged program. These do not add public
// output cases or constrain the student's choice of variable names or syntax.
struct CppInputProbe {
    std::string standard_input;
    std::vector<std::string> outputs;
    bool operator==(const CppInputProbe&) const = default;
};
struct CppInputCheck {
    std::vector<CppInputProbe> probes;
    // The loader still requires a whole probe to distinguish the first case.
    // An absent option retains the historical per-probe requirement.
    bool allow_same_output = false;
    bool operator==(const CppInputCheck&) const = default;
};

inline std::string cpp_input_normalize_newlines(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for(std::size_t index=0;index<text.size();++index) {
        if(text[index]=='\r'&&index+1<text.size()&&text[index+1]=='\n')continue;
        result+=text[index];
    }
    return result;
}

// Match the six ASCII separators used by the public input evaluator. Unicode
// whitespace is data, and exact input comparison deliberately retains CRLF.
inline std::string cpp_input_case_key(std::string_view text,bool token_comparison) {
    if(!token_comparison)return std::string(text);
    const auto separator=[](char c) {
        return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';
    };
    std::string result;
    for(std::size_t index=0;index<text.size();) {
        if(separator(text[index])) {++index;continue;}
        const auto start=index;
        while(index<text.size()&&!separator(text[index]))++index;
        result+=std::to_string(index-start)+":";
        result.append(text.substr(start,index-start));
    }
    return result;
}

inline bool cpp_input_check_valid(const CppInputCheck& check) {
    if(check.probes.empty()||check.probes.size()>8)return false;
    std::set<std::string> inputs;
    for(const auto& probe:check.probes) {
        if(probe.standard_input.size()>4096||
           probe.standard_input.find('\0')!=std::string::npos||
           !inputs.insert(probe.standard_input).second||
           probe.outputs.empty()||probe.outputs.size()>4)return false;
        std::set<std::string> outputs;
        for(const auto& output:probe.outputs) {
            if(output.size()>4096||output.find('\0')!=std::string::npos||
               !outputs.insert(cpp_input_normalize_newlines(output)).second)return false;
        }
    }
    return true;
}

inline bool cpp_input_expected_output(std::string_view observed,const CppInputProbe& probe) {
    const auto canonical=cpp_input_normalize_newlines(observed);
    return std::ranges::any_of(probe.outputs,[&](const auto& expected) {
        return cpp_input_normalize_newlines(expected)==canonical;
    });
}

} // namespace lgaed::curriculum
