#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

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
std::string root_property(std::string_view source, std::string_view name, std::string_view value) {
    // Replace a property through the JSON object, including when Work has already
    // integrated it. Appending another root key would make negative fixtures ambiguous.
    auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(source));
    root.Insert(winrt::to_hstring(name),winrt::Windows::Data::Json::JsonValue::Parse(winrt::to_hstring(value)));
    return winrt::to_string(root.Stringify());
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
        const auto linked_json = [&](std::string_view relations) {
            return root_property(lesson.raw_json,"referenceAfter",relations);
        };
        check(reference_activity(lesson,lesson.pages[5].blocks[0]) == StatePosition{4,6},
            "Integrated reference title is visible before its own compiler objective");
        const auto linked_source = linked_json(R"([{"activity":"block_0004_0002","blocks":["block_0005_0000","block_0005_0001","block_0005_0002","block_0005_0003"]},{"activity":"block_0005_0006","blocks":["block_0005_0007","block_0005_0008"]}])");
        check(linked_source.find("\"referenceAfter\"") == linked_source.rfind("\"referenceAfter\""),
            "Reference fixture duplicated an existing root property");
        const auto linked = parse_lesson_json(linked_source);
        check(linked.state_layout.lineage == lesson.state_layout.lineage && linked.state_layout.signatures == lesson.state_layout.signatures,
            "Presentation-only references changed historical fingerprints or state lineage");
        check(reference_activity(linked,linked.pages[5].blocks[0]) == StatePosition{4,6} &&
              reference_activity(linked,linked.pages[5].blocks[2]) == StatePosition{4,6} &&
              reference_activity(linked,linked.pages[5].blocks[8]) == StatePosition{5,6} &&
              !reference_activity(linked,linked.pages[5].blocks[5]), "Reference points to a nearby quiz instead of its own compiler");
        auto changed_goal = linked.pages[5].blocks[6]; changed_goal.output_check->cases[0].outputs = {"Different exact goal"};
        check(reference_objective_frame(linked,changed_goal) != reference_objective_frame(linked,linked.pages[5].blocks[6]),
            "Reference completion seal is not bound to its authored output objective");
        for (const auto invalid_relation : {
                R"([{"activity":"missing","blocks":["block_0005_0008"]}])",
                R"([{"activity":"block_0005_0006","blocks":["missing"]}])",
                R"([{"activity":"block_0004_0004","blocks":["block_0005_0008"]}])",
                R"([{"activity":"block_0005_0006","blocks":["block_0005_0006"]}])",
                R"([{"activity":"block_0004_0002","blocks":["block_0004_0004"]}])",
                R"([{"activity":"block_0005_0006","blocks":["block_0005_0008","block_0005_0008"]}])",
                R"([{"activity":"block_0004_0002","blocks":["block_0005_0008"]},{"activity":"block_0005_0006","blocks":["block_0005_0008"]}])",
                R"([{"activity":"block_0005_0006","blocks":[]}])",
                R"([{"activity":"block_0005_0006","blocks":["block_0005_0008"],"unknown":true}])"})
            rejects([&] { (void)parse_lesson_json(linked_json(invalid_relation)); }, "Malformed or unsafe reference relationship accepted");
        check(parse_lesson_json(linked_json("[]")).reference_after.empty(), "Reference fixture failed to replace the integrated property");

        const auto dependent_json = [&](std::string_view relations) {
            return root_property(lesson.raw_json,"activityAfter",relations);
        };
        const auto dependent = parse_lesson_json(dependent_json(
            R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]},{"activity":"block_0005_0006","requires":["ejecucion_original_editor","block_0004_0002"]}])"));
        check(dependent.activity_after.size() == 2 &&
              activity_prerequisites(dependent,dependent.pages[4].blocks[6]) == std::vector<StatePosition>{{4,3}} &&
              activity_prerequisites(dependent,dependent.pages[5].blocks[6]) == std::vector<StatePosition>{{4,3},{4,6}} &&
              activity_prerequisites(dependent,dependent.pages[4].blocks[3]).empty(),
            "Activity dependencies lost their authored owners/order or changed an unrelated editor");
        check(dependent.state_layout.lineage == lesson.state_layout.lineage &&
              dependent.state_layout.legacy_lineage == lesson.state_layout.legacy_lineage &&
              dependent.state_layout.signatures == lesson.state_layout.signatures &&
              draft_key("programming",dependent,dependent.pages[4].blocks[3]) == draft_key("programming",lesson,original) &&
              draft_key("programming",dependent,dependent.pages[4].blocks[6]) == draft_key("programming",lesson,modification) &&
              reference_activity(dependent,dependent.pages[5].blocks[0]) == StatePosition{4,6},
            "Activity-only metadata changed state fingerprints, historical draft owners or integrated references");
        auto compiler_without_check = dependent;
        compiler_without_check.pages[5].blocks[6].output_check.reset();
        validate_activity_relationships(compiler_without_check);
        compiler_without_check.pages[4].blocks[3].output_check.reset();
        rejects([&] { validate_activity_relationships(compiler_without_check); },
            "A prerequisite without a verifiable output objective can unlock another compiler");
        auto unstable_activity = dependent; unstable_activity.state_layout.version = 0;
        rejects([&] { validate_activity_relationships(unstable_activity); }, "Activity dependencies accepted transient display positions");
        for (const auto invalid_relation : {
                R"({"activity":"block_0004_0002","requires":["ejecucion_original_editor"]})",
                R"([{"activity":"missing","requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["missing"]}])",
                R"([{"activity":"block_0004_0004","requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["block_0004_0004"]}])",
                R"([{"activity":"block_0004_0002","requires":["block_0004_0002"]}])",
                R"([{"activity":"ejecucion_original_editor","requires":["block_0004_0002"]}])",
                R"([{"activity":"block_0004_0002","requires":["block_0005_0006"]}])",
                R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor","ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]},{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":[]}])",
                R"([{"activity":"block_0004_0002","requires":[""]}])",
                R"([{"activity":"block_0004_0002","requires":[3]}])",
                R"([{"activity":"block_0004_0002","requires":"ejecucion_original_editor"}])",
                R"([{"activity":"block_0004_0002"}])",
                R"([{"requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"],"unknown":true}])",
                R"([{"activity":"ejecucion_original_editor","requires":["block_0004_0002"]},{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]}])"})
            rejects([&] { (void)parse_lesson_json(dependent_json(invalid_relation)); }, "Malformed or unsafe activity dependency accepted");
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
        check(parse_lesson_json(root_property(basic.raw_json,"activityAfter","[]")).activity_after.empty(),
            "Empty activity metadata requires an unnecessary legacy state migration");
        rejects([&] { (void)parse_lesson_json(root_property(basic.raw_json,"activityAfter",
            R"([{"activity":"block_0_0","requires":["block_0_0"]}])")); }, "Nonempty activity relationships without stable state identities accepted");
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
