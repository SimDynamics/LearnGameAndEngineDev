#pragma once

#include "core/boolean_battery_check.hpp"

#include <algorithm>
#include <array>
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
// V1 checked acceptance; V2 also destinations; V3 bound case numbers; V4
// changed nonzero destinations. V5 additionally observes the actual boolean
// comparison for every numbered call, not just printed reports or process exit.
// Older receipts retain their history but cannot prove this stronger objective.
inline constexpr std::string_view open_battery_objective_version =
    "open-cmake-battery-independent-mutations-v5";
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
    // Each number and boolean verdict belongs to the actual library call
    // at the same position. Reports and process exit are separate evidence.
    std::vector<int> case_numbers;
    std::vector<unsigned char> comparison_results;
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
namespace open_battery_detail {
// Locate the student's real global function definition by the existing C++
// lexer, never by raw text. Formatting, comments and brace digraphs may vary;
// parameter names are learner-chosen, but the exact four-argument types and
// writable output reference remain compulsory. The first three by-value
// parameters may also be const-qualified, without changing their function type.
// Exact source offsets ensure every byte outside the original function's
// *name* remains unchanged in the private, temporary mutation.
inline std::optional<std::pair<std::size_t,std::size_t>> library_name_span(
        std::string_view source,std::string_view function) {
    if(!open_battery_identifier(function)||function=="main"||
       function.starts_with("lgaed_open_battery_"))return {};
    const auto scanned=boolean_battery_detail::tokens(source);
    if(!scanned)return {};
    const auto& tokens=*scanned;
    // This method is compiled again under a private wrapper. A source that
    // observes compile-time file, position, function or build metadata could
    // use those differences to pass only the independent test compilation.
    // Process-termination calls could also bypass the generated witness.
    // Test actual lexical identifiers, never comments or string contents.
    constexpr std::string_view environment_sensitive[]{
        "__LINE__","__FILE__","__FILEW__","__BASE_FILE__",
        "__COUNTER__","__INCLUDE_LEVEL__","__DATE__","__TIME__",
        "__TIMESTAMP__","__PRETTY_FUNCTION__","__FUNCSIG__",
        "__FUNCDNAME__","__builtin_LINE","__builtin_COLUMN",
        "__builtin_FILE","__builtin_FUNCTION",
        "__builtin_source_location","source_location"
    };
    constexpr std::string_view process_termination[]{
        "exit","_Exit","_exit","quick_exit","ExitProcess",
        "TerminateProcess","TerminateJobObject","ExitThread",
        "_endthread","_endthreadex","_c_exit","abort","terminate","raise"
    };
    for(const auto& item:tokens) {
        if(!item.value.starts_with("id:"))continue;
        const std::string_view name(item.value.data()+3,item.value.size()-3);
        if(std::ranges::find(environment_sensitive,name)!=
                std::end(environment_sensitive) ||
           std::ranges::find(process_termination,name)!=
                std::end(process_termination))
            return {};
    }
    const std::string function_token="id:"+std::string(function);
    const std::string renamed_token="id:lgaed_open_battery_original_"+std::string(function);
    const auto after_parameters=[&](std::size_t first)
            ->std::optional<std::size_t> {
        if(first>=tokens.size()||tokens[first].value!="op:(")return {};
        auto at=first+1;
        std::vector<std::string> identifiers;
        for(unsigned parameter=0;parameter<4;++parameter) {
            if(at<tokens.size()&&tokens[at].value=="id:const") {
                if(parameter==3)return {}; // const int& cannot receive the adjustment.
                ++at;
            }
            if(at>=tokens.size()||tokens[at++].value!="id:int")return {};
            if(at<tokens.size()&&tokens[at].value=="id:const") {
                if(parameter==3)return {}; // int const& is equally read-only.
                ++at;
            }
            // The fourth argument MUST be an unqualified writable int&.
            // Its output is observed independently after the library call.
            if(parameter==3) {
                if(at>=tokens.size()||tokens[at++].value!="op:&")return {};
            }
            if(at>=tokens.size()||!tokens[at].value.starts_with("id:"))
                return {};
            auto name=tokens[at++].value.substr(3);
            if(!open_battery_identifier(name)||
               std::ranges::find(identifiers,name)!=identifiers.end())
                return {};
            identifiers.push_back(std::move(name));
            if(parameter!=3) {
                if(at>=tokens.size()||tokens[at++].value!="op:,")return {};
            }
        }
        if(at>=tokens.size()||tokens[at].value!="op:)")return {};
        return at+1;
    };
    std::optional<std::pair<std::size_t,std::size_t>> found;
    std::size_t depth=0;
    for(std::size_t at=0;at<tokens.size();++at) {
        const auto& token=tokens[at];
        if(token.value==renamed_token)return {};
        if(token.value.starts_with("directive:")&&depth)return {};
        if(token.value==function_token&&at>0&&
           at+1<tokens.size()&&tokens[at+1].value=="op:("&&
           tokens[at-1].value=="id:bool") {
            if(depth||(at>1&&tokens[at-2].value!="op:;"&&
               tokens[at-2].value!="op:}"&&tokens[at-2].value!="op:%>"&&
               !tokens[at-2].value.starts_with("directive:")))return {};
            const auto after_index=after_parameters(at+1);
            if(!after_index)return {};
            if(*after_index>=tokens.size())return {};
            const auto& after=tokens[*after_index].value;
            if(after!="op:;") {
                if(after!="op:{"&&after!="op:<%")return {};
                if(found)return {};
                found=std::pair{token.begin,token.end};
            }
        }
        if(token.value=="op:{"||token.value=="op:<%") {
            if(++depth>4096)return {};
        }else if(token.value=="op:}"||token.value=="op:%>") {
            if(!depth)return {};
            --depth;
        }
    }
    if(depth)return {};
    return found;
}
} // namespace open_battery_detail

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
    const auto location=open_battery_detail::library_name_span(original,function);
    if(!location)return {};
    const auto [name_begin,name_end]=*location;
    const std::string renamed="lgaed_open_battery_original_"+std::string(function);
    std::string result="#include <charconv>\n#include <iostream>\n#include <limits>\n";
    result+=original.substr(0,name_begin);
    result+=renamed;
    result+=original.substr(name_end);
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
        "    lgaed_open_battery_emit(\"LGAED_OPEN_BATTERY_RESULT:\", "
        "sizeof(\"LGAED_OPEN_BATTERY_RESULT:\") - 1, "
        "lgaed_open_battery_result ? 1 : 0);\n"
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
    std::vector<unsigned char> comparison_results;
    std::string visible;
};
// This stricter decoder is used only for the numbered-comparison objective.
// The legacy call decoder above remains available to library-only consumers.
// Private records can follow partial student output, but must be complete and
// paired: BEGIN(number), one CALL(tuple), RESULT(0/1), END(the same number).
inline std::optional<OpenBatteryTrace> open_battery_parse_trace(std::string_view all) {
    constexpr std::string_view prefix="LGAED_OPEN_BATTERY_";
    constexpr std::string_view begin_marker="LGAED_OPEN_BATTERY_BEGIN:";
    constexpr std::string_view call_marker="LGAED_OPEN_BATTERY_CALL:";
    constexpr std::string_view result_marker="LGAED_OPEN_BATTERY_RESULT:";
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
    std::optional<bool> current_result;
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
        }else if(line.starts_with(result_marker)) {
            if(!current_number||!current_call||current_result.has_value())return {};
            const auto truth=line.substr(result_marker.size());
            if(truth!="0"&&truth!="1")return {};
            current_result=truth=="1";
        }else if(line.starts_with(end_marker)) {
            const auto ended=number(line.substr(end_marker.size()));
            if(!ended||!current_number||*ended!=*current_number||
               !current_call||!current_result.has_value())return {};
            result.calls.push_back(*current_call);
            result.case_numbers.push_back(*current_number);
            result.comparison_results.push_back(*current_result);
            if(result.calls.size()>34)return {};
            current_number.reset();
            current_call.reset();
            current_result.reset();
        }else return {};
        position=end+1;
    }
    if(current_number||current_call||current_result)return {};
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
    if(baseline.comparison_results.size()!=baseline.calls.size()||
       std::ranges::any_of(baseline.comparison_results,
           [](unsigned char result){return result!=1u;}))
        return {false,"Cada caso original debe devolver una comparación correcta.",{}};
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
           run.comparison_results.size()!=run.calls.size()||
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
            // Only the independently mutated target may fail its comparison.
            // A fabricated failure count or fixed nonzero return is insufficient.
            const unsigned char should_match=
                static_cast<unsigned char>(call!=*required.target);
            if(run.comparison_results[index]!=should_match)
                return {false,"El comparador no detectó exactamente la mutación del caso.",baseline.added};
        }
    }
    return {true,"Los casos originales y el nuevo conservan sus números y detectan las mutaciones.",baseline.added};
}
} // namespace lgaed::curriculum
