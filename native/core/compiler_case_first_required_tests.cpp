// Portable checks of the actual C++ output evaluator used by curricular
// compiler case revisions. No fixture or student profile is modified.
// These checks do not replace Windows receipt migration or GUI interaction.
#include "core/lesson.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::curriculum;

void require(bool condition, std::string_view why) {
    if(!condition) throw std::runtime_error(std::string(why));
}

OutputCase authored(std::string input, std::string output,
                    std::optional<std::string> target = std::nullopt) {
    OutputCase item;
    item.standard_input = std::move(input);
    item.outputs = {std::move(output)};
    item.executable = std::move(target);
    item.explanation = "Comprobación curricular de un caso.";
    return item;
}

FreshExecution observed(std::string input, std::string output,
                        int exit_code = 0,
                        std::optional<std::string> target = std::nullopt) {
    FreshExecution run;
    run.source_revision = 23;
    run.compilation_succeeded = true;
    run.execution_succeeded = exit_code == 0;
    run.execution_completed = true;
    run.exit_code = exit_code;
    run.standard_input = std::move(input);
    run.standard_output = std::move(output);
    run.executable = std::move(target);
    return run;
}

OutputVerdict evaluate(const OutputCheck& goal, FreshExecution run) {
    return evaluate_output_check(goal, run, 23).verdict;
}

void distinct_first_and_optional_cases() {
    // A historical batch did both operations in one process. A new client
    // runs each complete independent input on its own, not partial output.
    OutputCheck goal;
    goal.cases = {authored("1\n", "1:1:1\n"),
                  authored("0\n", "0:0:91\n")};
    require(evaluate(goal, observed("1\n", "1:1:1\n")) == OutputVerdict::correct,
            "The actual required first case was not accepted");
    require(evaluate(goal, observed("0\n", "0:0:91\n")) == OutputVerdict::correct,
            "The optional complete case stopped being available");
    require(evaluate(goal, observed("1\n", "1:0:91\n")) == OutputVerdict::incorrect,
            "A broken starter satisfied the discriminating first case");
    require(evaluate(goal, observed("1\n0\n", "1:1:1\n0:0:91\n"))
                == OutputVerdict::unavailable,
            "A historical aggregate impersonated a new independent input");
    require(evaluate_output_check(goal, observed("1\n", "1:1:1\n"), 24).verdict
                == OutputVerdict::unavailable,
            "A stale source revision was graded as the fresh first case");
}

void keep_stateful_session_intact() {
    OutputCheck goal;
    goal.cases = {
        authored("0 -1 5 21 4\n",
                 "Aceptados=3 Rechazados=2 Total=3\nCliente: OK\n"),
        authored("4 5\n", "Aceptados=2 Rechazados=0 Total=3\nCliente: OK\n")};
    require(evaluate(goal, observed("0 -1 5 21 4\n",
            "Aceptados=3 Rechazados=2 Total=3\nCliente: OK\n")) == OutputVerdict::correct,
            "A complete stateful first session was not selectable");
    require(evaluate(goal, observed("0 -1 5 21 4\n",
            "Aceptados=3 Rechazados=2 Total=81\nCliente: OK\n")) == OutputVerdict::incorrect,
            "The accumulator defect passed the complete session");
    require(evaluate(goal, observed("-1\n", "Rechazados=1\n")) == OutputVerdict::unavailable,
            "One rejected operation impersonated a full stateful session");
}

void target_and_normal_nonzero_exit() {
    OutputCheck targets;
    targets.cases = {
        authored("4 7\n", "3", std::string("consultar")),
        authored("4 7\n", "3", std::string("comprobar"))};
    require(evaluate(targets, observed("4 7\n", "3", 0, std::string("consultar")))
                == OutputVerdict::correct, "First CMake target was not selected");
    require(evaluate(targets, observed("4 7\n", "3", 0, std::string("comprobar")))
                == OutputVerdict::correct, "Second CMake target was not selected");
    require(evaluate(targets, observed("4 7\n", "3"))
                == OutputVerdict::unavailable,
            "Output alone credited a CMake target that was never executed");

    OutputCheck rejection;
    auto item = authored("-1 0 21 5\n", "Cajas=0\nCajas=2\n");
    item.exit_code = 1;
    item.standard_error_exact = "Petición 1: -1\nPetición 3: 21\n";
    rejection.cases = {item};
    auto correct = observed("-1 0 21 5\n", "Cajas=0\nCajas=2\n", 1);
    correct.standard_error = *item.standard_error_exact;
    require(evaluate(rejection, correct) == OutputVerdict::correct,
            "A normal nonzero exit with its exact diagnostic was rejected");
    auto no_diagnostic = correct;
    no_diagnostic.standard_error.clear();
    require(evaluate(rejection, no_diagnostic) == OutputVerdict::incorrect,
            "Matching stdout hid a missing required diagnostic");
    auto wrong_exit = correct;
    wrong_exit.exit_code = 0;
    require(evaluate(rejection, wrong_exit) == OutputVerdict::incorrect,
            "Matching stdout and stderr hid an incorrect exit code");
    auto crashed = correct;
    crashed.execution_completed = false;
    require(evaluate(rejection, crashed) == OutputVerdict::unavailable,
            "A failed process impersonated normal nonzero termination");
}

void receipts_need_the_same_evaluated_requirements() {
    auto original = authored("4 7\n", "3", std::string("consultar"));
    original.explanation = "Explicación anterior.";
    auto cosmetic = original;
    cosmetic.explanation = "Explicación revisada.";
    cosmetic.review_feedback.push_back({{"0"}, "Consulta otro caso."});
    require(same_output_case_requirements(original, cosmetic),
            "New teaching prose invalidated an unchanged executable objective");
    auto changed = original;
    changed.standard_input = "7 7\n";
    require(!same_output_case_requirements(original, changed),
            "An execution was reused for another input");
    changed = original;
    changed.outputs = {"0"};
    require(!same_output_case_requirements(original, changed),
            "Matching input hid a different expected output");
    changed = original;
    changed.executable = "comprobar";
    require(!same_output_case_requirements(original, changed),
            "A receipt crossed CMake targets");
    changed = original;
    changed.exit_code = 1;
    require(!same_output_case_requirements(original, changed),
            "A successful receipt crossed an exit-code requirement");
    changed = original;
    changed.standard_error = StandardErrorExpectation::non_blank;
    require(!same_output_case_requirements(original, changed),
            "A stdout-only receipt satisfied a new stderr condition");
    changed = original;
    changed.standard_error_exact = "";
    require(!same_output_case_requirements(original, changed),
            "Absence of a stderr check was treated as explicit empty stderr");
    changed = original;
    changed.balance_three_pairs = true;
    require(!same_output_case_requirements(original, changed),
            "A fixed input borrowed credit from a variable-input objective");
}

} // namespace

int main() {
    try {
        distinct_first_and_optional_cases();
        keep_stateful_session_intact();
        target_and_normal_nonzero_exit();
        receipts_need_the_same_evaluated_requirements();
        std::cout << "PASS portable compiler cases: first/optional input, "
                     "indivisible sessions, CMake targets, runtime channels "
                     "and requirement-bound historical reuse.\n";
    } catch(const std::exception& e) {
        std::cerr << "FAIL portable compiler cases: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
