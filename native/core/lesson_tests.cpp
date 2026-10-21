#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::curriculum;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action&& action, const char* message) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
void replace_once(std::string& text, std::string_view old_value, std::string_view new_value) {
    const auto index = text.find(old_value); check(index != text.npos, "Test fixture target missing");
    text.replace(index, old_value.size(), new_value);
}
}
int main() {
    try {
        using lgaed::windows::parse_lesson_json;
        const auto root = std::filesystem::path(lgaed::build::source_root);
        const auto path = root / "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json";
        const auto lesson = lgaed::windows::load_lesson(path);
        check(lesson.id == "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida", "Original lesson ID changed");
        check(lesson.pages.size() == 6 && lesson.pages[4].blocks.size() == 8, "Real lesson structure differs");
        check(lesson.state_layout.version == 2 && lesson.state_layout.family.size() == 3, "Historical language families were lost");
        check(lesson.state_layout.lineage == "9048491c38ff0e6d1cc9dbb47cc695cd96b50fc993bc0996255928b2ac64b6c5" &&
            lesson.state_layout.legacy_lineage == "88ca92f3c2d0938b7fbe360ce738dc3923be5b5608fdce3a14e386ac943b6d9a",
            "Cross-language state lineage differs from the independently calculated Android framing");
        const auto& prediction = lesson.pages[4].blocks[1];
        const auto& original = lesson.pages[4].blocks[3];
        const auto& modification = lesson.pages[4].blocks[6];
        check(prediction.id == "prediccion_zona_inicial" && prediction.address.page == 4 && prediction.address.block == 3, "Prediction no longer owns its historical slot");
        check(original.stable_id == "ejecucion_original_editor" && original.address.page == 6 && original.address.block == 1 &&
            original.address.addition_namespace.size() == 64, "New editor is not isolated in its allocated namespace");
        check(original.address.addition_namespace == "a1696bcfce824941b813aef3f0443d4b20cfddfaf2d95b5ecb82c71ea0b6a7cb", "Android addition namespace differs");
        check(modification.address.page == 4 && modification.address.block == 2 && modification.address.addition_namespace.empty(), "Modification editor moved to a new draft address");
        check(draft_key("programming", lesson, original) != draft_key("programming", lesson, modification), "Editor drafts collide");
        check(exercise_key(lesson, prediction).ends_with("_page_4_block_3"), "Exercise key no longer matches Android");
        check(original.initial_code == modification.initial_code && original.initial_code.ends_with("}\n"), "Source/indentation/LF was changed");
        check(!original.standard_input && !modification.standard_input, "Input invented for a lesson that does not require it");
        check(evaluate_text_answer(prediction, "Zona inicial").correct, "Exact authored prediction rejected");
        for (const auto value : {"\"Zona inicial\"", "Zona inicial ", "Zona inicial\n", " Zona inicial", "Zona  inicial"})
            check(!evaluate_text_answer(prediction, value).correct, "Exact output accepted unauthored characters");
        check(uses_single_line_answer(prediction), "Single-line Enter contract lost");
        check(!evaluate_choice(lesson.pages[4].blocks[7], 0).correct && evaluate_choice(lesson.pages[4].blocks[7], 2).correct, "Conceptual choice changed");
        check(evaluate_order(lesson.pages[3].blocks[2], std::vector<std::size_t>{1,3,0,2}).correct, "Authored code ordering rejected");
        check(!evaluate_order(lesson.pages[3].blocks[2], std::vector<std::size_t>{1,1,0,2}).correct, "Duplicate code fragment accepted");

        FreshExecution execution{5, true, true, 0, "", "Caja recibida", {}};
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::correct, "Fresh expected output rejected");
        check(evaluate_output_check(*modification.output_check, execution, 6).verdict == OutputVerdict::unavailable, "Stale execution reused after an edit");
        execution.compilation_succeeded = false;
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::unavailable, "Compilation failure became incorrect answer");
        execution.compilation_succeeded = true; execution.execution_succeeded = false;
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::unavailable, "Runtime failure became incorrect answer");
        execution.execution_succeeded = true; execution.standard_output = "Zona segura";
        auto review = evaluate_output_check(*modification.output_check, execution, 5);
        check(review.verdict == OutputVerdict::incorrect && !review.feedback.empty(), "Authored targeted review was lost");
        execution.standard_output = "unrecognized output"; review = evaluate_output_check(*modification.output_check, execution, 5);
        check(review.verdict == OutputVerdict::incorrect && review.feedback.empty(), "Invented feedback for an unknown output");
        execution.standard_output = "Caja recibida\n";
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::incorrect, "Console checker trimmed significant LF");
        execution.standard_output = "Zona inicial";
        check(evaluate_output_check(*original.output_check, execution, 5).verdict == OutputVerdict::correct, "Original editor checks modification output");

        check(normalize_cpp_answer("  cout  <<  \"a  b\"  ") == "cout << \"a  b\"", "Literal spacing was collapsed");
        check(normalize_cpp_answer(" R\"tag(a \t\nb)tag\" ") == "R\"tag(a \t\nb)tag\"", "Raw literal whitespace was collapsed");
        check(normalize_cpp_answer("1'000  +  2") == "1'000 + 2", "Digit separator was mistaken for a char literal");
        auto fill = lesson.pages[1].blocks[4];
        check(evaluate_text_answer(fill, "  \"Muelle cerrado\"  ").correct && !evaluate_text_answer(fill, "\"Muelle  cerrado\"").correct,
            "Fill compatibility/literal significance changed");
        fill.code = "cout << \"___\";"; fill.answers = {"a  b"};
        check(evaluate_text_answer(fill, "a  b").correct && !evaluate_text_answer(fill, "a b").correct && !evaluate_text_answer(fill, " a  b").correct,
            "Template literal hole lost its exact characters");
        const auto ranges = emphasis_ranges("uno dos uno", std::vector<Emphasis>{{"uno",2,"italic"},{"dos",0,"bold"},{"uno",0,"bold"}});
        check(ranges.size() == 2 && ranges[0].start == 4 && ranges[1].start == 8 && ranges[1].style == "italic", "Emphasis matching ignores source occurrence");
        for (std::size_t page = 0; page < lesson.pages.size(); ++page) check(display_page(lesson, storage_page(lesson, page)) == page, "Page map is not invertible");

        auto corrupt = lesson.raw_json;
        replace_once(corrupt, "\"correctIndex\": 1", "\"correctIndex\": 0");
        rejects([&] { (void)parse_lesson_json(corrupt); }, "State-bearing answer change passed a stale map");
        rejects([&] { (void)parse_lesson_json(lesson.raw_json, "fr"); }, "Wrong-language state family accepted");
        rejects([&] { (void)parse_lesson_json("{\"id\":\"x\",\"title\":\"x\",\"pages\":[{\"blocks\":[{\"type\":\"unknown\"}]}]}"); }, "Unsupported block silently ignored");
        rejects([&] { (void)parse_lesson_json("{ malformed"); }, "Malformed JSON accepted");
        const auto basic = parse_lesson_json("{\"id\":\"demo\",\"title\":\"Demo\",\"pages\":[{\"blocks\":[{\"type\":\"output_prediction\",\"answers\":[\"ok\"]}]}]}");
        check(evaluate_text_answer(basic.pages[0].blocks[0], " ok\n").correct, "Legacy trimmed prediction tolerance regressed");
        const auto input_lesson = parse_lesson_json("{\"id\":\"stdin\",\"title\":\"Input\",\"pages\":[{\"blocks\":[{\"type\":\"cpp_compiler\",\"initialCode\":\"int main(){}\\n\",\"initialStandardInput\":\" \\t \\n\"}]}]}");
        check(input_lesson.pages[0].blocks[0].standard_input == " \t \n", "Explicit prepared input lost significant whitespace");
        for (const auto language : {"en", "fr"}) {
            const auto translated = lgaed::windows::load_lesson(root / "app/src/main/assets/content" / language /
                "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json", language);
            check(translated.id == lesson.id && translated.pages.size() == 6 && translated.state_layout.version == 0,
                "Existing untranslated-layout family was reinterpreted or lost");
            bool note = false;
            for (const auto& page : translated.pages) for (const auto& block : page.blocks)
                if (block.type == BlockType::note && !block.text.empty()) note = true;
            check(note, "Translated note text was omitted");
        }
        std::cout << "Real U1L2, exact outputs, stable v2 families/slots, fresh compilation verdicts, C++ literals, emphasis and malformed contracts: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
