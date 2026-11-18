#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;

void require(bool result, std::string_view message) {
    if (!result) throw std::runtime_error(std::string(message));
}
void rejected(const std::filesystem::path& file,
              std::string_view expected_reason = {}) {
    // The read-only course-progress loader must reject the same forged
    // semantics and answer changes as the full visible lesson loader.
    for (const bool progress : {false, true}) {
        bool failed = false;
        try {
            if (progress) (void)windows::load_lesson_progress_metadata(file, "es");
            else (void)windows::load_lesson(file, "es");
        } catch (const std::exception& error) {
            failed = true;
            require(expected_reason.empty() ||
                    std::string_view(error.what()).find(expected_reason) != std::string_view::npos,
                std::string("Unexpected rejection of ") + file.filename().string() +
                    (progress ? " (progress): " : " (lesson): ") + error.what());
        }
        require(failed, std::string("The Windows ") +
            (progress ? "progress" : "lesson") +
            " loader accepted an unproved source/prose revision: " +
            file.filename().string());
    }
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected the isolated optional prose fixture folder");
        const std::filesystem::path dir(argv[1]);
        const auto before = windows::load_lesson(dir / "optional-prose.before.json", "es");
        const auto after = windows::load_lesson(dir / "optional-prose.after.json", "es");
        const auto metadata = windows::load_lesson_progress_metadata(
            dir / "optional-prose.after.json", "es");
        require(before.id == after.id && after.id == metadata.id &&
            before.pages.size() == 1 && after.pages.size() == 1 &&
            before.pages[0].blocks.size() == 2 && after.pages[0].blocks.size() == 2 &&
            metadata.pages.size() == 1 && metadata.pages[0].blocks.size() == 2,
            "The lexical/prose review moved or replaced the historical lesson");
        require(before.state_layout.signatures == after.state_layout.signatures &&
                before.state_layout.lineage == after.state_layout.lineage &&
                before.state_layout.source_format_revisions.empty() &&
                after.state_layout.source_format_revisions.size() == 1 &&
                metadata.state_layout.signatures == after.state_layout.signatures,
            "Optional prose and code formatting changed published response signatures");
        const auto& original = before.pages[0].blocks[0];
        const auto& current = after.pages[0].blocks[0];
        const auto& projected = metadata.pages[0].blocks[0];
        constexpr std::string_view explanation =
            "La salida A procede del literal; los espacios no cambian la evaluación.";
        require(original.type == curriculum::BlockType::output_prediction &&
                current.type == original.type && original.explanation.empty() &&
                current.explanation == explanation &&
                original.code == "std::cout << \"A\";" &&
                current.code == "std::cout  << \"A\";" &&
                original.answers == current.answers &&
                original.answers == std::vector<std::string>{"A"} &&
                original.stable_id == current.stable_id &&
                original.address.page == current.address.page &&
                original.address.block == current.address.block,
            "The optional explanation altered a source, result or state owner");
        const auto marked = current.text_semantics.find("/explanation");
        const auto summary = projected.text_semantics.find("/explanation");
        require(marked != current.text_semantics.end() &&
                summary != projected.text_semantics.end() &&
                marked->second.source == explanation && marked->second.spans.empty() &&
                marked->second == summary->second,
            "The new explanation lost its exact empty-span authored semantic marker");

        state::StateData progress;
        state::prepare_lesson_state(progress, before);
        state::Response previous;
        previous.correct = true;
        previous.answer = "A";
        previous.raw_json = R"({"opaque":{"oldAttempts":3}})";
        state::put_response(progress, before, original, previous);
        const auto snapshot = state::string_value(
            progress, "learning_progress",
            curriculum::exercise_key(before, original));
        const auto reopened = state::decode(state::encode(progress));
        auto migrated = reopened;
        state::prepare_lesson_state(migrated, after);
        state::validate_lesson_state(migrated, after);
        const auto receipt = state::get_response(migrated, after, current);
        require(receipt && receipt->correct && receipt->answer == "A" &&
                curriculum::exercise_key(before, original) ==
                    curriculum::exercise_key(after, current) &&
                state::string_value(migrated, "learning_progress",
                    curriculum::exercise_key(after, current)) == snapshot,
            "Adding explanation/spacing erased a valid historical answer or its attempts");
        require(progress == reopened,
            "Reopening the first isolated QA receipt modified prior student state");
        // The actual U1L4 gap is a drag_fill_code puzzle, not a prediction.
        // Adding its explanation must preserve every physical hole, choice,
        // answer index and the old saved response under the same owner.
        const auto& old_drag = before.pages[0].blocks[1];
        const auto& new_drag = after.pages[0].blocks[1];
        const auto& drag_metadata = metadata.pages[0].blocks[1];
        constexpr std::string_view drag_explanation =
            "La ficha main identifica la función, << inserta el texto y ; termina la instrucción.";
        constexpr std::string_view drag_template =
            "#include <iostream>\nint []() {\n    std::cout [] \"Revision lista\"[]\n    return 0;\n}";
        require(old_drag.type == curriculum::BlockType::drag_fill_code &&
                new_drag.type == old_drag.type && old_drag.explanation.empty() &&
                new_drag.explanation == drag_explanation &&
                old_drag.code_template == drag_template &&
                new_drag.code_template == old_drag.code_template &&
                old_drag.choices == std::vector<std::string>{"main", "<<", ";", "Main", "<"} &&
                new_drag.choices == old_drag.choices &&
                old_drag.correct_order == std::vector<std::size_t>{0, 1, 2} &&
                new_drag.correct_order == old_drag.correct_order &&
                old_drag.stable_id == new_drag.stable_id &&
                old_drag.address.page == new_drag.address.page &&
                old_drag.address.block == new_drag.address.block,
            "Adding a drag-fill explanation changed holes, distractors, answers or owner");
        const auto drag_mark = new_drag.text_semantics.find("/explanation");
        const auto drag_summary = drag_metadata.text_semantics.find("/explanation");
        require(drag_mark != new_drag.text_semantics.end() &&
                drag_summary != drag_metadata.text_semantics.end() &&
                drag_mark->second.source == drag_explanation &&
                drag_mark->second.spans.empty() &&
                drag_mark->second == drag_summary->second,
            "The drag-fill explanation lost its authored semantic descriptor");

        // CppFillView persists drag-fill selections in JSON "slots";
        // Response.order belongs to order_code and would be a false QA proxy.
        const std::array<std::size_t, 3> correct_slots{0, 1, 2};
        const std::array<std::size_t, 3> incorrect_slots{1, 0, 2};
        require(curriculum::evaluate_drag_answers(old_drag, correct_slots).correct &&
                curriculum::evaluate_drag_answers(new_drag, correct_slots).correct &&
                !curriculum::evaluate_drag_answers(new_drag, incorrect_slots).correct,
            "The new explanation changed the evaluated drag-fill answer or distractors");

        state::Response old_drag_answer;
        old_drag_answer.correct = true;
        old_drag_answer.raw_json =
            R"({"slots":[0,1,2],"incorrectAttempts":2,"selection":7})";
        state::put_response(progress, before, old_drag, old_drag_answer);
        const auto drag_snapshot = state::string_value(progress, "learning_progress",
            curriculum::exercise_key(before, old_drag));
        const auto drag_reopened = state::decode(state::encode(progress));
        auto drag_migrated = drag_reopened;
        state::prepare_lesson_state(drag_migrated, after);
        state::validate_lesson_state(drag_migrated, after);
        const auto drag_receipt = state::get_response(drag_migrated, after, new_drag);
        const auto other_receipt = state::get_response(drag_migrated, after, current);
        require(drag_receipt && drag_receipt->correct &&
                drag_receipt->order.empty() &&
                drag_receipt->raw_json.find(R"("slots":[0,1,2])") != std::string::npos &&
                drag_receipt->raw_json.find(R"("incorrectAttempts":2)") != std::string::npos &&
                drag_receipt->raw_json.find(R"("selection":7)") != std::string::npos &&
                drag_receipt->raw_json == drag_snapshot &&
                curriculum::exercise_key(before, old_drag) ==
                    curriculum::exercise_key(after, new_drag) &&
                state::string_value(drag_migrated, "learning_progress",
                    curriculum::exercise_key(after, new_drag)) == drag_snapshot &&
                other_receipt && other_receipt->correct && other_receipt->answer == "A",
            "Optional drag-fill prose lost an old answer, attempts or another puzzle receipt");
        require(drag_reopened == progress,
            "Reopening an isolated QA receipt modified its prior student state");

        // A failed drag-fill attempt is also a first-class historical state.
        // Preserve its native JSON slots, retry count and reviewed verdict
        // without treating an added explanation as a fresh correct attempt.
        auto failed_progress = reopened;
        state::Response failed_drag;
        failed_drag.correct = false;
        failed_drag.raw_json =
            R"({"slots":[1,0,2],"incorrectAttempts":4,"selection":3})";
        state::mark_response_reviewed(failed_drag, old_drag);
        require(state::response_reviewed(failed_drag, old_drag),
            "The original drag-fill failed attempt cannot be reviewed");
        state::put_response(failed_progress, before, old_drag, failed_drag);
        const auto failed_snapshot = state::string_value(
            failed_progress, "learning_progress",
            curriculum::exercise_key(before, old_drag));
        const auto failed_reopened = state::decode(state::encode(failed_progress));
        auto failed_migrated = failed_reopened;
        state::prepare_lesson_state(failed_migrated, after);
        state::validate_lesson_state(failed_migrated, after);
        const auto failed_receipt = state::get_response(failed_migrated, after, new_drag);
        const auto companion_receipt = state::get_response(failed_migrated, after, current);
        require(failed_receipt && !failed_receipt->correct &&
                state::response_reviewed(*failed_receipt, new_drag) &&
                failed_receipt->raw_json == failed_snapshot &&
                failed_receipt->raw_json.find(R"("slots":[1,0,2])") != std::string::npos &&
                failed_receipt->raw_json.find(R"("incorrectAttempts":4)") != std::string::npos &&
                companion_receipt && companion_receipt->correct &&
                failed_reopened == failed_progress,
            "Optional drag-fill prose rewrote a failed/reviewed historical attempt");

        for (const auto filename : {
                "optional-prose.missing-mark.json",
                "optional-prose.stale-mark.json",
                "optional-prose.changed-answer.json",
                "optional-prose.changed-code.json",
                "optional-prose.missing-drag-mark.json",
                "optional-prose.stale-drag-mark.json",
                "optional-prose.changed-drag-answer.json",
                "optional-prose.changed-drag-template.json"})
            rejected(dir / filename);
        rejected(dir / "optional-prose.blank-nel-explanation.json",
            "Combined source/explanation revision needs nonblank authored prose");
        rejected(dir / "optional-prose.blank-nel-question.json",
            "Combined source/question revision needs nonblank public wording");
        rejected(dir / "optional-prose.blank-nel-drag-explanation.json",
            "Combined source/explanation revision needs nonblank authored prose");
        rejected(dir / "optional-prose.blank-unmarked-drag-explanation.json",
            "Combined source/explanation revision needs nonblank authored prose");
        std::cout << "PASS optional C++ format/prose and drag-fill: both historical answers, "
            "two authored semantic markers, native progress and twelve forged revisions rejected\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL optional C++ format/prose: " << error.what() << '\n';
        return 1;
    }
}
