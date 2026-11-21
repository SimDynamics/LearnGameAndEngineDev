#pragma once

#include "core/cpp_expression_repair.hpp"

#include <algorithm>
#include <charconv>
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

// The private probe recompiles a modified file. A learner must not be able
// to produce the "changed" stdout from the probe's extra source lines,
// file name, build time or a premature exit rather than the local int.
// Inspect lexical identifiers only: comments and C++ string/raw literals
// explaining __LINE__ or exit are inert and must remain admissible.
inline bool recompile_observation_neutral(const Tokens& code) {
    constexpr std::string_view sensitive[]{
        "__LINE__", "__FILE__", "__FILEW__", "__BASE_FILE__", "__COUNTER__",
        "__INCLUDE_LEVEL__", "__DATE__", "__TIME__", "__TIMESTAMP__",
        "__func__", "__FUNCTION__", "__PRETTY_FUNCTION__", "__FUNCSIG__",
        "__FUNCDNAME__", "__builtin_LINE", "__builtin_COLUMN",
        "__builtin_FILE", "__builtin_FUNCTION", "__builtin_source_location",
        "source_location",
        // An early process exit can bypass the after-update state witness.
        // These are not required by elementary local-variable exercises.
        "exit", "_Exit", "_exit", "quick_exit", "_c_exit", "ExitProcess",
        "ExitThread", "TerminateProcess", "TerminateJobObject",
        "_endthread", "_endthreadex", "abort", "terminate",
        "longjmp", "_longjmp", "siglongjmp"
    };
    return std::ranges::none_of(code,[&](const Token& token) {
        if(!token.value.starts_with("id:"))return false;
        const auto name=std::string_view(token.value).substr(3);
        return std::ranges::find(sensitive,name)!=std::end(sensitive);
    });
}

inline std::optional<Tokens> lexical(std::string_view source) {
    auto parsed = cpp_expression_repair_detail::tokens(source);
    if (!parsed || !cpp_expression_repair_detail::source_tokens_valid(*parsed))
        return {};
    Tokens code;
    code.reserve(parsed->size());
    for (auto& item : *parsed)
        if (!item.value.starts_with("comment:")) code.push_back(std::move(item));
    if(!recompile_observation_neutral(code))return {};
    return code;
}

inline std::optional<std::pair<std::size_t, std::size_t>> main_body(
        const Tokens& code) {
    std::optional<std::pair<std::size_t, std::size_t>> result;
    for (std::size_t i = 0; i + 3 < code.size(); ++i) {
        const bool trailing=code[i].value=="id:auto";
        if ((!trailing&&code[i].value!="id:int") ||
            code[i + 1].value != "id:main" ||
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
        // C++ permits a trailing return spelling for main:
        // auto main() -> int { ... }. Preserve the same root-body and
        // balanced-noexcept protections as an ordinary int main().
        // Never interpret an arbitrary trailing type as a valid main.
        if(trailing) {
            if(at+1>=code.size()||code[at].value!="op:->"||
               code[at+1].value!="id:int")continue;
            at+=2;
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
// A hidden after-update perturbation cannot certify an increase if the
// student's statement did not change the preceding state at all. Recognize
// only provable literal identities; more complex expressions still require
// independent compiled output evidence rather than guessed arithmetic.
inline bool int_probe_literal_is(std::string_view token,char wanted) {
    if(!token.starts_with("number:")||(wanted!='0'&&wanted!='1'))return false;
    auto text=token.substr(7);
    while(!text.empty()&&(text.back()=='u'||text.back()=='U'||
           text.back()=='l'||text.back()=='L'||text.back()=='z'||
           text.back()=='Z'))text.remove_suffix(1);
    if(text.starts_with("0x")||text.starts_with("0X")||
       text.starts_with("0b")||text.starts_with("0B"))text.remove_prefix(2);
    if(text.empty())return false;
    bool one=false,has_digit=false;
    for(char ch:text) {
        if(ch=='\'')continue;
        if(ch=='0') {
            // The only digit 1 of an integer equal to one must be the
            // least-significant digit. For example, 01 is one, but 10,
            // 0x10 and 0b10 are not.
            if(one)return false;
            has_digit=true;
        }else if(ch=='1'&&wanted=='1'&&!one) {
            has_digit=true;one=true;
        }else return false;
    }
    return has_digit&&(wanted=='0'||one);
}

// Recognize only provably neutral integer expressions: a local variable,
// literal 0/1, parentheses and operations which preserve that value. Parse
// complete additive/multiplicative groups with their actual C++ precedence,
// rather than checking only one operator. No calls, casts, assignments, side
// effects or unknown variables are inferred as identities. This prevents a
// syntactically longer no-op from passing an after-update counterfactual.
inline bool int_probe_trivial_noop(const Tokens& code,std::size_t statement,
        std::size_t end,std::string_view variable) {
    if(statement+2>=end)return false;
    const auto op=std::string_view(code[statement+1].value);
    if(op!="op:="&&op!="op:+="&&op!="op:-=")return false;
    const std::string wanted="id:"+std::string(variable);
    enum class Known { unknown, zero, one, local };
    const auto unwrapped=[&](std::size_t a,std::size_t b) {
        while(b>a+1&&code[a].value=="op:("&&code[b-1].value=="op:)") {
            std::size_t depth=0;
            bool enclosing=true;
            for(auto p=a;p<b;++p) {
                if(code[p].value=="op:(")++depth;
                else if(code[p].value=="op:)") {
                    if(!depth){enclosing=false;break;}
                    --depth;
                    if(!depth&&p+1<b)enclosing=false;
                }
            }
            if(!enclosing||depth)break;
            ++a;--b;
        }
        return std::pair{a,b};
    };
    // Evaluate only bounded, side-effect-free decimal int constants. This
    // handles expressions such as (2-1), (4/2-2), and (6%3), without guessing
    // about a variable, a call, a cast, an unsigned suffix or a floating type.
    // Every intermediate is small enough to avoid signed overflow.
    const auto small_constant=[&](auto&& self,std::size_t first,
            std::size_t last,unsigned depth)->std::optional<std::int64_t> {
        if(depth>32||first>=last)return {};
        const auto [start,stop]=unwrapped(first,last);
        if(start>=stop)return {};
        if(stop==start+1) {
            if(!code[start].value.starts_with("number:"))return {};
            const auto digits=std::string_view(code[start].value).substr(7);
            if(digits.empty()||(digits.size()>1&&digits.front()=='0'))return {};
            std::int32_t literal=0;
            const auto [parsed_end,parse_error]=std::from_chars(
                digits.data(),digits.data()+digits.size(),literal,10);
            if(parse_error!=std::errc{}||
               parsed_end!=digits.data()+digits.size()||
               literal>4096)return {};
            return literal;
        }
        if(code[start].value=="op:+"||code[start].value=="op:-") {
            const auto inner=self(self,start+1,stop,depth+1);
            if(!inner)return {};
            return code[start].value=="op:-"?-*inner:*inner;
        }
        const auto top_operator=[&](bool additive)->std::size_t {
            std::size_t found=stop,nested=0;
            for(auto i=start;i<stop;++i) {
                const auto token=std::string_view(code[i].value);
                if(token=="op:(")++nested;
                else if(token=="op:)") {
                    if(!nested)return stop;
                    --nested;
                }else if(!nested&&i>start&&i+1<stop&&
                    (additive?(token=="op:+"||token=="op:-"):
                              (token=="op:*"||token=="op:/"||token=="op:%")))
                    found=i;
            }
            return nested?stop:found;
        };
        auto split=top_operator(true);
        if(split==stop)split=top_operator(false);
        if(split==stop)return {};
        const auto left=self(self,start,split,depth+1);
        const auto right=self(self,split+1,stop,depth+1);
        if(!left||!right)return {};
        const auto op_value=std::string_view(code[split].value);
        std::int64_t result=0;
        if(op_value=="op:+")result=*left+*right;
        else if(op_value=="op:-")result=*left-*right;
        else if(op_value=="op:*")result=*left**right;
        else if(op_value=="op:/"||op_value=="op:%") {
            if(*right==0)return {};
            result=op_value=="op:/"?*left / *right:*left % *right;
        }else return {};
        if(result < -4096||result>4096)return {};
        return result;
    };
    const auto known=[&](auto&& self,std::size_t first,
                         std::size_t last,unsigned depth)->Known {
        if(depth>64||first>=last)return Known::unknown;
        const auto [a,b]=unwrapped(first,last);
        if(a>=b)return Known::unknown;
        if(b==a+1) {
            if(code[a].value==wanted&&
               !qualified_member_name(code,a))return Known::local;
            if(int_probe_literal_is(code[a].value,'0'))return Known::zero;
            if(int_probe_literal_is(code[a].value,'1'))return Known::one;
            return Known::unknown;
        }
        // Only literal arithmetic can be folded. A computed identity factor
        // must not turn a harmless assignment into a fake U2L3 increase.
        if(const auto value=small_constant(small_constant,a,b,0)) {
            if(*value==0)return Known::zero;
            if(*value==1)return Known::one;
        }
        // Unary signs are permitted only where they cannot introduce an
        // unproved value (a negated zero stays zero).
        if(code[a].value=="op:+"||code[a].value=="op:-") {
            const auto inner=self(self,a+1,b,depth+1);
            return code[a].value=="op:+"||inner==Known::zero?
                inner:Known::unknown;
        }
        const auto rightmost=[&](bool additive) {
            std::size_t at=b,nesting=0;
            for(auto i=a;i<b;++i) {
                const auto token=std::string_view(code[i].value);
                if(token=="op:(")++nesting;
                else if(token=="op:)") {
                    if(!nesting)return b;
                    --nesting;
                }else if(!nesting&&i>a&&i+1<b&&
                        (additive?(token=="op:+"||token=="op:-"):
                                  (token=="op:*"||token=="op:/")))at=i;
            }
            return nesting?b:at;
        };
        auto at=rightmost(true);
        if(at==b)at=rightmost(false);
        if(at==b)return Known::unknown;
        const auto left=self(self,a,at,depth+1);
        const auto right=self(self,at+1,b,depth+1);
        const auto operation=std::string_view(code[at].value);
        if(operation=="op:+") {
            if(left==Known::zero)return right;
            if(right==Known::zero)return left;
        }else if(operation=="op:-") {
            if(right==Known::zero)return left;
            if(left==Known::one&&right==Known::one)return Known::zero;
            // Two identical plain decimal int literals cancel exactly.
            // Keep the proof limited to signed-int constants: a floating
            // subtraction or a larger unsigned type can change the type of
            // the surrounding addition even when the result prints as zero.
            if(at==a+1&&at+2==b&&
               code[a].value==code[at+1].value&&
               code[a].value.starts_with("number:")) {
                const auto digits=std::string_view(code[a].value).substr(7);
                if(!digits.empty()&&(digits.size()==1||digits.front()!='0')) {
                    std::int32_t literal=0;
                    const auto [parsed_end,err]=std::from_chars(
                        digits.data(),digits.data()+digits.size(),literal,10);
                    if(err==std::errc{}&&
                       parsed_end==digits.data()+digits.size())return Known::zero;
                }
            }
        }else if(operation=="op:*") {
            if(left==Known::one)return right;
            if(right==Known::one)return left;
            if((left==Known::zero&&right!=Known::unknown)||
               (right==Known::zero&&left!=Known::unknown))return Known::zero;
        }else if(operation=="op:/"&&right==Known::one) {
            return left;
        }
        return Known::unknown;
    };
    const auto value=known(known,statement+2,end,0);
    if(op=="op:="?value==Known::local:value==Known::zero)return true;
    // The identity classifier above handles explicit zero/one operands.
    // Detect composed additive cancellation as a separate, bounded proof:
    // aforo=aforo+2-2 or aforo+=(3+4-7) must not masquerade as an increase.
    // Only literal decimal int terms and this exact unqualified local are
    // considered; calls, other variables, casts, unusual bases, precedence
    // operators and unbounded arithmetic fail closed.
    using Affine=std::pair<int,std::int64_t>; // local coefficient, literal offset
    const auto additive=[&](auto&& self,std::size_t first,
                            std::size_t last,unsigned depth)->std::optional<Affine> {
        if(depth>32||first>=last)return {};
        const auto [a,b]=unwrapped(first,last);
        if(a>=b)return {};
        if(b==a+1) {
            if(code[a].value==wanted&&!qualified_member_name(code,a))
                return Affine{1,0};
            if(!code[a].value.starts_with("number:"))return {};
            const auto digits=std::string_view(code[a].value).substr(7);
            if(digits.empty()||(digits.size()>1&&digits.front()=='0'))return {};
            std::int32_t amount=0;
            const auto [parsed_end,parse_error]=std::from_chars(
                digits.data(),digits.data()+digits.size(),amount,10);
            if(parse_error!=std::errc{}||
               parsed_end!=digits.data()+digits.size()||
               amount>4096)return {};
            return Affine{0,amount};
        }
        if(code[a].value=="op:+"||code[a].value=="op:-") {
            auto inner=self(self,a+1,b,depth+1);
            if(!inner)return {};
            if(code[a].value=="op:-") {
                inner->first=-inner->first;
                inner->second=-inner->second;
            }
            return inner;
        }
        std::size_t split=b,level=0;
        for(auto p=a;p<b;++p) {
            const auto token=std::string_view(code[p].value);
            if(token=="op:(")++level;
            else if(token=="op:)") {
                if(!level)return {};
                --level;
            }else if(!level&&p>a&&p+1<b&&
                     (token=="op:+"||token=="op:-"))
                split=p; // last top-level addition: left associativity
        }
        if(level||split==b)return {};
        auto left=self(self,a,split,depth+1);
        auto right=self(self,split+1,b,depth+1);
        if(!left||!right)return {};
        const int sign=code[split].value=="op:+"?1:-1;
        const int coefficient=left->first+sign*right->first;
        const std::int64_t offset=left->second+sign*right->second;
        if(coefficient < -1||coefficient>1||
           offset < -4096||offset>4096)return {};
        return Affine{coefficient,offset};
    };
    const auto linear=additive(additive,statement+2,end,0);
    return linear&&linear->second==0&&
        linear->first==(op=="op:="?1:0);
}

// Locate an actual change to an initialized local int at the main-body
// level, rather than rewriting a literal 3. Both "aforo += 3" and
// "aforo = aforo + 3" are accepted, as well as equivalent +/- or increment
// syntax. A single extra addition is made AFTER the complete statement:
// a copy taken before it must stay independent; a copy made afterwards
// must see the modified state. Nested or expression-only writes fail closed.
inline std::optional<std::pair<std::size_t,std::size_t>> first_state_update(
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
                    if(!reads_prior)return {};
                    // Ignore pure self-copies and look for the next genuine
                    // top-level update; a later real increase remains valid.
                    if(!has_operation){i=*statement;continue;}
                }
                // A neutral operation can pass every stdout variation when
                // the learner adds the missing quantity only at print time.
                // Do not credit it as the required state change.
                if(int_probe_trivial_noop(code,i,*statement,variable)) {
                    i=*statement;
                    continue;
                }
                return std::pair{i,*statement};
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
    std::optional<std::size_t> update_begin;
    if (probe.after_update) {
        const auto updated = cpp_local_int_probe_detail::first_state_update(
            *code, end + 1, body->second, probe.variable);
        if (!updated) return {};
        update_begin = (*code)[updated->first].begin;
        end = updated->second;
    }
    const auto offset = (*code)[end].end;
    if(probe.after_update) {
        // An output perturbation alone does not prove a real increase.
        // A learner may leave the int unchanged with XOR 0, OR 0, or a
        // helper, and compensate at print time. The private executable
        // must therefore observe a strictly larger int across the actual
        // selected statement; this code is never a learner draft or receipt.
        constexpr std::string_view saved = "lgaed_local_int_before_6cb8e71d";
        // A comment, ordinary string or raw literal may describe this
        // private identifier without declaring it. Reject only a lexical
        // C++ identifier collision; never search arbitrary source bytes.
        const auto reserved = "id:" + std::string(saved);
        if(std::ranges::any_of(*code,[&](const auto& token){
            return token.value==reserved;
        }) || !update_begin || *update_begin >= offset)return {};
        std::string variant;
        variant.reserve(source.size() + probe.variable.size()*3 + 250);
        variant += source.substr(0,*update_begin);
        variant += "\n    const int ";
        variant += saved;
        variant += " = ";
        variant += probe.variable;
        variant += ";\n    ";
        variant += source.substr(*update_begin,offset-*update_begin);
        // The private delta is never allowed to overflow signed int.
        // Check in long long BEFORE applying it, including INT32_MIN.
        // An unsafe counterfactual is inconclusive, not learner success.
        variant += "\n    if (static_cast<long long>(";
        variant += probe.variable;
        variant += ") + (";
        variant += std::to_string(probe.replacement);
        variant += "LL) > 2147483647LL || static_cast<long long>(";
        variant += probe.variable;
        variant += ") + (";
        variant += std::to_string(probe.replacement);
        variant += "LL) < -2147483648LL) return 1;\n    ";
        variant += probe.variable;
        variant += " += ";
        variant += std::to_string(probe.replacement);
        variant += ";\n    if (static_cast<long long>(";
        variant += probe.variable;
        variant += ") - (";
        variant += std::to_string(probe.replacement);
        variant += "LL) <= static_cast<long long>(";
        variant += saved;
        variant += ")) return 1;\n";
        variant += source.substr(offset);
        return variant;
    }
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
