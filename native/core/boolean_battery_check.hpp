#pragma once

#include "core/code_format_compat.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

inline constexpr std::string_view boolean_battery_objective_version =
    "open-bool-pair-battery-independent-mutations-v1";
struct BooleanBatteryInput {
    std::int32_t first=0,second=0;
    bool operator==(const BooleanBatteryInput&) const = default;
};
struct BooleanBatteryCase {
    std::int32_t first=0,second=0;
    bool expected=false;
    bool operator==(const BooleanBatteryCase&) const = default;
};
enum class BooleanBatteryPublishedResult { success, failure };
struct BooleanBatterySpec {
    std::string function_name,protected_definition;
    std::vector<BooleanBatteryCase> cases;
    BooleanBatteryPublishedResult published_result=BooleanBatteryPublishedResult::success;
    bool operator==(const BooleanBatterySpec&) const = default;
};
enum class BooleanBatteryMutation { correct_control, invert_result, published };
struct BooleanBatteryPhase {
    BooleanBatteryMutation mutation=BooleanBatteryMutation::correct_control;
    std::optional<BooleanBatteryInput> target;
    bool operator==(const BooleanBatteryPhase&) const = default;
};
struct BooleanBatteryRun {
    std::string program_frame;
    bool compiled=false,executed=false,completed_normally=false;
    std::uint32_t exit_code=0;
    BooleanBatteryMutation mutation=BooleanBatteryMutation::correct_control;
    std::optional<BooleanBatteryInput> target;
    std::vector<BooleanBatteryInput> calls;
    std::string stdout_bytes,stderr_bytes;
};
struct BooleanBatteryTrace {
    std::vector<BooleanBatteryInput> calls;
    bool completed_normally=false;
    std::string visible;
};
struct BooleanBatteryVerdict {
    bool accepted=false;
    std::string reason;
};

namespace boolean_battery_detail {
inline bool identifier(std::string_view value) {
    if(value.empty()||value.size()>128)return false;
    const auto letter=[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
    };
    if(!letter(static_cast<unsigned char>(value.front())))return false;
    return std::ranges::all_of(value,[&](unsigned char c) {
        return letter(c)||(c>='0'&&c<='9');
    });
}
struct Token {
    std::string value;
    std::size_t begin=0,end=0;
};
inline std::optional<std::vector<Token>> tokens(std::string_view source) {
    if(source.empty()||source.size()>4*1024*1024||source.find('\0')!=source.npos||
       code_format::physical_splice(source))return {};
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned)return {};
    std::vector<Token> result;
    std::size_t offset=0;
    for(const auto& token:*scanned) {
        const auto colon=token.find(':');
        if(colon==token.npos)return {};
        const auto literal=std::string_view(token).substr(colon+1);
        const auto begin=source.find(literal,offset);
        if(begin==source.npos||!std::ranges::all_of(source.substr(offset,begin-offset),code_format::space))return {};
        offset=begin+literal.size();
        if(token.starts_with("comment:"))continue;
        if(token=="op:#"||token=="op:##"||token=="op:%:"||token=="op:%:%:")return {};
        if(token.starts_with("id:lgaed_boolean_battery_"))return {};
        if(token.starts_with("directive:")) {
            auto directive=literal;
            directive.remove_prefix(directive.starts_with("%:")?2:1);
            while(!directive.empty()&&code_format::space(directive.front()))directive.remove_prefix(1);
            if(!directive.starts_with("include"))return {};
            directive.remove_prefix(7);
            while(!directive.empty()&&code_format::space(directive.front()))directive.remove_prefix(1);
            if(directive.empty()||(directive.front()!='<'&&directive.front()!='"'))return {};
        }
        result.push_back({token,begin,offset});
    }
    return result;
}
struct Definition {
    std::size_t begin=0,opening_end=0,closing_begin=0,end=0;
    std::string first,second;
    std::vector<std::string> signature,contents;
};
inline std::optional<Definition> definition(std::string_view source,std::string_view function) {
    if(!identifier(function)||function.starts_with("lgaed_boolean_battery_"))return {};
    const auto lexical=tokens(source);
    if(!lexical)return {};
    const auto& t=*lexical;
    std::optional<Definition> found;
    std::vector<std::vector<std::string>> declarations;
    std::size_t depth=0;
    for(std::size_t i=0;i<t.size();++i) {
        const auto& value=t[i].value;
        if(value.starts_with("directive:")&&depth!=0)return {};
        const bool named_call=value=="id:"+std::string(function)&&
            i+1<t.size()&&t[i+1].value=="op:(";
        bool definition_after_parameters=false;
        if(named_call) {
            std::size_t close=i+2,parentheses=1;
            for(;close<t.size()&&parentheses;++close) {
                if(t[close].value=="op:(")++parentheses;
                else if(t[close].value=="op:)")--parentheses;
                if(parentheses>4096)return {};
            }
            if(parentheses)return {};
            definition_after_parameters=close<t.size()&&
                (t[close].value=="op:{"||t[close].value=="op:<%");
        }
        const bool id_declaration=i>0&&t[i-1].value.starts_with("id:")&&
            t[i-1].value!="id:return"&&t[i-1].value!="id:co_return"&&t[i-1].value!="id:throw";
        const bool reference_declaration=depth==0&&i>1&&t[i-2].value=="id:bool"&&
            (t[i-1].value=="op:&"||t[i-1].value=="op:*"||t[i-1].value=="op:&&");
        if(named_call&&(definition_after_parameters||id_declaration||reference_declaration)) {
            if(i==0||depth!=0||t[i-1].value!="id:bool"||i+8>=t.size())return {};
            if(i>1&&t[i-2].value!="op:;"&&t[i-2].value!="op:}"&&
               !t[i-2].value.starts_with("directive:"))return {};
            if(t[i+2].value!="id:int"||!t[i+3].value.starts_with("id:")||
               t[i+4].value!="op:,"||t[i+5].value!="id:int"||
               !t[i+6].value.starts_with("id:")||t[i+7].value!="op:)")return {};
            const auto first=t[i+3].value.substr(3),second=t[i+6].value.substr(3);
            if(!identifier(first)||!identifier(second)||first==second)return {};
            std::vector<std::string> signature;
            for(auto n=i-1;n<=i+7;++n)signature.push_back(t[n].value);
            if(t[i+8].value=="op:;")declarations.push_back(signature);
            else {
                if(t[i+8].value!="op:{"||found)return {};
                std::size_t close=i+9,nesting=1;
                for(;close<t.size()&&nesting;++close) {
                    if(t[close].value.starts_with("directive:"))return {};
                    if(t[close].value=="op:{"||t[close].value=="op:<%")++nesting;
                    else if(t[close].value=="op:}"||t[close].value=="op:%>")--nesting;
                    if(nesting>4096)return {};
                }
                if(nesting)return {};
                --close;
                Definition item{t[i-1].begin,t[i+8].end,t[close].begin,t[close].end,
                    first,second,signature,{}};
                for(auto n=i-1;n<=close;++n)item.contents.push_back(t[n].value);
                found=std::move(item);
            }
        }
        if(value=="op:{"||value=="op:<%") {
            if(++depth>4096)return {};
        }else if(value=="op:}"||value=="op:%>") {
            if(depth==0)return {};
            --depth;
        }
    }
    if(depth||!found)return {};
    for(const auto& declaration:declarations)
        if(declaration!=found->signature)return {};
    return found;
}
inline bool same_calls(std::span<const BooleanBatteryCase> cases,
        std::vector<BooleanBatteryInput> calls) {
    if(calls.size()!=cases.size())return false;
    std::vector<BooleanBatteryInput> expected;
    for(const auto& c:cases)expected.push_back({c.first,c.second});
    const auto order=[](const auto& a,const auto& b) {
        return std::tie(a.first,a.second)<std::tie(b.first,b.second);
    };
    std::ranges::sort(expected,order);
    std::ranges::sort(calls,order);
    return expected==calls;
}
} // namespace boolean_battery_detail

inline std::optional<std::string> boolean_battery_extract_definition(
        std::string_view source,std::string_view function) {
    const auto found=boolean_battery_detail::definition(source,function);
    if(!found)return {};
    return std::string(source.substr(found->begin,found->end-found->begin));
}
inline bool boolean_battery_spec_valid(const BooleanBatterySpec& spec) {
    if(spec.cases.empty()||spec.cases.size()>32||
       (spec.published_result!=BooleanBatteryPublishedResult::success&&
        spec.published_result!=BooleanBatteryPublishedResult::failure))return false;
    const auto found=boolean_battery_detail::definition(spec.protected_definition,spec.function_name);
    const auto lexical=boolean_battery_detail::tokens(spec.protected_definition);
    if(!found||!lexical||lexical->empty()||found->begin!=lexical->front().begin||
       found->end!=lexical->back().end)return false;
    for(std::size_t i=0;i<spec.cases.size();++i)
        for(std::size_t j=0;j<i;++j)
            if(spec.cases[i].first==spec.cases[j].first&&
               spec.cases[i].second==spec.cases[j].second)return false;
    return true;
}
inline bool boolean_battery_preserves_function(
        std::string_view candidate,const BooleanBatterySpec& spec) {
    if(!boolean_battery_spec_valid(spec))return false;
    const auto before=boolean_battery_detail::definition(spec.protected_definition,spec.function_name);
    const auto after=boolean_battery_detail::definition(candidate,spec.function_name);
    return before&&after&&before->contents==after->contents;
}
inline std::optional<std::vector<BooleanBatteryPhase>> boolean_battery_mutation_plan(
        const BooleanBatterySpec& spec) {
    if(!boolean_battery_spec_valid(spec))return {};
    std::vector<BooleanBatteryPhase> plan{{BooleanBatteryMutation::correct_control,{}}};
    for(const auto& c:spec.cases)
        plan.push_back({BooleanBatteryMutation::invert_result,BooleanBatteryInput{c.first,c.second}});
    plan.push_back({BooleanBatteryMutation::published,{}});
    return plan;
}

inline std::optional<std::string> boolean_battery_instrument(
        std::string_view source,const BooleanBatterySpec& spec,const BooleanBatteryPhase& phase) {
    if(!boolean_battery_preserves_function(source,spec))return {};
    const auto plan=boolean_battery_mutation_plan(spec);
    if(!plan||std::ranges::find(*plan,phase)==plan->end())return {};
    const auto function=boolean_battery_detail::definition(source,spec.function_name);
    if(!function)return {};
    // Decimal records bypass the candidate's C++ stream flags, width,
    // locale and error bits, without changing them for its next operation.
    std::string result=R"cpp(#include <charconv>
#include <cstdio>
#include <iostream>
#include <limits>
namespace lgaed_boolean_battery_private {
void write(const char* bytes, std::size_t count) {
    std::fwrite(bytes, 1, count, stderr);
    std::fflush(stderr);
}
void call(int first, int second) {
    char bytes[sizeof("LGAED_BOOLEAN_BATTERY_CALL:") +
        2 * (std::numeric_limits<int>::digits10 + 3)] = "LGAED_BOOLEAN_BATTERY_CALL:";
    char* end = bytes + sizeof("LGAED_BOOLEAN_BATTERY_CALL:") - 1;
    const int values[]{first, second};
    for (unsigned i = 0; i < 2; ++i) {
        const auto number = std::to_chars(end, bytes + sizeof(bytes) - 1, values[i]);
        if (number.ec != std::errc{}) return;
        end = number.ptr;
        *end++ = i == 1 ? '\n' : ',';
    }
    write(bytes, static_cast<std::size_t>(end - bytes));
}
struct Completion {
    ~Completion() { write("LGAED_BOOLEAN_BATTERY_END\n", sizeof("LGAED_BOOLEAN_BATTERY_END\n") - 1); }
};
Completion completion;
}
#line 1
)cpp";
    result+=source.substr(0,function->opening_end);
    result+="\n    lgaed_boolean_battery_private::call("+function->first+", "+function->second+");\n";
    if(phase.mutation==BooleanBatteryMutation::published) {
        result+="#line "+std::to_string(1+std::ranges::count(source.substr(0,function->opening_end),'\n'))+"\n";
        result+=source.substr(function->opening_end,function->closing_begin-function->opening_end);
    }else {
        result+="    bool lgaed_boolean_battery_result = false;\n";
        for(const auto& c:spec.cases) {
            const bool invert=phase.target==BooleanBatteryInput{c.first,c.second};
            result+="    if ("+function->first+" == "+std::to_string(c.first)+" && "+
                function->second+" == "+std::to_string(c.second)+") lgaed_boolean_battery_result = "+
                ((c.expected!=invert)?"true":"false")+";\n";
        }
        result+="    return lgaed_boolean_battery_result;\n";
    }
    result+="}\n#line "+std::to_string(1+std::ranges::count(source.substr(0,function->end),'\n'))+"\n";
    result+=source.substr(function->end);
    return result;
}

inline std::optional<BooleanBatteryTrace> boolean_battery_parse_trace(std::string_view all) {
    constexpr std::string_view call="LGAED_BOOLEAN_BATTERY_CALL:",finish="LGAED_BOOLEAN_BATTERY_END";
    BooleanBatteryTrace result;
    for(std::size_t position=0;position<all.size();) {
        const auto next_call=all.find(call,position),next_finish=all.find(finish,position);
        const auto begin=std::min(next_call,next_finish);
        if(begin==all.npos) {result.visible.append(all.substr(position));break;}
        result.visible.append(all.substr(position,begin-position));
        const bool completion=begin==next_finish;
        const auto payload=begin+(completion?finish.size():call.size());
        const auto end=all.find('\n',payload);
        if(end==all.npos)return {};
        auto line=all.substr(payload,end-payload);
        if(line.ends_with('\r'))line.remove_suffix(1);
        if(completion) {
            if(result.completed_normally||!line.empty())return {};
            result.completed_normally=true;
        }else {
            if(result.completed_normally||result.calls.size()>=32)return {};
            const auto comma=line.find(',');
            if(comma==line.npos)return {};
            BooleanBatteryInput input;
            const auto first=line.substr(0,comma),second=line.substr(comma+1);
            const auto parse=[](std::string_view field,std::int32_t& value) {
                if(field.empty())return false;
                const auto [ptr,ec]=std::from_chars(field.data(),field.data()+field.size(),value);
                return ec==std::errc{}&&ptr==field.data()+field.size();
            };
            if(!parse(first,input.first)||!parse(second,input.second))return {};
            result.calls.push_back(input);
        }
        position=end+1;
    }
    return result;
}

inline BooleanBatteryVerdict boolean_battery_verify(const BooleanBatterySpec& spec,
        std::span<const BooleanBatteryRun> runs,std::string_view program_frame) {
    const auto plan=boolean_battery_mutation_plan(spec);
    if(!plan)return {false,"El contrato de la batería booleana no es válido."};
    if(program_frame.empty()||runs.size()!=plan->size())
        return {false,"Faltan ejecuciones independientes de la batería."};
    for(std::size_t i=0;i<runs.size();++i) {
        const auto& run=runs[i];
        if(run.program_frame!=program_frame||!run.compiled||!run.executed||!run.completed_normally)
            return {false,"Una ejecución no terminó normalmente o pertenece a otra revisión."};
        if(run.mutation!=(*plan)[i].mutation||run.target!=(*plan)[i].target)
            return {false,"Una mutación no corresponde al caso declarado."};
        if(!boolean_battery_detail::same_calls(spec.cases,run.calls))
            return {false,"Faltan llamadas o hay llamadas repetidas o ajenas a la batería."};
        const bool success=i==0||(i+1==runs.size()&&
            spec.published_result==BooleanBatteryPublishedResult::success);
        if((run.exit_code==0)!=success)
            return {false,"Las comparaciones no determinaron el resultado de la ejecución."};
        if(!success&&((run.stdout_bytes.empty()&&run.stderr_bytes.empty())||
           (run.stdout_bytes==runs.front().stdout_bytes&&run.stderr_bytes==runs.front().stderr_bytes)))
            return {false,"La batería no comunicó la discrepancia observada."};
        if(run.mutation==BooleanBatteryMutation::invert_result)
            for(std::size_t prior=1;prior<i;++prior)
                if(run.stdout_bytes==runs[prior].stdout_bytes&&
                   run.stderr_bytes==runs[prior].stderr_bytes)
                    return {false,"El diagnóstico no permite distinguir qué caso falló."};
    }
    return {true,"Todas las comparaciones detectaron sus defectos independientes."};
}
} // namespace lgaed::curriculum
