#pragma once

#include "core/code_format_compat.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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
struct NamedRuleExecutionProbe {
    std::string harness_path, header;
    std::vector<NamedRuleProbeCase> cases;
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
struct Bounds { std::size_t open, close; };
struct LocalRule { std::string name; std::size_t end; int value; };

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
inline std::optional<int> number(const Tokens& t,std::size_t start,std::size_t end) {
    bool negative=false;
    if(start<end&&(op(t[start],"+")||op(t[start],"-"))) {
        negative=op(t[start],"-");++start;
    }
    if(start+1!=end||!t[start].starts_with("number:"))return {};
    const auto text=std::string_view(t[start]).substr(7);
    // A leading zero introduces an octal literal in C++. Reject it rather
    // than accidentally interpreting a different value as decimal.
    if(text.size()>1&&text.front()=='0')return {};
    int value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return {};
    return negative?-value:value;
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
    if(position+5>=limit||!(id(t[position],"const")||id(t[position],"constexpr"))||
       !id(t[position+1],"int")||!t[position+2].starts_with("id:"))return {};
    const auto name=t[position+2].substr(3);
    const auto assignment=position+3;
    if(op(t[assignment],"=")) {
        auto end=assignment+1;
        while(end<limit&&!op(t[end],";"))++end;
        if(end>=limit)return {};
        const auto value=arithmetic_constant(t,assignment+1,end);
        if(value)return LocalRule{name,end,*value};
    }
    if(op(t[assignment],"{")||op(t[assignment],"(")) {
        const bool brace=op(t[assignment],"{");
        const auto end=closing(t,assignment,brace?"{":"(",brace?"}":")");
        if(!end||*end+1>=limit||!op(t[*end+1],";"))return {};
        const auto value=arithmetic_constant(t,assignment+1,*end);
        if(value)return LocalRule{name,*end+1,*value};
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
inline bool productive(const Tokens& t,std::size_t i) {
    // Direct algebraic identities cannot establish that a constant is
    // meaningfully used. This is deliberately a bounded syntactic check;
    // arbitrary C++ still requires execution evidence.
    if((i>=2&&(op(t[i-1],"-")||op(t[i-1],"/")||
               op(t[i-1],"%"))&&t[i-2]==t[i])||
       (i+2<t.size()&&(op(t[i+1],"-")||op(t[i+1],"/")||
               op(t[i+1],"%"))&&t[i+2]==t[i]))
        return false;
    const zero_after=[&](std::size_t position) {
        if(position>=t.size())return false;
        if(t[position]=="number:0")return true;
        if(!op(t[position],"("))return false;
        const auto end=closing(t,position,"(",")");
        if(!end)return false;
        const auto value=arithmetic_constant(t,position,*end+1);
        return value&&*value==0;
    };
    const zero_before=[&](std::size_t position) {
        if(position>=t.size())return false;
        if(t[position]=="number:0")return true;
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
    return !(i>=2&&op(t[i-1],"*")&&zero_before(i-2))&&
           !(i+2<t.size()&&op(t[i+1],"*")&&zero_after(i+2));
}
inline std::size_t uses_in_return(const Tokens& t,Bounds bounds,
        std::string_view name) {
    // Count references that reach the returned expression. Counting every
    // appearance in the body lets unused logging or discarded calculations
    // satisfy minUses even when the real result uses only one hard-coded role.
    // Local aliases carry their originating use count across initializers and
    // assignments. An overwritten alias loses its previous evidence.
    constexpr std::size_t limit=64;
    std::vector<std::pair<std::string,std::size_t>> aliases;
    const auto uses=[&](std::size_t first,std::size_t last) {
        std::size_t count=0;
        for(auto i=first;i<last;++i) {
            if(!productive(t,i))continue;
            std::size_t contribution=id(t[i],name)?1:0;
            for(const auto& alias:aliases)
                if(id(t[i],alias.first)) {
                    contribution=alias.second;
                    break;
                }
            count+=std::min(contribution,limit-count);
        }
        return count;
    };
    bool declared=false;
    for(auto i=bounds.open+1;i<bounds.close;++i) {
        if(id(t[i],"return")) {
            auto last=i+1;
            while(last<bounds.close&&!op(t[last],";"))++last;
            if(uses(i+1,last)>0)return uses(i+1,last);
            i=last;
            continue;
        }
        auto start=i;
        if(id(t[start],"const")||id(t[start],"constexpr"))++start;
        if(start+2<bounds.close&&
           (id(t[start],"int")||id(t[start],"auto"))&&
           t[start+1].starts_with("id:")&&
           (op(t[start+2],"=")||op(t[start+2],"{")||
            op(t[start+2],"("))) {
            const auto local=t[start+1].substr(3);
            auto last=start+3;
            while(last<bounds.close&&!op(t[last],";"))++last;
            if(local==name) {
                if(declared)return 0; // Ambiguous shadowing of the named rule.
                declared=true;
            }else {
                const auto count=uses(start+3,last);
                const auto found=std::ranges::find_if(aliases,
                    [&](const auto& alias){return alias.first==local;});
                if(found==aliases.end())aliases.emplace_back(local,count);
                else found->second=count;
            }
            i=last;
            continue;
        }
        if(i+1<bounds.close&&t[i].starts_with("id:")&&
           op(t[i+1],"=")) {
            const auto local=t[i].substr(3);
            auto last=i+2;
            while(last<bounds.close&&!op(t[last],";"))++last;
            if(local==name)return 0;
            const auto count=uses(i+2,last);
            const auto found=std::ranges::find_if(aliases,
                [&](const auto& alias){return alias.first==local;});
            if(found==aliases.end())aliases.emplace_back(local,count);
            else found->second=count;
            i=last;
        }
    }
    return 0;
}
} // namespace named_rule_detail

inline NamedRuleAnalysis analyze_named_rule_sources(
        std::string_view source,std::span<const NamedRuleRequirement> requirements) {
    using namespace named_rule_detail;
    if(requirements.empty())return {false,"No hay reglas declaradas."};
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned)return {false,"El codigo fuente no admite analisis lexico."};
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
    for(const auto& token:*scanned)
        if(!token.starts_with("comment:")&&!token.starts_with("directive:")&&
           !token.starts_with("literal:"))t.push_back(token);
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
    for(const auto sentinel:{"__LINE__","source_location"})
        if(original.find(sentinel)!=original.npos||
           candidate.find(sentinel)!=candidate.npos)return false;
    const auto before=code_format::cpp_tokens(original);
    const auto after=code_format::cpp_tokens(candidate);
    if(!before||!after)return false;
    const auto only_code=[](const auto& tokens) {
        std::vector<std::string> code;
        for(const auto& token:tokens)
            if(!token.starts_with("comment:"))code.push_back(token);
        return code;
    };
    return only_code(*before)==only_code(*after);
}

// Bounded source generation is shared by the worker and its verifier; it
// accepts identifiers, integer values and a safe include path, never raw C++.
inline std::optional<std::string> named_rule_probe_source(
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
    if(probe.cases.empty()||probe.cases.size()>32||probe.header.empty()||
       probe.header.size()>256||probe.header.starts_with('/')||
       probe.header.starts_with('.')||probe.header.find("..")!=std::string::npos)
        return {};
    for(const unsigned char c:probe.header)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/'))
            return {};
    std::string code="#include \""+probe.header+"\"\n\nint main() {\n";
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
    code+="    return 0;\n}\n";
    return code;
}

} // namespace lgaed::curriculum
