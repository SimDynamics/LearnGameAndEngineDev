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
    std::vector<std::string> expected_stdout{};
    // False: replace the initialized local int. True: add an independent
    // delta immediately after its first complete root-level state update.
    // This witnesses a changed increase without rewriting the student's
    // original initializer, addition expression or saved source.
    bool after_update = false;
    bool operator==(const CppLocalIntProbe&) const = default;
};

struct CppLocalIntCheck {
    std::vector<CppLocalIntProbe> probes;
    bool operator==(const CppLocalIntCheck&) const = default;
};

// The expression evaluator also recognizes tokens such as numeric literals
// and bool constants; its keyword test alone is not a C++ identifier lexer.
// Require the same identifier boundaries as the source tokenizer, including
// its existing support for non-ASCII identifier bytes.
[[nodiscard]] inline bool cpp_local_probe_identifier(std::string_view name) {
    if(!cpp_expression_repair_detail::identifier(name)||
       name=="true"||name=="false"||name=="nullptr")return false;
    if(!code_format::identifier_start(static_cast<unsigned char>(name.front())))
        return false;
    for(std::size_t i=1;i<name.size();++i)
        if(!code_format::identifier_part(static_cast<unsigned char>(name[i])))
            return false;
    return true;
}

// A console may spell one line ending as LF or CRLF. Normalize those
// spellings before deciding whether two expected observations really differ.
[[nodiscard]] inline std::string cpp_local_int_canonical_stdout(
        std::string_view bytes) {
    std::string canonical;
    canonical.reserve(bytes.size());
    for(std::size_t i=0;i<bytes.size();++i) {
        if(bytes[i]=='\r'&&i+1<bytes.size()&&bytes[i+1]=='\n')continue;
        canonical+=bytes[i];
    }
    return canonical;
}

inline bool cpp_local_int_check_valid(const CppLocalIntCheck& check) {
    if(check.probes.empty() || check.probes.size()>8)return false;
    for(std::size_t index=0;index<check.probes.size();++index) {
        const auto& probe=check.probes[index];
        if(!cpp_local_probe_identifier(probe.variable)||
           probe.variable=="main"||probe.expected_stdout.empty()||
           probe.expected_stdout.size()>4)return false;
        std::set<std::string> distinct;
        for(const auto& output:probe.expected_stdout)
            if(output.empty()||output.size()>4096||
               output.find('\0')!=std::string::npos||
               !distinct.insert(cpp_local_int_canonical_stdout(output)).second)return false;
        for(std::size_t prior=0;prior<index;++prior)
            if(check.probes[prior].variable==probe.variable&&
               check.probes[prior].replacement==probe.replacement&&
               check.probes[prior].after_update==probe.after_update)return false;
    }
    return true;
}

inline bool cpp_local_int_expected_output(const CppLocalIntProbe& probe,
                                           std::string_view observed) {
    const auto canonical=cpp_local_int_canonical_stdout(observed);
    for(const auto& expected:probe.expected_stdout)
        if(canonical==cpp_local_int_canonical_stdout(expected))return true;
    return false;
}

namespace cpp_local_int_probe_detail {
using Token = cpp_expression_repair_detail::Token;
using Tokens = std::vector<Token>;

inline bool boundary(std::string_view token) {
    return token == "op:{" || token == "op:;" || token == "op:}";
}

// A field or namespace-qualified spelling is not the unqualified local.
// Reuse this rule in declarations and pre-initialization read checks.
inline bool qualified_member_name(const Tokens& code,std::size_t index) {
    if(index==0)return false;
    const auto previous=std::string_view(code[index-1].value);
    return previous=="op:."||previous=="op:->"||previous=="op:::";
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
        if (parentheses) continue;
        // An ordinary int main() may have a non-throwing exception
        // specification. It does not change the location or ownership of
        // the function body. Skip the complete, balanced noexcept clause,
        // but never treat its nested parentheses or braces as the body.
        if (at < code.size() && code[at].value == "id:noexcept") {
            ++at;
            if (at < code.size() && code[at].value == "op:(") {
                std::size_t depth = 1;
                for (++at; at < code.size() && depth; ++at) {
                    if (code[at].value == "op:(") ++depth;
                    else if (code[at].value == "op:)") --depth;
                }
                if (depth) continue;
            }
        }
        if (at >= code.size() || code[at].value != "op:{")
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

// Walk all declarators in one complete signed/int statement. An inserted
// mutation belongs AFTER the final semicolon, never between declarators.
// Reject pointers, references, arrays, duplicate names and a later initializer
// that reads the probed int: mutating after the statement would otherwise
// miss that data dependency and could falsely credit an independent output.
inline std::optional<Declaration> simple_int_declaration(
        const Tokens& code, std::size_t at, std::size_t closing,
        std::string_view variable) {
    const std::string wanted="id:"+std::string(variable);
    std::optional<bool> initialized;
    for(;;) {
        if(at+1>=closing||!code[at].value.starts_with("id:")||
           !cpp_local_probe_identifier(
               std::string_view(code[at].value).substr(3)))return {};
        const bool owns=code[at].value==wanted;
        const auto first=code[at+1].value;
        if(first!="op:="&&first!="op:{"&&first!="op:("&&
           first!="op:,"&&first!="op:;")return {};
        if(owns) {
            if(initialized)return {};
            initialized=first!="op:,"&&first!="op:;";
        }
        std::size_t parentheses=0,braces=0,brackets=0;
        std::size_t end=at+1;
        for(;end<closing;++end) {
            const auto& token=code[end].value;
            if(!parentheses&&!braces&&!brackets&&
               (token=="op:,"||token=="op:;"))break;
            // A sibling initialized from this value would be frozen before
            // our after-statement variation, giving a misleading result.
            if(token==wanted&&!qualified_member_name(code,end))return {};
            if(token=="op:(")++parentheses;
            else if(token=="op:{")++braces;
            else if(token=="op:[")++brackets;
            else if(token=="op:)") {
                if(!parentheses)return {};
                --parentheses;
            }else if(token=="op:}") {
                if(!braces)return {};
                --braces;
            }else if(token=="op:]") {
                if(!brackets)return {};
                --brackets;
            }
        }
        if(end>=closing||parentheses||braces||brackets)return {};
        if(code[end].value=="op:;")
            return initialized?std::optional<Declaration>{
                Declaration{end,*initialized}}:std::nullopt;
        at=end+1;
    }
}

inline std::optional<Declaration> local_declaration(
        const Tokens& code, std::size_t opening, std::size_t closing,
        std::string_view variable) {
    std::optional<Declaration> result;
    std::size_t braces = 1, parentheses = 0, brackets = 0;
    for (auto i = opening + 1; i < closing; ++i) {
        const auto& t = code[i].value;
        // signed, signed int and int signed all declare the C++ type int.
        // Match only complete main-body declarations, not nested scopes.
        if (braces == 1 && !parentheses && !brackets &&
            i > 0 && boundary(code[i - 1].value) &&
            (t == "id:int" || t == "id:signed")) {
            auto name = i + 1;
            if (name < closing &&
                ((t == "id:signed" && code[name].value == "id:int") ||
                 (t == "id:int" && code[name].value == "id:signed")))
                ++name;
            if(name < closing) {
                if(const auto found=simple_int_declaration(
                        code,name,closing,variable)) {
                    if(result)return {};
                    result=*found;
                }
            }
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
            // Other objects may expose a field of the same name before
            // the local's first assignment. This is not a read of the local.
            if(qualified_member_name(code,i))continue;
            if (braces != 1 || parentheses || brackets ||
                i + 1 >= closing || code[i + 1].value != "op:=" ||
                !boundary(code[i - 1].value)) return {};
            const auto end=statement_end(code,i+2,closing);
            if(!end)return {};
            // An uninitialized local cannot establish its first value
            // by reading itself. But a member or qualified name with the
            // same spelling (data.llaves, ptr->llaves, config::llaves) is
            // a different object and must not be rejected as that read.
            for(auto j=i+2;j<*end;++j)
                if(code[j].value==wanted&&!qualified_member_name(code,j))
                    return {};
            return end;
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
// Locate an actual change to an initialized local int at the main-body
// level, rather than rewriting a literal 3. Both "aforo += 3" and
// "aforo = aforo + 3" are accepted, as well as equivalent +/- or increment
// syntax. A single extra addition is made AFTER the complete statement:
// a copy taken before it must stay independent; a copy made afterwards
// must see the modified state. Nested or expression-only writes fail closed.
inline std::optional<std::size_t> first_state_update(
        const Tokens& code,std::size_t after,std::size_t closing,
        std::string_view variable) {
    const auto wanted="id:"+std::string(variable);
    std::size_t braces=1,parentheses=0,brackets=0;
    const auto updates=[](std::string_view value) {
        return value=="op:=" || value=="op:+=" || value=="op:-=" ||
               value=="op:++" || value=="op:--";
    };
    for(std::size_t i=after;i<closing;++i) {
        const auto& t=code[i].value;
        if(braces==1&&!parentheses&&!brackets&&i>0&&
           boundary(code[i-1].value)) {
            const bool postfix=t==wanted&&i+1<closing&&
                updates(code[i+1].value);
            const bool prefix=(t=="op:++"||t=="op:--")&&
                i+1<closing&&code[i+1].value==wanted;
            if(postfix||prefix) {
                const auto statement=statement_end(code,i+2,closing);
                if(!statement)return {};
                if(postfix&&code[i+1].value=="op:=") {
                    // A plain replacement (aforo = 8) is not an increase:
                    // the after-update variation would still produce the
                    // expected stdout without proving use of the prior state.
                    // Require an unqualified read of this same local int.
                    bool reads_prior=false,has_operation=false;
                    for(auto j=i+2;j<*statement;++j) {
                        const auto& rhs=code[j].value;
                        if(rhs==wanted&&!qualified_member_name(code,j))
                            reads_prior=true;
                        if(rhs!="op:("&&rhs!="op:)"&&rhs!=wanted)
                            has_operation=true;
                    }
                    // aforo = aforo and aforo = (aforo) are no-ops, not
                    // evidence of an actual state change.
                    if(!reads_prior||!has_operation)return {};
                }
                return statement;
            }
        }
        if(t=="op:{")++braces;
        else if(t=="op:}") {
            if(!braces)return {};
            --braces;
        } else if(t=="op:(")++parentheses;
        else if(t=="op:)") {
            if(!parentheses)return {};
            --parentheses;
        } else if(t=="op:[")++brackets;
        else if(t=="op:]") {
            if(!brackets)return {};
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
    if (!cpp_local_probe_identifier(probe.variable) ||
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
    if(probe.after_update) {
        const auto updated=cpp_local_int_probe_detail::first_state_update(
            *code,end+1,body->second,probe.variable);
        if(!updated)return {};
        end=*updated;
    }
    const auto offset = (*code)[end].end;
    std::string variant;
    variant.reserve(source.size() + probe.variable.size() + 32);
    variant += source.substr(0, offset);
    variant += "\n    ";
    variant += probe.variable;
    variant += probe.after_update?" += ":" = ";
    variant += std::to_string(probe.replacement);
    variant += ";\n";
    variant += source.substr(offset);
    return variant;
}

} // namespace lgaed::curriculum
