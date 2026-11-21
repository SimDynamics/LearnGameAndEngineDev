#pragma once

#include "core/code_format_compat.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// A source prerequisite, never an execution receipt on its own. Authoring
// supplies the purpose vocabulary; there is no global list of "right" names.
struct NamedRuleRequirement {
    std::string function;
    int value = 0;
    std::vector<std::string> purpose_words;
    std::size_t min_uses = 1;
    bool operator==(const NamedRuleRequirement&) const = default;
};
struct NamedRuleAnalysis {
    bool source_evidence = false;
    std::string reason;
};
// Independently execute named C++ functions using a generated test driver,
// never the student-editable test executable. Every expected result is authored
// separately from the candidate. No user-provided C++ snippet is injected.
struct NamedRuleProbeCase {
    std::string function;
    int input = 0, expected = 0;
    bool operator==(const NamedRuleProbeCase&) const = default;
};
// Required calls of the student's own battery, independently authored from
// the private functional probe. source_path is derived from the source check;
// empty denotes the single-file editor. The driver remains student-authored.
struct NamedRuleBatterySpec {
    std::string source_path;
    std::vector<NamedRuleProbeCase> cases;
    std::uint32_t failure_exit_code = 1;
    bool operator==(const NamedRuleBatterySpec&) const = default;
};
// Optional counterfactuals change one authenticated local agreement, then
// execute independently authored answers. The source path and requirements
// are derived from NamedRuleSourceCheck; they are not separately authored.
struct NamedRuleConstantVariation {
    std::string function;
    int value = 0;
    std::vector<NamedRuleProbeCase> cases;
    bool operator==(const NamedRuleConstantVariation&) const = default;
};
struct NamedRuleConstantVariationSpec {
    std::string source_path;
    std::vector<NamedRuleRequirement> requirements;
    std::vector<NamedRuleConstantVariation> variations;
    bool operator==(const NamedRuleConstantVariationSpec&) const = default;
};
struct NamedRuleExecutionProbe {
    std::string harness_path, header;
    std::vector<NamedRuleProbeCase> cases;
    std::optional<NamedRuleBatterySpec> battery_check = std::nullopt;
    std::optional<NamedRuleConstantVariationSpec> constant_variations = std::nullopt;
    bool operator==(const NamedRuleExecutionProbe&) const = default;
};

// One explicitly authored source in an editor, with independent function
// requirements. Empty path denotes the ordinary single-file C++ editor.
// Source evidence is necessary but never sufficient for a passing execution.
struct NamedRuleSourceCheck {
    std::string path;
    std::vector<NamedRuleRequirement> requirements;
    // In a CMake exercise, only the reviewed C++ source may change unless
    // the original author expressly permits further existing project files.
    std::vector<std::string> editable_paths;
    // An editable header can be limited to comment-only changes so C++ code,
    // declarations and macros cannot redirect the independent test driver.
    std::vector<std::string> comment_only_paths;
    // Optional trusted CMake verification. A passing stdout from the edited
    // harness alone never proves the project functions calculate these cases.
    std::optional<NamedRuleExecutionProbe> execution_probe;
    bool operator==(const NamedRuleSourceCheck&) const = default;
};

namespace named_rule_detail {
using Tokens = std::vector<std::string>;

// C++ offers brace/bracket digraphs and reserved alternative operator words.
// Normalize these only for proof analysis. Source bytes, authored
// responses, UTF-8 offsets and lexical-format revision rules remain unchanged.
inline std::string canonical_rule_token(std::string_view token) {
    if(token=="op:<%")return "op:{";
    if(token=="op:%>")return "op:}";
    if(token=="op:<:")return "op:[";
    if(token=="op::>")return "op:]";
    // These are reserved operator keywords in C++, never identifier names.
    // Keep %: and %:%: unnormalized: they denote preprocessor operators
    // and must still be rejected by the source-authentication gate.
    constexpr std::pair<std::string_view,std::string_view> alternatives[]{
        {"id:and", "op:&&"}, {"id:or", "op:||"}, {"id:not", "op:!"},
        {"id:and_eq", "op:&="}, {"id:or_eq", "op:|="},
        {"id:xor_eq", "op:^="}, {"id:bitand", "op:&"},
        {"id:bitor", "op:|"}, {"id:xor", "op:^"},
        {"id:compl", "op:~"}, {"id:not_eq", "op:!="}
    };
    for(const auto& [before,after]:alternatives)
        if(token==before)return std::string(after);
    return std::string(token);
}
struct Bounds { std::size_t open, close; };
struct LocalRule {
    std::string name;
    std::size_t end;
    int value;
    // Token bounds exclude initialization delimiters and any trailing list
    // comma. The whole expression admits equivalent bases and arithmetic.
    std::size_t initializer_first = 0, initializer_last = 0;
};

// Independent probes recompile the submitted source under a private main.
// Compile-time location/build identifiers can reveal that second compilation
// and make a function behave correctly only for the private driver. Ignore
// comments and literal text; deny source-level dependence on these markers.
inline bool probe_environment_neutral(
        const std::vector<std::string>& tokens) {
    constexpr std::string_view sensitive[]={
        "__LINE__","__FILE__","__FILEW__","__BASE_FILE__",
        "__COUNTER__","__INCLUDE_LEVEL__","__DATE__","__TIME__",
        "__TIMESTAMP__","__PRETTY_FUNCTION__","__FUNCSIG__",
        "__FUNCDNAME__","__builtin_LINE","__builtin_COLUMN",
        "__builtin_FILE","__builtin_FUNCTION",
        "__builtin_source_location","source_location"
    };
    return std::ranges::none_of(tokens,[&](std::string_view token) {
        if(!token.starts_with("id:"))return false;
        const auto name=token.substr(3);
        return std::ranges::find(sensitive,name)!=std::end(sensitive);
    });
}

// A fixed completion witness must not be supplied by student code before
// the independent main finishes. Reject explicit premature process-exit
// calls when the authored objective includes an independent execution probe.
// The lexical scan ignores harmless mentions in comments and string literals;
// it is a bounded source safeguard, not a native-code security sandbox.
inline bool probe_source_no_early_exit(std::string_view source) {
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned)return false;
    constexpr std::string_view terminating[]={
        "exit","_Exit","_exit","quick_exit","ExitProcess",
        "TerminateProcess","ExitThread","_endthread","_endthreadex",
        "_c_exit"
    };
    for(const auto& token:*scanned) {
        if(!token.starts_with("id:"))continue;
        const auto name=std::string_view(token).substr(3);
        // Inspect identifiers, not just direct calls: an alias such as
        // auto finish=&std::exit; finish(0); also ends a probe early.
        if(std::ranges::find(terminating,name)!=std::end(terminating))
            return false;
    }
    return true;
}

inline bool id(std::string_view token,std::string_view name) {
    return token.starts_with("id:")&&token.substr(3)==name;
}
inline bool op(std::string_view token,std::string_view name) {
    return token.starts_with("op:")&&token.substr(3)==name;
}
inline std::optional<std::size_t> closing(
        const Tokens& t,std::size_t at,std::string_view left,std::string_view right) {
    if(at>=t.size()||!op(t[at],left))return {};
    std::size_t depth=0;
    for(auto i=at;i<t.size();++i) {
        if(op(t[i],left))++depth;
        else if(op(t[i],right)) {
            if(!depth)return {};
            if(--depth==0)return i;
        }
    }
    return {};
}
inline std::optional<Bounds> body(const Tokens& t,std::string_view name) {
    std::optional<Bounds> found;
    for(std::size_t i=0;i+1<t.size();++i) {
        if(!id(t[i],name)||!op(t[i+1],"("))continue;
        const auto args=closing(t,i+1,"(",")");
        if(!args)return {};
        if(*args+1>=t.size()||!op(t[*args+1],"{"))continue;
        const auto end=closing(t,*args+1,"{","}");
        if(!end||found)return {};
        found=Bounds{*args+1,*end};
    }
    return found;
}
// Match C++ integer literal bases and separators without treating octal 010
// as decimal ten. Only standard bounded integral suffixes are accepted.
// A source-side parse is not a substitute for the independent execution probe.
inline std::optional<int> number(const Tokens& t,std::size_t start,std::size_t end) {
    bool negative=false;
    if(start<end&&(op(t[start],"+")||op(t[start],"-"))) {
        negative=op(t[start],"-");++start;
    }
    if(start+1!=end||!t[start].starts_with("number:"))return {};
    const auto source=std::string_view(t[start]).substr(7);
    if(source.empty()||source.size()>96)return {};
    int base=10;
    std::size_t offset=0;
    if(source.starts_with("0x")||source.starts_with("0X")) {
        base=16;offset=2;
    }else if(source.starts_with("0b")||source.starts_with("0B")) {
        base=2;offset=2;
    }else if(source.size()>1&&source.front()=='0') {
        base=8;
    }
    const auto digit=[&](unsigned char c) {
        if(c>='0'&&c<='9')return c-'0';
        if(c>='a'&&c<='f')return c-'a'+10;
        if(c>='A'&&c<='F')return c-'A'+10;
        return -1;
    };
    std::string digits;
    for(;offset<source.size();++offset) {
        const auto c=static_cast<unsigned char>(source[offset]);
        if(c=='\'' ) {
            if(digits.empty()||offset+1>=source.size()||
               digit(static_cast<unsigned char>(source[offset+1]))<0||
               digit(static_cast<unsigned char>(source[offset+1]))>=base)
                return {};
            continue;
        }
        const auto value=digit(c);
        if(value<0||value>=base)break;
        digits+=static_cast<char>(c);
    }
    if(digits.empty()||source[offset?offset-1:0]=='\'')return {};
    // A decimal point, exponent, identifier or UDL suffix is not an integer
    // constant. The unsigned, long and C++23 size suffixes are independent.
    const auto suffix=source.substr(offset);
    bool unsigned_suffix=false,size_suffix=false,long_suffix=false;
    std::size_t i=0;
    while(i<suffix.size()) {
        const auto c=suffix[i];
        if(c=='u'||c=='U') {
            if(unsigned_suffix)return {};
            unsigned_suffix=true;++i;
        }else if(c=='l'||c=='L') {
            if(long_suffix||size_suffix)return {};
            long_suffix=true;
            if(i+1<suffix.size()&&suffix[i+1]==c)i+=2;
            else ++i;
        }else if(c=='z'||c=='Z') {
            if(size_suffix||long_suffix)return {};
            size_suffix=true;++i;
        }else return {};
    }
    // Apostrophes are removed only after confirming each one separates
    // digits in the selected base. This also rejects malformed 0b102, 09.
    unsigned long long magnitude=0;
    const auto parsed=std::from_chars(
        digits.data(),digits.data()+digits.size(),magnitude,base);
    if(parsed.ec!=std::errc{}||
       parsed.ptr!=digits.data()+digits.size())return {};
    constexpr auto maximum=static_cast<unsigned long long>(
        std::numeric_limits<int>::max());
    if(magnitude>maximum+(negative?1ULL:0ULL))return {};
    // -2147483648 is representable as an int; nondecimal unsuffixed
    // 0x80000000 is unsigned on 32-bit int and is not an equivalent spell.
    if(negative&&magnitude>maximum&&(base!=10||unsigned_suffix))
        return {};
    const auto signed_value=negative?
        -static_cast<long long>(magnitude):
        static_cast<long long>(magnitude);
    return static_cast<int>(signed_value);
}
// Boolean conditions and neutral factors obey the C++ integer literal
// value, not its textual base. For example, 0x0 and 0b0 are false, while
// 0x1 and 02 are true. Keep this deliberately literal-only: never evaluate
// a student's arbitrary condition just to manufacture static evidence.
inline std::optional<bool> boolean_literal(
        const Tokens& t,std::size_t start,std::size_t end) {
    while(start<end&&op(t[start],"(")) {
        const auto close=closing(t,start,"(",")");
        if(!close||*close!=end-1)break;
        ++start;--end;
    }
    if(start+1!=end)return {};
    if(id(t[start],"true"))return true;
    if(id(t[start],"false"))return false;
    if(const auto value=number(t,start,end))return *value!=0;
    return {};
}
// Accept bounded integer arithmetic as well as decimal literals. Requiring
// just one spelling of a constant would reject equivalent student solutions.
// Do not evaluate arbitrary C++ or macros: these are source-side hints only,
// and the independently compiled probe still establishes actual behavior.
inline std::optional<int> arithmetic_constant(
        const Tokens& t,std::size_t first,std::size_t last) {
    if(first>=last||last-first>64)return {};
    while(op(t[first],"(")) {
        const auto close=closing(t,first,"(",")");
        if(!close||*close!=last-1)break;
        ++first;--last;
    }
    if(first>=last)return {};
    // Split at the last top-level operator to preserve left associativity.
    // Process addition before multiplication so precedence matches C++.
    for(int precedence=0;precedence<2;++precedence) {
        std::size_t depth=0;
        for(auto i=last;i-->first;) {
            if(op(t[i],")")){++depth;continue;}
            if(op(t[i],"(")) {
                if(!depth)return {};
                --depth;continue;
            }
            if(depth||i==first)continue;
            const bool add=op(t[i],"+")||op(t[i],"-");
            const bool multiply=op(t[i],"*")||op(t[i],"/")||op(t[i],"%");
            if(!(precedence==0?add:multiply))continue;
            // A sign after another operator is unary, not a binary split.
            if(!(t[i-1].starts_with("number:")||op(t[i-1],")")))continue;
            const auto left=arithmetic_constant(t,first,i);
            const auto right=arithmetic_constant(t,i+1,last);
            if(!left||!right)return {};
            const long long a=*left,b=*right;
            long long result=0;
            if(op(t[i],"+"))result=a+b;
            else if(op(t[i],"-"))result=a-b;
            else if(op(t[i],"*"))result=a*b;
            else {
                if(b==0)return {};
                result=op(t[i],"/")?a/b:a%b;
            }
            if(result<std::numeric_limits<int>::min()||
               result>std::numeric_limits<int>::max())return {};
            return static_cast<int>(result);
        }
        if(depth)return {};
    }
    if(op(t[first],"+")||op(t[first],"-")) {
        // Decimal INT_MIN has a positive literal of type long long. Parse
        // the complete signed token before applying an int-range check.
        if(first+2==last&&t[first+1].starts_with("number:"))
            return number(t,first,last);
        const auto value=arithmetic_constant(t,first+1,last);
        if(!value)return {};
        const long long result=op(t[first],"-")?
            -static_cast<long long>(*value):*value;
        if(result<std::numeric_limits<int>::min()||
           result>std::numeric_limits<int>::max())return {};
        return static_cast<int>(result);
    }
    return number(t,first,last);
}
inline std::optional<LocalRule> constant(
        const Tokens& t,std::size_t position,std::size_t limit) {
    // C++ permits both "constexpr int" and "int constexpr", as well as
    // "const int" and "int const". These are equivalent purpose-named
    // constant declarations for a reviewed local integer rule.
    if(position+5>=limit)return {};
    const bool const_before=(id(t[position],"const")||
        id(t[position],"constexpr"))&&id(t[position+1],"int");
    const bool const_after=id(t[position],"int")&&
        (id(t[position+1],"const")||id(t[position+1],"constexpr"));
    if(!(const_before||const_after)||!t[position+2].starts_with("id:"))
        return {};
    const auto name=t[position+2].substr(3);
    auto initializer=position+3;
    // Copy-list and direct-list initialization name the same scalar rule.
    if(op(t[initializer],"=")&&op(t[initializer+1],"{"))++initializer;
    if(op(t[initializer],"=")) {
        auto end=initializer+1;
        while(end<limit&&!op(t[end],";"))++end;
        if(end>=limit)return {};
        const auto value=arithmetic_constant(t,initializer+1,end);
        if(value)return LocalRule{name,end,*value,initializer+1,end};
    }
    if(op(t[initializer],"{")||op(t[initializer],"(")) {
        const bool brace=op(t[initializer],"{");
        const auto end=closing(t,initializer,brace?"{":"(",brace?"}":")");
        if(!end||*end+1>=limit||!op(t[*end+1],";"))return {};
        auto last=*end;
        // A scalar list may end with one comma. Keep that delimiter outside
        // the replaceable expression; empty or multiple elements still fail.
        if(brace&&last>initializer+1&&op(t[last-1],","))--last;
        const auto value=arithmetic_constant(t,initializer+1,last);
        if(value)return LocalRule{name,*end+1,*value,initializer+1,last};
    }
    return {};
}
inline bool role(std::string_view name,const std::vector<std::string>& words) {
    std::string folded;
    for(const unsigned char c:name)folded+=static_cast<char>(std::tolower(c));
    for(const auto& item:words) {
        if(item.size()<3)continue;
        std::string word;
        for(const unsigned char c:item)word+=static_cast<char>(std::tolower(c));
        if(folded.find(word)!=std::string::npos)return true;
    }
    return false;
}
// Return expressions need the same source protection even without extra
// parentheses. Respect C++'s && precedence over ||: a false operand kills
// only its own AND clause, while a standalone true operand settles the
// entire OR expression. A ternary/comma is conservatively left undecided.
inline bool absorbed_by_boolean_literal(const Tokens& t,std::size_t at,
        std::size_t first,std::size_t last) {
    if(at<first||at>=last||last-first>256)return false;
    const auto literal=[&](std::size_t start,std::size_t end,bool expected) {
        const auto value=boolean_literal(t,start,end);
        return value&&*value==expected;
    };
    std::vector<std::size_t> ors;
    std::size_t depth=0;
    for(auto i=first;i<last;++i) {
        if(op(t[i],"(")){++depth;continue;}
        if(op(t[i],")")) {
            if(depth)--depth;
            else return false;
            continue;
        }
        if(depth)continue;
        if(op(t[i],"?")||op(t[i],":")||op(t[i],","))return false;
        if(op(t[i],"||"))ors.push_back(i);
    }
    if(depth)return false;
    std::size_t begin=first;
    std::optional<std::pair<std::size_t,std::size_t>> containing;
    for(std::size_t part=0;part<=ors.size();++part) {
        const auto end=part<ors.size()?ors[part]:last;
        if(!ors.empty()&&literal(begin,end,true))return true;
        if(at>=begin&&at<end)containing={{begin,end}};
        begin=end+1;
    }
    if(!containing)return false;
    const auto [start,end]=*containing;
    std::vector<std::size_t> ands;
    depth=0;
    for(auto i=start;i<end;++i) {
        if(op(t[i],"(")){++depth;continue;}
        if(op(t[i],")")) {
            if(depth)--depth;
            else return false;
            continue;
        }
        if(!depth&&op(t[i],"&&"))ands.push_back(i);
    }
    if(depth||ands.empty())return false;
    begin=start;
    for(std::size_t part=0;part<=ands.size();++part) {
        const auto finish=part<ands.size()?ands[part]:end;
        if(literal(begin,finish,false))return true;
        begin=finish+1;
    }
    return false;
}

// An unevaluated operand observes the type or validity of an expression,
// not the current value of a purpose-named local constant. Such references
// must never contribute evidence that the arithmetic uses that agreement.
// Limit the recognition to C++ unevaluated operators, leaving arbitrary
// expressions to the independent runtime probe.
inline bool unevaluated_rule_use(const Tokens& t,std::size_t i,
        std::size_t scope_start) {
    if(i>=t.size()||i<scope_start)return false;
    const auto query=[&](std::size_t at) {
        return id(t[at],"sizeof")||id(t[at],"decltype")||
            id(t[at],"noexcept")||id(t[at],"typeid");
    };
    // Both sizeof value and sizeof(+value) are unevaluated. The
    // parenthesized forms below also handle nested sizeof(decltype(value)).
    if(i>scope_start&&query(i-1))return true;
    if(i>=scope_start+2&&query(i-2)&&
       (op(t[i-1],"+")||op(t[i-1],"-")||
        op(t[i-1],"*")||op(t[i-1],"&")))return true;
    std::vector<std::size_t> open;
    for(auto at=scope_start;at<i;++at) {
        if(op(t[at],"("))open.push_back(at);
        else if(op(t[at],")")&&!open.empty())open.pop_back();
    }
    return std::ranges::any_of(open,[&](std::size_t at){
        return at>scope_start&&query(at-1);
    });
}
// Parentheses do not turn a self-cancelling expression into useful
// evidence: (cupo - (cupo)), ((cupo) / cupo), and cupo % (cupo)
// are independent of the named integer agreement. Recognize only an
// entire, provably simple binary expression. Do not discard references
// in "cupo - (cupo * 2)" or "(cupo + 1) / cupo".
inline bool neutral_named_expression_use(const Tokens& t,std::size_t i,
        std::size_t scope_start) {
    if(i>=t.size()||i<scope_start||!t[i].starts_with("id:"))return false;
    const auto name=t[i].substr(3);
    const auto unwrap=[&](std::size_t& first,std::size_t& last) {
        while(first<last&&op(t[first],"(")) {
            const auto close=closing(t,first,"(",")");
            if(!close||*close+1!=last)break;
            ++first;--last;
        }
    };
    const auto single=[&](std::size_t first,std::size_t last) {
        unwrap(first,last);
        return first+1==last&&id(t[first],name);
    };
    const auto negated=[&](std::size_t first,std::size_t last) {
        unwrap(first,last);
        return first<last&&op(t[first],"-")&&single(first+1,last);
    };
    const auto neutral=[&](std::size_t first,std::size_t last) {
        unwrap(first,last);
        std::size_t depth=0;
        for(auto at=first;at<last;++at) {
            if(op(t[at],"(")){++depth;continue;}
            if(op(t[at],")")){if(depth)--depth;continue;}
            if(depth)continue;
            if((op(t[at],"-")||op(t[at],"/")||op(t[at],"%")||
                op(t[at],"^"))&&
                single(first,at)&&single(at+1,last))return true;
            // Proven zero-valued bitwise and remainder expressions do not
            // use the agreement as a numeric input. C++ evaluates both
            // operands of &, but the local const has no side effects.
            // Only fold a complete, bounded, pure literal opposite operand:
            // "x & 0", "0 & x" and "x % 1". Do not classify x & 1,
            // x % 2, x ^ y, or x | 0 as neutral; those may depend on x.
            const auto literal_is=[&](std::size_t begin,std::size_t end,int wanted) {
                const auto result=arithmetic_constant(t,begin,end);
                return result&&*result==wanted;
            };
            if(op(t[at],"&")&&
               ((single(first,at)&&literal_is(at+1,last,0))||
                (literal_is(first,at,0)&&single(at+1,last))))return true;
            if(op(t[at],"%")&&single(first,at)&&
               literal_is(at+1,last,1))return true;
            // The two lexical references also cancel as x+(-x) or (-x)+x.
            if(op(t[at],"+")&&
                ((single(first,at)&&negated(at+1,last))||
                 (negated(first,at)&&single(at+1,last))))return true;
        }
        return false;
    };
    // A complete parenthesized subexpression can occur anywhere in a
    // return, initializer or alias. Only tokens already in that exact
    // expression are suppressed, never all other uses of the constant.
    std::vector<std::size_t> enclosing;
    for(auto at=scope_start;at<i;++at) {
        if(op(t[at],"("))enclosing.push_back(at);
        else if(op(t[at],")")&&!enclosing.empty())enclosing.pop_back();
    }
    for(const auto open:enclosing) {
        const auto close=closing(t,open,"(",")");
        if(close&&*close>i&&neutral(open+1,*close))return true;
    }
    // Also handle a complete unparenthesized return/assignment such as
    // return cupo - (cupo); without interpreting a larger C++ expression.
    auto first=i;
    while(first>scope_start&&!op(t[first-1],";")&&
            !op(t[first-1],"{")&&!op(t[first-1],"}")&&
            !op(t[first-1],"=")&&!id(t[first-1],"return"))
        --first;
    auto last=i+1;
    while(last<t.size()&&!op(t[last],";")&&!op(t[last],"}"))++last;
    return neutral(first,last);
}
// In the built-in comma operator, every left operand is evaluated and
// discarded. A plain named integer or side-effect-free arithmetic on that
// integer does not contribute to the value of the rightmost operand.
// Recognize parenthesized and direct return comma expressions; calls such as
// f(capacidad, capacidad) have argument separators, not discarded operands.
inline bool discarded_comma_rule_use(const Tokens& t,std::size_t i,
        std::size_t scope_start) {
    if(i>=t.size()||i<scope_start||!t[i].starts_with("id:"))return false;
    const auto named=t[i];
    const auto pure_named_arithmetic=[&](std::size_t first,std::size_t last) {
        bool has_named=false;
        for(auto at=first;at<last;++at) {
            if(t[at]==named){has_named=true;continue;}
            if(t[at].starts_with("number:")||
                op(t[at],"(")||op(t[at],")")||
                op(t[at],"+")||op(t[at],"-")||
                op(t[at],"*")||op(t[at],"/")||op(t[at],"%"))
                continue;
            // An assignment, call, increment or other expression can have
            // effects even if its resulting value is discarded.
            return false;
        }
        return has_named;
    };
    const auto discarded_in=[&](std::size_t first,std::size_t last) {
        std::size_t parentheses=0,brackets=0,braces=0,part=first;
        for(auto at=first;at<last;++at) {
            if(op(t[at],"(")){++parentheses;continue;}
            if(op(t[at],")")){if(parentheses)--parentheses;continue;}
            if(op(t[at],"[")){++brackets;continue;}
            if(op(t[at],"]")){if(brackets)--brackets;continue;}
            if(op(t[at],"{")){++braces;continue;}
            if(op(t[at],"}")){if(braces)--braces;continue;}
            if(parentheses||brackets||braces||!op(t[at],","))continue;
            if(i>=part&&i<at&&pure_named_arithmetic(part,at))
                return true;
            part=at+1;
        }
        return false;
    };
    std::vector<std::size_t> enclosing;
    for(auto at=scope_start;at<i;++at) {
        if(op(t[at],"("))enclosing.push_back(at);
        else if(op(t[at],")")&&!enclosing.empty())enclosing.pop_back();
    }
    for(const auto open:enclosing) {
        if(open>scope_start&&
            ((t[open-1].starts_with("id:")&&
              !id(t[open-1],"return")&&!id(t[open-1],"co_return"))||
             op(t[open-1],")")||op(t[open-1],"]")))
            continue;
        const auto close=closing(t,open,"(",")");
        if(close&&*close>i&&discarded_in(open+1,*close))return true;
    }
    // The return-expression itself can be a comma expression without outer
    // parentheses. Nested call arguments and braced initializers retain
    // their own delimiters and are never treated as top-level commas.
    auto first=i;
    while(first>scope_start&&!op(t[first-1],";")&&
            !op(t[first-1],"{")&&!op(t[first-1],"}")) {
        if(id(t[first-1],"return")||id(t[first-1],"co_return")) {
            auto last=i+1;
            while(last<t.size()&&!op(t[last],";"))++last;
            return discarded_in(first,last);
        }
        --first;
    }
    return false;
}
inline bool productive(const Tokens& t,std::size_t i,
        std::size_t scope_start=0) {
    if(unevaluated_rule_use(t,i,scope_start)||
       neutral_named_expression_use(t,i,scope_start)||
       discarded_comma_rule_use(t,i,scope_start))return false;
    // A direct x-x pair is neutral only when C++ precedence has not
    // attached either x to a multiplicative expression first. For example,
    // x - x*2 and 2*x - x both genuinely depend on x. An adjacent token
    // match alone must never reject these valid alternative expressions.
    // Division/remainder by the same direct operand retain the existing
    // conservative check (and require independent runtime verification).
    const auto multiplicative=[&](std::size_t at) {
        return at<t.size()&&(op(t[at],"*")||op(t[at],"/")||op(t[at],"%"));
    };
    const auto direct_neutral=[&](std::size_t left,std::size_t middle,
            std::size_t right) {
        if(t[left]!=t[right])return false;
        if(op(t[middle],"-"))
            return !(left>0&&multiplicative(left-1))&&
                   !(right+1<t.size()&&multiplicative(right+1));
        return op(t[middle],"/")||op(t[middle],"%");
    };
    if((i>=2&&direct_neutral(i-2,i-1,i))||
       (i+2<t.size()&&direct_neutral(i,i+1,i+2)))
        return false;
    const auto zero_after=[&](std::size_t position) {
        if(position>=t.size())return false;
        if(const auto value=number(t,position,position+1);value&&*value==0)
            return true;
        if(!op(t[position],"("))return false;
        const auto end=closing(t,position,"(",")");
        if(!end)return false;
        const auto value=arithmetic_constant(t,position,*end+1);
        return value&&*value==0;
    };
    const auto zero_before=[&](std::size_t position) {
        if(position>=t.size())return false;
        if(const auto value=number(t,position,position+1);value&&*value==0)
            return true;
        if(!op(t[position],")"))return false;
        std::size_t depth=0;
        for(auto cursor=position+1;cursor-->0;) {
            if(op(t[cursor],")"))++depth;
            else if(op(t[cursor],"(")) {
                if(--depth==0) {
                    const auto value=arithmetic_constant(t,cursor,position+1);
                    return value&&*value==0;
                }
            }
        }
        return false;
    };
    if((i>=2&&op(t[i-1],"*")&&zero_before(i-2))||
       (i+2<t.size()&&op(t[i+1],"*")&&zero_after(i+2)))
        return false;
    // A zero factor can apply to an entire parenthesized expression:
    // 0*(capacidad+capacidad) has two lexical uses but contributes nothing.
    // Examine enclosing groups, not just the tokens adjacent to the name.
    std::vector<std::size_t> containing;
    for(auto at=scope_start;at<i;++at) {
        if(op(t[at],"("))containing.push_back(at);
        else if(op(t[at],")")&&!containing.empty())
            containing.pop_back();
    }
    for(const auto open:containing) {
        const auto close=closing(t,open,"(",")");
        if(!close||*close<=i)continue;
        // In an AND-only group, a literal false forces the entire expression
        // to false even when it comes AFTER the named constant. Likewise a
        // literal true makes an OR-only group independent of its other terms.
        // Inspect top-level operands only: mixing && with ||, a ternary or a
        // comma changes grouping, so do not infer an always-constant result.
        std::vector<std::size_t> logical_operators;
        bool and_only=true,or_only=true,ambiguous=false;
        std::size_t logical_depth=0;
        for(auto at=open+1;at<*close;++at) {
            if(op(t[at],"(")){++logical_depth;continue;}
            if(op(t[at],")")) {
                if(logical_depth)--logical_depth;
                continue;
            }
            if(logical_depth)continue;
            if(op(t[at],"?")||op(t[at],":")||op(t[at],",")) {
                ambiguous=true;break;
            }
            if(op(t[at],"&&")||op(t[at],"||")) {
                logical_operators.push_back(at);
                if(!op(t[at],"&&"))and_only=false;
                if(!op(t[at],"||"))or_only=false;
            }
        }
        if(!ambiguous&&!logical_operators.empty()&&(and_only||or_only)) {
            const auto literal_operand=[&](std::size_t first,std::size_t last,
                    bool desired) {
                const auto value=boolean_literal(t,first,last);
                return value&&*value==desired;
            };
            std::size_t first=open+1;
            bool constant_group=false;
            for(std::size_t index=0;index<=logical_operators.size();++index) {
                const auto last=index<logical_operators.size()?
                    logical_operators[index]:*close;
                if(literal_operand(first,last,or_only)) {
                    constant_group=true;break;
                }
                first=last+1;
            }
            if(constant_group)return false;
        }
        if((open>=2&&op(t[open-1],"*")&&zero_before(open-2))||
           (*close+2<t.size()&&op(t[*close+1],"*")&&
            zero_after(*close+2)))
            return false;
        // Short-circuiting constants are not meaningful input to a rule:
        // false && (capacidad + capacidad) and true || (capacidad > 0)
        // do not evaluate their right-hand branches. Limit this proof to
        // literal first operands and top-level operators in the enclosing
        // parentheses. An || after false && starts a potentially live branch;
        // a comma or conditional expression starts a separate result.
        if(open+2<*close) {
            const auto first=boolean_literal(t,open+1,open+2);
            const bool dead_and=first&&!*first&&op(t[open+2],"&&");
            const bool dead_or=first&&*first&&op(t[open+2],"||");
            if(dead_and||dead_or) {
                std::size_t nested=0;
                for(auto at=open+3;at<*close;++at) {
                    if(op(t[at],"(")){++nested;continue;}
                    if(op(t[at],")")) {
                        if(nested)--nested;
                        continue;
                    }
                    if(!nested&&(op(t[at],"?")||op(t[at],",")||
                                 (dead_and&&op(t[at],"||"))))break;
                    if(at==i)return false;
                }
            }
        }
        // Equal, integral constant results cannot convey a named rule,
        // whatever the condition does. In "capacidad > 0 ? 0 : 0" the
        // constant appears in a condition but neither outcome depends on it.
        // Examine only a top-level, non-nested ternary with independently
        // evaluable integral arms; any ambiguous expression stays uncredited
        // only by the ordinary, conservative source and execution checks.
        std::size_t question=*close,colon=*close,branch_parentheses=0;
        bool nested_ternary=false;
        for(auto at=open+1;at<*close;++at) {
            if(op(t[at],"(")){++branch_parentheses;continue;}
            if(op(t[at],")")) {
                if(branch_parentheses)--branch_parentheses;
                continue;
            }
            if(branch_parentheses)continue;
            if(op(t[at],"?")) {
                if(question!=*close){nested_ternary=true;break;}
                question=at;
            }else if(op(t[at],":")) {
                if(colon!=*close){nested_ternary=true;break;}
                colon=at;
            }
        }
        if(!nested_ternary&&question<colon&&colon<*close&&i<question) {
            const auto first=arithmetic_constant(t,question+1,colon);
            const auto second=arithmetic_constant(t,colon+1,*close);
            if(first&&second&&*first==*second)return false;
        }
        // A literal conditional expression cannot contribute source evidence
        // from its impossible branch. For example, both "capacidad" mentions
        // in (false ? capacidad + capacidad : 0) are dead, even though the
        // returned hard-coded quotient can pass an independent numeric probe.
        // This deliberately handles only provable literal conditions, not
        // arbitrary C++ constant expressions or data-dependent branches.
        if(open+2>=*close||!op(t[open+2],"?"))continue;
        const bool never_first=id(t[open+1],"false")||
            t[open+1]=="number:0";
        const bool never_second=id(t[open+1],"true")||
            t[open+1]=="number:1";
        if(!never_first&&!never_second)continue;
        std::size_t nested=0,paren_depth=0;
        for(auto at=open+3;at<*close;++at) {
            if(op(t[at],"(")){++paren_depth;continue;}
            if(op(t[at],")")) {
                if(paren_depth)--paren_depth;
                continue;
            }
            if(paren_depth)continue;
            if(op(t[at],"?")){++nested;continue;}
            if(!op(t[at],":"))continue;
            if(nested){--nested;continue;}
            if((never_first&&i<at)||(never_second&&i>at))
                return false;
            break;
        }
    }
    return true;
}
inline std::size_t uses_in_return(const Tokens& t,Bounds bounds,
        std::string_view name) {
    // Count references that reach the returned expression. Counting every
    // appearance in the body lets unused logging or discarded calculations
    // satisfy minUses even when the real result uses only one hard-coded role.
    // Local aliases carry their originating use count across initializers and
    // assignments. An overwritten alias loses its previous evidence.
    constexpr std::size_t limit=64;
    // A block may declare a new variable with an existing outer name.
    // Its evidence expires at scope exit; assignments to the outer variable
    // still propagate when there is no inner shadowing declaration.
    struct Alias { std::string name; std::size_t count,depth; bool zero=false; };
    std::vector<Alias> aliases;
    // An inner declaration with the same spelling as the required rule
    // temporarily hides the outer rule; it does not invalidate that rule.
    std::vector<std::size_t> named_rule_shadows;
    bool declared=false;
    std::size_t nesting=0;
    const auto unsafe_alias_call=[&](std::size_t first,std::size_t last) {
        // For result aliases, a call may write through an int& or int*.
        // An inner shadow dies when its block ends and must not revoke the
        // value of a distinct, still-live outer alias.
        for(auto at=first;at+1<last;++at) {
            if(!op(t[at+1],"("))continue;
            // A built-in functional cast observes the value but cannot
            // mutate the original int through a non-const reference.
            const bool value_cast=id(t[at],"int")||id(t[at],"long")||
                id(t[at],"short")||id(t[at],"bool")||
                id(t[at],"char")||id(t[at],"float")||
                id(t[at],"double")||id(t[at],"unsigned")||
                id(t[at],"signed");
            const bool call=(t[at].starts_with("id:")&&!value_cast&&
                !id(t[at],"sizeof")&&!id(t[at],"decltype")&&
                !id(t[at],"noexcept")&&!id(t[at],"typeid"))||
                op(t[at],")")||op(t[at],"]");
            if(!call)continue;
            const auto end=closing(t,at+1,"(",")");
            if(!end||*end>=last)continue;
            for(auto arg=at+2;arg<*end;++arg) {
                if(!t[arg].starts_with("id:"))continue;
                const auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                    [&](const Alias& alias){return id(t[arg],alias.name);});
                if(found!=aliases.rend()&&
                   (nesting==0||found->depth<nesting))return true;
            }
        }
        return false;
    };
    // The RHS of >> can be an int& receiving data from a stream. A
    // candidate's source proof cannot assume a shift rather than extraction
    // when the C++ type of the left operand is not known lexically.
    const auto extraction_operand=[&](std::size_t at,std::size_t first) {
        if(at>=t.size()||at<first||!t[at].starts_with("id:"))
            return false;
        auto operand=at;
        while(operand>first&&op(t[operand-1],"("))--operand;
        if(operand==first||!op(t[operand-1],">>"))return false;
        // A numeric left operand is a built-in shift, not a stream.
        return operand<first+2||
            !t[operand-2].starts_with("number:");
    };
    const auto extracted_alias=[&](std::size_t first,std::size_t last) {
        for(auto at=first;at<last;++at) {
            if(!extraction_operand(at,first))continue;
            if(std::ranges::any_of(aliases,[&](const Alias& alias){
                return id(t[at],alias.name);
            }))return true;
        }
        return false;
    };
    const auto uses=[&](std::size_t first,std::size_t last) {
        if(unsafe_alias_call(first,last)||extracted_alias(first,last))
            return std::size_t{0};
        std::size_t count=0;
        for(auto i=first;i<last;++i) {
            if(absorbed_by_boolean_literal(t,i,first,last)||
               !productive(t,i,bounds.open+1))continue;
            std::size_t contribution=
                named_rule_shadows.empty()&&id(t[i],name)?1:0;
            for(auto alias=aliases.rbegin();alias!=aliases.rend();++alias)
                if(id(t[i],alias->name)) {
                    contribution=alias->count;
                    break;
                }
            count+=std::min(contribution,limit-count);
        }
        return count;
    };
    // Only a proven literal-zero alternative may omit the named calculation.
    // A different hard-coded default is not a domain guard, even if the
    // conditional branch uses the right constant.
    const auto zero_expression=[&](std::size_t first,std::size_t last) {
        while(first<last&&(op(t[first],"(")||op(t[first],"{"))) {
            const auto braces=op(t[first],"{");
            const auto close=closing(t,first,braces?"{":"(",braces?"}":")");
            if(!close||*close+1!=last)break;
            ++first;--last;
        }
        if(first==last)return true; // int value{} is zero-initialized.
        const auto value=arithmetic_constant(t,first,last);
        return (value&&*value==0)||
            (first+1==last&&id(t[first],"false"));
    };
    // A conditional branch that computes a nontrivial result is part of
    // the function's contract even when a later unconditional return uses
    // the named constant correctly. Keep the weakest live return path.
    std::optional<std::size_t> prior_nontrivial_uses;
    const auto include_prior=[&](std::size_t count) {
        return prior_nontrivial_uses?
            std::min(*prior_nontrivial_uses,count):count;
    };
    const auto return_uses=[&](std::size_t keyword) {
        auto last=keyword+1;
        while(last<bounds.close&&!op(t[last],";"))++last;
        return uses(keyword+1,last);
    };
    // An early zero/false return is a normal domain guard. It contributes
    // no calculated result; the remaining path must still use the rule.
    const auto trivial_guard_return=[&](std::size_t keyword) {
        if(keyword>=bounds.close||!id(t[keyword],"return"))return false;
        auto last=keyword+1;
        while(last<bounds.close&&!op(t[last],";"))++last;
        if(last>=bounds.close||last==keyword+1)return false;
        if(last==keyword+2&&id(t[keyword+1],"false"))return true;
        const auto value=arithmetic_constant(t,keyword+1,last);
        return value&&*value==0;
    };
    // Only a full direct return or a brace-enclosed single return is
    // understood. Other branches are conservatively left uncredited.
    const auto direct_return=[&](std::size_t at)
            ->std::optional<std::pair<std::size_t,std::size_t>> {
        if(at>=bounds.close)return {};
        if(id(t[at],"return")) {
            auto last=at+1;
            while(last<bounds.close&&!op(t[last],";"))++last;
            if(last>=bounds.close)return {};
            return std::pair{return_uses(at),last};
        }
        if(!op(t[at],"{")||at+1>=bounds.close||
           !id(t[at+1],"return"))return {};
        const auto end=closing(t,at,"{","}");
        if(!end||*end>=bounds.close)return {};
        auto semicolon=at+2;
        while(semicolon<*end&&!op(t[semicolon],";"))++semicolon;
        if(semicolon+1!=*end)return {};
        return std::pair{return_uses(at+1),*end};
    };
    // Some valid solutions assign the calculated answer in both branches
    // and return it only after the if/else. Treat this as one assignment
    // whose evidence is the weakest nontrivial branch. A literal zero is a
    // permitted domain guard, just as for the direct-return form above.
    // Only one direct assignment per branch is recognized: arbitrary blocks
    // and calls remain outside this conservative source-side proof.
    struct BranchAssignment {
        std::string name;
        std::size_t count, end;
        bool zero;
    };
    const auto direct_assignment=[&](std::size_t statement)
            ->std::optional<BranchAssignment> {
        if(statement>=bounds.close)return {};
        auto at=statement;
        std::optional<std::size_t> block_end;
        if(op(t[at],"{")) {
            block_end=closing(t,at,"{","}");
            if(!block_end||*block_end>=bounds.close)return {};
            ++at;
        }
        if(at+2>=bounds.close||!t[at].starts_with("id:")||
           !op(t[at+1],"="))return {};
        const auto local=t[at].substr(3);
        if(std::ranges::find_if(aliases,[&](const Alias& alias) {
            return alias.name==local;
        })==aliases.end())return {};
        auto semicolon=at+2;
        while(semicolon<bounds.close&&!op(t[semicolon],";"))++semicolon;
        if(semicolon>=bounds.close||semicolon==at+2)return {};
        if(block_end&&semicolon+1!=*block_end)return {};
        return BranchAssignment{local,uses(at+2,semicolon),
            block_end?*block_end:semicolon,zero_expression(at+2,semicolon)};
    };
    const auto statement_end=[&](std::size_t at)->std::optional<std::size_t> {
        if(at>=bounds.close||id(t[at],"if"))return {};
        if(op(t[at],"{"))return closing(t,at,"{","}");
        while(at<bounds.close&&!op(t[at],";"))++at;
        return at<bounds.close?std::optional<std::size_t>(at):std::nullopt;
    };
    // When a branch is unrelated to the returned value, skip it rather
    // than treating its statements (or the other arm of an else) as
    // unconditional writes. A variable declared inside a branch may shadow
    // an outer result without modifying it. Follow lexical block lifetimes
    // and reject any write to an actual outer alias, early return, or nested
    // control flow that this small source proof cannot establish.
    const auto harmless_branch=[&](std::size_t statement)
            ->std::optional<std::size_t> {
        const auto last=statement_end(statement);
        if(!last)return {};
        struct Shadow { std::string name; std::size_t depth; };
        std::vector<Shadow> shadows;
        std::size_t depth=0;
        const auto assignment=[&](std::string_view token) {
            return op(token,"=")||op(token,"+=")||op(token,"-=")||
                op(token,"*=")||op(token,"/=")||op(token,"%=")||
                op(token,"&=")||op(token,"|=")||op(token,"^=")||
                op(token,"<<=")||op(token,">>=")||
                op(token,"++")||op(token,"--");
        };
        // Calls may mutate an int through a reference or pointer even when
        // no assignment operator is visible in this branch. Track enclosing
        // argument lists once, instead of rescanning the complete body for
        // every occurrence of a result alias.
        std::vector<bool> parentheses;
        std::size_t active_calls=0;
        const auto unevaluated=[&](std::size_t at) {
            return id(t[at],"sizeof")||id(t[at],"decltype")||
                id(t[at],"noexcept")||id(t[at],"typeid");
        };
        for(auto at=statement;at<=*last;++at) {
            if(op(t[at],"(")) {
                // int copia(resultado) is direct initialization of a local,
                // not a call that could modify the source result by reference.
                const bool local_initializer=at>=statement+2&&
                    (id(t[at-2],"int")||id(t[at-2],"auto")||
                     (at>=statement+3&&
                      (id(t[at-2],"const")||id(t[at-2],"constexpr"))&&
                      (id(t[at-3],"int")||id(t[at-3],"auto"))));
                const bool value_cast=at>statement&&
                    (id(t[at-1],"int")||id(t[at-1],"long")||
                     id(t[at-1],"short")||id(t[at-1],"bool")||
                     id(t[at-1],"char")||id(t[at-1],"float")||
                     id(t[at-1],"double")||id(t[at-1],"unsigned")||
                     id(t[at-1],"signed"));
                const bool call=at>statement&&!local_initializer&&
                    ((t[at-1].starts_with("id:")&&!unevaluated(at-1)&&
                      !value_cast)||
                     op(t[at-1],")")||op(t[at-1],"]"));
                parentheses.push_back(call);
                if(call)++active_calls;
                continue;
            }
            if(op(t[at],")")) {
                if(parentheses.empty())return {};
                if(parentheses.back())--active_calls;
                parentheses.pop_back();
                continue;
            }
            if(op(t[at],"{")){++depth;continue;}
            if(op(t[at],"}")) {
                std::erase_if(shadows,[&](const Shadow& shadow){
                    return shadow.depth==depth;
                });
                if(depth)--depth;
                continue;
            }
            if(id(t[at],"return")||id(t[at],"co_return")||
               id(t[at],"throw")||id(t[at],"if")||
               id(t[at],"for")||id(t[at],"while")||
               id(t[at],"switch")||id(t[at],"goto")||
               id(t[at],"try")||id(t[at],"break")||
               id(t[at],"continue"))return {};
            if((id(t[at],"int")||id(t[at],"auto"))&&
               at+1<=*last) {
                auto declared_at=at+1;
                if(id(t[declared_at],"const")||
                   id(t[declared_at],"constexpr"))++declared_at;
                if(declared_at<=*last&&
                   t[declared_at].starts_with("id:"))
                    shadows.push_back({t[declared_at].substr(3),depth});
            }
            if(!t[at].starts_with("id:"))continue;
            const auto known=std::ranges::any_of(aliases,
                [&](const Alias& alias){return id(t[at],alias.name);});
            const auto shadowed=std::ranges::any_of(shadows,
                [&](const Shadow& shadow){return id(t[at],shadow.name);});
            if(!known||shadowed)continue;
            // std::swap(result, other), mutate(result), a forwarded
            // reference, an address-taking expression or an extraction can
            // change an outer variable without spelling "result =".
            if(active_calls||extraction_operand(at,statement)||
               (at>statement&&op(t[at-1],"&")))
                return {};
            // int& alias=result and auto&& alias=result can later change
            // the outer result, even if that write uses a different name.
            auto begin=at;
            while(begin>statement&&!op(t[begin-1],";")&&
                  !op(t[begin-1],"{")&&!op(t[begin-1],"}"))
                --begin;
            for(auto cursor=begin;cursor+2<at;++cursor) {
                if(!(op(t[cursor],"&")||op(t[cursor],"&&"))||
                   !t[cursor+1].starts_with("id:")||
                   !op(t[cursor+2],"="))continue;
                if(std::ranges::any_of(t.begin()+static_cast<std::ptrdiff_t>(begin),
                        t.begin()+static_cast<std::ptrdiff_t>(cursor),
                        [&](const std::string& token) {
                            return id(token,"int")||id(token,"auto");
                        }))return {};
            }
            if((at+1<=*last&&assignment(t[at+1]))||
               (at>statement&&(op(t[at-1],"++")||
                               op(t[at-1],"--"))))return {};
        }
        if(!parentheses.empty())return {};
        return last;
    };
    for(auto i=bounds.open+1;i<bounds.close;++i) {
        if(op(t[i],"{")){++nesting;continue;}
        if(op(t[i],"}")){
            if(nesting) {
                std::erase_if(aliases,[&](const Alias& alias){
                    return alias.depth==nesting;
                });
                std::erase_if(named_rule_shadows,[&](std::size_t depth){
                    return depth==nesting;
                });
                --nesting;
            }
            continue;
        }
        // The first unconditional return ends the function path. A later
        // unreachable return must not manufacture the required source uses.
        // A simple runtime guard remains conditional and does not suppress
        // an otherwise valid normal result.
        if(id(t[i],"if")) {
            auto condition=i+1;
            if(condition<bounds.close&&id(t[condition],"constexpr"))
                ++condition;
            if(condition<bounds.close&&op(t[condition],"(")) {
                const auto close=closing(t,condition,"(",")");
                if(close&&*close+1<bounds.close) {
                    const auto statement=*close+1;
                    const auto literal=boolean_literal(
                        t,condition+1,*close);
                    const bool never=literal&&!*literal;
                    const bool always=literal&&*literal;
                    if(never) {
                        std::size_t skipped=statement;
                        if(op(t[statement],"{")) {
                            const auto end=closing(t,statement,"{","}");
                            if(!end||*end>=bounds.close)return 0;
                            skipped=*end;
                        }else{
                            while(skipped<bounds.close&&!op(t[skipped],";"))++skipped;
                            if(skipped>=bounds.close)return 0;
                        }
                        // The else arm of an always-false condition is certain
                        // to run. A single return there makes later code dead.
                        if(skipped+2<bounds.close&&id(t[skipped+1],"else")) {
                            const auto alternate=skipped+2;
                            if(id(t[alternate],"return"))
                                return return_uses(alternate);
                            if(op(t[alternate],"{")&&alternate+1<bounds.close&&
                               id(t[alternate+1],"return")) {
                                const auto end=closing(t,alternate,"{","}");
                                auto semicolon=alternate+2;
                                while(semicolon<bounds.close&&
                                      !op(t[semicolon],";"))++semicolon;
                                if(end&&semicolon+1==*end)
                                    return return_uses(alternate+1);
                            }
                        }
                        i=skipped;
                        continue;
                    }
                    // Nontrivial conditional results must use the rule;
                    // a later correct fallthrough cannot excuse a shortcut.
                    if(always) {
                        if(const auto branch=direct_return(statement))
                            return include_prior(branch->first);
                        if(const auto assignment=direct_assignment(statement)) {
                            auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                                [&](const Alias& alias){
                                    return alias.name==assignment->name;
                                });
                            if(found==aliases.rend())return 0;
                            found->count=assignment->count;
                            found->zero=assignment->zero;
                            auto end=assignment->end;
                            if(end+1<bounds.close&&id(t[end+1],"else")) {
                                const auto skipped=statement_end(end+2);
                                if(!skipped)return 0;
                                end=*skipped;
                            }
                            i=end;
                            continue;
                        }
                        // A certain, harmless branch may be skipped; any
                        // else is dead, even if it contains unrelated code.
                        // Unrecognized writes or nested returns fail closed.
                        if(const auto end=harmless_branch(statement)) {
                            auto last=*end;
                            if(last+1<bounds.close&&id(t[last+1],"else")) {
                                const auto skipped=statement_end(last+2);
                                if(!skipped)return 0;
                                last=*skipped;
                            }
                            i=last;
                            continue;
                        }
                        return 0;
                    }else if(const auto branch=direct_return(statement)) {
                        const auto first_return=
                            id(t[statement],"return")?statement:statement+1;
                        const auto else_position=branch->second+1;
                        if(else_position<bounds.close&&
                           id(t[else_position],"else")) {
                            const auto other=direct_return(else_position+1);
                            if(!other)return 0;
                            const auto other_statement=else_position+1;
                            const auto second_return=
                                id(t[other_statement],"return")?
                                    other_statement:other_statement+1;
                            const auto first_is_guard=
                                trivial_guard_return(first_return);
                            const auto second_is_guard=
                                trivial_guard_return(second_return);
                            const auto count=first_is_guard?
                                other->first:
                                second_is_guard?branch->first:
                                    std::min(branch->first,other->first);
                            return include_prior(count);
                        }
                        if(!trivial_guard_return(first_return))
                            prior_nontrivial_uses=include_prior(branch->first);
                        // Skip the recognized return, including braces,
                        // rather than crediting it a second time.
                        i=branch->second;
                        continue;
                    }
                    // Unconditional branches are handled separately above.
                    // For a runtime condition, both simple assignments must
                    // target the same already-known result variable.
                    if(!literal&&nesting==0) {
                        if(const auto first=direct_assignment(statement)) {
                            const auto found=std::find_if(
                                aliases.rbegin(),aliases.rend(),
                                [&](const Alias& alias){
                                    return alias.name==first->name;
                                });
                            if(found==aliases.rend())return 0;
                            const auto else_at=first->end+1;
                            if(else_at<bounds.close&&id(t[else_at],"else")) {
                                const auto other=direct_assignment(else_at+1);
                                if(!other||first->name!=other->name)return 0;
                                found->count=first->zero&&other->zero?0:
                                    first->zero?other->count:
                                    other->zero?first->count:
                                    std::min(first->count,other->count);
                                found->zero=first->zero&&other->zero;
                                i=other->end;
                            }else {
                                // An absent else leaves the prior value live.
                                // Only a literal zero on either path is a
                                // possible guard; all other paths must use
                                // the purpose-named agreement.
                                found->count=found->zero?first->count:
                                    first->zero?found->count:
                                    std::min(found->count,first->count);
                                found->zero=found->zero&&first->zero;
                                i=first->end;
                            }
                            continue;
                        }
                        // Unknown branches must not lend lexical uses
                        // to the returned result. Skip only fully harmless
                        // paths, including the complete else arm if present.
                        const auto first_end=harmless_branch(statement);
                        if(!first_end)return 0;
                        auto last=*first_end;
                        if(last+1<bounds.close&&id(t[last+1],"else")) {
                            const auto other=harmless_branch(last+2);
                            if(!other)return 0;
                            last=*other;
                        }
                        i=last;
                        continue;
                    }
                }
            }
        }
        if(id(t[i],"return")) {
            auto last=i+1;
            while(last<bounds.close&&!op(t[last],";"))++last;
            const auto result_uses=uses(i+1,last);
            // Returns inside branches that were not fully understood must
            // not be ignored: a hardcoded path could otherwise borrow credit
            // from a correct but later fallthrough.
            if(nesting)return 0;
            return include_prior(result_uses);
        }
        // An input expression such as stream >> result is an observable
        // write even though it has no assignment operator or function-call
        // parentheses in the student's source. Invalidate only this alias:
        // an unrelated genuine returned calculation remains acceptable.
        if(extraction_operand(i,bounds.open+1)) {
            const auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                [&](const Alias& alias){return id(t[i],alias.name);});
            if(found!=aliases.rend()){
                found->count=0;
                found->zero=false;
            }
        }
        // A reference/pointer to the result also allows later writes
        // through a different name; such an escape cannot be credited by
        // the simple lexical dependency proof.
        if(id(t[i],"int")||id(t[i],"auto")) {
            auto modifier=i+1;
            if(modifier<bounds.close&&
               (id(t[modifier],"const")||id(t[modifier],"constexpr")))
                ++modifier;
            if(modifier+2<bounds.close&&
               (op(t[modifier],"&")||op(t[modifier],"&&")||
                op(t[modifier],"*"))&&
               t[modifier+1].starts_with("id:")&&
               op(t[modifier+2],"=")) {
                auto end=modifier+3;
                while(end<bounds.close&&!op(t[end],";"))++end;
                if(end>=bounds.close)return 0;
                for(auto at=modifier+3;at<end;++at) {
                    if(!t[at].starts_with("id:"))continue;
                    const auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                        [&](const Alias& alias){return id(t[at],alias.name);});
                    if(found!=aliases.rend()&&
                       (nesting==0||found->depth<nesting))return 0;
                }
            }
        }
        // The earlier guarded-if check cannot cover a straight-line call.
        // A passed result alias may be replaced without an assignment in
        // this function (for example, swap(result, hardcoded)).
        if(i+1<bounds.close&&op(t[i+1],"(")&&
           (t[i].starts_with("id:")||op(t[i],")")||op(t[i],"]"))) {
            const auto end=closing(t,i+1,"(",")");
            if(end&&unsafe_alias_call(i,*end+1))return 0;
        }
        auto start=i;
        if(id(t[start],"const")||id(t[start],"constexpr"))++start;
        // Local intermediates can use either position of const/constexpr,
        // including "auto constexpr ajuste", without losing their real
        // dependency on the named constant.
        const bool has_type=start<bounds.close&&
            (id(t[start],"int")||id(t[start],"auto"));
        const bool trailing_const=has_type&&start+1<bounds.close&&
            (id(t[start+1],"const")||id(t[start+1],"constexpr"));
        const auto variable=start+1+(trailing_const?1u:0u);
        const auto initializer=variable+1;
        if(has_type&&initializer<bounds.close&&
           t[variable].starts_with("id:")&&
           (op(t[initializer],"=")||op(t[initializer],"{")||
            op(t[initializer],"("))) {
            const auto local=t[variable].substr(3);
            auto last=initializer+1;
            while(last<bounds.close&&!op(t[last],";"))++last;
            if(local==name) {
                if(nesting==0) {
                    // Two declarations in one function scope are not an
                    // authenticated local agreement.
                    if(declared)return 0;
                    declared=true;
                }else {
                    // C++ permits a nested shadow. It can neither borrow
                    // nor destroy the outer constant's source evidence.
                    named_rule_shadows.push_back(nesting);
                }
            }else {
                // An initializer that forwards the result to a function
                // can have side effects even when the intermediate is never
                // returned. Do not silently discard that mutation.
                if(unsafe_alias_call(initializer+1,last)||
                   extracted_alias(initializer+1,last))return 0;
                const auto count=uses(initializer+1,last);
                const auto zero=zero_expression(
                    op(t[initializer],"=")?initializer+1:initializer,last);
                // A declaration within braces shadows rather than updating
                // an alias that belongs to the enclosing scope.
                const auto found=std::ranges::find_if(aliases,
                    [&](const Alias& alias){
                        return alias.name==local&&alias.depth==nesting;
                    });
                if(found==aliases.end())aliases.push_back({local,count,nesting,zero});
                else {found->count=count;found->zero=zero;}
            }
            i=last;
            continue;
        }
        // A local result may be declared first and assigned on both sides
        // of a later if/else. No evidence exists at declaration time, and
        // a missing branch must never inherit an imaginary initialization.
        if(has_type&&initializer<bounds.close&&
           t[variable].starts_with("id:")&&op(t[initializer],";")) {
            const auto local=t[variable].substr(3);
            if(local==name) {
                if(nesting==0)return 0;
                named_rule_shadows.push_back(nesting);
            }else{
                if(std::ranges::any_of(aliases,[&](const Alias& alias){
                    return alias.name==local&&alias.depth==nesting;
                }))return 0;
                aliases.push_back({local,0,nesting,false});
            }
            i=initializer;
            continue;
        }
        if(i+1<bounds.close&&t[i].starts_with("id:")&&
           op(t[i+1],"=")) {
            const auto local=t[i].substr(3);
            auto last=i+2;
            while(last<bounds.close&&!op(t[last],";"))++last;
            if(local==name)return 0;
            if(unsafe_alias_call(i+2,last)||
               extracted_alias(i+2,last))return 0;
            const auto count=uses(i+2,last);
            const auto zero=zero_expression(i+2,last);
            // Assignments target the nearest in-scope declaration.
            const auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                [&](const Alias& alias){return alias.name==local;});
            if(found==aliases.rend())aliases.push_back({local,count,nesting,zero});
            else {found->count=count;found->zero=zero;}
            i=last;
            continue;
        }
        // Compound updates and increments mutate a result without a plain
        // '=' token. Ignoring them would allow (result ^= result) followed
        // by a hard-coded reconstruction to inherit the original named-rule
        // uses, even when every independent output probe still passes.
        if(i+1<bounds.close&&t[i].starts_with("id:")) {
            const auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                [&](const Alias& alias){return id(t[i],alias.name);});
            if(found!=aliases.rend()) {
                if(op(t[i+1],"++")||op(t[i+1],"--")) {
                    // Invalidate this value's evidence, not unrelated
                    // named expressions that may be returned instead.
                    found->count=0;found->zero=false;
                    ++i;
                    continue;
                }
                const bool compound=op(t[i+1],"+=")||op(t[i+1],"-=")||
                    op(t[i+1],"*=")||op(t[i+1],"/=")||
                    op(t[i+1],"%=")||op(t[i+1],"^=")||
                    op(t[i+1],"&=")||op(t[i+1],"|=")||
                    op(t[i+1],"<<=")||op(t[i+1],">>=");
                if(compound) {
                    auto last=i+2;
                    while(last<bounds.close&&!op(t[last],";"))++last;
                    if(last>=bounds.close||unsafe_alias_call(i+2,last))
                        return 0;
                    const bool rhs_zero=zero_expression(i+2,last);
                    // Adding/subtracting/or-ing/xor-ing zero is neutral for
                    // the integer result and preserves its earlier evidence.
                    const bool neutral=rhs_zero&&
                        (op(t[i+1],"+=")||op(t[i+1],"-=")||
                         op(t[i+1],"|=")||op(t[i+1],"^="));
                    if(neutral) {
                        i=last;
                        continue;
                    }
                    // A zero-initialized result can be accumulated with +=
                    // as an equivalent spelling of a direct assignment.
                    if(op(t[i+1],"+=")&&found->zero) {
                        found->count=uses(i+2,last);
                        found->zero=rhs_zero;
                        i=last;
                        continue;
                    }
                    // Other compound operations invalidate the affected
                    // local's source evidence. An unused auxiliary variable
                    // must not reject an otherwise correct direct return.
                    found->count=0;
                    found->zero=false;
                    i=last;
                    continue;
                }
            }
        }
        if(i+1<bounds.close&&(op(t[i],"++")||op(t[i],"--"))&&
           t[i+1].starts_with("id:")) {
            const auto found=std::find_if(aliases.rbegin(),aliases.rend(),
                [&](const Alias& alias){return id(t[i+1],alias.name);});
            if(found!=aliases.rend()) {
                found->count=0;found->zero=false;
                ++i;
                continue;
            }
        }
    }
    // A conditional-only return does not prove that every input path uses the rule.
    return 0;
}
} // namespace named_rule_detail

inline NamedRuleAnalysis analyze_named_rule_sources(
        std::string_view source,std::span<const NamedRuleRequirement> requirements) {
    using namespace named_rule_detail;
    if(requirements.empty())return {false,"No hay reglas declaradas."};
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned)return {false,"El codigo fuente no admite analisis lexico."};
    if(!probe_environment_neutral(*scanned))
        return {false,"El codigo depende de la posicion o del entorno de compilacion."};
    // The lexical source gate must not mistake code hidden behind #if 0,
    // macro substitutions, or generated declarations for compiled evidence.
    // A reviewed named-rule exercise only needs ordinary #include directives.
    for(const auto& token:*scanned) {
        // A C++ comment is replaced by whitespace *before* preprocessing.
        // The shared source lexer may not recognize /*comment*/#if 0 as a
        // directive, leaving a standalone '#' or the %: digraph as a token.
        // Such a token cannot be trusted as a compiled function definition.
        if(token=="op:#"||token=="op:%:"||
           token=="op:##"||token=="op:%:%:")
            return {false,"Directiva de preprocesador no admitida."};
        if(!token.starts_with("directive:"))continue;
        std::string_view directive(token);
        directive.remove_prefix(std::string_view("directive:").size());
        if(directive.empty()||directive.front()!='#')
            return {false,"Directiva de preprocesador no admitida."};
        directive.remove_prefix(1);
        while(!directive.empty()&&std::isspace(
                static_cast<unsigned char>(directive.front())))
            directive.remove_prefix(1);
        constexpr std::string_view include="include";
        if(!directive.starts_with(include))
            return {false,"Solo se admiten directivas #include en reglas nombradas."};
        directive.remove_prefix(include.size());
        if(directive.empty()||!(std::isspace(
                static_cast<unsigned char>(directive.front()))||
                directive.front()=='<'||directive.front()=='"'))
            return {false,"Directiva #include no valida en reglas nombradas."};
    }
    Tokens t;
    // Retain literals as opaque operands. Removing one from {4, "text"}
    // would manufacture a valid single-element list with a trailing comma.
    for(const auto& token:*scanned)
        if(!token.starts_with("comment:")&&!token.starts_with("directive:"))
            t.push_back(named_rule_detail::canonical_rule_token(token));
    for(const auto& rule:requirements) {
        if(rule.function.empty()||rule.purpose_words.empty()||!rule.min_uses)
            return {false,"La declaracion de la regla esta incompleta."};
        const auto bounds=body(t,rule.function);
        if(!bounds)return {false,"Falta definir la funcion "+rule.function+"."};
        std::vector<LocalRule> candidates;
        std::size_t depth=0;
        for(auto i=bounds->open+1;i<bounds->close;++i) {
            if(op(t[i],"{")){++depth;continue;}
            if(op(t[i],"}")){if(depth)--depth;continue;}
            if(depth)continue;
            const auto item=constant(t,i,bounds->close);
            if(item&&role(item->name,rule.purpose_words))candidates.push_back(*item);
        }
        if(candidates.size()!=1)
            return {false,"Falta una constante local de proposito en "+rule.function+"."};
        const auto& value=candidates.front();
        if(value.value!=rule.value)
            return {false,"La constante de "+rule.function+" no representa el acuerdo."};
        if(uses_in_return(t,*bounds,value.name)<rule.min_uses)
            return {false,"La constante no interviene suficientemente en el resultado de "+
                rule.function+"."};
    }
    return {true,{}};
}
// A reviewed comment may change, but its code, includes, declarations and
// preprocessor directives must retain exactly the same C++ tokens. Only
// comment lexemes are ignored; string literals and raw strings are retained.
// Line-sensitive macros are conservatively rejected if the source changes.
inline bool named_rule_comment_only_edit(std::string_view original,
        std::string_view candidate) {
    if(original==candidate)return true;
    if(code_format::physical_splice(original)||code_format::physical_splice(candidate))
        return false;
    const auto before=code_format::cpp_tokens(original);
    const auto after=code_format::cpp_tokens(candidate);
    if(!before||!after)return false;
    // A comment edit can move a real line/column query. Quoting the name in
    // the documentation is not itself such a query; inspect lexical tokens.
    if(code_format::cpp_position_sensitive_tokens(*before)||
       code_format::cpp_position_sensitive_tokens(*after))return false;
    const auto only_code=[](const auto& tokens) {
        std::vector<std::string> code;
        for(const auto& token:tokens)
            if(!token.starts_with("comment:"))code.push_back(token);
        return code;
    };
    return only_code(*before)==only_code(*after);
}

// Independently compiled checks are generated from reviewed identifiers,
// integer facts and a bounded input list. The student's stdout is not trusted.
// A zero process exit alone is not proof: a student function can call
// exit(0) before the generated main has checked all authored cases. The
// trusted driver emits this exact final witness only after its last check.
// No newline avoids CRT LF/CRLF differences between Windows and WASI.
inline constexpr std::string_view named_rule_probe_completion_marker=
    "LGAED_NAMED_RULE_PROBE_COMPLETE_V1";

inline std::optional<std::string> named_rule_probe_checks(
        const NamedRuleExecutionProbe& probe) {
    const auto identifier=[](std::string_view word) {
        if(word.empty()||word.size()>128)return false;
        const auto letter=[](unsigned char c) {
            return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
        };
        if(!letter(static_cast<unsigned char>(word.front())))return false;
        for(const unsigned char c:word)
            if(!letter(c)&&!(c>='0'&&c<='9'))return false;
        return true;
    };
    if(probe.cases.empty()||probe.cases.size()>32)return {};
    std::string code="int main() {\n";
    for(std::size_t i=0;i<probe.cases.size();++i) {
        const auto& item=probe.cases[i];
        if(!identifier(item.function))return {};
        for(std::size_t j=0;j<i;++j)
            if(probe.cases[j].function==item.function&&
               probe.cases[j].input==item.input)return {};
        code+="    if (static_cast<int>("+item.function+"("+
            std::to_string(item.input)+")) != "+
            std::to_string(item.expected)+") return "+
            std::to_string(i+1)+";\n";
    }
    code += "    std::fputs(\"";
    code += std::string(named_rule_probe_completion_marker);
    code += "\", stderr);\n";
    code+="    return 0;\n}\n";
    return code;
}
// CMake: replace the original test executable with a trusted test driver.
inline std::optional<std::string> named_rule_probe_source(
        const NamedRuleExecutionProbe& probe) {
    if(probe.header.empty()||probe.header.size()>256||
       probe.header.starts_with('/')||probe.header.starts_with('.')||
       probe.header.find("..")!=std::string::npos)return {};
    for(const unsigned char c:probe.header)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/'))
            return {};
    const auto checks=named_rule_probe_checks(probe);
    if(!checks)return {};
    return "#include <cstdio>\n#include \""+probe.header+"\"\n\n"+*checks;
}
// Single-file: compile the unchanged submitted functions alongside a fresh,
// private main. The student's editable main and reports are not executed by
// the probe. The source-gate separately rejects student preprocessor macros.
inline std::optional<std::string> named_rule_single_file_probe_source(
        const NamedRuleExecutionProbe& probe,std::string_view candidate) {
    if(!probe.harness_path.empty()||!probe.header.empty()||
       candidate.empty()||candidate.size()>4*1024*1024)return {};
    const auto parsed=code_format::cpp_tokens(candidate);
    if(!parsed||!named_rule_detail::probe_environment_neutral(*parsed)||
       !named_rule_detail::probe_source_no_early_exit(candidate))
        return {};
    const auto checks=named_rule_probe_checks(probe);
    if(!checks)return {};
    return "#include <cstdio>\n#define main lgaed_student_original_main\n"+
        std::string(candidate)+"\n#undef main\n\n"+*checks;
}

} // namespace lgaed::curriculum
