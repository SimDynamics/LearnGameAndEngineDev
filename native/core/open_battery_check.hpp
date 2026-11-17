#pragma once

#include "core/boolean_battery_check.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Pure verification primitives for separately compiled CMake battery mutants.
// The worker must bind these observations to the submitted program fingerprint;
// this module never writes a student draft, a curricular asset or progress.
struct OpenBatteryCall {
    int value=0,lower=0,upper=0,initial=0;
    bool operator==(const OpenBatteryCall&) const = default;
};
enum class OpenBatteryMutation {
    baseline, invert_acceptance, corrupt_destination, corrupt_nonzero_destination
};
// V1 checked only acceptance; V2 also checked destinations; V3 bound each
// comparison to its case number. V4 also changes a nonzero destination to a
// different nonzero value, so truthiness alone cannot verify an exact adjustment.
// Preserve historical receipts without promoting them to this stronger goal.
inline constexpr std::string_view open_battery_objective_version =
    "open-cmake-battery-independent-mutations-v4";
struct OpenBatteryPhase {
    OpenBatteryMutation mutation=OpenBatteryMutation::baseline;
    std::optional<OpenBatteryCall> target;
    bool operator==(const OpenBatteryPhase&) const = default;
};
struct OpenBatteryProbeSpec {
    std::string library_path, test_path, function_name, comparison_name, test_target;
    std::vector<OpenBatteryCall> original_cases;
    // Derived from the authenticated original driver, aligned with original_cases.
    std::vector<int> original_case_numbers;
    bool operator==(const OpenBatteryProbeSpec&) const = default;
};
struct OpenBatteryRun {
    std::string program_frame;
    bool compiled=false,executed=false;
    int exit_code=-1;
    // Exact private variant and source-independent input tuple exercised.
    // A duplicated passing mutant must not stand in for a missing case.
    OpenBatteryMutation mutation=OpenBatteryMutation::baseline;
    std::optional<OpenBatteryCall> target;
    std::vector<OpenBatteryCall> calls;
    // Student-visible streams without private witness lines.
    std::string stdout_bytes, stderr_bytes;
    // Each number belongs to the actual library call at the same position.
    std::vector<int> case_numbers;
};
struct OpenBatteryVerdict {
    bool accepted=false;
    std::string reason;
    std::optional<OpenBatteryCall> added;
};

inline bool open_battery_identifier(std::string_view name) {
    if(name.empty()||name.size()>128)return false;
    const auto letter=[](unsigned char c) {
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';
    };
    if(!letter(static_cast<unsigned char>(name.front())))return false;
    for(unsigned char c:name)
        if(!letter(c)&&!(c>='0'&&c<='9'))return false;
    return true;
}
inline bool open_battery_originals_valid(std::span<const OpenBatteryCall> cases) {
    if(cases.empty()||cases.size()>32)return false;
    for(std::size_t i=0;i<cases.size();++i)
        for(std::size_t j=0;j<i;++j)
            if(cases[i]==cases[j])return false;
    return true;
}
// Shared by the native worker and verifier: one clean execution, then an
// acceptance defect and two distinct destination defects for every old/new call.
// Call distinctness is checked by the verifier before awarding credit.
inline std::optional<std::vector<OpenBatteryPhase>> open_battery_mutation_plan(
        std::span<const OpenBatteryCall> original,OpenBatteryCall added) {
    if(!open_battery_originals_valid(original))return {};
    std::vector<OpenBatteryPhase> plan;
    plan.reserve(3*original.size()+4);
    plan.push_back({OpenBatteryMutation::baseline,std::nullopt});
    const auto append=[&](OpenBatteryCall target) {
        plan.push_back({OpenBatteryMutation::invert_acceptance,target});
        plan.push_back({OpenBatteryMutation::corrupt_destination,target});
        plan.push_back({OpenBatteryMutation::corrupt_nonzero_destination,target});
    };
    for(const auto& target:original)append(target);
    append(added);
    return plan;
}
inline bool open_battery_case_numbers_valid(std::span<const int> numbers) {
    if(numbers.empty())return false;
    for(std::size_t i=0;i<numbers.size();++i)
        for(std::size_t j=0;j<i;++j)
            if(numbers[i]==numbers[j])return false;
    return true;
}
// Authenticated source file names and finite mutation work are required.
// The curriculum loader, not an editable student project, owns this request.
inline bool open_battery_probe_spec_valid(const OpenBatteryProbeSpec& spec) {
    const auto safe=[](std::string_view path) {
        if(path.empty()||path.size()>256||path.starts_with('/')||
           path.starts_with('.')||path.find("..")!=path.npos||
           path.find('\\')!=path.npos)return false;
        for(unsigned char c:path)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'||c=='-'||
                 c=='.'||c=='/'))return false;
        return true;
    };
    return safe(spec.library_path)&&safe(spec.test_path)&&
        spec.library_path!=spec.test_path&&
        open_battery_identifier(spec.function_name)&&
        open_battery_identifier(spec.comparison_name)&&
        open_battery_identifier(spec.test_target)&&
        open_battery_originals_valid(spec.original_cases)&&
        spec.original_case_numbers.size()==spec.original_cases.size()&&
        open_battery_case_numbers_valid(spec.original_case_numbers);
}
// Independent agreement for bool f(int valor,int inferior,int superior,int&).
// A rejected request preserves the destination, including negative sentinels.
inline std::pair<bool,int> open_battery_expected(const OpenBatteryCall& c) {
    if(c.value < -20||c.value > 20||c.lower < -20||c.lower > 20||
       c.upper < -20||c.upper > 20||c.lower > c.upper)
        return {false,c.initial};
    return {true,c.value<c.lower?c.lower-c.value:
        c.value>c.upper?c.upper-c.value:0};
}
inline std::optional<std::string> open_battery_mutated_library(
        std::string_view original,std::string_view function,
        OpenBatteryMutation mutation,
        std::optional<OpenBatteryCall> target=std::nullopt) {
    if(original.empty()||original.size()>4*1024*1024||
       !open_battery_identifier(function)||
       (mutation!=OpenBatteryMutation::baseline&&
        mutation!=OpenBatteryMutation::invert_acceptance&&
        mutation!=OpenBatteryMutation::corrupt_destination&&
        mutation!=OpenBatteryMutation::corrupt_nonzero_destination)||
       ((mutation==OpenBatteryMutation::baseline)!=!target.has_value()))return {};
    const std::string declaration="bool "+std::string(function)+
        "(int valor, int inferior, int superior, int& ajuste) {";
    const auto position=original.find(declaration);
    if(position==original.npos||
       original.find(declaration,position+declaration.size())!=original.npos)return {};
    const std::string renamed="lgaed_open_battery_original_"+std::string(function);
    if(original.find(renamed)!=original.npos)return {};
    std::string result="#include <charconv>\n#include <iostream>\n#include <limits>\n";
    result+=original.substr(0,position);
    result+="bool "+renamed+"(int valor, int inferior, int superior, int& ajuste) {";
    result+=original.substr(position+declaration.size());
    result+="\nbool "+std::string(function)+
        "(int valor, int inferior, int superior, int& ajuste) {\n";
    result+="    const int anterior = ajuste;\n";
    // Serialize the private record independently of the student's numeric
    // flags, locale and pending field width. Unformatted write preserves all
    // of those settings for the student's next output operation.
    result+="    char traza[sizeof(\"LGAED_OPEN_BATTERY_CALL:\") +\n"
            "        4 * (std::numeric_limits<int>::digits10 + 3)] =\n"
            "        \"LGAED_OPEN_BATTERY_CALL:\";\n"
            "    char* fin = traza + sizeof(\"LGAED_OPEN_BATTERY_CALL:\") - 1;\n"
            "    const int datos[]{valor, inferior, superior, anterior};\n"
            "    for (unsigned i = 0; i < 4; ++i) {\n"
            "        const auto numero = std::to_chars(fin, traza + sizeof(traza) - 1, datos[i]);\n"
            "        if (numero.ec != std::errc{}) return false;\n"
            "        fin = numero.ptr;\n"
            "        *fin++ = i == 3 ? '\\n' : ',';\n"
            "    }\n"
            "    std::cerr.write(traza, static_cast<std::streamsize>(fin - traza));\n";
    result+="    const bool aceptado = "+renamed+
        "(valor, inferior, superior, ajuste);\n";
    if(target) {
        result+="    const bool seleccionado = valor == "+
            std::to_string(target->value)+" && inferior == "+
            std::to_string(target->lower)+" && superior == "+
            std::to_string(target->upper)+" && anterior == "+
            std::to_string(target->initial)+";\n";
        if(mutation==OpenBatteryMutation::invert_acceptance)
            result+="    if (seleccionado) return !aceptado;\n";
        else if(mutation==OpenBatteryMutation::corrupt_destination)
            result+="    if (seleccionado) ajuste = ajuste == 0 ? 1 : 0;\n";
        else if(mutation==OpenBatteryMutation::corrupt_nonzero_destination)
            result+="    if (seleccionado) ajuste = ajuste == 1 ? 2 : 1;\n";
    }
    result+="    return aceptado;\n}\n";
    return result;
}

namespace open_battery_detail {
struct ComparisonDefinition {
    std::size_t declaration_begin=0,name_begin=0,name_end=0,opening_begin=0;
    std::vector<std::string> parameters;
};
// The shared lexer supplies original offsets and ignores comments. Locate the
// definition, not literal calls: computed arguments, loops and pointers to the
// same comparison continue through the wrapper. Parameter names are unrestricted.
inline std::optional<ComparisonDefinition> comparison_definition(
        std::string_view source,std::string_view comparison) {
    if(!open_battery_identifier(comparison)||comparison=="main"||
       comparison.starts_with("lgaed_open_battery_"))return {};
    const auto scanned=boolean_battery_detail::tokens(source);
    if(!scanned)return {};
    const auto& tokens=*scanned;
    for(const auto& token:tokens)
        if(token.value.starts_with("id:lgaed_open_battery_"))return {};
    const auto opening=[](std::string_view token) {
        return token=="op:{"||token=="op:<%";
    };
    const auto closing=[](std::string_view token) {
        return token=="op:}"||token=="op:%>";
    };
    const auto matching_parenthesis=[&](std::size_t begin)->std::optional<std::size_t> {
        std::size_t depth=0;
        for(auto i=begin;i<tokens.size();++i) {
            if(tokens[i].value=="op:("){if(++depth>4096)return {};}
            else if(tokens[i].value=="op:)") {
                if(!depth)return {};
                if(!--depth)return i;
            }
        }
        return {};
    };
    std::optional<ComparisonDefinition> found;
    std::size_t depth=0;
    for(std::size_t i=0;i<tokens.size();++i) {
        const auto& token=tokens[i];
        if(token.value.starts_with("directive:")&&depth)return {};
        if(token.value=="id:"+std::string(comparison)&&
           i+1<tokens.size()&&tokens[i+1].value=="op:(") {
            const auto close=matching_parenthesis(i+1);
            if(!close)return {};
            auto after=*close+1;
            if(after<tokens.size()&&tokens[after].value=="id:noexcept") {
                ++after;
                if(after<tokens.size()&&tokens[after].value=="op:(") {
                    const auto end=matching_parenthesis(after);
                    if(!end)return {};
                    after=*end+1;
                }
            }
            const bool body=after<tokens.size()&&opening(tokens[after].value);
            const bool declaration=i&&tokens[i-1].value.starts_with("id:")&&
                tokens[i-1].value!="id:return"&&tokens[i-1].value!="id:co_return"&&
                tokens[i-1].value!="id:throw";
            if(body||declaration) {
                if(depth||!i||tokens[i-1].value!="id:bool"||
                   after>=tokens.size()||
                   (!body&&tokens[after].value!="op:;"))return {};
                auto beginning=i-1;
                while(beginning&&
                      (tokens[beginning-1].value=="id:inline"||
                       tokens[beginning-1].value=="id:static"))--beginning;
                if(beginning&&tokens[beginning-1].value!="op:;"&&
                   !closing(tokens[beginning-1].value)&&
                   !tokens[beginning-1].value.starts_with("directive:"))return {};
                std::vector<std::string> parameters;
                auto at=i+2;
                for(unsigned parameter=0;parameter<7;++parameter) {
                    if(at<*close&&tokens[at].value=="id:const")++at;
                    const std::string_view expected=parameter==5?"id:bool":"id:int";
                    if(at>=*close||tokens[at++].value!=expected)return {};
                    if(at<*close&&tokens[at].value=="id:const")++at;
                    std::string name;
                    if(at<*close&&tokens[at].value.starts_with("id:")) {
                        name=tokens[at++].value.substr(3);
                        if(!open_battery_identifier(name))return {};
                    }
                    if(body&&(name.empty()||
                        std::ranges::find(parameters,name)!=parameters.end()))return {};
                    parameters.push_back(std::move(name));
                    if(parameter<6) {
                        if(at>=*close||tokens[at++].value!="op:,")return {};
                    }
                }
                if(at!=*close)return {};
                if(body) {
                    if(found)return {};
                    found=ComparisonDefinition{tokens[beginning].begin,
                        token.begin,token.end,tokens[after].begin,std::move(parameters)};
                }
            }
        }
        if(opening(token.value)){if(++depth>4096)return {};}
        else if(closing(token.value)){if(!depth)return {};--depth;}
    }
    if(depth)return {};
    return found;
}
} // namespace open_battery_detail

// Only the definition is renamed. Original declarations and every runtime call
// keep the authored name; a forwarding declaration also handles calls preceding
// that definition. The original function body, argument expressions and output
// remain untouched in this private, never-saved variant.
inline std::optional<std::string> open_battery_instrumented_comparison(
        std::string_view original,std::string_view comparison_name) {
    const auto definition=open_battery_detail::comparison_definition(original,comparison_name);
    if(!definition)return {};
    const auto& item=*definition;
    const auto declaration=original.substr(item.declaration_begin,
        item.opening_begin-item.declaration_begin);
    const std::string renamed="lgaed_open_battery_original_"+std::string(comparison_name);
    std::string source="#include <charconv>\n#include <iostream>\n#include <limits>\n";
    source+=original.substr(0,item.declaration_begin);
    source+=declaration;
    source+=";\n";
    source+=original.substr(item.declaration_begin,item.name_begin-item.declaration_begin);
    source+=renamed;
    source+=original.substr(item.name_end);
    source+="\n";
    source+=declaration;
    source+="{\n"
        "    const auto lgaed_open_battery_emit = [](const char* marker, "
        "std::size_t length, int number) {\n"
        "        char buffer[64 + std::numeric_limits<int>::digits10];\n"
        "        char* end = buffer;\n"
        "        for (std::size_t i = 0; i < length; ++i) *end++ = marker[i];\n"
        "        const auto converted = std::to_chars(end, buffer + sizeof(buffer) - 1, number);\n"
        "        if (converted.ec != std::errc{}) return;\n"
        "        end = converted.ptr;\n"
        "        *end++ = '\\n';\n"
        "        std::cerr.write(buffer, static_cast<std::streamsize>(end - buffer));\n"
        "    };\n"
        "    lgaed_open_battery_emit(\"LGAED_OPEN_BATTERY_BEGIN:\", "
        "sizeof(\"LGAED_OPEN_BATTERY_BEGIN:\") - 1, "+item.parameters.front()+");\n"
        "    const bool lgaed_open_battery_result = "+renamed+"(";
    for(std::size_t i=0;i<item.parameters.size();++i) {
        if(i)source+=", ";
        source+=item.parameters[i];
    }
    source+=");\n"
        "    lgaed_open_battery_emit(\"LGAED_OPEN_BATTERY_END:\", "
        "sizeof(\"LGAED_OPEN_BATTERY_END:\") - 1, "+item.parameters.front()+");\n"
        "    return lgaed_open_battery_result;\n"
        "}\n";
    return source;
}

namespace open_battery_detail {
struct DecodedTrace {
    std::vector<OpenBatteryCall> calls;
    std::string visible;
};
// A caller may write a partial message before invoking the library. The
// private record need not begin a line: retain the prefix byte-for-byte and
// remove only a complete, numerically valid record. Parsing and filtering
// share one definition so private evidence never becomes student feedback.
inline std::optional<DecodedTrace> decode_trace(std::string_view all) {
    constexpr std::string_view marker="LGAED_OPEN_BATTERY_CALL:";
    DecodedTrace result;
    for(std::size_t position=0;position<all.size();) {
        const auto begin=all.find(marker,position);
        if(begin==all.npos) {
            result.visible.append(all.substr(position));
            break;
        }
        result.visible.append(all.substr(position,begin-position));
        const auto payload=begin+marker.size();
        auto end=all.find('\n',payload);
        if(end==all.npos)end=all.size();
        auto line=all.substr(payload,end-payload);
        if(line.ends_with('\r'))line.remove_suffix(1);
        int value[4]{};
        for(unsigned i=0;i<4;++i) {
            const auto comma=line.find(',');
            const auto field=line.substr(0,comma);
            if(field.empty())return {};
            const auto [ptr,ec]=std::from_chars(field.data(),
                field.data()+field.size(),value[i]);
            if(ec!=std::errc{}||ptr!=field.data()+field.size())return {};
            if(i==3) {
                if(comma!=line.npos)return {};
            }else{
                if(comma==line.npos)return {};
                line.remove_prefix(comma+1);
            }
        }
        result.calls.push_back({value[0],value[1],value[2],value[3]});
        if(result.calls.size()>34)return {};
        position=end<all.size()?end+1:end;
    }
    return result;
}
} // namespace open_battery_detail

inline std::string open_battery_student_stderr(std::string_view all) {
    auto decoded=open_battery_detail::decode_trace(all);
    // Malformed or truncated evidence fails the call parser. Do not also
    // erase diagnostics from the failed execution while reporting the error.
    return decoded?std::move(decoded->visible):std::string(all);
}
inline std::optional<std::vector<OpenBatteryCall>> open_battery_parse_calls(
        std::string_view stderr_bytes) {
    auto decoded=open_battery_detail::decode_trace(stderr_bytes);
    if(!decoded)return {};
    return std::move(decoded->calls);
}

struct OpenBatteryTrace {
    std::vector<OpenBatteryCall> calls;
    std::vector<int> case_numbers;
    std::string visible;
};
// This stricter decoder is used only for the numbered-comparison objective.
// The legacy call decoder above remains available to library-only consumers.
// Private records can follow partial student output, but must be complete and
// paired: BEGIN(number), one CALL(tuple), END(the same number).
inline std::optional<OpenBatteryTrace> open_battery_parse_trace(std::string_view all) {
    constexpr std::string_view prefix="LGAED_OPEN_BATTERY_";
    constexpr std::string_view begin_marker="LGAED_OPEN_BATTERY_BEGIN:";
    constexpr std::string_view call_marker="LGAED_OPEN_BATTERY_CALL:";
    constexpr std::string_view end_marker="LGAED_OPEN_BATTERY_END:";
    const auto number=[](std::string_view text)->std::optional<int> {
        if(text.empty())return {};
        int value=0;
        const auto converted=std::from_chars(text.data(),text.data()+text.size(),value);
        if(converted.ec!=std::errc{}||converted.ptr!=text.data()+text.size())return {};
        return value;
    };
    OpenBatteryTrace result;
    std::optional<int> current_number;
    std::optional<OpenBatteryCall> current_call;
    for(std::size_t position=0;position<all.size();) {
        const auto begin=all.find(prefix,position);
        if(begin==all.npos) {
            result.visible.append(all.substr(position));
            break;
        }
        result.visible.append(all.substr(position,begin-position));
        const auto end=all.find('\n',begin);
        if(end==all.npos)return {};
        auto line=all.substr(begin,end-begin);
        if(line.ends_with('\r'))line.remove_suffix(1);
        if(line.starts_with(begin_marker)) {
            if(current_number)return {};
            current_number=number(line.substr(begin_marker.size()));
            if(!current_number)return {};
        }else if(line.starts_with(call_marker)) {
            if(!current_number||current_call)return {};
            auto payload=line.substr(call_marker.size());
            int values[4]{};
            for(unsigned i=0;i<4;++i) {
                const auto comma=payload.find(',');
                const auto value=number(payload.substr(0,comma));
                if(!value)return {};
                values[i]=*value;
                if(i==3) {
                    if(comma!=payload.npos)return {};
                }else{
                    if(comma==payload.npos)return {};
                    payload.remove_prefix(comma+1);
                }
            }
            current_call=OpenBatteryCall{values[0],values[1],values[2],values[3]};
        }else if(line.starts_with(end_marker)) {
            const auto ended=number(line.substr(end_marker.size()));
            if(!ended||!current_number||*ended!=*current_number||!current_call)return {};
            result.calls.push_back(*current_call);
            result.case_numbers.push_back(*current_number);
            if(result.calls.size()>34)return {};
            current_number.reset();
            current_call.reset();
        }else return {};
        position=end+1;
    }
    if(current_number||current_call)return {};
    return result;
}

// The worker supplies the baseline and all three independent defects for every
// historical call and the new call, in open_battery_mutation_plan order.
// This core does not infer completion from stdout or a forged "Fallos=0".
// The worker may reject a malformed numbered baseline before compiling the
// mutation matrix. The complete verifier repeats this same gate independently.
inline OpenBatteryVerdict open_battery_verify_baseline(
        std::span<const OpenBatteryCall> original,std::span<const int> original_case_numbers,
        const OpenBatteryRun& baseline,std::string_view program_frame) {
    if(!open_battery_originals_valid(original)||
       original_case_numbers.size()!=original.size()||
       !open_battery_case_numbers_valid(original_case_numbers))
        return {false,"Los casos originales o sus números no son válidos.",{}};
    if(program_frame.empty()||baseline.program_frame!=program_frame||
       !baseline.compiled||!baseline.executed)
        return {false,"La ejecución pertenece a otra revisión o no terminó.",{}};
    if(baseline.exit_code!=0||baseline.mutation!=OpenBatteryMutation::baseline||
       baseline.target.has_value())
        return {false,"La biblioteca original no pasó su ejecución sin mutación.",{}};
    if(baseline.stdout_bytes.empty()&&baseline.stderr_bytes.empty())
        return {false,"La batería no comunicó ningún resultado observable.",{}};
    if(baseline.calls.size()!=original.size()+1)
        return {false,"Se exige exactamente una nueva llamada.",{}};
    if(baseline.case_numbers.size()!=baseline.calls.size()||
       !open_battery_case_numbers_valid(baseline.case_numbers))
        return {false,"Cada comparación necesita un número propio y no repetido.",{}};
    for(std::size_t i=0;i<original.size();++i) {
        const auto found=std::ranges::find(baseline.calls,original[i]);
        if(found==baseline.calls.end()||std::ranges::count(baseline.calls,original[i])!=1)
            return {false,"Una llamada histórica falta o está duplicada.",{}};
        const auto index=static_cast<std::size_t>(found-baseline.calls.begin());
        if(baseline.case_numbers[index]!=original_case_numbers[i])
            return {false,"Una comparación histórica cambió de número o se atribuyó a otro caso.",{}};
    }
    std::optional<OpenBatteryCall> extra;
    for(const auto& item:baseline.calls)
        if(std::ranges::find(original,item)==original.end())extra=item;
    if(!extra)return {false,"No se identificó la prueba añadida.",{}};
    for(const auto& item:original)
        if(item.value==extra->value&&item.lower==extra->lower&&item.upper==extra->upper)
            return {false,"La nueva terna repite una original.",{}};
    return {true,"Los casos históricos conservan sus números y la nueva prueba tiene uno propio.",extra};
}
inline OpenBatteryVerdict open_battery_verify(
        std::span<const OpenBatteryCall> original,std::span<const int> original_case_numbers,
        std::span<const OpenBatteryRun> runs,std::string_view program_frame) {
    if(runs.empty())return {false,"Faltan mutaciones independientes compiladas.",{}};
    const auto baseline=open_battery_verify_baseline(
        original,original_case_numbers,runs.front(),program_frame);
    if(!baseline.accepted)return baseline;
    const auto plan=open_battery_mutation_plan(original,*baseline.added);
    if(!plan||plan->size()!=runs.size())
        return {false,"Faltan mutaciones independientes compiladas.",baseline.added};
    for(std::size_t i=1;i<runs.size();++i) {
        const auto& run=runs[i];
        const auto& required=(*plan)[i];
        if(run.program_frame!=program_frame||!run.compiled||!run.executed)
            return {false,"La ejecución pertenece a otra revisión o no terminó.",baseline.added};
        if(run.mutation!=required.mutation||run.target!=required.target)
            return {false,"La mutación no corresponde a su caso original o nuevo.",baseline.added};
        if(run.exit_code!=1)
            return {false,"Una mutación no cambió el resultado final.",baseline.added};
        // Keep student-visible reporting separate from private trace records.
        if(run.stdout_bytes==runs.front().stdout_bytes&&
           run.stderr_bytes==runs.front().stderr_bytes)
            return {false,"La comparación no comunicó el fallo observado.",baseline.added};
        if(run.calls.size()!=runs.front().calls.size()||
           run.case_numbers.size()!=run.calls.size()||
           !open_battery_case_numbers_valid(run.case_numbers))
            return {false,"Faltan comparaciones o sus números no son propios.",baseline.added};
        // Execution order may change; case identity must remain associated
        // with the same actual input and initial destination in every phase.
        for(std::size_t j=0;j<runs.front().calls.size();++j) {
            const auto& call=runs.front().calls[j];
            const auto found=std::ranges::find(run.calls,call);
            if(found==run.calls.end()||std::ranges::count(run.calls,call)!=1)
                return {false,"Se omitieron llamadas o se interrumpió la batería.",baseline.added};
            const auto index=static_cast<std::size_t>(found-run.calls.begin());
            if(run.case_numbers[index]!=runs.front().case_numbers[j])
                return {false,"El número de una comparación cambió entre ejecuciones.",baseline.added};
        }
    }
    return {true,"Los casos originales y el nuevo conservan sus números y detectan las mutaciones.",baseline.added};
}
} // namespace lgaed::curriculum
