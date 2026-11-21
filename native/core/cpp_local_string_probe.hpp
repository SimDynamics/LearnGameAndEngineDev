#pragma once

#include "core/cpp_local_int_probe.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Hidden C++23 source perturbations. A caller must compile and run the original
// source and each isolated variant against separately authored stdout goals.
// Never write an instrumented variant into a student's draft or stored receipt.
enum class CppLocalStringMoment {
    after_initialization,
    after_first_assignment,
    before_output
};

struct CppLocalStringProbe {
    std::string variable;
    std::string replacement;
    CppLocalStringMoment moment=CppLocalStringMoment::after_initialization;
    std::vector<std::string> expected_stdout;
    bool operator==(const CppLocalStringProbe&) const = default;
};
struct CppLocalStringCheck {
    std::vector<CppLocalStringProbe> probes;
    bool operator==(const CppLocalStringCheck&) const = default;
};

inline bool cpp_local_string_check_valid(const CppLocalStringCheck& check) {
    if(check.probes.empty() || check.probes.size()>12)return false;
    std::set<std::pair<std::string,CppLocalStringMoment>> distinct;
    for(const auto& probe:check.probes) {
        if(!cpp_expression_repair_detail::identifier(probe.variable)||
           probe.variable=="main"||probe.replacement.empty()||
           probe.replacement.size()>128||probe.replacement.find('\0')!=std::string::npos||
           probe.expected_stdout.empty()||probe.expected_stdout.size()>4||
           !distinct.emplace(probe.variable,probe.moment).second)return false;
        std::set<std::string> outputs;
        for(const auto& expected:probe.expected_stdout) {
            if(expected.empty()||expected.size()>4096||
               expected.find('\0')!=std::string::npos||
               !outputs.emplace(cpp_local_int_canonical_stdout(expected)).second)return false;
        }
    }
    return true;
}
inline bool cpp_local_string_expected_output(
        const CppLocalStringProbe& probe,std::string_view observed) {
    const auto actual=cpp_local_int_canonical_stdout(observed);
    return std::ranges::any_of(probe.expected_stdout,[&](const auto& wanted){
        return actual==cpp_local_int_canonical_stdout(wanted);
    });
}

namespace cpp_local_string_detail {
using Tokens=cpp_local_int_probe_detail::Tokens;

inline bool root_start(const Tokens& code,std::size_t i) {
    return i>0&&cpp_local_int_probe_detail::boundary(code[i-1].value);
}
inline std::optional<std::size_t> string_type_end(
        const Tokens& code,std::size_t i,std::size_t close) {
    if(i>=close)return {};
    if(code[i].value=="id:string"||code[i].value=="id:auto")return i+1;
    if(i+2<close&&code[i].value=="id:std"&&
       code[i+1].value=="op:::"&&code[i+2].value=="id:string")return i+3;
    return {};
}
inline std::optional<std::size_t> root_declaration_end(
        const Tokens& code,std::size_t opening,std::size_t closing,
        std::string_view variable) {
    const std::string wanted="id:"+std::string(variable);
    std::optional<std::size_t> result;
    std::size_t braces=1,parentheses=0,brackets=0;
    for(std::size_t i=opening+1;i<closing;++i) {
        const auto& value=code[i].value;
        if(braces==1&&!parentheses&&!brackets&&root_start(code,i)) {
            const auto after_type=string_type_end(code,i,closing);
            if(after_type&&*after_type+1<closing&&
               code[*after_type].value==wanted) {
                const auto initial=code[*after_type+1].value;
                if(initial!="op:="&&initial!="op:{"&&
                   initial!="op:("&&initial!="op:;")return {};
                const auto end=cpp_local_int_probe_detail::statement_end(
                    code,*after_type+1,closing);
                if(!end||result)return {};
                result=*end;
            }
        }
        if(value=="op:{")++braces;
        else if(value=="op:}") {
            if(!braces)return {};
            --braces;
        }else if(value=="op:(")++parentheses;
        else if(value=="op:)") {
            if(!parentheses)return {};
            --parentheses;
        }else if(value=="op:[")++brackets;
        else if(value=="op:]") {
            if(!brackets)return {};
            --brackets;
        }
    }
    return result;
}

inline std::optional<std::size_t> root_reassignment_end(
        const Tokens& code,std::size_t after,std::size_t closing,
        std::string_view variable) {
    const std::string wanted="id:"+std::string(variable);
    std::size_t braces=1,parentheses=0,brackets=0;
    for(std::size_t i=after;i<closing;++i) {
        const auto& value=code[i].value;
        if(braces==1&&!parentheses&&!brackets&&root_start(code,i)&&
           value==wanted&&i+1<closing&&
           (code[i+1].value=="op:="||code[i+1].value=="op:+="))
            return cpp_local_int_probe_detail::statement_end(code,i+2,closing);
        if(value=="op:{")++braces;
        else if(value=="op:}") {
            if(!braces)return {};
            --braces;
        }else if(value=="op:(")++parentheses;
        else if(value=="op:)") {
            if(!parentheses)return {};
            --parentheses;
        }else if(value=="op:[")++brackets;
        else if(value=="op:]") {
            if(!brackets)return {};
            --brackets;
        }
    }
    return {};
}

// Return the first root-level output statement that really reads this symbol.
// An unrelated literal saying "etiqueta" does not count as a variable use.
inline std::optional<std::size_t> first_output_start(
        const Tokens& code,std::size_t after,std::size_t closing,
        std::string_view variable) {
    const std::string wanted="id:"+std::string(variable);
    std::size_t braces=1,parentheses=0,brackets=0;
    for(std::size_t i=after;i<closing;++i) {
        const auto& value=code[i].value;
        if(braces==1&&!parentheses&&!brackets&&root_start(code,i)) {
            const auto first=(value=="id:cout")?i:
                (i+2<closing&&value=="id:std"&&
                 code[i+1].value=="op:::"&&code[i+2].value=="id:cout")?i+2:closing;
            if(first<closing) {
                const auto end=cpp_local_int_probe_detail::statement_end(
                    code,first+1,closing);
                if(!end)return {};
                bool inserted=false,reads_variable=false;
                for(auto n=first+1;n<*end;++n) {
                    inserted=inserted||code[n].value=="op:<<";
                    reads_variable=reads_variable||code[n].value==wanted;
                }
                if(inserted&&reads_variable)return i;
            }
        }
        if(value=="op:{")++braces;
        else if(value=="op:}") {
            if(!braces)return {};
            --braces;
        }else if(value=="op:(")++parentheses;
        else if(value=="op:)") {
            if(!parentheses)return {};
            --parentheses;
        }else if(value=="op:[")++brackets;
        else if(value=="op:]") {
            if(!brackets)return {};
            --brackets;
        }
    }
    return {};
}

// Encode individual UTF-8 bytes as fixed-width C++ octal escapes. This keeps
// leading zeros, spacing and non-ASCII bytes exact without source-encoding or
// adjacent-hex-escape ambiguities. Reject NUL rather than creating two meanings.
inline std::string cpp_bytes_literal(std::string_view value) {
    std::string result{"\""};
    result.reserve(2+value.size()*4);
    for(const unsigned char c:value) {
        result+='\\';
        result+=static_cast<char>('0'+((c>>6)&7));
        result+=static_cast<char>('0'+((c>>3)&7));
        result+=static_cast<char>('0'+(c&7));
    }
    result+='"';
    return result;
}
} // namespace cpp_local_string_detail

[[nodiscard]] inline std::optional<std::string> cpp_local_string_override(
        std::string_view source,const CppLocalStringProbe& probe) {
    if(!cpp_expression_repair_detail::identifier(probe.variable)||
       probe.variable=="main"||probe.replacement.empty()||
       probe.replacement.size()>128||
       probe.replacement.find('\0')!=std::string::npos)return {};
    const auto code=cpp_local_int_probe_detail::lexical(source);
    if(!code)return {};
    const auto body=cpp_local_int_probe_detail::main_body(*code);
    if(!body)return {};
    const auto declaration=cpp_local_string_detail::root_declaration_end(
        *code,body->first,body->second,probe.variable);
    if(!declaration)return {};
    auto location=(*code)[*declaration].end;
    bool before=false;
    if(probe.moment==CppLocalStringMoment::after_first_assignment) {
        const auto assignment=cpp_local_string_detail::root_reassignment_end(
            *code,*declaration+1,body->second,probe.variable);
        if(!assignment)return {};
        location=(*code)[*assignment].end;
    }else if(probe.moment==CppLocalStringMoment::before_output) {
        const auto output=cpp_local_string_detail::first_output_start(
            *code,*declaration+1,body->second,probe.variable);
        if(!output)return {};
        location=(*code)[*output].begin;
        before=true;
    }else if(probe.moment!=CppLocalStringMoment::after_initialization)return {};
    std::string inserted;
    inserted.reserve(probe.variable.size()+probe.replacement.size()*4+50);
    if(!before)inserted+='\n';
    inserted+="    ";
    inserted+=probe.variable;
    inserted+=" = std::string{";
    inserted+=cpp_local_string_detail::cpp_bytes_literal(probe.replacement);
    inserted+="};\n";
    std::string result;
    result.reserve(source.size()+inserted.size());
    result+=source.substr(0,location);
    result+=inserted;
    result+=source.substr(location);
    return result;
}
} // namespace lgaed::curriculum
