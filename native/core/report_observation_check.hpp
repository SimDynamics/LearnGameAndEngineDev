#pragma once

#include "core/code_format_compat.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

enum class ReportMode { complete, differences };

struct ReportExpectation {
    std::string standard_output, standard_error;
    int exit_code = 0;
};

namespace report_detail {
inline bool whitespace(char c) {
    return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';
}
inline std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    for(std::size_t i=0;i<line.size();) {
        while(i<line.size()&&whitespace(line[i]))++i;
        const auto first=i;
        while(i<line.size()&&!whitespace(line[i]))++i;
        if(i>first)result.push_back(line.substr(first,i-first));
    }
    return result;
}
inline std::optional<int> integer(std::string_view token) {
    if(token.starts_with('+'))token.remove_prefix(1);
    if(token.empty())return {};
    int value=0;
    const auto result=std::from_chars(token.data(),token.data()+token.size(),value,10);
    if(result.ec!=std::errc{}||result.ptr!=token.data()+token.size())return {};
    return value;
}
// The MSVC CRT may translate LF to CRLF on a redirected text stream. No
// trimming, case-folding or alternate spelling is otherwise accepted.
inline bool exact_or_native_crlf(std::string_view expected,std::string_view actual) {
    if(expected==actual)return true;
    if(expected.find('\r')!=expected.npos||expected.find('\n')==expected.npos)return false;
    std::string native;
    native.reserve(expected.size()+8);
    for(const char c:expected){if(c=='\n')native+='\r';native+=c;}
    return native==actual;
}
} // namespace report_detail

// The independent oracle receives observations, never re-runs their producer.
// Domain: at most twelve physical lines, ASCII, signed representable int,
// three whitespace-separated fields; a trailing unterminated line counts.
inline std::optional<ReportExpectation> expected_observation_report(
        ReportMode mode,std::string_view input) {
    if(input.size()>4*1024*1024)return {};
    for(const unsigned char c:input)if(c==0||c>127)return {};
    ReportExpectation report;
    std::size_t line_number=0,valid=0,equal=0,different=0,malformed=0;
    for(std::size_t first=0;first<input.size();) {
        if(++line_number>12)return {};
        const auto end=input.find('\n',first);
        const auto line=input.substr(first,end==input.npos?input.npos:end-first);
        const auto fields=report_detail::fields(line);
        bool accepted=fields.size()==3;
        std::optional<int> expected,actual;
        if(accepted){
            expected=report_detail::integer(fields[1]);
            actual=report_detail::integer(fields[2]);
            accepted=expected.has_value()&&actual.has_value();
        }
        if(!accepted){
            ++malformed;
            report.standard_error+="Linea="+std::to_string(line_number)+":FORMATO\n";
        }else{
            ++valid;
            const bool match=*expected==*actual;
            if(match)++equal;else ++different;
            if(mode==ReportMode::complete||!match) {
                report.standard_output+="Linea="+std::to_string(line_number)+" Caso="+
                    std::string(fields[0])+" Esperado="+std::to_string(*expected)+
                    " Obtenido="+std::to_string(*actual)+(match?" COINCIDE\n":" DIFIERE\n");
            }
        }
        if(end==input.npos)break;
        first=end+1;
    }
    report.standard_output+="Resumen: validos="+std::to_string(valid)+
        ";coinciden="+std::to_string(equal)+";difieren="+std::to_string(different)+
        ";formato="+std::to_string(malformed)+"\n";
    report.exit_code=(different||malformed)?1:0;
    return report;
}

inline bool matches_observation_report(ReportMode mode,std::string_view input,
        std::string_view stdout_bytes,std::string_view stderr_bytes,int exit_code) {
    const auto expected=expected_observation_report(mode,input);
    return expected&&expected->exit_code==exit_code&&
        report_detail::exact_or_native_crlf(expected->standard_output,stdout_bytes)&&
        report_detail::exact_or_native_crlf(expected->standard_error,stderr_bytes);
}

// U3L8 may change main's processing and presentation only. Preserve the
// parser implementations, includes, original main signature and final scope.
inline bool report_main_only_edit(std::string_view original,std::string_view candidate) {
    constexpr std::string_view main_marker="int main() {";
    const auto at=original.find(main_marker);
    if(at==original.npos||original.find(main_marker,at+1)!=original.npos)return false;
    const auto prefix=original.substr(0,at+main_marker.size());
    constexpr std::string_view suffix="\n}\n";
    if(!original.ends_with(suffix)||!candidate.starts_with(prefix)||
        !candidate.ends_with(suffix)||candidate.size()<prefix.size()+suffix.size())return false;
    const auto body=candidate.substr(prefix.size(),candidate.size()-prefix.size()-suffix.size());
    return code_format::confined_function_body_edit(body);
}

// Discriminating fresh inputs, separate from the student's three public
// cases: duplicate labels on either side of malformed lines, empty/blank
// rows, partial numbers, extra columns, adjacent numeric characters and a
// final row without LF. Expected reports are computed independently above.
inline constexpr std::array<std::string_view,2> observation_probe_inputs{{
    "Z 0 0\nZ 1 2\n\nH 3 3\nQ 2x 2\nH 3 2\nG 3 1\n",
    "P -2 -2\nP 1 1\nX 2 2 2\nG 1-1 0\nF 7 7\n  \nH +2 2\nN 3 4\nZ 0 0"
}};

} // namespace lgaed::curriculum
