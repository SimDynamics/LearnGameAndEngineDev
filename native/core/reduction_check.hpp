#pragma once

#include "core/boolean_battery_check.hpp"
#include <array>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace lgaed::curriculum {

inline constexpr std::string_view reduction_objective_version =
    "stateful-vector-int-reproduction-independent-observation-v1";
struct ReductionDependency {
    std::string header;
    std::vector<std::string> symbols;
    bool operator==(const ReductionDependency&) const = default;
};
struct ReductionCall {
    std::vector<std::int32_t> values;
    std::int32_t argument=0,result=0,state_after=0;
    bool operator==(const ReductionCall&) const = default;
};
struct ReductionSpec {
    std::string function_name,state_name,initial_source,protected_definition;
    std::int32_t initial_state=0;
    std::vector<ReductionCall> calls;
    std::size_t observed_call=1;
    std::int32_t required_result=0;
    std::uint32_t exit_code=0;
    std::vector<ReductionDependency> dependencies;
    bool operator==(const ReductionSpec&) const = default;
};
// A missing replacement is the published body and its real returned value.
struct ReductionPhase {
    std::optional<std::int32_t> replacement;
    bool operator==(const ReductionPhase&) const = default;
};
struct ReductionObservation {
    std::vector<std::int32_t> values;
    std::int32_t argument=0,state_before=0,state_after=0,result=0,delivered=0;
    bool operator==(const ReductionObservation&) const = default;
};
struct ReductionRun {
    std::string program_frame;
    bool compiled=false,executed=false,completed_normally=false;
    std::uint32_t exit_code=0;
    ReductionPhase phase;
    std::vector<ReductionObservation> calls;
    std::string stdout_bytes,stderr_bytes;
};
struct ReductionTrace {
    std::vector<ReductionObservation> calls;
    bool completed_normally=false;
    std::string visible;
};
struct ReductionVerdict {
    bool accepted=false;
    std::string reason;
};

namespace reduction_detail {
using Token=boolean_battery_detail::Token;
using Tokens=std::vector<Token>;
inline bool identifier(std::string_view name) {
    return boolean_battery_detail::identifier(name)&&
        !name.starts_with("lgaed_reduction_");
}
inline std::optional<Tokens> tokens(std::string_view source) {
    auto result=boolean_battery_detail::tokens(source);
    if(!result)return {};
    for(const auto& item:*result)
        if(item.value.starts_with("id:lgaed_reduction_"))return {};
    return result;
}
inline bool opening(std::string_view token,std::string_view expected) {
    return token==expected||(expected=="op:{"&&token=="op:<%");
}
inline bool ending(std::string_view token,std::string_view expected) {
    return token==expected||(expected=="op:}"&&token=="op:%>");
}
inline std::optional<std::size_t> closing(const Tokens& t,std::size_t start,
        std::string_view first,std::string_view last) {
    std::size_t depth=0;
    for(auto i=start;i<t.size();++i) {
        if(opening(t[i].value,first)){if(++depth>4096)return {};}
        else if(ending(t[i].value,last)) {
            if(!depth)return {};
            if(!--depth)return i;
        }
    }
    return {};
}
inline std::vector<std::string> values(const Tokens& t,std::size_t first,std::size_t last) {
    std::vector<std::string> result;
    for(auto i=first;i<last;++i)result.push_back(t[i].value);
    return result;
}
struct Definition {
    std::size_t first=0,last=0,opening_end=0,closing_begin=0,begin=0,end=0;
    std::string sequence,argument;
    std::vector<std::string> signature,contents;
};
inline std::optional<Definition> definition(const Tokens& t,std::string_view name) {
    if(!identifier(name)||name=="main")return {};
    std::optional<Definition> found;
    std::vector<std::vector<std::string>> prototypes;
    std::size_t depth=0;
    for(std::size_t i=0;i<t.size();++i) {
        const auto& value=t[i].value;
        if(value.starts_with("directive:")&&depth)return {};
        if(value=="id:"+std::string(name)&&i+1<t.size()&&t[i+1].value=="op:(") {
            const auto close=closing(t,i+1,"op:(","op:)");
            if(!close)return {};
            const auto previous=i?t[i-1].value:std::string{};
            const auto following=*close+1<t.size()?t[*close+1].value:std::string{};
            const bool declaration=(previous.starts_with("id:")&&
                previous!="id:return"&&previous!="id:co_return"&&previous!="id:throw")||
                following=="op:{"||following=="op:<%"||
                (!depth&&i>1&&t[i-2].value=="id:int"&&
                 (previous=="op:&"||previous=="op:*"||previous=="op:&&"));
            if(declaration) {
                if(depth||i==0||previous!="id:int"||i+13>=t.size()||
                   (i>1&&t[i-2].value!="op:;"&&t[i-2].value!="op:}"&&
                    !t[i-2].value.starts_with("directive:")))return {};
                const std::array<std::pair<std::size_t,std::string_view>,10> expected{{
                    {1,"op:("},{2,"id:std"},{3,"op:::"},{4,"id:vector"},
                    {5,"op:<"},{6,"id:int"},{7,"op:>"},{9,"op:,"},
                    {10,"id:int"},{12,"op:)"}
                }};
                for(const auto& [offset,token]:expected)if(t[i+offset].value!=token)return {};
                if(!t[i+8].value.starts_with("id:")||!t[i+11].value.starts_with("id:"))return {};
                const auto sequence=t[i+8].value.substr(3),argument=t[i+11].value.substr(3);
                if(!identifier(sequence)||!identifier(argument)||sequence==argument)return {};
                auto signature=values(t,i-1,i+13);
                if(t[i+13].value=="op:;")prototypes.push_back(std::move(signature));
                else {
                    if(t[i+13].value!="op:{"||found)return {};
                    const auto body_end=closing(t,i+13,"op:{","op:}");
                    if(!body_end)return {};
                    for(auto n=i+14;n<*body_end;++n)
                        if(t[n].value.starts_with("directive:")||
                           t[n].value=="id:__func__"||t[n].value=="id:__FUNCTION__"||
                           t[n].value=="id:__FUNCSIG__")return {};
                    found=Definition{i-1,*body_end+1,t[i+13].end,t[*body_end].begin,
                        t[i-1].begin,t[*body_end].end,sequence,argument,
                        std::move(signature),values(t,i-1,*body_end+1)};
                }
            }
        }
        if(opening(value,"op:{")){if(++depth>4096)return {};}
        else if(ending(value,"op:}")){if(!depth)return {};--depth;}
    }
    if(depth||!found)return {};
    for(const auto& signature:prototypes)if(signature!=found->signature)return {};
    return found;
}
inline std::optional<std::int32_t> decimal(std::span<const std::string> t) {
    if(t.empty()||t.size()>2)return {};
    std::string text;
    if(t.size()==2) {
        if(t.front()!="op:+"&&t.front()!="op:-")return {};
        text=t.front().substr(3);
    }
    if(!t.back().starts_with("number:"))return {};
    const auto number=std::string_view(t.back()).substr(7);
    if(number.empty()||(number.size()>1&&number.front()=='0')||
       !std::ranges::all_of(number,[](char c){return c>='0'&&c<='9';}))return {};
    text+=number;
    std::int64_t parsed=0;
    auto input=std::string_view(text);if(input.starts_with('+'))input.remove_prefix(1);
    const auto [end,error]=std::from_chars(input.data(),input.data()+input.size(),parsed);
    if(error!=std::errc{}||end!=input.data()+input.size()||
       parsed<std::numeric_limits<std::int32_t>::min()||
       parsed>std::numeric_limits<std::int32_t>::max())return {};
    return static_cast<std::int32_t>(parsed);
}
struct StateDeclaration {std::size_t first=0,last=0;};
inline std::optional<StateDeclaration> state_declaration(const Tokens& t,
        std::string_view name,std::int32_t expected) {
    if(!identifier(name))return {};
    std::optional<StateDeclaration> found;
    std::size_t depth=0;
    for(std::size_t i=0;i<t.size();++i) {
        const auto& value=t[i].value;
        if(value=="id:"+std::string(name)) {
            const auto previous=i?t[i-1].value:std::string{};
            const auto type_name=[](std::string_view token) {
                return token.starts_with("id:")&&token!="id:return"&&
                    token!="id:co_return"&&token!="id:throw";
            };
            const bool declaration=type_name(previous)||(i>1&&type_name(t[i-2].value)&&
                (previous=="op:&"||previous=="op:*"||previous=="op:&&"));
            if(declaration) {
                if(found||depth||previous!="id:int"||i==0||i+1>=t.size()||
                   (i>1&&t[i-2].value!="op:;"&&t[i-2].value!="op:}"&&
                    !t[i-2].value.starts_with("directive:")))return {};
                const auto next=i+1;
                std::size_t last=next;
                std::optional<std::int32_t> initial;
                if(t[next].value=="op:;")initial=0;
                else if(t[next].value=="op:{"||t[next].value=="op:(") {
                    const auto close=closing(t,next,t[next].value,
                        t[next].value=="op:{"?"op:}":"op:)");
                    if(!close||*close+1>=t.size()||t[*close+1].value!="op:;")return {};
                    const auto initializer=values(t,next+1,*close);
                    if(initializer.empty()) {
                        if(t[next].value!="op:{")return {};
                        initial=0;
                    }else initial=decimal(initializer);
                    last=*close+1;
                }else if(t[next].value=="op:=") {
                    last=next+1;
                    while(last<t.size()&&t[last].value!="op:;")++last;
                    if(last==t.size())return {};
                    initial=decimal(values(t,next+1,last));
                }else return {};
                if(!initial||*initial!=expected)return {};
                found=StateDeclaration{i-1,last+1};
            }
        }
        if(opening(value,"op:{")){if(++depth>4096)return {};}
        else if(ending(value,"op:}")){if(!depth)return {};--depth;}
    }
    return depth?std::optional<StateDeclaration>{}:found;
}
inline std::optional<std::string> included_header(std::string_view token) {
    if(!token.starts_with("directive:"))return {};
    auto text=token.substr(10);text.remove_prefix(text.starts_with("%:")?2:1);
    while(!text.empty()&&code_format::space(text.front()))text.remove_prefix(1);
    if(!text.starts_with("include"))return {};
    text.remove_prefix(7);
    while(!text.empty()&&code_format::space(text.front()))text.remove_prefix(1);
    if(text.empty()||(text.front()!='<'&&text.front()!='"'))return {};
    const auto end=text.find(text.front()=='<'?'>':'"',1);
    if(end==text.npos)return {};
    return std::string(text.substr(1,end-1));
}
inline std::optional<std::vector<std::string>> symbol_tokens(std::string_view symbol) {
    if(symbol.empty()||symbol.size()>512)return {};
    std::vector<std::string> result;
    for(std::size_t start=0;start<symbol.size();) {
        const auto end=symbol.find("::",start);
        const auto component=symbol.substr(start,end==symbol.npos?symbol.size()-start:end-start);
        if(!identifier(component))return {};
        result.push_back("id:"+std::string(component));
        if(end==symbol.npos)break;
        result.emplace_back("op:::");start=end+2;
        if(start==symbol.size())return {};
    }
    return result.size()>=3?std::optional{result}:std::nullopt;
}
inline bool dependency_contract(const std::vector<ReductionDependency>& dependencies) {
    if(dependencies.empty()||dependencies.size()>32)return false;
    std::set<std::string> headers,symbols;
    for(const auto& dependency:dependencies) {
        const auto& header=dependency.header;
        if(header.empty()||header.size()>64||header.front()<'a'||header.front()>'z'||
           header.find("..")!=header.npos||!headers.insert(header).second||
           !std::ranges::all_of(header,[](char c){return(c>='a'&&c<='z')||
               (c>='0'&&c<='9')||c=='_'||c=='.';})||
           dependency.symbols.empty()||dependency.symbols.size()>32)return false;
        for(const auto& symbol:dependency.symbols)
            if(!symbol_tokens(symbol)||!symbols.insert(symbol).second)return false;
    }
    return true;
}
inline bool contains(const Tokens& t,std::span<const std::string> pattern) {
    if(pattern.empty())return false;
    for(std::size_t i=0;i+pattern.size()<=t.size();++i) {
        bool equal=true;
        for(std::size_t n=0;n<pattern.size();++n)if(t[i+n].value!=pattern[n]){equal=false;break;}
        if(equal)return true;
    }
    return false;
}
inline bool dependencies_present(const Tokens& t,const std::vector<ReductionDependency>& dependencies) {
    std::set<std::string> headers;
    for(const auto& token:t)if(token.value.starts_with("directive:")) {
        const auto header=included_header(token.value);if(!header)return false;headers.insert(*header);
    }
    const std::array<std::string,4> using_std{"id:using","id:namespace","id:std","op:;"};
    const bool imported_std=contains(t,using_std);
    for(const auto& dependency:dependencies)for(const auto& symbol:dependency.symbols) {
        const auto pattern=*symbol_tokens(symbol);
        bool used=contains(t,pattern);
        if(pattern.front()=="id:std"&&pattern.size()==3) {
            const std::array<std::string,5> using_one{"id:using","id:std","op:::",pattern.back(),"op:;"};
            if(imported_std||contains(t,using_one))
                used=used||std::ranges::any_of(t,[&](const auto& item){return item.value==pattern.back();});
        }
        if(used&&!headers.contains(dependency.header))return false;
    }
    return true;
}
inline std::optional<std::string> literal(std::string_view token) {
    if(!token.starts_with("literal:"))return {};
    auto text=token.substr(8);
    const auto first=text.find_first_of("\"'");
    if(first==text.npos)return {};
    if(first&&text[first-1]=='R') {
        const auto begin=text.find('(',first+1);if(begin==text.npos)return {};
        const auto tail=")"+std::string(text.substr(first+1,begin-first-1))+"\"";
        if(!text.ends_with(tail))return {};
        return std::string(text.substr(begin+1,text.size()-tail.size()-begin-1));
    }
    if(text.back()!=text[first])return {};
    std::string result;
    for(std::size_t i=first+1;i+1<text.size();++i) {
        char c=text[i];
        if(c!='\\'){result+=c;continue;}
        if(++i+1>=text.size())return {};
        c=text[i];
        switch(c) {
        case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
        case 'v':result+='\v';break;case 'f':result+='\f';break;
        case '\\':case '\'':case '"':result+=c;break;
        default:return {};
        }
    }
    return result;
}
inline bool non_space(std::string_view text) {
    return std::ranges::any_of(text,[](char c){return !code_format::space(c);});
}
inline std::optional<std::size_t> format_units(std::string_view format,bool semantic=true) {
    std::size_t units=0;std::string literal_part;
    const auto flush=[&]{if(semantic?non_space(literal_part):!literal_part.empty())++units;literal_part.clear();};
    for(std::size_t i=0;i<format.size();++i) {
        if(format[i]!='%'){literal_part+=format[i];continue;}
        if(i+1<format.size()&&format[i+1]=='%'){literal_part+='%';++i;continue;}
        flush();++i;
        while(i<format.size()&&std::string_view("diouxXfFeEgGaAcspn").find(format[i])==std::string_view::npos)++i;
        if(i==format.size()||format[i]=='n')return {};
        ++units;
    }
    flush();return units;
}
// Count structural work, never source lines, spelling lengths or comments.
// Stream insertions retain their individual payload units across a merge of
// two statements. printf's constant format is expanded to the same payload
// units; newline-only separators and standard formatting adapters add none.
using Profile=std::array<std::size_t,5>; // bindings, functions, calls, output, operations
inline std::optional<Profile> profile(const Tokens& t,const Definition& function,
        const StateDeclaration& state,bool semantic=true) {
    Profile result{};
    std::vector<bool> skip(t.size()),declarations(t.size()),initializers(t.size()),output_ops(t.size()),output_calls(t.size());
    for(auto i=function.first;i<function.last;++i)skip[i]=true;
    for(auto i=state.first;i<state.last;++i)skip[i]=true;
    for(std::size_t i=0;i<t.size();++i)if(t[i].value.starts_with("directive:"))skip[i]=true;
    const auto control=[](std::string_view s) {
        return s=="id:if"||s=="id:for"||s=="id:while"||s=="id:switch"||
            s=="id:catch"||s=="id:sizeof"||s=="id:alignof"||s=="id:decltype";
    };
    for(std::size_t i=0;i+1<t.size();++i) {
        if(skip[i]||!t[i].value.starts_with("id:")||control(t[i].value)||t[i+1].value!="op:(")continue;
        const auto close=closing(t,i+1,"op:(","op:)");if(!close)return {};
        if(*close+1<t.size()&&t[*close+1].value=="op:{") {
            if(t[i].value!="id:main")++result[1];
            for(auto n=i;n<=*close;++n)skip[n]=true;
        }
    }
    const std::set<std::string_view> ignored_ids{
        "id:const","id:volatile","id:constexpr","id:static","id:inline","id:extern",
        "id:return","id:co_return","id:throw","id:if","id:else","id:for","id:while",
        "id:switch","id:case","id:break","id:continue","id:using","id:namespace",
        "id:public","id:private","id:protected","id:template","id:typename"
    };
    for(std::size_t i=0;i<t.size();++i) {
        if(skip[i]||!t[i].value.starts_with("id:")||ignored_ids.contains(t[i].value))continue;
        if(i&&t[i-1].value=="op:::")continue;
        auto n=i+1;
        while(n+1<t.size()&&t[n].value=="op:::"&&t[n+1].value.starts_with("id:"))n+=2;
        if(n<t.size()&&t[n].value=="op:<") {
            std::size_t depth=1;++n;
            for(;n<t.size()&&depth;++n) {
                if(t[n].value=="op:<")++depth;
                else if(t[n].value=="op:>")--depth;
                else if(t[n].value=="op:>>"){if(depth<2)break;depth-=2;}
                if(depth>4096)break;
            }
            if(depth)continue;
        }
        while(n<t.size()&&(t[n].value=="op:&"||t[n].value=="op:&&"||t[n].value=="op:*"||
                t[n].value=="id:const"||t[n].value=="id:volatile"))++n;
        if(n+1>=t.size()||skip[n]||!t[n].value.starts_with("id:")||ignored_ids.contains(t[n].value))continue;
        if(t[n+1].value!="op:="&&t[n+1].value!="op:{"&&t[n+1].value!="op:("&&
           t[n+1].value!="op:;"&&t[n+1].value!="op:,")continue;
        if(!declarations[n]){++result[0];declarations[n]=true;}
        if(t[n+1].value=="op:=")initializers[n+1]=true;
    }
    for(std::size_t i=0;i+1<t.size();++i) {
        if(skip[i])continue;
        const auto& token=t[i].value;
        if((token=="id:cout"||token=="id:cerr"||token=="id:clog")&&t[i+1].value=="op:<<") {
            std::size_t position=i+1;
            while(position<t.size()&&t[position].value=="op:<<") {
                output_ops[position]=true;
                const auto begin=position+1;auto end=begin;
                std::size_t parentheses=0,braces=0;
                for(;end<t.size();++end) {
                    const auto& s=t[end].value;
                    if(!parentheses&&!braces&&(s=="op:<<"||s=="op:;"))break;
                    if(s=="op:(")++parentheses;else if(s=="op:)"){if(!parentheses)break;--parentheses;}
                    if(opening(s,"op:{"))++braces;else if(ending(s,"op:}")){if(!braces)break;--braces;}
                }
                if(end==begin)return {};
                bool payload=true;
                if(semantic&&end==begin+1)if(const auto text=literal(t[begin].value))payload=non_space(*text);
                if((end==begin+1&&t[begin].value=="id:endl")||
                   (end==begin+3&&t[begin].value=="id:std"&&t[begin+1].value=="op:::"&&
                    (t[begin+2].value=="id:endl"||t[begin+2].value=="id:flush"||
                     t[begin+2].value=="id:hex"||t[begin+2].value=="id:dec"||
                     t[begin+2].value=="id:boolalpha"||t[begin+2].value=="id:showpos")))payload=!semantic;
                if(payload)++result[3];
                position=end;
            }
        }
        if((token=="id:printf"||token=="id:fprintf"||token=="id:puts"||
            token=="id:fputs"||token=="id:putchar"||token=="id:fputc")&&t[i+1].value=="op:(") {
            const auto close=closing(t,i+1,"op:(","op:)");if(!close)return {};
            auto format=i+2;
            if(token=="id:fprintf") {
                while(format<*close&&t[format].value!="op:,")++format;
                ++format;
            }
            if(format>=*close)return {};
            const auto text=literal(t[format].value);
            std::size_t count=1;
            if(token=="id:printf"||token=="id:fprintf") {
                std::size_t end=format,depth=0;
                for(;end<*close;++end) {
                    if(!depth&&t[end].value=="op:,")break;
                    if(t[end].value=="op:(")++depth;
                    else if(t[end].value=="op:)"){if(!depth)return {};--depth;}
                }
                bool found=false;count=0;
                for(auto n=format;n<end;) {
                    auto part=literal(t[n].value);
                    if(!part){++n;continue;}
                    ++n;
                    while(n<end) {
                        const auto adjacent=literal(t[n].value);
                        if(!adjacent)break;
                        *part+=*adjacent;++n;
                    }
                    const auto expanded=format_units(*part,semantic);if(!expanded)return {};
                    count=std::max(count,*expanded);found=true;
                }
                if(!found)return {};
            }else if(text)count=!semantic||non_space(*text)?1:0;
            result[3]+=count;output_calls[i]=true;
        }
    }
    const std::set<std::string_view> standard_adapters{
        "id:to_string","id:setw","id:setprecision","id:setfill"
    };
    const std::set<std::string_view> operations{
        "op:=","op:+=","op:-=","op:*=","op:/=","op:%=","op:++","op:--",
        "op:+","op:-","op:*","op:/","op:%","op:&&","op:||","op:!","op:?",
        "op:==","op:!=","op:<=","op:>=","op:<<","op:>>","op:&","op:|","op:^"
    };
    for(std::size_t i=0;i<t.size();++i) {
        if(skip[i])continue;
        const auto& token=t[i].value;
        // An unrelated helper may have any name, including to_string. Only
        // the explicitly qualified standard adapter has no additional work.
        const bool adapter=i>=2&&t[i-2].value=="id:std"&&t[i-1].value=="op:::"&&
            standard_adapters.contains(token);
        if(i+1<t.size()&&token.starts_with("id:")&&t[i+1].value=="op:("&&
           !control(token)&&!declarations[i]&&!output_calls[i]&&!adapter)++result[2];
        if((token=="id:if"||token=="id:for"||token=="id:while"||token=="id:switch"||token=="id:catch")||
           (operations.contains(token)&&!initializers[i]&&!output_ops[i]))++result[4];
    }
    return result;
}
inline std::optional<std::vector<std::int32_t>> observable_numbers(std::string_view text) {
    std::vector<std::int32_t> result;
    const auto part=[](unsigned char c){return(c>='0'&&c<='9')||(c>='A'&&c<='Z')||
        (c>='a'&&c<='z')||c=='_'||c>=128;};
    for(std::size_t i=0;i<text.size();) {
        const auto begin=i;
        if((text[i]=='+'||text[i]=='-')&&i+1<text.size()&&text[i+1]>='0'&&text[i+1]<='9')++i;
        else if(text[i]<'0'||text[i]>'9'){++i;continue;}
        const auto digits=i;
        while(i<text.size()&&text[i]>='0'&&text[i]<='9')++i;
        if((begin&&(part(static_cast<unsigned char>(text[begin-1]))||text[begin-1]=='.'))||
           (i<text.size()&&(part(static_cast<unsigned char>(text[i]))||
              (text[i]=='.'&&i+1<text.size()&&text[i+1]>='0'&&text[i+1]<='9'))))continue;
        auto value=text.substr(begin,i-begin);
        if(value.starts_with('+'))value.remove_prefix(1);
        std::int64_t number=0;
        const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),number);
        if(error!=std::errc{}||end!=value.data()+value.size()||
           number<std::numeric_limits<std::int32_t>::min()||number>std::numeric_limits<std::int32_t>::max())continue;
        (void)digits;
        if(result.size()>=4096)return {};
        result.push_back(static_cast<std::int32_t>(number));
    }
    return result;
}
inline std::optional<std::vector<std::int32_t>> observable_numbers(const ReductionRun& run) {
    auto result=observable_numbers(run.stdout_bytes),errors=observable_numbers(run.stderr_bytes);
    if(!result||!errors||result->size()+errors->size()>4096)return {};
    result->insert(result->end(),errors->begin(),errors->end());std::ranges::sort(*result);return result;
}
} // namespace reduction_detail

inline std::optional<std::string> reduction_extract_definition(
        std::string_view source,std::string_view function) {
    const auto tokens=reduction_detail::tokens(source);if(!tokens)return {};
    const auto found=reduction_detail::definition(*tokens,function);if(!found)return {};
    return std::string(source.substr(found->begin,found->end-found->begin));
}
inline bool reduction_spec_valid(const ReductionSpec& spec) {
    using namespace reduction_detail;
    if(!identifier(spec.function_name)||!identifier(spec.state_name)||spec.function_name==spec.state_name||
       spec.calls.size()<2||spec.calls.size()>32||!spec.observed_call||spec.observed_call>=spec.calls.size()||
       spec.exit_code!=0||spec.required_result==spec.calls[spec.observed_call].result||
       !dependency_contract(spec.dependencies))return false;
    for(const auto& call:spec.calls)if(call.values.size()>64)return false;
    const auto initial=tokens(spec.initial_source),protected_tokens=tokens(spec.protected_definition);
    if(!initial||!protected_tokens)return false;
    const auto original=definition(*initial,spec.function_name);
    const auto protected_function=definition(*protected_tokens,spec.function_name);
    return original&&protected_function&&original->sequence!=spec.state_name&&
        original->argument!=spec.state_name&&protected_function->first==0&&
        protected_function->last==protected_tokens->size()&&
        original->contents==protected_function->contents&&
        state_declaration(*initial,spec.state_name,spec.initial_state)&&
        dependencies_present(*initial,spec.dependencies);
}
inline std::string reduction_spec_frame(const ReductionSpec& spec) {
    if(!reduction_spec_valid(spec))throw std::invalid_argument("Invalid reduction contract");
    std::string result;
    const auto field=[&](std::string_view value){result+=std::to_string(value.size())+":";result+=value;};
    const auto source=[&](std::string_view text) {
        const auto tokens=*reduction_detail::tokens(text);
        field(std::to_string(tokens.size()));for(const auto& token:tokens)field(token.value);
    };
    field(reduction_objective_version);field(spec.function_name);field(spec.state_name);
    source(spec.initial_source);source(spec.protected_definition);
    field(std::to_string(spec.initial_state));field(std::to_string(spec.calls.size()));
    for(const auto& call:spec.calls) {
        field(std::to_string(call.values.size()));for(const auto value:call.values)field(std::to_string(value));
        field(std::to_string(call.argument));field(std::to_string(call.result));field(std::to_string(call.state_after));
    }
    field(std::to_string(spec.observed_call));field(std::to_string(spec.required_result));field(std::to_string(spec.exit_code));
    field(std::to_string(spec.dependencies.size()));
    for(const auto& dependency:spec.dependencies) {
        field(dependency.header);field(std::to_string(dependency.symbols.size()));
        for(const auto& symbol:dependency.symbols)field(symbol);
    }
    return result;
}
inline ReductionVerdict reduction_source_check(std::string_view source,const ReductionSpec& spec) {
    using namespace reduction_detail;
    if(!reduction_spec_valid(spec))return {false,"El contrato de reproducción no es válido."};
    const auto candidate=tokens(source);if(!candidate)return {false,"La fuente contiene una construcción no admitida para comprobar esta reproducción."};
    const auto function=definition(*candidate,spec.function_name);
    const auto original_tokens=tokens(spec.initial_source);
    const auto original=definition(*original_tokens,spec.function_name);
    const auto state=state_declaration(*candidate,spec.state_name,spec.initial_state);
    if(!function||function->contents!=original->contents||!state)
        return {false,"Conserva la función investigada y la declaración inicial de su contador."};
    if(!dependencies_present(*candidate,spec.dependencies))
        return {false,"La fuente utiliza un servicio sin declarar directamente la cabecera necesaria."};
    const auto before=profile(*original_tokens,*original,*state_declaration(*original_tokens,spec.state_name,spec.initial_state));
    const auto after=profile(*candidate,*function,*state);
    const auto before_structure=profile(*original_tokens,*original,
        *state_declaration(*original_tokens,spec.state_name,spec.initial_state),false);
    const auto after_structure=profile(*candidate,*function,*state,false);
    if(!before||!after||!before_structure||!after_structure)
        return {false,"No se pudo comprobar la estructura de este recorte; conserva una reproducción de un solo archivo."};
    const auto total=[](const Profile& value){return std::accumulate(value.begin(),value.end(),std::size_t{0});};
    if(total(*after)>=total(*before)||total(*after_structure)>=total(*before_structure))
        return {false,"Todavía no se acreditó una reducción del código accesorio; cambiar su formato o sus nombres no retira trabajo."};
    return {true,"El recorte conserva la función, el contador y sus dependencias."};
}
inline std::optional<std::vector<ReductionPhase>> reduction_plan(const ReductionSpec& spec) {
    if(!reduction_spec_valid(spec))return {};
    const auto actual=spec.calls[spec.observed_call].result;
    for(const std::int64_t other:{std::int64_t{actual}+1,std::int64_t{actual}-1,
            std::int64_t{0},std::int64_t{1},std::int64_t{-1}})
        if(other>=std::numeric_limits<std::int32_t>::min()&&
           other<=std::numeric_limits<std::int32_t>::max()&&
           other!=actual&&other!=spec.required_result)
            return std::vector<ReductionPhase>{{{}},{spec.required_result},{static_cast<std::int32_t>(other)}};
    return {};
}
inline std::optional<std::string> reduction_instrument(std::string_view source,
        const ReductionSpec& spec,const ReductionPhase& phase) {
    using namespace reduction_detail;
    const auto plan=reduction_plan(spec);
    if(!plan||std::ranges::find(*plan,phase)==plan->end())return {};
    const auto candidate=tokens(source);if(!candidate)return {};
    const auto function=definition(*candidate,spec.function_name);
    const auto protected_tokens=tokens(spec.protected_definition);
    const auto original=definition(*protected_tokens,spec.function_name);
    if(!function||function->contents!=original->contents||
       !state_declaration(*candidate,spec.state_name,spec.initial_state))return {};
    std::string result=R"cpp(#include <charconv>
#include <cstdio>
#include <iostream>
#include <vector>
namespace lgaed_reduction_private {
unsigned count=0;
void write(const char* bytes, std::size_t size) {
    std::fwrite(bytes,1,size,stderr);
    std::fflush(stderr);
}
void call(unsigned index,const std::vector<int>& values,int argument,int before,int after,int result,int delivered) {
    if(values.size()>64)return;
    char bytes[2048]="LGAED_REDUCTION_CALL:";
    char* end=bytes+sizeof("LGAED_REDUCTION_CALL:")-1;
    const auto append=[&](long long value) {
        const auto converted=std::to_chars(end,bytes+sizeof(bytes)-2,value);
        if(converted.ec!=std::errc{})return false;
        end=converted.ptr;*end++=',';return true;
    };
    if(!append(index)||!append(argument)||!append(before)||!append(after)||
       !append(result)||!append(delivered)||!append(values.size()))return;
    for(const auto value:values)if(!append(value))return;
    end[-1]='\n';write(bytes,static_cast<std::size_t>(end-bytes));
}
struct Completion {
    ~Completion(){write("LGAED_REDUCTION_END\n",sizeof("LGAED_REDUCTION_END\n")-1);}
};
Completion completion;
}
#line 1
)cpp";
    result+=source.substr(0,function->opening_end);
    result+="\n    const auto lgaed_reduction_index=lgaed_reduction_private::count++;\n";
    result+="    const auto lgaed_reduction_values="+function->sequence+";\n";
    result+="    const int lgaed_reduction_argument="+function->argument+";\n";
    result+="    const int lgaed_reduction_before="+spec.state_name+";\n";
    result+="    const int lgaed_reduction_result=[&]() -> int {\n#line "+
        std::to_string(1+std::ranges::count(source.substr(0,function->opening_end),'\n'))+"\n";
    result+=source.substr(function->opening_end,function->closing_begin-function->opening_end);
    result+="\n    }();\n    const int lgaed_reduction_after="+spec.state_name+";\n";
    result+="    const int lgaed_reduction_delivered=";
    if(phase.replacement)result+="lgaed_reduction_index=="+std::to_string(spec.observed_call)+
        " ? "+std::to_string(*phase.replacement)+" : ";
    result+="lgaed_reduction_result;\n";
    result+="    lgaed_reduction_private::call(lgaed_reduction_index,lgaed_reduction_values,lgaed_reduction_argument,lgaed_reduction_before,lgaed_reduction_after,lgaed_reduction_result,lgaed_reduction_delivered);\n";
    result+="    return lgaed_reduction_delivered;\n}\n#line "+
        std::to_string(1+std::ranges::count(source.substr(0,function->end),'\n'))+"\n";
    result+=source.substr(function->end);return result;
}
inline std::optional<ReductionTrace> reduction_parse_trace(std::string_view all) {
    constexpr std::string_view call="LGAED_REDUCTION_CALL:",finish="LGAED_REDUCTION_END";
    ReductionTrace result;
    for(std::size_t position=0;position<all.size();) {
        const auto next_call=all.find(call,position),next_finish=all.find(finish,position);
        const auto begin=std::min(next_call,next_finish);
        if(begin==all.npos){result.visible.append(all.substr(position));break;}
        result.visible.append(all.substr(position,begin-position));
        const bool completion=begin==next_finish;
        const auto payload=begin+(completion?finish.size():call.size());
        const auto end=all.find('\n',payload);if(end==all.npos)return {};
        auto line=all.substr(payload,end-payload);if(line.ends_with('\r'))line.remove_suffix(1);
        if(completion) {
            if(result.completed_normally||!line.empty())return {};
            result.completed_normally=true;
        }else {
            if(result.completed_normally||result.calls.size()>=32)return {};
            std::vector<std::int64_t> fields;
            for(std::size_t start=0;start<=line.size();) {
                if(fields.size()>=71)return {};
                const auto comma=line.find(',',start),last=comma==line.npos?line.size():comma;
                const auto item=line.substr(start,last-start);if(item.empty())return {};
                std::int64_t value=0;const auto [ptr,error]=std::from_chars(item.data(),item.data()+item.size(),value);
                if(error!=std::errc{}||ptr!=item.data()+item.size())return {};
                fields.push_back(value);if(comma==line.npos)break;start=comma+1;
            }
            if(fields.size()<7||fields[0]!=static_cast<std::int64_t>(result.calls.size())||
               fields[6]<0||fields[6]>64||fields.size()!=7+static_cast<std::size_t>(fields[6]))return {};
            for(std::size_t i=1;i<fields.size();++i)
                if(fields[i]<std::numeric_limits<std::int32_t>::min()||
                   fields[i]>std::numeric_limits<std::int32_t>::max())return {};
            ReductionObservation observation;
            observation.argument=static_cast<std::int32_t>(fields[1]);
            observation.state_before=static_cast<std::int32_t>(fields[2]);
            observation.state_after=static_cast<std::int32_t>(fields[3]);
            observation.result=static_cast<std::int32_t>(fields[4]);
            observation.delivered=static_cast<std::int32_t>(fields[5]);
            for(std::size_t i=7;i<fields.size();++i)observation.values.push_back(static_cast<std::int32_t>(fields[i]));
            result.calls.push_back(std::move(observation));
        }
        position=end+1;
    }
    return result;
}
inline ReductionVerdict reduction_verify(const ReductionSpec& spec,
        std::span<const ReductionRun> runs,std::string_view program_frame) {
    const auto plan=reduction_plan(spec);
    if(!plan)return {false,"El contrato de reproducción no es válido."};
    if(program_frame.empty()||runs.size()!=plan->size())
        return {false,"Faltan ejecuciones independientes de la reproducción."};
    for(std::size_t i=0;i<runs.size();++i) {
        const auto& run=runs[i];
        if(run.program_frame!=program_frame||!run.compiled||!run.executed||!run.completed_normally||
           run.phase!=(*plan)[i])
            return {false,"Una ejecución no terminó normalmente o pertenece a otra revisión."};
        if(run.calls.size()!=spec.calls.size())
            return {false,"La reproducción no ejecutó exactamente la secuencia de llamadas solicitada."};
        auto state=spec.initial_state;
        for(std::size_t n=0;n<spec.calls.size();++n) {
            const auto& expected=spec.calls[n];const auto& actual=run.calls[n];
            const auto delivered=n==spec.observed_call&&run.phase.replacement?
                *run.phase.replacement:expected.result;
            if(actual.values!=expected.values||actual.argument!=expected.argument||
               actual.state_before!=state||actual.state_after!=expected.state_after||
               actual.result!=expected.result||actual.delivered!=delivered)
                return {false,"Los datos, el orden, el contador o el resultado calculado no conservan el fallo investigado."};
            state=expected.state_after;
        }
        if(i==0&&run.exit_code!=spec.exit_code)
            return {false,"La reproducción publicada debe terminar normalmente con código cero."};
    }
    const auto original=reduction_detail::observable_numbers(runs.front());
    if(!original)return {false,"La salida numérica es excesiva para comprobar la observación."};
    const auto observed=spec.calls[spec.observed_call].result;
    const auto copies=static_cast<std::size_t>(std::ranges::count(*original,observed));
    if(!copies)return {false,"La salida no hace visible el resultado entero de la llamada observada."};
    for(std::size_t count=1;count<=copies;++count) {
        bool correct=true;
        for(std::size_t i=1;i<runs.size();++i) {
            auto expected=*original;
            for(std::size_t n=0;n<count;++n)expected.erase(std::ranges::find(expected,observed));
            expected.insert(expected.end(),count,*runs[i].phase.replacement);
            std::ranges::sort(expected);
            const auto actual=reduction_detail::observable_numbers(runs[i]);
            if(!actual||*actual!=expected){correct=false;break;}
        }
        if(correct)return {true,"La reproducción conserva la secuencia y muestra su resultado calculado."};
    }
    return {false,"La salida no sigue el valor calculado por la llamada observada; una cifra fija o un aviso diferente no bastan."};
}
} // namespace lgaed::curriculum
