#include "core/report_observation_check.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::ReportExpectation;
using lgaed::curriculum::ReportMode;
using lgaed::curriculum::expected_observation_report;
using lgaed::curriculum::matches_observation_report;

void require(bool value,std::string_view reason) {
    if(!value)throw std::runtime_error(std::string(reason));
}
void matches(ReportMode mode,std::string_view input,
        std::string_view output,std::string_view error,int exit_code) {
    const auto result=expected_observation_report(mode,input);
    require(result.has_value(),"Report oracle refused an original authored input");
    require(result->standard_output==output&&result->standard_error==error&&
            result->exit_code==exit_code,
        "The independently authored observation expectation changed");
    require(matches_observation_report(mode,input,output,error,exit_code),
        "An exact report was rejected by its runtime evaluator");
}
std::string as_crlf(std::string_view original) {
    std::string changed;
    for(const auto character:original) {
        if(character=='\n')changed+='\r';
        changed+=character;
    }
    return changed;
}

void exact_original_reports() {
    constexpr std::string_view prepared_complete=
        "A 0 0\nA 2 1\nB -1 -1\nA 2 1\nA 1-1\n"
        "C;=lote 0 1\nC;=lote 0 1\nA 1 -1\n";
    constexpr std::string_view expected_complete=
        "Linea=1 Caso=A Esperado=0 Obtenido=0 COINCIDE\n"
        "Linea=2 Caso=A Esperado=2 Obtenido=1 DIFIERE\n"
        "Linea=3 Caso=B Esperado=-1 Obtenido=-1 COINCIDE\n"
        "Linea=4 Caso=A Esperado=2 Obtenido=1 DIFIERE\n"
        "Linea=6 Caso=C;=lote Esperado=0 Obtenido=1 DIFIERE\n"
        "Linea=7 Caso=C;=lote Esperado=0 Obtenido=1 DIFIERE\n"
        "Linea=8 Caso=A Esperado=1 Obtenido=-1 DIFIERE\n"
        "Resumen: validos=7;coinciden=2;difieren=5;formato=1\n";
    constexpr std::string_view complete_errors="Linea=5:FORMATO\n";
    matches(ReportMode::complete,prepared_complete,expected_complete,complete_errors,1);

    constexpr std::string_view prepared_differences=
        "N 0 0\nR -1 -1\nR 0 -1\nP 3\nR 0 -1\nZ 9 9\n";
    constexpr std::string_view expected_differences=
        "Linea=3 Caso=R Esperado=0 Obtenido=-1 DIFIERE\n"
        "Linea=5 Caso=R Esperado=0 Obtenido=-1 DIFIERE\n"
        "Resumen: validos=5;coinciden=3;difieren=2;formato=1\n";
    matches(ReportMode::differences,prepared_differences,expected_differences,
        "Linea=4:FORMATO\n",1);

    // A final physical line without LF must not be discarded; a completely
    // empty file has no malformed phantom row.
    constexpr std::string_view unclosed="0 0 0\nN -2 -2";
    matches(ReportMode::complete,unclosed,
        "Linea=1 Caso=0 Esperado=0 Obtenido=0 COINCIDE\n"
        "Linea=2 Caso=N Esperado=-2 Obtenido=-2 COINCIDE\n"
        "Resumen: validos=2;coinciden=2;difieren=0;formato=0\n","",0);
    matches(ReportMode::differences,unclosed,
        "Resumen: validos=2;coinciden=2;difieren=0;formato=0\n","",0);
    for(const auto mode:{ReportMode::complete,ReportMode::differences})
        matches(mode,"","Resumen: validos=0;coinciden=0;difieren=0;formato=0\n","",0);

    // C++ std::getline accepts CRLF without treating the trailing CR as an
    // observation. The OUTPUT remains authored LF, or CRT-converted CRLF.
    matches(ReportMode::complete,"N 0 0\r\n",
        "Linea=1 Caso=N Esperado=0 Obtenido=0 COINCIDE\n"
        "Resumen: validos=1;coinciden=1;difieren=0;formato=0\n","",0);
}

void boundaries_and_malformed_rows() {
    constexpr std::string_view varied=
        "Z 0 0\nZ 1 2\n\nH 3 3\nQ 2x 2\nH 3 2\nG 3 1\n";
    matches(ReportMode::complete,varied,
        "Linea=1 Caso=Z Esperado=0 Obtenido=0 COINCIDE\n"
        "Linea=2 Caso=Z Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=4 Caso=H Esperado=3 Obtenido=3 COINCIDE\n"
        "Linea=6 Caso=H Esperado=3 Obtenido=2 DIFIERE\n"
        "Linea=7 Caso=G Esperado=3 Obtenido=1 DIFIERE\n"
        "Resumen: validos=5;coinciden=2;difieren=3;formato=2\n",
        "Linea=3:FORMATO\nLinea=5:FORMATO\n",1);
    matches(ReportMode::differences,varied,
        "Linea=2 Caso=Z Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=6 Caso=H Esperado=3 Obtenido=2 DIFIERE\n"
        "Linea=7 Caso=G Esperado=3 Obtenido=1 DIFIERE\n"
        "Resumen: validos=5;coinciden=2;difieren=3;formato=2\n",
        "Linea=3:FORMATO\nLinea=5:FORMATO\n",1);

    // Signed limits are normal int values; overflow is FORMATO, and later
    // records must still be counted. Tokens like "1-1" are NOT two ints.
    constexpr std::string_view integer_limits=
        "V -2147483648 -2147483648\n"
        "W 2147483647 2147483647\n"
        "X 2147483648 0\n"
        "Y -2147483649 0\n"
        "F +2 2\n"
        "G 1-1 0";
    matches(ReportMode::differences,integer_limits,
        "Resumen: validos=3;coinciden=3;difieren=0;formato=3\n",
        "Linea=3:FORMATO\nLinea=4:FORMATO\nLinea=6:FORMATO\n",1);

    constexpr std::string_view nine_lines=
        "P -2 -2\nP 1 1\nX 2 2 2\nG 1-1 0\nF 7 7\n  \n"
        "H +2 2\nN 3 4\nZ 0 0";
    matches(ReportMode::differences,nine_lines,
        "Linea=8 Caso=N Esperado=3 Obtenido=4 DIFIERE\n"
        "Resumen: validos=6;coinciden=5;difieren=1;formato=3\n",
        "Linea=3:FORMATO\nLinea=4:FORMATO\nLinea=6:FORMATO\n",1);

    std::string twelve;
    for(int i=0;i<12;++i)twelve+="R 0 0\n";
    const auto within_limit=expected_observation_report(ReportMode::complete,twelve);
    require(within_limit&&within_limit->exit_code==0&&
        within_limit->standard_output.find(
            "Resumen: validos=12;coinciden=12;difieren=0;formato=0\n")!=
            std::string::npos,
        "The twelfth allowed physical line was lost");
    require(!expected_observation_report(ReportMode::complete,twelve+"R 0 0\n"),
        "A thirteenth physical line was accepted");
    require(!expected_observation_report(ReportMode::complete,"N \xC3\xA1 0\n"),
        "Non-ASCII text was accepted outside the authored test domain");
    require(!expected_observation_report(ReportMode::complete,
        std::string("R 0 0")+'\0'),
        "Embedded NUL was accepted as an observation");
}

void twelve_line_preservation_and_same_label_after_error() {
    // Twelve actual physical lines, including the last row without LF:
    // repeated labels are independent observations; no malformed record
    // suppresses later data or changes the numbering of its successors.
    constexpr std::string_view input=
        "X 0 0\n"                          // 1 match
        "X 1 2\n"                          // 2 different
        "\n"                               // 3 malformed
        "X 1 2\n"                          // 4 different
        "Q 0 0 9\n"                        // 5 fourth field
        "Q 0 1\n"                          // 6 different
        "Z 2147483647 2147483647\n"        // 7 match
        "Y -2147483648 -2147483648\n"      // 8 match
        "H +2 2\n"                         // 9 match
        "X 3 3\n"                          // 10 match
        " \t\n"                            // 11 blanks
        "X 1 1";                           // 12 match, no final LF
    constexpr std::string_view errors=
        "Linea=3:FORMATO\nLinea=5:FORMATO\nLinea=11:FORMATO\n";
    constexpr std::string_view summary=
        "Resumen: validos=9;coinciden=6;difieren=3;formato=3\n";
    const auto all=std::string(
        "Linea=1 Caso=X Esperado=0 Obtenido=0 COINCIDE\n"
        "Linea=2 Caso=X Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=4 Caso=X Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=6 Caso=Q Esperado=0 Obtenido=1 DIFIERE\n"
        "Linea=7 Caso=Z Esperado=2147483647 Obtenido=2147483647 COINCIDE\n"
        "Linea=8 Caso=Y Esperado=-2147483648 Obtenido=-2147483648 COINCIDE\n"
        "Linea=9 Caso=H Esperado=2 Obtenido=2 COINCIDE\n"
        "Linea=10 Caso=X Esperado=3 Obtenido=3 COINCIDE\n"
        "Linea=12 Caso=X Esperado=1 Obtenido=1 COINCIDE\n")+std::string(summary);
    const auto filtered=std::string(
        "Linea=2 Caso=X Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=4 Caso=X Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=6 Caso=Q Esperado=0 Obtenido=1 DIFIERE\n")+std::string(summary);
    matches(ReportMode::complete,input,all,errors,1);
    matches(ReportMode::differences,input,filtered,errors,1);
    require(matches_observation_report(
        ReportMode::differences,input,as_crlf(filtered),as_crlf(errors),1),
        "CRT line endings changed a complete 12-line input report");
    // A superficial solution that counts just the displayed differences
    // would fabricate validos=3 instead of preserving all nine valid rows.
    const auto wrong=std::string(
        "Linea=2 Caso=X Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=4 Caso=X Esperado=1 Obtenido=2 DIFIERE\n"
        "Linea=6 Caso=Q Esperado=0 Obtenido=1 DIFIERE\n"
        "Resumen: validos=3;coinciden=0;difieren=3;formato=3\n");
    require(!matches_observation_report(
        ReportMode::differences,input,wrong,errors,1),
        "A display-filtered count replaced the original number of observations");
    require(!matches_observation_report(
        ReportMode::differences,input,filtered,
        "Linea=3:FORMATO\nLinea=5:FORMATO\nLinea=12:FORMATO\n",1),
        "The final unclosed row was misreported as a malformed record");
}

void receipt_requires_same_output_error_and_exit() {
    constexpr std::string_view source="R 0 -1\n";
    const auto report=expected_observation_report(ReportMode::differences,source);
    require(report.has_value()&&report->exit_code==1&&
        report->standard_error.empty(),"Incorrect report base for receipt tests");
    require(matches_observation_report(ReportMode::differences,source,
        as_crlf(report->standard_output),"",1),
        "MSVC CRLF output must be equivalent to authored LF");
    require(!matches_observation_report(ReportMode::differences,source,
        report->standard_output,"",0),
        "The correct text with the wrong normal exit status earned credit");
    require(!matches_observation_report(ReportMode::differences,source,
        "Resumen: validos=1;coinciden=0;difieren=1;formato=0\n","",1),
        "Dropping an observed difference passed as an honest report");
    require(!matches_observation_report(ReportMode::differences,source,
        report->standard_output+" ","",1),
        "Appending whitespace altered the exact report without detection");
    require(!matches_observation_report(ReportMode::differences,source,
        report->standard_output,"unexpected\n",1),
        "An unobserved diagnostic passed an exact empty stderr objective");

    constexpr std::string_view malformed="R 0 -1\nR 2x 2\n";
    const auto with_error=expected_observation_report(ReportMode::differences,malformed);
    require(with_error.has_value()&&with_error->exit_code==1,
        "Incorrect malformed-report base");
    require(matches_observation_report(ReportMode::differences,malformed,
        as_crlf(with_error->standard_output),as_crlf(with_error->standard_error),1),
        "The true MSVC CRLF pair was not accepted");
    require(!matches_observation_report(ReportMode::differences,malformed,
        with_error->standard_output,"FORMATO\n",1),
        "The wrong diagnostic number or prefix earned credit");
}
} // namespace

int main() {
    try {
        exact_original_reports();
        boundaries_and_malformed_rows();
        twelve_line_preservation_and_same_label_after_error();
        receipt_requires_same_output_error_and_exit();
        std::cout<<"PASS U3L8 independent reports: original two editors, "
            "six observable cases, malformed lines, signed limits and receipts\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL U3L8 observation report tests: "<<error.what()<<'\n';
        return 1;
    }
}
