#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"

#include <array>
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
void rejected(const std::filesystem::path& file) {
    // The read-only course-progress loader must reject the same forged
    // semantics and answer changes as the full visible lesson loader.
    for (const bool progress : {false, true}) {
        bool failed = false;
        try {
            if (progress) (void)windows::load_lesson_progress_metadata(file, "es");
            else (void)windows::load_lesson(file, "es");
        } catch (const std::exception&) { failed = true; }
        require(failed, "The Windows loader accepted an unproved source/prose revision");
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
            before.pages[0].blocks.size() == 1 && after.pages[0].blocks.size() == 1,
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
            "Reopening a read-only QA receipt changed its original student data");

        for (const auto filename : {
                "optional-prose.missing-mark.json",
                "optional-prose.stale-mark.json",
                "optional-prose.changed-answer.json",
                "optional-prose.changed-code.json"})
            rejected(dir / filename);
        std::cout << "PASS optional C++ format/prose: published answer, "
            "authored UTF-16 marker, native progress and four forged revisions rejected\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL optional C++ format/prose: " << error.what() << '\n';
        return 1;
    }
}
