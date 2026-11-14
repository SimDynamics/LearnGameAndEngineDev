#pragma once

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
enum class OpenBatteryMutation { baseline, invert_acceptance, corrupt_destination };
struct OpenBatteryProbeSpec {
    std::string library_path, test_path, function_name, test_target;
    std::vector<OpenBatteryCall> original_cases;
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
        open_battery_identifier(spec.test_target)&&
        open_battery_originals_valid(spec.original_cases);
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
       ((mutation==OpenBatteryMutation::baseline)!=!target.has_value()))return {};
    const std::string declaration="bool "+std::string(function)+
        "(int valor, int inferior, int superior, int& ajuste) {";
    const auto position=original.find(declaration);
    if(position==original.npos||
       original.find(declaration,position+declaration.size())!=original.npos)return {};
    const std::string renamed="lgaed_open_battery_original_"+std::string(function);
    if(original.find(renamed)!=original.npos)return {};
    std::string result="#include <iostream>\n";
    result+=original.substr(0,position);
    result+="bool "+renamed+"(int valor, int inferior, int superior, int& ajuste) {";
    result+=original.substr(position+declaration.size());
    result+="\nbool "+std::string(function)+
        "(int valor, int inferior, int superior, int& ajuste) {\n";
    result+="    const int anterior = ajuste;\n";
    result+="    std::cerr << \"LGAED_OPEN_BATTERY_CALL:\" << valor << ',' << inferior"
            " << ',' << superior << ',' << anterior << '\\n';\n";
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
        else
            result+="    if (seleccionado) ajuste = ajuste == 0 ? 1 : 0;\n";
    }
    result+="    return aceptado;\n}\n";
    return result;
}
inline std::optional<std::vector<OpenBatteryCall>> open_battery_parse_calls(
        std::string_view stderr_bytes) {
    constexpr std::string_view marker="LGAED_OPEN_BATTERY_CALL:";
    std::vector<OpenBatteryCall> calls;
    for(std::size_t offset=0;offset<stderr_bytes.size();) {
        auto end=stderr_bytes.find('\n',offset);
        if(end==stderr_bytes.npos)end=stderr_bytes.size();
        auto line=stderr_bytes.substr(offset,end-offset);
        if(line.ends_with('\r'))line.remove_suffix(1);
        if(line.starts_with(marker)) {
            line.remove_prefix(marker.size());
            int value[4]{};
            for(unsigned i=0;i<4;++i) {
                const auto comma=line.find(',');
                const auto field=line.substr(0,comma);
                if(field.empty())return {};
                auto [ptr,ec]=std::from_chars(field.data(),
                    field.data()+field.size(),value[i]);
                if(ec!=std::errc{}||ptr!=field.data()+field.size())return {};
                if(i==3) {
                    if(comma!=line.npos)return {};
                }else{
                    if(comma==line.npos)return {};
                    line.remove_prefix(comma+1);
                }
            }
            calls.push_back({value[0],value[1],value[2],value[3]});
            if(calls.size()>34)return {};
        }
        offset=end<stderr_bytes.size()?end+1:end;
    }
    return calls;
}
// The runner supplies the baseline followed by one inversion of each old
// call, then one acceptance and one destination mutation of the new call.
// This core does not infer completion from stdout or a forged "Fallos=0".
inline OpenBatteryVerdict open_battery_verify(
        std::span<const OpenBatteryCall> original,
        std::span<const OpenBatteryRun> runs,std::string_view program_frame) {
    if(!open_battery_originals_valid(original))
        return {false,"Casos originales vacíos, repetidos o excesivos.",{}};
    if(program_frame.empty()||runs.size()!=original.size()+3)
        return {false,"Faltan mutaciones independientes compiladas.",{}};
    for(const auto& run:runs)
        if(run.program_frame!=program_frame||!run.compiled||!run.executed)
            return {false,"La ejecución pertenece a otra revisión o no terminó.",{}};
    if(runs.front().exit_code!=0||
       runs.front().mutation!=OpenBatteryMutation::baseline||
       runs.front().target.has_value())
        return {false,"La biblioteca original no pasó su ejecución sin mutación.",{}};
    auto baseline=runs.front().calls;
    const auto order=[](const auto& a,const auto& b) {
        return std::tie(a.value,a.lower,a.upper,a.initial)<
               std::tie(b.value,b.lower,b.upper,b.initial);
    };
    std::ranges::sort(baseline,order);
    for(const auto& item:original)
        if(std::ranges::count(baseline,item)!=1)
            return {false,"Una llamada histórica falta o está duplicada.",{}};
    if(baseline.size()!=original.size()+1)
        return {false,"Se exige exactamente una nueva llamada.",{}};
    std::optional<OpenBatteryCall> extra;
    for(const auto& item:baseline)
        if(std::ranges::find(original,item)==original.end())extra=item;
    if(!extra)return {false,"No se identificó la prueba añadida.",{}};
    for(const auto& item:original)
        if(item.value==extra->value&&item.lower==extra->lower&&
           item.upper==extra->upper)
            return {false,"La nueva terna repite una original.",{}};
    for(std::size_t i=1;i<runs.size();++i) {
        const auto required=i<=original.size()?
            OpenBatteryMutation::invert_acceptance:
            i==original.size()+1?
                OpenBatteryMutation::invert_acceptance:
                OpenBatteryMutation::corrupt_destination;
        const auto target=i<=original.size()?original[i-1]:*extra;
        if(runs[i].mutation!=required||runs[i].target!=target)
            return {false,"La mutación no corresponde a su caso original o nuevo.",extra};
        if(runs[i].exit_code!=1)
            return {false,"Una mutación no cambió el resultado final.",extra};
        auto calls=runs[i].calls;
        std::ranges::sort(calls,order);
        if(calls!=baseline)
            return {false,"Se omitieron llamadas o se interrumpió la batería.",extra};
    }
    return {true,"Los casos originales y el nuevo detectan las mutaciones.",extra};
}
} // namespace lgaed::curriculum
