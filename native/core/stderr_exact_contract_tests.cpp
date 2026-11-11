#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reject(std::string_view json, const char* message) {
    try {
        (void)lgaed::windows::parse_lesson_json(json);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        using namespace lgaed::curriculum;
        using lgaed::windows::parse_lesson_json;

        const auto lesson = parse_lesson_json(R"json({
            "id":"stderr_exact_contract",
            "title":"Diagnóstico de salida exacta",
            "pages":[{"blocks":[{
                "type":"cpp_compiler",
                "initialCode":"int main(){}",
                "outputCheck":{"cases":[{
                    "standardInput":"2x\n",
                    "outputs":["Resumen: validos=0\n"],
                    "exitCode":1,
                    "standardErrorExact":"Linea=1:FORMATO\n",
                    "explanation":"La petición fue rechazada."
                }]}
            }]}]
        })json");
        const auto& block = lesson.pages.front().blocks.front();
        const auto& check_goal = *block.output_check;
        check(check_goal.cases.front().standard_error_exact ==
                std::optional<std::string>{"Linea=1:FORMATO\n"},
            "The UTF-8 exact stderr criterion was not loaded correctly.");
        FreshExecution current{13,true,false,1,"2x\n","Resumen: validos=0\n",{}};
        current.execution_completed = true;
        current.standard_error = "Linea=1:FORMATO\n";
        auto decision = evaluate_output_check(check_goal,current,13);
        check(decision.verdict==OutputVerdict::correct && decision.stdout_correct &&
              decision.stderr_correct && decision.exit_code_correct,
              "Correct normal exit and exact stderr were not credited.");

        for (const std::string malformed : {"", "Linea=1:FORMATO",
                     "Linea=1:FORMATO\r\n", "Linea=2:FORMATO\n",
                     "Linea=1:FORMATO\nAviso adicional\n"}) {
            current.standard_error = malformed;
            decision = evaluate_output_check(check_goal,current,13);
            check(decision.verdict==OutputVerdict::incorrect && decision.stdout_correct &&
                  !decision.stderr_correct &&
                  decision.feedback.find("exactamente")!=std::string::npos,
                  "Different runtime stderr was accepted as exact.");
        }
        current.standard_error="Linea=1:FORMATO\n";
        current.exit_code=0;
        check(evaluate_output_check(check_goal,current,13).verdict!=OutputVerdict::correct,
              "Wrong process exit status passed exact stderr.");
        current.exit_code=1;
        current.execution_completed=false;
        check(evaluate_output_check(check_goal,current,13).verdict==OutputVerdict::unavailable,
              "Abnormal termination was treated as a normal process result.");
        current.execution_completed=true;
        check(evaluate_output_check(check_goal,current,12).verdict==OutputVerdict::unavailable,
              "Stale execution was accepted after source revision.");

        auto prior=block;
        prior.output_check->cases.front().standard_error_exact.reset();
        check(reference_objective_frame(lesson,block)!=reference_objective_frame(lesson,prior),
              "An old output-only completion proof would be reused for exact stderr.");
        const auto legacy = parse_lesson_json(R"json({
            "id":"legacy_stdout","title":"Original",
            "pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main(){}",
            "outputCheck":{"cases":[{"standardInput":"","outputs":["OK"]}]}}]}]
        })json");
        FreshExecution legacy_execution{2,true,true,0,"","OK",{}};
        check(evaluate_output_check(*legacy.pages.front().blocks.front().output_check,
                                    legacy_execution,2).verdict==OutputVerdict::correct,
              "Existing stdout-only compiler goals changed behavior.");
        const auto empty_stderr = parse_lesson_json(R"json({
            "id":"empty_stderr","title":"Silence",
            "pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main(){}",
            "outputCheck":{"cases":[{"standardInput":"","outputs":["OK"],
            "standardErrorExact":""}]}}]}]
        })json");
        const auto& empty_goal = *empty_stderr.pages.front().blocks.front().output_check;
        check(evaluate_output_check(empty_goal,legacy_execution,2).verdict==OutputVerdict::correct,
              "An explicitly empty runtime stderr was rejected.");
        legacy_execution.standard_error="Unexpected error";
        check(evaluate_output_check(empty_goal,legacy_execution,2).verdict==OutputVerdict::incorrect,
              "Additional stderr was accepted with an explicitly empty criterion.");
        reject(R"json({
            "id":"bad_mix","title":"Conflict",
            "pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main(){}",
            "outputCheck":{"cases":[{"standardInput":"","outputs":["OK"],
            "standardError":"nonBlank","standardErrorExact":"X"}]}}]}]
        })json","Contradictory stderr modes were accepted.");
        reject(R"json({
            "id":"bad_type","title":"Wrong type",
            "pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main(){}",
            "outputCheck":{"cases":[{"standardInput":"","outputs":["OK"],
            "standardErrorExact":false}]}}]}]
        })json","Non-text stderr exact criterion was accepted.");

        std::cout << "PASS: native Windows exact runtime stderr, normal nonzero exit, "
                     "old proof isolation, empty stderr and parser rejection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
