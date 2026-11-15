#pragma once

#include "core/boolean_battery_check.hpp"
#include <array>
#include <limits>
#include <set>
#include <stdexcept>

namespace lgaed::curriculum {
inline constexpr std::string_view integer_battery_objective_version =
    "open-cmake-int-unary-battery-independent-mutations-v1";
struct IntegerBatteryCase {
    std::int32_t input=0,expected=0;
    bool operator==(const IntegerBatteryCase&) const = default;
};
struct IntegerBatteryClientCall {
    std::int32_t input=0,result=0;
    bool operator==(const IntegerBatteryClientCall&) const = default;
};
struct IntegerBatteryPrerequisite {
    std::string target;
    std::vector<IntegerBatteryClientCall> calls;
    std::string standard_input,standard_output,standard_error;
    std::uint32_t exit_code=0;
    bool operator==(const IntegerBatteryPrerequisite&) const = default;
};
enum class IntegerBatteryPublishedResult { success, failure };
struct IntegerBatterySpec {
    std::string library_path,test_path,function_name,test_target,protected_definition;
    std::vector<IntegerBatteryCase> cases;
    IntegerBatteryPublishedResult published_result=IntegerBatteryPublishedResult::success;
    std::optional<std::uint32_t> failure_exit_code{1};
    std::optional<IntegerBatteryPrerequisite> prerequisite;
    bool operator==(const IntegerBatterySpec&) const = default;
};
enum class IntegerBatteryMutation { control, mutate, published };
struct IntegerBatteryPhase {
    IntegerBatteryMutation mutation=IntegerBatteryMutation::control;
    std::optional<std::size_t> target;
    bool operator==(const IntegerBatteryPhase&) const = default;
};
struct IntegerBatteryObservation {
    std::int32_t input=0,result=0;
    bool operator==(const IntegerBatteryObservation&) const = default;
};
struct IntegerBatteryRun {
    std::string program_frame;
    bool compiled=false,executed=false,completed_normally=false;
    std::uint32_t exit_code=0;
    IntegerBatteryPhase phase;
    std::vector<IntegerBatteryObservation> calls;
    std::string stdout_bytes,stderr_bytes;
};
struct IntegerBatteryTrace {
    std::vector<IntegerBatteryObservation> calls;
    bool completed_normally=false;
    std::string visible;
};
struct IntegerBatteryVerdict {
    bool accepted=false;
    std::string reason;
};

namespace integer_battery_detail {
using Token=boolean_battery_detail::Token;
using Tokens=std::vector<Token>;
inline bool identifier(std::string_view name) {
    return boolean_battery_detail::identifier(name)&&
        !name.starts_with("lgaed_integer_battery_")&&
        !name.starts_with("lgaed_boolean_battery_");
}
inline std::optional<Tokens> tokens(std::string_view source) {
    auto result=boolean_battery_detail::tokens(source);
    if(!result)return {};
    for(const auto& token:*result)
        if(token.value.starts_with("id:lgaed_integer_battery_"))return {};
    return result;
}
inline bool open(std::string_view token) {return token=="op:{"||token=="op:<%";}
inline bool close(std::string_view token) {return token=="op:}"||token=="op:%>";}
struct Definition {
    std::size_t begin=0,opening_end=0,closing_begin=0,end=0;
    std::string parameter;
    std::vector<std::string> signature,contents;
};
inline std::optional<Definition> definition(std::string_view source,std::string_view function) {
    if(!identifier(function)||function=="main")return {};
    const auto lexical=tokens(source);if(!lexical)return {};
    const auto& t=*lexical;
    std::optional<Definition> found;
    std::vector<std::vector<std::string>> declarations;
    std::size_t depth=0;
    for(std::size_t i=0;i<t.size();++i) {
        const auto& value=t[i].value;
        if(value.starts_with("directive:")&&depth)return {};
        const bool named=value=="id:"+std::string(function)&&
            i+1<t.size()&&t[i+1].value=="op:(";
        bool body=false;
        if(named) {
            std::size_t end=i+2,nesting=1;
            for(;end<t.size()&&nesting;++end) {
                if(t[end].value=="op:(")++nesting;
                else if(t[end].value=="op:)")--nesting;
                if(nesting>4096)return {};
            }
            if(nesting)return {};
            body=end<t.size()&&open(t[end].value);
        }
        const bool declaration=i&&t[i-1].value.starts_with("id:")&&
            t[i-1].value!="id:return"&&t[i-1].value!="id:co_return"&&t[i-1].value!="id:throw";
        const bool reference=!depth&&i>1&&t[i-2].value=="id:int"&&
            (t[i-1].value=="op:&"||t[i-1].value=="op:&&"||t[i-1].value=="op:*");
        if(named&&(body||declaration||reference)) {
            if(!i||depth||t[i-1].value!="id:int"||i+5>=t.size())return {};
            if(i>1&&t[i-2].value!="op:;"&&!close(t[i-2].value)&&
               !t[i-2].value.starts_with("directive:"))return {};
            if(t[i+2].value!="id:int"||!t[i+3].value.starts_with("id:")||
               t[i+4].value!="op:)")return {};
            const auto parameter=t[i+3].value.substr(3);
            if(!identifier(parameter))return {};
            std::vector<std::string> signature;
            for(auto n=i-1;n<=i+4;++n)signature.push_back(t[n].value);
            if(t[i+5].value=="op:;")declarations.push_back(signature);
            else {
                if(t[i+5].value!="op:{"||found)return {};
                std::size_t end=i+6,nesting=1;
                for(;end<t.size()&&nesting;++end) {
                    if(t[end].value.starts_with("directive:")||
                       t[end].value=="id:__func__"||t[end].value=="id:__FUNCTION__"||
                       t[end].value=="id:__FUNCSIG__")return {};
                    if(open(t[end].value))++nesting;
                    else if(close(t[end].value))--nesting;
                    if(nesting>4096)return {};
                }
                if(nesting)return {};
                --end;
                Definition item{t[i-1].begin,t[i+5].end,t[end].begin,t[end].end,
                    parameter,signature,{}};
                for(auto n=i-1;n<=end;++n)item.contents.push_back(t[n].value);
                found=std::move(item);
            }
        }
        if(open(value)){if(++depth>4096)return {};}
        else if(close(value)){if(!depth)return {};--depth;}
    }
    if(depth||!found)return {};
    for(const auto& declaration:declarations)if(declaration!=found->signature)return {};
    return found;
}
inline bool path(std::string_view value) {
    if(value.empty()||value.size()>256||value.front()=='/'||value.front()=='.'||
       value.find("..")!=value.npos||!value.ends_with(".cpp"))return false;
    return std::ranges::all_of(value,[](unsigned char c) {
        return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
            c=='_'||c=='-'||c=='.'||c=='/';
    });
}
inline bool stream(std::string_view value) {
    return value.size()<=4*1024*1024&&value.find('\0')==value.npos;
}
inline std::int32_t changed(std::int32_t value) {
    return value==std::numeric_limits<std::int32_t>::min()?value+1:value-1;
}
inline std::size_t line(std::string_view source,std::size_t end) {
    return 1+static_cast<std::size_t>(std::ranges::count(source.substr(0,end),'\n'));
}
inline std::optional<std::int32_t> decimal(std::string_view text) {
    std::int32_t value=0;
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{}||end!=text.data()+text.size())return {};
    return value;
}
} // namespace integer_battery_detail

inline std::optional<std::string> integer_battery_extract_definition(
        std::string_view source,std::string_view function) {
    const auto found=integer_battery_detail::definition(source,function);
    if(!found)return {};
    return std::string(source.substr(found->begin,found->end-found->begin));
}
inline bool integer_battery_spec_valid(const IntegerBatterySpec& spec) {
    using namespace integer_battery_detail;
    if(!path(spec.library_path)||!path(spec.test_path)||spec.library_path==spec.test_path||
       !identifier(spec.test_target)||spec.cases.empty()||spec.cases.size()>32||
       (spec.failure_exit_code&&*spec.failure_exit_code==0)||
       (spec.published_result!=IntegerBatteryPublishedResult::success&&
        spec.published_result!=IntegerBatteryPublishedResult::failure))return false;
    const auto found=definition(spec.protected_definition,spec.function_name);
    const auto lexical=tokens(spec.protected_definition);
    if(!found||!lexical||lexical->empty()||
       found->begin!=lexical->front().begin||found->end!=lexical->back().end)return false;
    for(std::size_t i=0;i<spec.cases.size();++i)
        for(std::size_t j=0;j<i;++j)
            if(spec.cases[i].input==spec.cases[j].input)return false;
    if(spec.prerequisite) {
        const auto& p=*spec.prerequisite;
        if(!identifier(p.target)||p.target==spec.test_target||p.calls.empty()||
           p.calls.size()>32||p.exit_code!=0||!stream(p.standard_input)||
           !stream(p.standard_output)||!stream(p.standard_error))return false;
    }
    return true;
}
inline bool integer_battery_preserves_function(std::string_view source,const IntegerBatterySpec& spec) {
    if(!integer_battery_spec_valid(spec))return false;
    const auto before=integer_battery_detail::definition(spec.protected_definition,spec.function_name);
    const auto after=integer_battery_detail::definition(source,spec.function_name);
    return before&&after&&before->contents==after->contents;
}
// The editable driver cannot name the private implementation or hide code
// behind preprocessing. Protected project headers need no such restriction.
inline bool integer_battery_driver_valid(std::string_view source) {
    return integer_battery_detail::tokens(source).has_value();
}
inline bool integer_battery_driver_has_main(std::string_view source) {
    const auto lexical=integer_battery_detail::tokens(source);if(!lexical)return false;
    const auto& t=*lexical;std::size_t depth=0,definitions=0;
    for(std::size_t i=0;i<t.size();++i) {
        if(!depth&&t[i].value=="id:main"&&i+1<t.size()&&t[i+1].value=="op:(") {
            std::size_t end=i+2,parentheses=1;
            for(;end<t.size()&&parentheses;++end) {
                if(t[end].value=="op:(")++parentheses;
                else if(t[end].value=="op:)")--parentheses;
                if(parentheses>4096)return false;
            }
            if(parentheses)return false;
            for(;end<t.size()&&t[end].value!="op:;"&&!integer_battery_detail::open(t[end].value);++end) {}
            if(end<t.size()&&integer_battery_detail::open(t[end].value))++definitions;
        }
        if(integer_battery_detail::open(t[i].value)){if(++depth>4096)return false;}
        else if(integer_battery_detail::close(t[i].value)){if(!depth)return false;--depth;}
    }
    return !depth&&definitions==1;
}
inline std::optional<std::string> integer_battery_instrument_driver(
        std::string_view source,const IntegerBatterySpec& spec) {
    if(!integer_battery_spec_valid(spec)||!integer_battery_driver_has_main(source))return {};
    return std::string(R"cpp(#include <cstdio>
#include <iostream>
extern "C" void lgaed_integer_battery_anchor();
namespace lgaed_integer_battery_private {
struct Completion {
    Completion() { lgaed_integer_battery_anchor(); }
    ~Completion() {
        std::fwrite("LGAED_INTEGER_BATTERY_END\n",1,sizeof("LGAED_INTEGER_BATTERY_END\n")-1,stderr);
    }
} completion;
}
#line 1
)cpp")+std::string(source);
}
inline std::string integer_battery_spec_frame(const IntegerBatterySpec& spec) {
    if(!integer_battery_spec_valid(spec))throw std::invalid_argument("Invalid integer battery specification");
    std::string result;
    const auto field=[&](std::string_view text) {
        result+=std::to_string(text.size());result+=':';result+=text;
    };
    field(integer_battery_objective_version);field(spec.library_path);field(spec.test_path);
    field(spec.function_name);field(spec.test_target);
    const auto function=integer_battery_detail::definition(spec.protected_definition,spec.function_name);
    field(std::to_string(function->contents.size()));
    for(const auto& token:function->contents)field(token);
    field(spec.published_result==IntegerBatteryPublishedResult::success?"success":"failure");
    field(spec.failure_exit_code?std::to_string(*spec.failure_exit_code):"nonzero");
    field(std::to_string(spec.cases.size()));
    for(const auto& c:spec.cases){field(std::to_string(c.input));field(std::to_string(c.expected));}
    field(spec.prerequisite?"client":"none");
    if(spec.prerequisite) {
        const auto& p=*spec.prerequisite;field(p.target);field(std::to_string(p.calls.size()));
        for(const auto& c:p.calls){field(std::to_string(c.input));field(std::to_string(c.result));}
        field(p.standard_input);field(p.standard_output);field(p.standard_error);field(std::to_string(p.exit_code));
    }
    return result;
}
inline std::optional<std::vector<IntegerBatteryPhase>> integer_battery_plan(const IntegerBatterySpec& spec) {
    if(!integer_battery_spec_valid(spec))return {};
    std::vector<IntegerBatteryPhase> result{{IntegerBatteryMutation::control,{}}};
    for(std::size_t i=0;i<spec.cases.size();++i)result.push_back({IntegerBatteryMutation::mutate,i});
    result.push_back({IntegerBatteryMutation::published,{}});return result;
}
inline std::optional<std::string> integer_battery_instrument(std::string_view source,
        const IntegerBatterySpec& spec,const IntegerBatteryPhase& phase) {
    if(!integer_battery_preserves_function(source,spec))return {};
    const auto plan=integer_battery_plan(spec);
    if(!plan||std::ranges::find(*plan,phase)==plan->end())return {};
    const auto function=integer_battery_detail::definition(source,spec.function_name);
    if(!function)return {};
    std::string output=R"cpp(#include <charconv>
#include <cstdio>
#include <iostream>
namespace lgaed_integer_battery_private {
unsigned count=0;
volatile unsigned anchor_value=0;
void literal(const char* text, std::size_t size) { std::fwrite(text,1,size,stderr); }
void number(int value) {
    char text[32];
    const auto converted=std::to_chars(text,text+sizeof(text),value);
    if(converted.ec==std::errc{})std::fwrite(text,1,static_cast<unsigned>(converted.ptr-text),stderr);
}
void call(int input,int result) {
    literal("LGAED_INTEGER_BATTERY_CALL:",sizeof("LGAED_INTEGER_BATTERY_CALL:")-1);
    number(static_cast<int>(count++));literal(",",1);
    number(input);literal(",",1);number(result);literal("\n",1);
}
}
extern "C" void lgaed_integer_battery_anchor() { lgaed_integer_battery_private::anchor_value=1; }
#line 1
)cpp";
    output+=source.substr(0,function->opening_end);
    output+="\n    const int lgaed_integer_battery_input="+function->parameter+";\n";
    output+="    const int lgaed_integer_battery_original=[&]()->int {\n#line "+
        std::to_string(integer_battery_detail::line(source,function->opening_end))+"\n";
    output+=source.substr(function->opening_end,function->closing_begin-function->opening_end);
    output+="\n    }();\n    int lgaed_integer_battery_result=lgaed_integer_battery_original;\n";
    if(phase.mutation!=IntegerBatteryMutation::published) {
        for(std::size_t i=0;i<spec.cases.size();++i) {
            const auto& c=spec.cases[i];
            const auto delivered=phase.mutation==IntegerBatteryMutation::mutate&&phase.target==i
                ?integer_battery_detail::changed(c.expected):c.expected;
            output+="    if(lgaed_integer_battery_input=="+std::to_string(c.input)+
                ") lgaed_integer_battery_result="+std::to_string(delivered)+";\n";
        }
    }
    output+="    lgaed_integer_battery_private::call(lgaed_integer_battery_input,lgaed_integer_battery_result);\n"
        "    return lgaed_integer_battery_result;\n#line "+
        std::to_string(integer_battery_detail::line(source,function->closing_begin))+"\n";
    output+=source.substr(function->closing_begin);return output;
}
inline std::optional<IntegerBatteryTrace> integer_battery_parse_trace(std::string_view stderr_bytes) {
    if(stderr_bytes.size()>4*1024*1024)return {};
    constexpr std::string_view prefix="LGAED_INTEGER_BATTERY_";
    constexpr std::string_view call="LGAED_INTEGER_BATTERY_CALL:";
    constexpr std::string_view end="LGAED_INTEGER_BATTERY_END";
    IntegerBatteryTrace result;
    for(std::size_t position=0;position<stderr_bytes.size();) {
        const auto begin=stderr_bytes.find(prefix,position);
        if(begin==stderr_bytes.npos){result.visible+=stderr_bytes.substr(position);break;}
        result.visible+=stderr_bytes.substr(position,begin-position);
        const auto newline=stderr_bytes.find('\n',begin);
        if(newline==stderr_bytes.npos)return {};
        auto record=stderr_bytes.substr(begin,newline-begin);
        if(record.ends_with('\r'))record.remove_suffix(1);
        if(record==end) {
            if(result.completed_normally)return {};
            result.completed_normally=true;
        }else {
            if(result.completed_normally||!record.starts_with(call)||result.calls.size()>=32)return {};
            record.remove_prefix(call.size());
            std::array<std::int32_t,3> values{};
            for(std::size_t i=0;i<values.size();++i) {
                const auto comma=record.find(',');
                const auto number=integer_battery_detail::decimal(record.substr(0,comma));
                if(!number||(i+1==values.size())!=(comma==record.npos))return {};
                values[i]=*number;
                if(comma!=record.npos)record.remove_prefix(comma+1);
            }
            if(values[0]!=static_cast<std::int32_t>(result.calls.size()))return {};
            result.calls.push_back({values[1],values[2]});
        }
        position=newline+1;
    }
    return result;
}

namespace integer_battery_detail {
struct VisibleInteger {std::size_t begin=0,end=0;std::int32_t value=0;};
struct Record {std::string text;std::vector<VisibleInteger> numbers;};
inline std::optional<Record> record(std::string text) {
    if(text.size()>65536)return {};
    Record result{std::move(text),{}};
    const std::string_view source=result.text;
    const auto part=[](unsigned char c) {
        return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c>=128;
    };
    for(std::size_t i=0;i<source.size();) {
        const auto begin=i;
        if((source[i]=='+'||source[i]=='-')&&i+1<source.size()&&source[i+1]>='0'&&source[i+1]<='9')++i;
        else if(source[i]<'0'||source[i]>'9'){++i;continue;}
        while(i<source.size()&&source[i]>='0'&&source[i]<='9')++i;
        if((begin&&(part(static_cast<unsigned char>(source[begin-1]))||source[begin-1]=='.'))||
           (i<source.size()&&(part(static_cast<unsigned char>(source[i]))||
             (source[i]=='.'&&i+1<source.size()&&source[i+1]>='0'&&source[i+1]<='9'))))continue;
        auto number=source.substr(begin,i-begin);
        if(number.starts_with('+'))number.remove_prefix(1);
        const auto value=decimal(number);if(!value)continue;
        result.numbers.push_back({begin,i,*value});
        if(result.numbers.size()>64)return {};
    }
    return result;
}
inline std::optional<std::vector<Record>> records(const IntegerBatteryRun& run,bool groups) {
    std::vector<Record> result;
    const auto append=[&](std::string text)->bool {
        if(std::ranges::all_of(text,code_format::space))return true;
        const auto parsed=record(std::move(text));
        if(!parsed||result.size()>=1024)return false;
        result.push_back(*parsed);return true;
    };
    for(const auto source:{std::string_view(run.stdout_bytes),std::string_view(run.stderr_bytes)}) {
        std::string group;
        for(std::size_t position=0;position<source.size();) {
            const auto newline=source.find('\n',position);
            const auto line=source.substr(position,newline==source.npos?source.size()-position:newline-position);
            position=newline==source.npos?source.size():newline+1;
            const bool blank=std::ranges::all_of(line,code_format::space);
            if(!groups) {if(!blank&&!append(std::string(line)))return {};}
            else if(blank) {if(!append(std::move(group)))return {};group.clear();}
            else {group+=line;group+='\n';}
        }
        if(groups&&!append(std::move(group)))return {};
    }
    return result;
}
inline bool contains(const Record& item,IntegerBatteryCase wanted,std::int32_t obtained) {
    std::array<std::int32_t,3> values{wanted.input,wanted.expected,obtained};
    std::vector<bool> consumed(item.numbers.size());
    for(const auto value:values) {
        bool found=false;
        for(std::size_t i=0;i<item.numbers.size();++i)
            if(!consumed[i]&&item.numbers[i].value==value){consumed[i]=true;found=true;break;}
        if(!found)return false;
    }
    return true;
}
enum class MatchStatus { absent,unique,ambiguous };
struct Match {MatchStatus status=MatchStatus::absent;std::vector<std::size_t> rows;};
inline Match match(std::span<const IntegerBatteryCase> cases,std::span<const std::int32_t> obtained,
        std::span<const Record> available) {
    std::vector<std::vector<std::size_t>> edges(cases.size());
    for(std::size_t i=0;i<cases.size();++i)
        for(std::size_t j=0;j<available.size();++j)
            if(contains(available[j],cases[i],obtained[i]))edges[i].push_back(j);
    const auto solve=[&](std::optional<std::pair<std::size_t,std::size_t>> forbidden)
            ->std::optional<std::vector<std::size_t>> {
        std::vector<std::optional<std::size_t>> owner(available.size());
        const auto augment=[&](auto&& self,std::size_t c,std::vector<bool>& seen)->bool {
            for(const auto row:edges[c]) {
                if(seen[row]||(forbidden&&*forbidden==std::pair{c,row}))continue;
                seen[row]=true;
                if(!owner[row]||self(self,*owner[row],seen)){owner[row]=c;return true;}
            }
            return false;
        };
        for(std::size_t c=0;c<cases.size();++c) {
            std::vector<bool> seen(available.size());
            if(!augment(augment,c,seen))return {};
        }
        std::vector<std::size_t> result(cases.size());
        for(std::size_t row=0;row<owner.size();++row)if(owner[row])result[*owner[row]]=row;
        return result;
    };
    const auto selected=solve({});
    if(!selected)return {};
    for(std::size_t c=0;c<cases.size();++c)
        if(solve(std::pair{c,(*selected)[c]}))return {MatchStatus::ambiguous,{}};
    return {MatchStatus::unique,*selected};
}
inline std::string remainder(const Record& record,const std::array<std::size_t,3>& fields) {
    std::vector<std::string> parts;
    const auto append=[&](std::string_view text) {
        const auto word=[](unsigned char c) {
            return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c>=128;
        };
        for(std::size_t i=0;i<text.size();) {
            if(code_format::space(text[i])){++i;continue;}
            const auto begin=i++;
            if(word(static_cast<unsigned char>(text[begin]))) {
                while(i<text.size()&&word(static_cast<unsigned char>(text[i])))++i;
                auto token=std::string(text.substr(begin,i-begin));
                for(auto& c:token)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
                parts.push_back("text:"+token);
            }else parts.push_back("symbol:"+std::string(text.substr(begin,1)));
        }
    };
    std::size_t position=0;
    for(std::size_t i=0;i<record.numbers.size();++i) {
        const auto& number=record.numbers[i];
        append(std::string_view(record.text).substr(position,number.begin-position));
        if(std::ranges::find(fields,i)!=fields.end())parts.push_back("field");
        else parts.push_back("number:"+std::to_string(number.value));
        position=number.end;
    }
    append(std::string_view(record.text).substr(position));
    std::ranges::sort(parts);std::string result;
    for(const auto& part:parts)result+=std::to_string(part.size())+":"+part;
    return result;
}
inline std::optional<std::set<std::string>> verdicts(
        const Record& record,IntegerBatteryCase wanted,std::int32_t obtained) {
    std::set<std::string> result;std::size_t combinations=0;
    for(std::size_t input=0;input<record.numbers.size();++input) {
        if(record.numbers[input].value!=wanted.input)continue;
        for(std::size_t expected=0;expected<record.numbers.size();++expected) {
            if(expected==input||record.numbers[expected].value!=wanted.expected)continue;
            for(std::size_t actual=0;actual<record.numbers.size();++actual) {
                if(actual==input||actual==expected||record.numbers[actual].value!=obtained)continue;
                if(++combinations>4096)return {};
                result.insert(remainder(record,{input,expected,actual}));
            }
        }
    }
    if(result.empty())return {};
    return result;
}
// Fields can change their order and labels between success and failure.
// Removing the observed triple must leave a changed verdict in a mismatch,
// with no unchanged interpretation available among repeated numeric fields.
inline bool truthful_record(IntegerBatteryCase wanted,std::span<const Record* const> rows,
        std::span<const std::int32_t> obtained) {
    if(rows.empty()||rows.size()!=obtained.size())return false;
    const auto control=verdicts(*rows.front(),wanted,obtained.front());
    if(!control)return false;
    const auto compact=[](std::string_view text) {
        std::string result;for(const auto c:text)if(!code_format::space(c))result+=c;return result;
    };
    const auto complete=compact(rows.front()->text);
    for(std::size_t r=1;r<rows.size();++r) {
        const auto current=verdicts(*rows[r],wanted,obtained[r]);if(!current)return false;
        const bool unchanged=std::ranges::any_of(*current,[&](const auto& item){return control->contains(item);});
        const bool mismatch=obtained[r]!=wanted.expected;
        if(unchanged==mismatch)return false;
        if(mismatch&&compact(rows[r]->text)==complete)return false;
    }
    return true;
}
inline IntegerBatteryVerdict diagnostics(const IntegerBatterySpec& spec,
        std::span<const IntegerBatteryRun> runs,const std::vector<std::vector<std::int32_t>>& obtained) {
    bool groups=false;
    auto control=records(runs.front(),false);
    if(!control)return {false,"El informe excede los límites de registros y enteros visibles."};
    auto selected=match(spec.cases,obtained.front(),*control);
    if(selected.status==MatchStatus::ambiguous)
        return {false,"El informe permite atribuir el mismo diagnóstico a casos distintos."};
    if(selected.status==MatchStatus::absent) {
        groups=true;control=records(runs.front(),true);
        if(!control)return {false,"El informe por grupos no se pudo delimitar."};
        selected=match(spec.cases,obtained.front(),*control);
    }
    if(selected.status!=MatchStatus::unique)
        return {false,"Cada caso necesita su propio registro con dato, esperado y obtenido."};
    std::vector<std::vector<Record>> all;all.reserve(runs.size());all.push_back(std::move(*control));
    std::vector<std::vector<std::size_t>> rows;rows.push_back(std::move(selected.rows));
    for(std::size_t r=1;r<runs.size();++r) {
        auto current=records(runs[r],groups);
        if(!current)return {false,"No se conservó el formato acotado de los diagnósticos."};
        const auto matched=match(spec.cases,obtained[r],*current);
        if(matched.status!=MatchStatus::unique)
            return {false,"Una ejecución no identifica fielmente todos los resultados por caso."};
        all.push_back(std::move(*current));rows.push_back(matched.rows);
    }
    for(std::size_t c=0;c<spec.cases.size();++c) {
        std::vector<const Record*> compared;std::vector<std::int32_t> results;
        for(std::size_t r=0;r<runs.size();++r) {
            compared.push_back(&all[r][rows[r][c]]);results.push_back(obtained[r][c]);
        }
        if(!truthful_record(spec.cases[c],compared,results))
            return {false,"El registro no cambia su obtenido y su veredicto según la comparación real."};
    }
    return {true,"Cada caso comunica sus datos, el resultado obtenido y el veredicto observado."};
}
} // namespace integer_battery_detail

inline IntegerBatteryVerdict integer_battery_verify(const IntegerBatterySpec& spec,
        std::span<const IntegerBatteryRun> runs,std::string_view program_frame) {
    const auto plan=integer_battery_plan(spec);
    if(!plan||program_frame.empty()||runs.size()!=plan->size())
        return {false,"Faltan el control correcto, las alteraciones independientes o la ejecución publicada."};
    std::vector<std::vector<std::int32_t>> obtained;
    for(std::size_t r=0;r<runs.size();++r) {
        const auto& run=runs[r];
        if(run.program_frame!=program_frame||run.phase!=(*plan)[r]||
           !run.compiled||!run.executed||!run.completed_normally)
            return {false,"La evidencia pertenece a otra revisión, fase o ejecución sin terminación normal."};
        if(!integer_battery_detail::stream(run.stdout_bytes)||!integer_battery_detail::stream(run.stderr_bytes))
            return {false,"El informe contiene bytes no admitidos o excede su límite."};
        if(run.calls.size()!=spec.cases.size())
            return {false,"Deben ejecutarse todos los casos exactamente una vez, también tras una discrepancia."};
        std::vector<std::optional<std::int32_t>> observed(spec.cases.size());
        for(const auto& call:run.calls) {
            const auto found=std::ranges::find(spec.cases,call.input,&IntegerBatteryCase::input);
            if(found==spec.cases.end())return {false,"Se observó una llamada ajena a los casos declarados."};
            const auto index=static_cast<std::size_t>(found-spec.cases.begin());
            if(observed[index])return {false,"Una llamada repetida no acredita otro caso ni un esperado independiente."};
            observed[index]=call.result;
            if(run.phase.mutation!=IntegerBatteryMutation::published) {
                const auto expected=run.phase.target==index?
                    integer_battery_detail::changed(found->expected):found->expected;
                if(call.result!=expected)return {false,"La traza no contiene el resultado de su control independiente."};
            }
        }
        std::vector<std::int32_t> values;std::size_t failures=0;
        for(std::size_t i=0;i<observed.size();++i) {
            if(!observed[i])return {false,"Falta una llamada requerida."};
            values.push_back(*observed[i]);if(*observed[i]!=spec.cases[i].expected)++failures;
        }
        const bool expected_exit=failures
            ?(run.exit_code!=0&&(!spec.failure_exit_code||run.exit_code==*spec.failure_exit_code))
            :run.exit_code==0;
        if(!expected_exit)
            return {false,"El código final no corresponde al resultado de las comparaciones y su política declarada."};
        if(run.phase.mutation==IntegerBatteryMutation::published&&
           (failures==0)!=(spec.published_result==IntegerBatteryPublishedResult::success))
            return {false,"La biblioteca publicada no conserva el resultado declarado."};
        obtained.push_back(std::move(values));
    }
    return integer_battery_detail::diagnostics(spec,runs,obtained);
}
} // namespace lgaed::curriculum
