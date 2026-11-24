#include "core/lesson.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::curriculum;
std::size_t evaluated = 0;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

Block prediction(std::vector<std::string> answers,
                 TextComparison comparison = TextComparison::exact) {
    Block block;
    block.type = BlockType::output_prediction;
    block.answers = std::move(answers);
    block.output_comparison = comparison;
    block.explanation = "The authored output matches.";
    block.review_feedback = "Keep the exact output content.";
    return block;
}

void expect(const Block& block, std::string answer, bool correct, std::string_view context) {
    const auto canonical = block.answers;
    const auto original = answer;
    const auto result = evaluate_text_answer(block, answer);
    ++evaluated;
    check(result.correct == correct, context);
    check(result.feedback == (correct ? block.explanation : block.review_feedback),
          "A line-ending decision changed authored feedback");
    check(block.answers == canonical && answer == original,
          "Evaluation rewrote a canonical answer or the learner's original bytes");
}

std::string crlf(std::string_view text) {
    std::string result;
    for (const char character : text) {
        if (character == '\n') result += '\r';
        result += character;
    }
    return result;
}

void published_multiline_payloads() {
    // Independent core oracles for the four active exact predictions. Native
    // view QA separately loads their current JSON through the actual loader.
    constexpr std::array<std::string_view, 4> payloads{
        "Rojo\n\nVerde", "Resumen\nZona Oeste", "Rita 4\nRita: 7",
        "Modulo 02\nInicio: 4\nAhora: 9"};
    for (const auto payload : payloads) {
        const std::string value(payload);
        const auto block = prediction({value});
        check(!uses_single_line_answer(block), "A multiline prediction became a single-line control");
        expect(block, value, true, "The authored multiline output was rejected");
        expect(block, value + '\n', true, "One optional final LF was rejected");
        expect(block, value + "\r\n", true, "One optional final CRLF was rejected");
        expect(block, crlf(value), true, "Native CRLF line breaks were rejected");
        expect(block, crlf(value) + "\r\n", true, "Native CRLF plus one final terminator was rejected");
        expect(block, crlf(value) + '\n', true, "Mixed CRLF/LF line breaks changed the payload");
        for (const auto suffix : {"\n\n", "\r\n\r\n", "\n\r\n", "\r\n\n",
                                  "\r", " ", "\t", " \n", "\t\r\n"})
            expect(block, value + suffix, false, "An extra final line or significant suffix was accepted");
        expect(block, crlf(value) + "\r\n\r\n", false, "An extra native blank final line was accepted");
        for (const auto prefix : {" ", "\t", "\n", "\r\n"})
            expect(block, prefix + value, false, "Significant leading output was discarded");
        const auto first = value.find('\n');
        auto missing = value;
        missing.erase(first, 1);
        expect(block, missing, false, "An interior line break or blank line was discarded");
        auto extra = value;
        extra.insert(first, 1, '\n');
        expect(block, extra, false, "An extra interior blank line was accepted");
        auto spaced = value;
        spaced.insert(first, 1, ' ');
        expect(block, spaced, false, "A significant space before a line break was discarded");
        auto lone_cr = value;
        lone_cr[first] = '\r';
        expect(block, lone_cr, false, "A lone CR was reinterpreted as a line break");
        auto doubled_cr = value;
        doubled_cr.replace(first, 1, "\r\r\n");
        expect(block, doubled_cr, false, "A literal CR before native CRLF was discarded");
    }
    expect(prediction({"Rojo\n\nVerde"}), "Rojo\nVerde\n", false,
           "The final terminator replaced the required interior blank line");
}

void authored_terminators_and_significant_final_lines() {
    const auto terminated = prediction({"Rojo\n\nVerde\n"});
    for (const auto value : {"Rojo\n\nVerde", "Rojo\n\nVerde\n",
                            "Rojo\r\n\r\nVerde", "Rojo\r\n\r\nVerde\r\n"})
        expect(terminated, value, true, "An authored final terminator became mandatory");
    expect(terminated, "Rojo\n\nVerde\n\n", false,
           "An authored terminator allowed another blank final line");
    const auto native = prediction({"Rojo\r\n\r\nVerde\r\n"});
    expect(native, "Rojo\n\nVerde", true, "The authored CRLF terminator was not optional");
    expect(native, "Rojo\n\nVerde\n", true, "An authored CRLF payload rejected LF presentation");
    expect(native, "Rojo\n\nVerde\n\n", false, "CRLF authoring hid an extra final blank line");

    // Two authored terminal breaks include a significant blank line. Remove
    // only the authored terminator, then accept that payload with zero or one
    // final terminator; stripping both sides would reject the first valid form.
    const auto final_blank = prediction({"Rojo\nVerde\n\n"});
    expect(final_blank, "Rojo\nVerde\n", true, "A significant blank final line lost its no-terminator form");
    expect(final_blank, "Rojo\nVerde\n\n", true, "The authored blank final line was rejected");
    expect(final_blank, "Rojo\r\nVerde\r\n", true, "Native final blank-line content changed");
    expect(final_blank, "Rojo\r\nVerde\r\n\r\n", true, "Native authored final blank line was rejected");
    expect(final_blank, "Rojo\nVerde", false, "A required blank final line was discarded");
    expect(final_blank, "Rojo\nVerde\n\n\n", false, "An extra authored blank final line was accepted");

    const auto spaces = prediction({"  Rojo\t\n \nVerde \t\n"});
    expect(spaces, "  Rojo\t\n \nVerde \t", true, "Significant spaces/tabs required a final terminator");
    expect(spaces, "  Rojo\t\r\n \r\nVerde \t\r\n", true, "CRLF changed significant spaces/tabs");
    expect(spaces, "  Rojo\t\n\nVerde \t", false, "A space-only interior line was treated as empty");
    expect(spaces, "  Rojo\t\n \nVerde\n", false, "Trailing spaces/tabs in the payload were trimmed");
    expect(spaces, "Rojo\t\n \nVerde \t", false, "Leading payload spaces were trimmed");
    const auto unicode = prediction({"Área\nniño"});
    expect(unicode, "Área\r\nniño\r\n", true, "UTF-8 text changed while normalizing native line breaks");
    expect(unicode, "Area\nniño\n", false, "Line-ending tolerance accepted different UTF-8 text");
}

void alternatives_and_empty_contracts() {
    const auto choices = prediction({"Rojo\nVerde\n", "Azul\r\nOcre"});
    expect(choices, "Rojo\nVerde", true, "The first alternative lost its optional terminator");
    expect(choices, "Azul\nOcre\n", true, "The second alternative lost its independent payload");
    expect(choices, "Rojo\nOcre\n", false, "Parts of different alternatives were combined");
    const auto mixed = prediction({"Una línea", "Dos\nlíneas"});
    check(!uses_single_line_answer(mixed), "A multiline alternative selected a single-line control");
    expect(mixed, "Una línea\n", true, "A multiline control rejected its shorter alternative's terminator");
    expect(mixed, "Una línea\n\n", false, "The shorter alternative accepted an extra blank line");
    const auto no_answers = prediction({});
    expect(no_answers, "", false, "Missing answers fabricated an empty accepted payload");
    expect(no_answers, "\n", false, "Missing answers accepted a line terminator");
    const auto one_empty_line = prediction({"\n"});
    expect(one_empty_line, "", true, "The authored empty line's single terminator became mandatory");
    expect(one_empty_line, "\r\n", true, "An empty line rejected its native terminator");
    expect(one_empty_line, "\n\n", false, "An empty payload accepted an additional blank line");
}

void unchanged_single_line_trimmed_fill_and_runtime_policies() {
    const auto one_line = prediction({"Zona inicial"});
    check(uses_single_line_answer(one_line), "The single-line Enter policy changed");
    expect(one_line, "Zona inicial", true, "An exact single-line answer was rejected");
    for (const auto value : {"Zona inicial\n", "Zona inicial\r\n", "Zona inicial ",
                            " Zona inicial", "Zona  inicial", "Zona inicial\t"})
        expect(one_line, value, false, "Multiline tolerance leaked into an exact single-line answer");
    const auto empty = prediction({""});
    check(uses_single_line_answer(empty), "An authored empty answer changed its control policy");
    expect(empty, "", true, "The exact empty answer was rejected");
    expect(empty, "\n", false, "An exact single-line empty answer accepted a newline");

    const auto trimmed = prediction({"Rojo\nVerde"}, TextComparison::trimmed);
    expect(trimmed, " \tRojo\nVerde\r\n\n\t ", true, "Legacy trimmed boundary tolerance changed");
    expect(trimmed, "Rojo\r\nVerde", false, "Exact-only CRLF normalization leaked into legacy trimmed content");
    expect(trimmed, "Rojo\n\nVerde", false, "Legacy trimmed comparison lost interior line content");

    auto fill = prediction({"Rojo\nVerde"});
    fill.type = BlockType::fill_code;
    fill.code = R"cpp(const char* text = R"words(___)words";)cpp";
    expect(fill, "Rojo\nVerde", true, "A fill-code literal answer was rejected");
    expect(fill, "Rojo\nVerde\n", false, "Optional prediction terminators leaked into literal code");
    expect(fill, "Rojo\r\nVerde", false, "Prediction CRLF normalization changed a literal code answer");
    fill.code = "const int value = ___;";
    fill.answers = {"1 + 2"};
    expect(fill, " \t1  +\t2\n", true, "Ordinary C++ answer normalization changed");
    fill.answer_comparison = AnswerComparison::cpp_decimal_literal_value;
    fill.answers = {"3.0"};
    expect(fill, " 30e-1\n", true, "Exact decimal value comparison changed");
    expect(fill, "3 .0", false, "The decimal literal contract accepted split tokens");

    OutputCheck contract;
    OutputCase item;
    item.outputs = {"Rojo\nVerde"};
    contract.cases.push_back(item);
    FreshExecution execution;
    execution.source_revision = 7;
    execution.compilation_succeeded = true;
    execution.execution_succeeded = true;
    execution.exit_code = 0;
    execution.standard_output = "Rojo\nVerde";
    check(evaluate_output_check(contract, execution, 7).verdict == OutputVerdict::correct,
          "The genuine compiler stdout fixture lost its exact contract");
    for (const auto output : {"Rojo\nVerde\n", "Rojo\r\nVerde", "Rojo\nVerde "}) {
        execution.standard_output = output;
        check(evaluate_output_check(contract, execution, 7).verdict == OutputVerdict::incorrect,
              "Prediction input tolerance changed an ordinary compiler stdout objective");
    }
    check(contract.cases.front().outputs == item.outputs,
          "Evaluating a prediction changed the compiler's canonical output");
}
}

int main() {
    try {
        published_multiline_payloads();
        authored_terminators_and_significant_final_lines();
        alternatives_and_empty_contracts();
        unchanged_single_line_trimmed_fill_and_runtime_policies();
        std::cout << "PASS exact multiline predictions: four current payloads, optional single final LF/CRLF, "
                     "significant interior/final blank lines and spaces, original bytes and other policies; "
                  << evaluated << " answer evaluations\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL exact multiline predictions: " << error.what() << '\n';
        return 1;
    }
}
