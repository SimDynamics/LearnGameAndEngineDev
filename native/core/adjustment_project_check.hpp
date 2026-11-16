#pragma once

#include "core/cmake_graph_check.hpp"
#include "core/code_format_compat.hpp"
#include "core/open_battery_check.hpp"
#include "core/rejection_diagnostic_facts.hpp"

#include <array>
#include <limits>
#include <locale>
#include <sstream>

namespace lgaed::curriculum {

// This is the construction contract, not the legacy one-new-case exercise.
// Original authored paths/header and all current project bytes are bound by
// the caller. No receipt for one part can be joined to another submission.
inline constexpr std::string_view adjustment_project_objective_version =
    "cmake-adjustment-project-v1";
inline constexpr std::size_t adjustment_project_max_battery_calls = 64;
struct AdjustmentProjectSpec {
    std::string library_path, header_path, header, function_name, library_target;
    std::string client_path, client_target, test_path, test_target;
    // Derived from the immutable owner's input, never a second JSON input.
    std::string client_input;
    bool operator==(const AdjustmentProjectSpec&) const = default;
};
enum class AdjustmentProjectPart { component, client, battery };
struct AdjustmentProjectPhase {
    AdjustmentProjectPart part = AdjustmentProjectPart::component;
    OpenBatteryMutation mutation = OpenBatteryMutation::baseline;
    std::optional<std::size_t> call_index;
    std::size_t sample_index = 0;
    bool assertions_disabled = false;
    // Repeated requests additionally receive a shared defect. A checker
    // cannot obtain its expected value by calling the same function again.
    // Matching deliberately ignores the destination's initial sentinel.
    std::optional<OpenBatteryCall> matching_input{};
    bool operator==(const AdjustmentProjectPhase&) const = default;
};
struct AdjustmentProjectObservation {
    OpenBatteryCall call;
    bool accepted = false;
    int destination = 0;
    bool operator==(const AdjustmentProjectObservation&) const = default;
};
struct AdjustmentProjectRun {
    std::string program_frame;
    bool compiled = false, executed = false;
    int exit_code = -1;
    AdjustmentProjectPhase phase;
    std::vector<AdjustmentProjectObservation> observations;
    std::string stdout_bytes, stderr_bytes, standard_input;
};
struct AdjustmentProjectVerdict {
    bool accepted = false;
    std::string reason;
};

namespace adjustment_project_detail {
inline bool safe_path(std::string_view path) {
    if(path.empty()||path.size()>256||path.front()=='.'||path.front()=='/')return false;
    for(unsigned char c:path)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
             c=='_'||c=='-'||c=='.'||c=='/'))return false;
    for(std::size_t at=0;at<=path.size();) {
        const auto slash=path.find('/',at);
        const auto part=path.substr(at,slash==path.npos?path.size()-at:slash-at);
        if(part.empty()||part=="."||part=="..")return false;
        if(slash==path.npos)break;
        at=slash+1;
    }
    return true;
}
inline bool identifier(std::string_view name) {
    return open_battery_identifier(name)&&name!="main"&&
        !name.starts_with("lgaed_adjustment_");
}
inline std::optional<std::vector<OpenBatteryCall>> input_calls(std::string_view input) {
    if(input.size()>4096)return {};
    std::istringstream stream{std::string(input)};
    stream.imbue(std::locale::classic());
    std::vector<int> values;
    for(;;) {
        stream>>std::ws;
        if(stream.eof())break;
        int value=0;
        if(!(stream>>value)||values.size()==24)return {};
        values.push_back(value);
    }
    if(stream.bad()||values.size()%3)return {};
    std::vector<OpenBatteryCall> result;
    for(std::size_t i=0;i<values.size();i+=3)
        result.push_back({values[i],values[i+1],values[i+2],0});
    return result;
}
inline bool same_request(const OpenBatteryCall& a,const OpenBatteryCall& b) {
    return a.value==b.value&&a.lower==b.lower&&a.upper==b.upper;
}
inline int corrupt(int value) { return value==0?1:0; }
inline std::pair<bool,int> observed_expected(const OpenBatteryCall& call,
        const AdjustmentProjectPhase& phase,std::size_t index) {
    auto result=open_battery_expected(call);
    if((phase.call_index&&*phase.call_index==index)||
       (phase.matching_input&&same_request(call,*phase.matching_input))) {
        if(phase.mutation==OpenBatteryMutation::invert_acceptance)result.first=!result.first;
        if(phase.mutation==OpenBatteryMutation::corrupt_destination)result.second=corrupt(result.second);
    }
    return result;
}
inline std::string trace_writer_source() {
    // Unformatted output is independent of boolalpha, base, width and locale.
    // Emit after the actual function returns, so exit()/throw cannot forge a
    // completed call by merely entering the component.
    return R"cpp(
namespace lgaed_adjustment_private {
void record(std::size_t index, int value, int lower, int upper,
            int initial, bool accepted, int destination) {
    char bytes[256] = "LGAED_ADJUSTMENT_CALL:";
    char* next = bytes + sizeof("LGAED_ADJUSTMENT_CALL:") - 1;
    const int values[]{static_cast<int>(index), value, lower, upper,
                       initial, accepted ? 1 : 0, destination};
    for (unsigned i=0; i<7; ++i) {
        const auto written=std::to_chars(next,bytes+sizeof(bytes)-1,values[i]);
        if(written.ec!=std::errc{})return;
        next=written.ptr;
        *next++=i==6?'\n':',';
    }
    std::cerr.write(bytes,static_cast<std::streamsize>(next-bytes));
}
}
)cpp";
}
struct DecodedTrace {
    std::vector<AdjustmentProjectObservation> observations;
    std::string visible;
};
inline std::optional<DecodedTrace> decode_trace(std::string_view text) {
    constexpr std::string_view marker="LGAED_ADJUSTMENT_CALL:";
    DecodedTrace result;
    for(std::size_t at=0;at<text.size();) {
        const auto begin=text.find(marker,at);
        if(begin==text.npos) {result.visible.append(text.substr(at));break;}
        result.visible.append(text.substr(at,begin-at));
        const auto payload=begin+marker.size();
        const auto end=text.find('\n',payload);
        // A truncated record cannot be used as a completed function call.
        if(end==text.npos)return {};
        auto line=text.substr(payload,end-payload);
        if(line.ends_with('\r'))line.remove_suffix(1);
        int values[7]{};
        for(std::size_t i=0;i<7;++i) {
            const auto comma=line.find(',');
            const auto field=line.substr(0,comma);
            if(field.empty())return {};
            const auto [ptr,error]=std::from_chars(field.data(),field.data()+field.size(),values[i]);
            if(error!=std::errc{}||ptr!=field.data()+field.size())return {};
            if(i==6) {if(comma!=line.npos)return {};}
            else {if(comma==line.npos)return {};line.remove_prefix(comma+1);}
        }
        if(values[0]<0||static_cast<std::size_t>(values[0])!=result.observations.size()||
           (values[5]!=0&&values[5]!=1)||result.observations.size()==256)return {};
        result.observations.push_back({{values[1],values[2],values[3],values[4]},
            values[5]!=0,values[6]});
        at=end+1;
    }
    return result;
}
enum class Field {request,value,lower,upper,adjustment,accepted,rejected,sum};
struct Fact {Field field;int number;std::size_t start;};
inline std::optional<Field> field(std::string_view word) {
    using rejection_detail::one_of;
    if(one_of(word,{"peticion","solicitud","pedido","consulta","caso","entrada","operacion"}))return Field::request;
    if(one_of(word,{"valor","dato"}))return Field::value;
    if(word=="inferior")return Field::lower;
    if(word=="superior")return Field::upper;
    if(one_of(word,{"ajuste","cambio","desplazamiento","correccion"}))return Field::adjustment;
    if(one_of(word,{"aceptadas","aceptados","aceptaciones","admitidas","admitidos"}))return Field::accepted;
    if(one_of(word,{"rechazadas","rechazados","rechazos"}))return Field::rejected;
    if(one_of(word,{"suma","acumulado","acumulada"}))return Field::sum;
    return {};
}
inline std::vector<Fact> facts(std::string_view text) {
    const auto words=rejection_detail::words(text);
    std::vector<Fact> result;
    for(std::size_t i=0;i<words.size();++i) {
        const auto kind=field(words[i].text);
        if(!kind)continue;
        for(std::size_t j=i+1;j<words.size()&&j<=i+5;++j) {
            if(words[j].number) {
                result.push_back({*kind,*words[j].number,words[i].start});break;
            }
            if(!rejection_detail::one_of(words[j].text,
                {"numero","num","de","del","es","son","vale","total","final","nuevo","nuevos",
                 "nueva","nuevas","obtenido","obtenida","resultado","iguales","igual","a","las","los",
                 "peticiones","solicitudes","ajustes","datos","recibido","recibida"}))break;
        }
    }
    return result;
}
inline std::vector<int> values(std::span<const Fact> facts,Field wanted) {
    std::vector<int> result;
    for(const auto& fact:facts)if(fact.field==wanted)result.push_back(fact.number);
    return result;
}
inline bool all_equal_nonempty(std::span<const int> numbers,int expected) {
    return !numbers.empty()&&std::ranges::all_of(numbers,[&](int value){return value==expected;});
}
inline std::string semantic_text(std::string_view text) {
    auto normalized=std::string(text);
    for(const auto& [unicode,ascii]:std::array<std::pair<std::string_view,std::string_view>,2>{
            std::pair{"\xE2\x89\xA4","<="},std::pair{"\xE2\x89\xA5",">="}}) {
        for(auto at=normalized.find(unicode);at!=normalized.npos;at=normalized.find(unicode,at+ascii.size()))
            normalized.replace(at,unicode.size(),ascii);
    }
    return rejection_detail::fold_spanish(normalized);
}
inline bool order_condition(std::string_view text) {
    const auto folded=semantic_text(text);
    const auto tokens=rejection_detail::words(folded);
    bool found=false;
    for(std::size_t i=0;i<tokens.size();++i) {
        if(tokens[i].text!="inferior"&&tokens[i].text!="superior")continue;
        for(std::size_t j=i+1;j<tokens.size()&&j<=i+10;++j) {
            if(tokens[j].number)break; // observed data are not an order rule
            if(tokens[j].text!="inferior"&&tokens[j].text!="superior")continue;
            if(tokens[i].text==tokens[j].text)break;
            const auto previous=folded.find_last_of(".;\n",tokens[i].start);
            const auto following=folded.find_first_of(".;\n",tokens[j].end);
            const auto clause=std::string_view(folded).substr(previous==folded.npos?0:previous+1,
                following==folded.npos?folded.npos:following-(previous==folded.npos?0:previous+1));
            for(const auto denied:{"no hace falta","no se requiere","no se exige","no es necesario",
                    "no es necesaria","no se necesita","no importa","no es obligatorio","no es obligatoria"})
                if(clause.find(denied)!=clause.npos)return false;
            auto between=folded.substr(tokens[i].end,tokens[j].start-tokens[i].end);
            std::string compact;
            for(unsigned char c:between)if(!std::isspace(c))compact+=static_cast<char>(c);
            const bool forward=tokens[i].text=="inferior";
            if(compact.find('<')!=compact.npos||compact.find('>')!=compact.npos) {
                if(compact!=(forward?"<=":">="))return false;
                found=true;
            }else {
                const bool inclusive=between.find(forward?"menor o igual":"mayor o igual")!=between.npos;
                const bool negated=between.find("no ")!=between.npos;
                const bool reversed=between.find(forward?"mayor":"menor")!=between.npos;
                const bool exceed=forward&&(between.find("supera")!=between.npos||
                    between.find("supere")!=between.npos);
                if(inclusive&&!negated)found=true;
                else if(negated&&(reversed||exceed))found=true;
                else if(between.find("menor")!=between.npos||between.find("mayor")!=between.npos||exceed)
                    return false;
            }
            break;
        }
    }
    return found;
}
inline AdjustmentProjectVerdict client_output(std::span<const OpenBatteryCall> inputs,
        const AdjustmentProjectRun& run) {
    std::vector<int> accepted_numbers,adjustments,rejected_numbers;
    int sum=0;
    for(std::size_t i=0;i<inputs.size();++i) {
        const auto expected=open_battery_expected(inputs[i]);
        if(expected.first) {
            accepted_numbers.push_back(static_cast<int>(i+1));
            adjustments.push_back(expected.second);sum+=expected.second;
        }else rejected_numbers.push_back(static_cast<int>(i+1));
    }
    if(run.exit_code!=(rejected_numbers.empty()?0:1))
        return {false,"El cliente no devolvió el estado del lote actual."};
    const auto out=facts(run.stdout_bytes);
    if(values(out,Field::request)!=accepted_numbers||values(out,Field::adjustment)!=adjustments)
        return {false,"Los resultados aceptados no identifican sus peticiones y ajustes en stdout."};
    if(!all_equal_nonempty(values(out,Field::accepted),static_cast<int>(accepted_numbers.size()))||
       !all_equal_nonempty(values(out,Field::rejected),static_cast<int>(rejected_numbers.size()))||
       !all_equal_nonempty(values(out,Field::sum),sum))
        return {false,"Los contadores o la suma no corresponden a los resultados nuevos."};
    // Word offsets refer to the folded text. Slice that same text so UTF-8
    // accents in one diagnostic cannot move the next request's boundary.
    const auto folded_error=semantic_text(run.stderr_bytes);
    const auto err=facts(folded_error);
    if(values(err,Field::request)!=rejected_numbers)
        return {false,"Cada rechazo debe identificar su petición en stderr."};
    if(rejected_numbers.empty()) {
        if(std::ranges::any_of(run.stderr_bytes,[](unsigned char c){return !std::isspace(c);}))
            return {false,"El cliente comunicó un rechazo o diagnóstico ajeno al lote aceptado."};
        return {true,{}};
    }
    std::vector<std::size_t> starts;
    for(const auto& fact:err)if(fact.field==Field::request)starts.push_back(fact.start);
    for(std::size_t i=0;i<starts.size();++i) {
        const auto begin=i==0?0:starts[i];
        const auto end=i+1<starts.size()?starts[i+1]:folded_error.size();
        const auto record=std::string_view(folded_error).substr(begin,end-begin);
        const auto data=facts(record);
        const auto& request=inputs[static_cast<std::size_t>(rejected_numbers[i]-1)];
        if(!all_equal_nonempty(values(data,Field::value),request.value)||
           !all_equal_nonempty(values(data,Field::lower),request.lower)||
           !all_equal_nonempty(values(data,Field::upper),request.upper))
            return {false,"Un rechazo omite o confunde valor, inferior y superior."};
        const auto words=rejection_detail::words(record);
        if(!rejection_detail::range(record,words,20,-20))
            return {false,"Cada rechazo debe comunicar el dominio permitido [-20,20]."};
        if(!order_condition(record))
            return {false,"Cada rechazo debe comunicar inferior menor o igual que superior."};
        if(!rejection_detail::actionable(words))
            return {false,"Cada rechazo necesita una acción para revisar o corregir sus datos."};
    }
    return {true,{}};
}
} // namespace adjustment_project_detail

inline bool adjustment_project_spec_valid(const AdjustmentProjectSpec& spec) {
    using namespace adjustment_project_detail;
    for(const auto* path:{&spec.library_path,&spec.header_path,&spec.header,&spec.client_path,&spec.test_path})
        if(!safe_path(*path))return false;
    const auto cpp=[](std::string_view path){return path.ends_with(".cpp")||path.ends_with(".cc")||path.ends_with(".cxx");};
    if(!cpp(spec.library_path)||!cpp(spec.client_path)||!cpp(spec.test_path)||
       !(spec.header_path.ends_with(".h")||spec.header_path.ends_with(".hpp")||spec.header_path.ends_with(".hxx")))return false;
    if(spec.library_path==spec.client_path||spec.library_path==spec.test_path||spec.client_path==spec.test_path)return false;
    const auto suffix="/"+spec.header;
    if(!spec.header_path.ends_with(suffix)||spec.header_path.size()==suffix.size())return false;
    for(const auto* name:{&spec.function_name,&spec.library_target,&spec.client_target,&spec.test_target})
        if(!identifier(*name))return false;
    return spec.library_target!=spec.client_target&&spec.library_target!=spec.test_target&&
        spec.client_target!=spec.test_target&&input_calls(spec.client_input).has_value();
}
inline std::string adjustment_project_spec_frame(const AdjustmentProjectSpec& spec) {
    std::string result(adjustment_project_objective_version);
    for(const auto* value:{&spec.library_path,&spec.header_path,&spec.header,&spec.function_name,&spec.library_target,
            &spec.client_path,&spec.client_target,&spec.test_path,&spec.test_target,&spec.client_input})
        result+="\n"+std::to_string(value->size())+":"+*value;
    return result;
}
inline bool adjustment_project_header_valid(std::string_view source,const AdjustmentProjectSpec& spec) {
    const auto all=code_format::cpp_tokens(source);
    if(!all||!adjustment_project_spec_valid(spec))return false;
    std::vector<std::string_view> tokens;
    for(const auto& token:*all)
        if(!token.starts_with("comment:")&&!token.starts_with("directive:"))tokens.push_back(token);
    std::size_t at=0;
    const auto take=[&](std::string_view value){if(at>=tokens.size()||tokens[at]!=value)return false;++at;return true;};
    if(!take("id:bool")||!take("id:"+spec.function_name)||!take("op:("))return false;
    for(unsigned i=0;i<4;++i) {
        if(!take("id:int")||(i==3&&!take("op:&")))return false;
        if(at<tokens.size()&&tokens[at].starts_with("id:"))++at;
        if(!take(i==3?"op:)":"op:,"))return false;
    }
    return take("op:;")&&at==tokens.size();
}
inline bool adjustment_project_source_includes_header(std::string_view source,const AdjustmentProjectSpec& spec) {
    if(!adjustment_project_spec_valid(spec)||source.empty()||source.size()>4*1024*1024)return false;
    // Translation phase 2 precedes comments/directives: a continued line
    // comment must not manufacture an include on its next physical line.
    std::string joined;
    joined.reserve(source.size());
    for(std::size_t at=0;at<source.size();) {
        if(source.substr(at).starts_with("\\\r\n"))at+=3;
        else if(source.substr(at).starts_with("\\\n"))at+=2;
        else joined+=source[at++];
    }
    const auto without_comments=[](std::string_view input)->std::optional<std::string> {
        const auto tokens=code_format::cpp_tokens(input);
        if(!tokens)return {};
        std::string result;
        std::size_t position=0;
        for(const auto& token:*tokens) {
            const auto text=std::string_view(token).substr(token.find(':')+1);
            const auto found=input.find(text,position);
            if(found==input.npos)return {};
            result.append(input.substr(position,found-position));
            if(token.starts_with("comment:")) {
                for(char c:text)result+=c=='\n'||c=='\r'?c:' ';
            }else result+=text;
            position=found+text.size();
        }
        result.append(input.substr(position));
        return result;
    };
    // Removing real comments also recognizes legal '/* comment */ #include'
    // at the start of a logical line. Literal/raw-string contents stay tokens.
    const auto clean=without_comments(joined);
    if(!clean)return false;
    const auto tokens=code_format::cpp_tokens(*clean);
    if(!tokens)return false;
    for(const auto& token:*tokens) {
        if(!token.starts_with("directive:"))continue;
        auto body=std::string_view(token).substr(10);
        if(body.starts_with("%:"))body.remove_prefix(2);
        else if(body.starts_with('#'))body.remove_prefix(1);
        else continue;
        const auto cleaned_body=without_comments(body);
        if(!cleaned_body)continue;
        const auto parts=code_format::cpp_tokens(*cleaned_body);
        if(!parts||parts->size()<2||parts->front()!="id:include")continue;
        if(parts->size()==2&&parts->back()=="literal:\""+spec.header+"\"")return true;
        if((*parts)[1]!="op:<"||parts->back()!="op:>")continue;
        const auto open=cleaned_body->find('<'),close=cleaned_body->find('>',open+1);
        if(open!=cleaned_body->npos&&close!=cleaned_body->npos&&
           std::string_view(*cleaned_body).substr(open+1,close-open-1)==spec.header)return true;
    }
    return false;
}
inline bool adjustment_project_graph_valid(const AdjustmentProjectSpec& spec,const CmakeGraphRequirement& graph) {
    if(!adjustment_project_spec_valid(spec)||!graph.minimum_cmake_version_at_least)return false;
    const auto version=cmake_numeric_version(graph.minimum_cmake_version);
    if(!version||*version<std::array<std::uint32_t,4>{3,20,0,0})return false;
    const auto root=spec.header_path.substr(0,spec.header_path.size()-spec.header.size()-1);
    const auto target=[&](std::string_view name)->const CmakeRequiredTarget* {
        const CmakeRequiredTarget* result=nullptr;
        for(const auto& candidate:graph.targets)if(candidate.name==name) {
            if(result)return nullptr;
            result=&candidate;
        }
        return result;
    };
    const auto* library=target(spec.library_target);
    const auto* client=target(spec.client_target);
    const auto* battery=target(spec.test_target);
    if(!library||!client||!battery)return false;
    const auto common=[](const CmakeRequiredTarget& role,std::string_view type,std::string_view path) {
        return role.type==type&&role.exact_sources&&role.sources==std::vector<std::string>{std::string(path)}&&
            role.require_explicit_cxx20&&role.require_explicit_extensions_off&&
            role.require_explicit_msvc_warnings&&
            std::ranges::find(role.authored_compile_options,"/W0")==role.authored_compile_options.end()&&
            std::ranges::find(role.authored_compile_options,"/w")==role.authored_compile_options.end();
    };
    return common(*library,"STATIC_LIBRARY",spec.library_path)&&
        common(*client,"EXECUTABLE",spec.client_path)&&common(*battery,"EXECUTABLE",spec.test_path)&&
        std::ranges::find(library->public_include_roots,root)!=library->public_include_roots.end()&&
        std::ranges::find(client->links_to,spec.library_target)!=client->links_to.end()&&
        std::ranges::find(battery->links_to,spec.library_target)!=battery->links_to.end();
}
inline std::vector<std::string> adjustment_project_client_inputs(const AdjustmentProjectSpec& spec) {
    return {spec.client_input,"",
        "-20 20 20\n20 -20 -20\n0 -20 20\n-20 -20 20\n20 -20 20\n6 6 6\n-9 -4 2\n12 -3 5\n",
        std::to_string(std::numeric_limits<int>::min())+" -3 7\n0 -21 7\n0 21 7\n0 -3 -21\n0 -3 21\n0 7 3\n"+
            std::to_string(std::numeric_limits<int>::max())+" -3 7\n-6 -4 2\n"};
}
inline std::vector<OpenBatteryCall> adjustment_project_component_cases() {
    std::vector<OpenBatteryCall> result;
    constexpr std::array intervals{std::pair{-20,-20},std::pair{-20,20},std::pair{20,20},
        std::pair{-7,3},std::pair{-3,-3},std::pair{0,0}};
    for(const auto& [lower,upper]:intervals)
        for(const int value:{-20,-19,-7,-3,0,3,19,20})
            for(const int initial:{0,73})result.push_back({value,lower,upper,initial});
    for(const int outside:{std::numeric_limits<int>::min(),-21,21,std::numeric_limits<int>::max()}) {
        result.push_back({outside,-3,7,-91});
        result.push_back({0,outside,7,37});
        result.push_back({0,-3,outside,-43});
    }
    for(const int initial:{std::numeric_limits<int>::min(),-7,0,39,std::numeric_limits<int>::max()})
        result.push_back({0,7,3,initial});
    return result;
}
inline std::optional<std::string> adjustment_project_component_source(const AdjustmentProjectSpec& spec) {
    if(!adjustment_project_spec_valid(spec))return {};
    std::string source="#include \""+spec.header+"\"\n#include <charconv>\n#include <iostream>\n#include <cstddef>\n";
    source+=adjustment_project_detail::trace_writer_source();
    source+="\nint main() {\n    using Function=bool(*)(int,int,int,int&);\n    const Function function=&"+
        spec.function_name+";\n    const int cases[][4]={\n";
    for(const auto& call:adjustment_project_component_cases())
        source+="        {"+std::to_string(call.value)+","+std::to_string(call.lower)+","+
            std::to_string(call.upper)+","+std::to_string(call.initial)+"},\n";
    source+="    };\n    std::size_t index=0;\n    for(const auto& call:cases) {\n"
        "        int destination=call[3];\n        const bool accepted=function(call[0],call[1],call[2],destination);\n"
        "        lgaed_adjustment_private::record(index++,call[0],call[1],call[2],call[3],accepted,destination);\n"
        "    }\n    return 0;\n}\n";
    return source;
}
inline std::optional<std::string> adjustment_project_instrumented_library(std::string_view original,
        const AdjustmentProjectSpec& spec,const AdjustmentProjectPhase& phase) {
    if(!adjustment_project_spec_valid(spec)||original.empty()||original.size()>4*1024*1024||
       original.find("lgaed_adjustment_")!=original.npos||phase.part==AdjustmentProjectPart::component||
       ((phase.mutation==OpenBatteryMutation::baseline)!=(!phase.call_index&&!phase.matching_input))||
       (phase.call_index&&phase.matching_input)||
       (phase.matching_input&&phase.part!=AdjustmentProjectPart::battery)||
       (phase.call_index&&*phase.call_index>=adjustment_project_max_battery_calls))return {};
    const auto renamed="lgaed_adjustment_original_"+spec.function_name;
    std::string source="#include <charconv>\n#include <iostream>\n#include <cstddef>\n#define "+spec.function_name+" "+renamed+"\n";
    source+=original;
    source+="\n#undef "+spec.function_name+"\n";
    source+=adjustment_project_detail::trace_writer_source();
    source+="\nbool "+spec.function_name+"(int value,int lower,int upper,int& destination) {\n"
        "    static std::size_t next=0;\n    const std::size_t index=next++;\n"
        "    const int initial=destination;\n    bool accepted="+renamed+"(value,lower,upper,destination);\n";
    if(phase.call_index) {
        source+="    if(index=="+std::to_string(*phase.call_index)+") ";
        source+=phase.mutation==OpenBatteryMutation::invert_acceptance?
            "accepted=!accepted;\n":"destination=destination==0?1:0;\n";
    }
    if(phase.matching_input) {
        const auto& match=*phase.matching_input;
        source+="    if(value=="+std::to_string(match.value)+" && lower=="+
            std::to_string(match.lower)+" && upper=="+std::to_string(match.upper)+") ";
        source+=phase.mutation==OpenBatteryMutation::invert_acceptance?
            "accepted=!accepted;\n":"destination=destination==0?1:0;\n";
    }
    source+="    lgaed_adjustment_private::record(index,value,lower,upper,initial,accepted,destination);\n"
        "    return accepted;\n}\n";
    return source;
}
inline std::optional<std::vector<AdjustmentProjectObservation>> adjustment_project_parse_observations(std::string_view stderr_bytes) {
    auto decoded=adjustment_project_detail::decode_trace(stderr_bytes);
    if(!decoded)return {};
    return std::move(decoded->observations);
}
inline std::string adjustment_project_student_stderr(std::string_view stderr_bytes) {
    auto decoded=adjustment_project_detail::decode_trace(stderr_bytes);
    return decoded?std::move(decoded->visible):std::string(stderr_bytes);
}
inline AdjustmentProjectVerdict adjustment_project_battery_coverage(std::span<const AdjustmentProjectObservation> observations) {
    if(observations.empty()||observations.size()>adjustment_project_max_battery_calls)
        return {false,"La batería necesita llamadas observables dentro del límite de ejecución."};
    // Structural obligations, not the eleven reference tuples. One case may
    // establish several boundaries; additional or repeated calls are allowed.
    std::array<bool,6> accepted{};
    std::array<bool,6> edges{};
    std::array<bool,7> rejected{};
    for(const auto& observed:observations) {
        const auto& c=observed.call;
        const auto expected=open_battery_expected(c);
        if(observed.accepted!=expected.first||observed.destination!=expected.second)
            return {false,"La biblioteca no cumplió el contrato en una llamada de la batería."};
        if(expected.first) {
            accepted[0]=accepted[0]||c.value<c.lower;
            accepted[1]=accepted[1]||(c.lower<c.value&&c.value<c.upper);
            accepted[2]=accepted[2]||c.value>c.upper;
            accepted[3]=accepted[3]||(c.value==c.lower&&c.lower<c.upper);
            accepted[4]=accepted[4]||(c.value==c.upper&&c.lower<c.upper);
            accepted[5]=accepted[5]||c.lower==c.upper;
            const int fields[]{c.value,c.lower,c.upper};
            for(std::size_t i=0;i<3;++i) {
                edges[2*i]=edges[2*i]||fields[i]==-20;
                edges[2*i+1]=edges[2*i+1]||fields[i]==20;
            }
        }else {
            rejected[0]=rejected[0]||c.value< -20;rejected[1]=rejected[1]||c.value>20;
            rejected[2]=rejected[2]||c.lower< -20;rejected[3]=rejected[3]||c.lower>20;
            rejected[4]=rejected[4]||c.upper< -20;rejected[5]=rejected[5]||c.upper>20;
            rejected[6]=rejected[6]||(c.value>=-20&&c.value<=20&&c.lower>=-20&&c.lower<=20&&
                c.upper>=-20&&c.upper<=20&&c.lower>c.upper);
        }
    }
    const auto all=[](const auto& values){return std::ranges::all_of(values,[](bool value){return value;});};
    if(!all(accepted))return {false,"Faltan posiciones, extremos inclusivos o un intervalo degenerado en la batería."};
    if(!all(edges))return {false,"La batería no contrasta ambos límites admitidos de los tres datos."};
    if(!all(rejected))return {false,"La batería no observa todos los límites rechazados y el orden invertido."};
    return {true,{}};
}
inline std::optional<std::vector<AdjustmentProjectPhase>> adjustment_project_plan(const AdjustmentProjectSpec& spec,
        std::span<const AdjustmentProjectObservation> battery_baseline) {
    if(!adjustment_project_spec_valid(spec)||battery_baseline.empty()||
       battery_baseline.size()>adjustment_project_max_battery_calls)return {};
    std::vector<AdjustmentProjectPhase> result{{AdjustmentProjectPart::component,OpenBatteryMutation::baseline,{},0}};
    const auto inputs=adjustment_project_client_inputs(spec);
    for(std::size_t i=0;i<inputs.size();++i)result.push_back({AdjustmentProjectPart::client,OpenBatteryMutation::baseline,{},i});
    result.push_back({AdjustmentProjectPart::battery,OpenBatteryMutation::baseline,{},0});
    result.push_back({AdjustmentProjectPart::battery,OpenBatteryMutation::baseline,{},0,true});
    const auto client=adjustment_project_detail::input_calls(spec.client_input);
    if(!client)return {};
    for(std::size_t i=0;i<client->size();++i) {
        // Never turn a rejection into an acceptance in the client: its
        // preserved sentinel can be any int, and a correct client may sum
        // newly accepted values under the documented [-40,40] guarantee.
        // Both observations of every genuinely accepted call are varied.
        if(!open_battery_expected((*client)[i]).first)continue;
        result.push_back({AdjustmentProjectPart::client,OpenBatteryMutation::invert_acceptance,i,0});
        result.push_back({AdjustmentProjectPart::client,OpenBatteryMutation::corrupt_destination,i,0});
    }
    for(std::size_t i=0;i<battery_baseline.size();++i) {
        result.push_back({AdjustmentProjectPart::battery,OpenBatteryMutation::invert_acceptance,i,0,true});
        result.push_back({AdjustmentProjectPart::battery,OpenBatteryMutation::corrupt_destination,i,0,true});
    }
    for(std::size_t i=0;i<battery_baseline.size();++i) {
        const auto& call=battery_baseline[i].call;
        bool earlier=false,later=false;
        for(std::size_t j=0;j<battery_baseline.size();++j) {
            if(!adjustment_project_detail::same_request(call,battery_baseline[j].call))continue;
            earlier=earlier||j<i;later=later||j>i;
        }
        if(earlier||!later)continue;
        result.push_back({AdjustmentProjectPart::battery,OpenBatteryMutation::invert_acceptance,{},0,true,call});
        result.push_back({AdjustmentProjectPart::battery,OpenBatteryMutation::corrupt_destination,{},0,true,call});
    }
    return result;
}
inline AdjustmentProjectVerdict adjustment_project_verify(const AdjustmentProjectSpec& spec,
        std::span<const AdjustmentProjectRun> runs,std::string_view program_frame) {
    using namespace adjustment_project_detail;
    if(!adjustment_project_spec_valid(spec)||program_frame.empty())return {false,"La sonda no tiene un contrato autenticado."};
    const auto client_inputs=adjustment_project_client_inputs(spec);
    const auto battery_index=client_inputs.size()+1;
    if(runs.size()<=battery_index+1)return {false,"Faltan la biblioteca, el cliente o la batería de esta construcción."};
    const auto& public_battery=runs[battery_index];
    const auto& battery=runs[battery_index+1];
    const auto plan=adjustment_project_plan(spec,battery.observations);
    if(!plan||runs.size()!=plan->size())return {false,"Faltan mutaciones independientes de las llamadas observadas."};
    for(std::size_t i=0;i<runs.size();++i) {
        const auto& run=runs[i];
        if(!run.compiled||!run.executed||run.program_frame!=program_frame||run.phase!=(*plan)[i])
            return {false,"Una fase no terminó o pertenece a otra revisión, entrada o mutación."};
        const auto expected_input=run.phase.part==AdjustmentProjectPart::client?
            client_inputs[run.phase.sample_index]:std::string{};
        if(run.standard_input!=expected_input)return {false,"La entrada observada no pertenece a esta fase."};
        for(std::size_t call=0;call<run.observations.size();++call) {
            const auto expected=observed_expected(run.observations[call].call,run.phase,call);
            if(run.observations[call].accepted!=expected.first||run.observations[call].destination!=expected.second)
                return {false,"La llamada observada no coincide con el contrato y la mutación de esta fase."};
        }
    }
    const auto component=adjustment_project_component_cases();
    if(runs.front().observations.size()!=component.size()||runs.front().exit_code!=0||
       !runs.front().stdout_bytes.empty()||!runs.front().stderr_bytes.empty())
        return {false,"El componente no terminó todas las llamadas o produjo una salida propia."};
    for(std::size_t i=0;i<component.size();++i)
        if(runs.front().observations[i].call!=component[i])return {false,"La sonda no llamó a la interfaz pública con sus datos independientes."};
    for(std::size_t i=0;i<client_inputs.size();++i) {
        const auto inputs=input_calls(client_inputs[i]);
        if(!inputs)return {false,"La entrada del cliente no contiene ternas completas del dominio declarado."};
        const auto& run=runs[i+1];
        if(run.observations.size()!=inputs->size())return {false,"El cliente debe llamar una vez por petición y continuar tras los rechazos."};
        for(std::size_t j=0;j<inputs->size();++j)
            if(!same_request(run.observations[j].call,(*inputs)[j]))return {false,"El cliente no llamó a la biblioteca con la petición actual."};
        const auto visible=client_output(*inputs,run);
        if(!visible.accepted)return visible;
    }
    if(public_battery.exit_code!=0||battery.exit_code!=0)
        return {false,"La batería original no terminó sin discrepancias."};
    if(public_battery.observations!=battery.observations)
        return {false,"Las llamadas y sus resultados deben conservarse cuando assert está desactivado."};
    const auto coverage=adjustment_project_battery_coverage(battery.observations);
    if(!coverage.accepted)return coverage;
    const auto prepared=input_calls(spec.client_input);
    for(std::size_t i=battery_index+2;i<runs.size();++i) {
        const auto& run=runs[i];
        if((run.phase.call_index&&*run.phase.call_index>=run.observations.size())||
           (run.phase.matching_input&&!std::ranges::any_of(run.observations,[&](const auto& observation){
                return same_request(observation.call,*run.phase.matching_input);}))||
           (!run.phase.call_index&&!run.phase.matching_input))
            return {false,"La llamada seleccionada no alcanzó la biblioteca original."};
        if(run.phase.part==AdjustmentProjectPart::client) {
            const auto& baseline=runs[1];
            if(run.exit_code<0||run.exit_code>255||run.observations.size()>prepared->size())
                return {false,"La sonda del cliente no terminó normalmente."};
            for(std::size_t j=0;j<run.observations.size();++j)
                if(!same_request(run.observations[j].call,(*prepared)[j]))
                    return {false,"La mutación del cliente alteró las peticiones observadas."};
            // A defensive client may stop after detecting a deliberately
            // inconsistent component. Only ordinary baselines must continue.
            if(run.exit_code==baseline.exit_code&&run.stdout_bytes==baseline.stdout_bytes&&run.stderr_bytes==baseline.stderr_bytes)
                return {false,"El cliente descartó el resultado real de una llamada a la biblioteca."};
        }else {
            if(run.exit_code!=1||run.observations.size()!=battery.observations.size())
                return {false,"La batería no detectó la discrepancia o dejó llamadas sin ejecutar."};
            for(std::size_t j=0;j<run.observations.size();++j)
                if(run.observations[j].call!=battery.observations[j].call)
                    return {false,"Una discrepancia impidió conservar todas las comprobaciones de la batería."};
            if(run.stdout_bytes==battery.stdout_bytes&&run.stderr_bytes==battery.stderr_bytes)
                return {false,"La batería devolvió un fallo sin comunicar el cambio observado."};
        }
    }
    return {true,"Componente, cliente y batería contrastados en la misma construcción."};
}

} // namespace lgaed::curriculum
