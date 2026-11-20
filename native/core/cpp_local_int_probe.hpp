#pragma once

#include "core/cpp_expression_repair.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// An opt-in source perturbation only. The caller must compile and run both
// the unchanged student program and each isolated probe against independent
// output goals. This instrumented source is never a learner draft or a receipt.
struct CppLocalIntProbe {
    std::string variable;
    std::int32_t replacement = 0;
    // Separate, independently authored outputs after replacing this int.
    // The ordinary visible OutputCheck is not part of this hidden evidence.
    std::vector<std::string> expected_stdout;
    bool operator==(const CppLocalIntProbe&) const = default;
};

struct CppLocalIntCheck {
    std::vector<CppLocalIntProbe> probes;
    bool operator==(const CppLocalIntCheck&) const = default;
};

inline bool cpp_local_int_check_valid(const CppLocalIntCheck& check) {
    if(check.probes.empty() || check.probes.size()>8)return false;
    for(std::size_t index=0;index<check.probes.size();++index) {
        const auto& probe=check.probes[index];
        if(!cpp_expression_repair_detail::identifier(probe.variable)||
           probe.variable=="main"||probe.expected_stdout.empty()||
           probe.expected_stdout.size()>4)return false;
        std::set<std::string> distinct;
        for(const auto& output:probe.expected_stdout)
            if(output.empty()||output.size()>4096||
               output.find('\0')!=std::string::npos||
               !distinct.insert(output).second)return false;
        for(std::size_t prior=0;prior<index;++prior)
            if(check.probes[prior].variable==probe.variable&&
               check.probes[prior].replacement==probe.replacement)return false;
    }
    return true;
}

inline bool cpp_local_int_expected_output(const CppLocalIntProbe& probe,
                                           std::string_view observed) {
    for(const auto& expected:probe.expected_stdout) {
        if(observed==expected)return true;
        // CRLF from a native std::ostream is an alternative spelling only
        // when the authored expectation contains LF but no literal CR.
        if(expected.find('\r')!=expected.npos||expected.find('\n')==expected.npos)continue;
        std::string native;
        native.reserve(expected.size()+std::count(expected.begin(),expected.end(),'\n'));
        for(const char c:expected) {
            if(c=='\n')native+='\r';
            native+=c;
        }
        if(observed==native)return true;
    }
    return false;
}

namespace cpp_local_int_probe_detail {
using Token = cpp_expression_repair_detail::Token;
using Tokens = std::vector<Token>;

inline bool boundary(std::string_view token) {
    return token == "op:{" || token == "op:;" || token == "op:}";
}

inline std::optional<Tokens> lexical(std::string_view source) {
    auto parsed = cpp_expression_repair_detail::tokens(source);
    if (!parsed || !cpp_expression_repair_detail::source_tokens_valid(*parsed))
        return {};
    Tokens code;
    code.reserve(parsed->size());
    for (auto& item : *parsed)
        if (!item.value.starts_with("comment:")) code.push_back(std::move(item));
    return code;
}

inline std::optional<std::pair<std::size_t, std::size_t>> main_body(
        const Tokens& code) {
    std::optional<std::pair<std::size_t, std::size_t>> result;
    for (std::size_t i = 0; i + 3 < code.size(); ++i) {
        if (code[i].value != "id:int" || code[i + 1].value != "id:main" ||
            code[i + 2].value != "op:(") continue;
        std::size_t parentheses = 1;
        auto at = i + 3;
        for (; at < code.size() && parentheses; ++at) {
            if (code[at].value == "op:(") ++parentheses;
            else if (code[at].value == "op:)") --parentheses;
        }
        if (parentheses || at >= code.size() || code[at].value != "op:{")
            continue;
        const auto opening = at;
        std::size_t braces = 1;
        for (++at; at < code.size() && braces; ++at) {
            if (code[at].value == "op:{") ++braces;
            else if (code[at].value == "op:}") --braces;
        }
        if (braces || result) return {};
        result = std::pair{opening, at - 1};
    }
    return result;
}

// Never guess the insertion position within a multi-declarator statement.
inline std::optional<std::size_t> statement_end(
        const Tokens& code, std::size_t first, std::size_t stop) {
    std::size_t parentheses = 0, braces = 0, brackets = 0;
    for (auto i = first; i < stop; ++i) {
        const auto& t = code[i].value;
        if (t == "op:;" && !parentheses && !braces && !brackets) return i;
        if (t == "op:," && !parentheses && !braces && !brackets) return {};
        if (t == "op:(") ++parentheses;
        else if (t == "op:{") ++braces;
        else if (t == "op:[") ++brackets;
        else if (t == "op:)") {
            if (!parentheses) return {};
            --parentheses;
        } else if (t == "op:}") {
            if (!braces) return {};
            --braces;
        } else if (t == "op:]") {
            if (!brackets) return {};
            --brackets;
        }
    }
    return {};
}

struct Declaration {
    std::size_t end;
    bool initialized;
};

inline std::optional<Declaration> local_declaration(
        const Tokens& code, std::size_t opening, std::size_t closing,
        std::string_view variable) {
    std::optional<Declaration> result;
    std::size_t braces = 1, parentheses = 0, brackets = 0;
    const auto wanted = "id:" + std::string(variable);
    for (auto i = opening + 1; i < closing; ++i) {
        const auto& t = code[i].value;
        if (braces == 1 && !parentheses && !brackets &&
            t == "id:int" && i + 2 < closing && code[i + 1].value == wanted) {
            if (i == 0 || !boundary(code[i - 1].value) || result)
                return {};
            const auto& start = code[i + 2].value;
            if (start != "op:=" && start != "op:{" &&
                start != "op:(" && start != "op:;") return {};
            const auto end = statement_end(code, i + 2, closing);
            if (!end) return {};
            result = Declaration{*end, start != "op:;"};
        }
        if (t == "op:{") ++braces;
        else if (t == "op:}") {
            if (!braces) return {};
            --braces;
        } else if (t == "op:(") ++parentheses;
        else if (t == "op:)") {
            if (!parentheses) return {};
            --parentheses;
        } else if (t == "op:[") ++brackets;
        else if (t == "op:]") {
            if (!brackets) return {};
            --brackets;
        }
    }
    return result;
}

// A plain int may be initialized in a separate complete statement. Refuse
// any read or conditional update before that assignment, rather than
// fabricating a probe at a location that did not establish its first value.
inline std::optional<std::size_t> first_assignment(
        const Tokens& code, std::size_t after, std::size_t closing,
        std::string_view variable) {
    const auto wanted = "id:" + std::string(variable);
    std::size_t braces = 1, parentheses = 0, brackets = 0;
    for (auto i = after; i < closing; ++i) {
        const auto& t = code[i].value;
        if (t == wanted) {
            if (braces != 1 || parentheses || brackets ||
                i + 1 >= closing || code[i + 1].value != "op:=" ||
                !boundary(code[i - 1].value)) return {};
            return statement_end(code, i + 2, closing);
        }
        if (t == "op:{") ++braces;
        else if (t == "op:}") {
            if (!braces) return {};
            --braces;
        } else if (t == "op:(") ++parentheses;
        else if (t == "op:)") {
            if (!parentheses) return {};
            --parentheses;
        } else if (t == "op:[") ++brackets;
        else if (t == "op:]") {
            if (!brackets) return {};
            --brackets;
        }
    }
    return {};
}
} // namespace cpp_local_int_probe_detail

// Insert a new value just after a complete local int initialization inside
// main(). Reject ambiguous syntax, macros and other scopes instead of
// substituting coincidental names in strings, comments or unrelated code.
[[nodiscard]] inline std::optional<std::string> cpp_local_int_override(
        std::string_view source, const CppLocalIntProbe& probe) {
    if (!cpp_expression_repair_detail::identifier(probe.variable) ||
        probe.variable == "main") return {};
    const auto code = cpp_local_int_probe_detail::lexical(source);
    if (!code) return {};
    const auto body = cpp_local_int_probe_detail::main_body(*code);
    if (!body) return {};
    const auto declaration = cpp_local_int_probe_detail::local_declaration(
        *code, body->first, body->second, probe.variable);
    if (!declaration) return {};
    auto end = declaration->end;
    if (!declaration->initialized) {
        const auto assignment = cpp_local_int_probe_detail::first_assignment(
            *code, end + 1, body->second, probe.variable);
        if (!assignment) return {};
        end = *assignment;
    }
    const auto offset = (*code)[end].end;
    std::string variant;
    variant.reserve(source.size() + probe.variable.size() + 32);
    variant += source.substr(0, offset);
    variant += "\n    ";
    variant += probe.variable;
    variant += " = ";
    variant += std::to_string(probe.replacement);
    variant += ";\n";
    variant += source.substr(offset);
    return variant;
}

} // namespace lgaed::curriculum
