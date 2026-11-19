#include "core/catalog.hpp"
#include "core/lesson_state.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using lgaed::curriculum::CatalogScope;
using lgaed::curriculum::reset_catalog_progress;
using lgaed::state::StateData;

void check(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

struct Fixture {
    CatalogScope scope{{"cpp_fundamentals"},
                       {"cpp_fundamentals__unit"},
                       {"cpp_fundamentals__unit__first", "cpp_fundamentals__unit__second"}};
    StateData state;
    std::string completion = "lesson_cpp_fundamentals__unit__first_completed";
    std::string answer = "exercise_cpp_fundamentals__unit__first_page_0_block_1";
    std::string first_read = "lesson_cpp_fundamentals__unit__first_read_pages_v2_es";
    std::string second_read = "lesson_cpp_fundamentals__unit__second_read_blocks_v2_fr";

    Fixture() {
        auto& progress = state.preferences["learning_progress"];
        progress[completion] = true;
        progress["lesson_cpp_fundamentals__unit__second_started"] = true;
        progress[answer] = std::string{"correct historical answer"};
        progress[first_read] = std::vector<std::string>{"page_0000"};
        progress[second_read] = std::vector<std::string>{"block_0001"};
        progress["unit_cpp_fundamentals__unit_completed"] = true;
        progress["course_cpp_fundamentals_completed"] = true;
        progress["lesson_cpp_fundamentals__unit__first_state_lineage_v2_es"] =
            std::string{"immutable published provenance"};
        progress["lesson_cpp_fundamentals__unit__first_extra_completed"] = true;
        progress["lesson_other_course__unit__first_completed"] = true;
        progress["lesson_other_course__unit__first_read_pages_v2_es"] = std::int64_t{12};
        state.preferences["native_windows"]["scroll/cpp_fundamentals__unit__first/reader"] =
            std::int64_t{4};
        state.preferences["native_windows"]["compiler-puzzle/v1/es/cpp_fundamentals__unit__second/0/1"] =
            std::string{"old receipt"};
        state.preferences["native_windows"]["reference-proof/v1/es/programming/cpp_fundamentals__unit__first/0/1"] =
            std::string{"old proof"};
        state.preferences["cpp_lesson_drafts"]["programming/cpp_fundamentals__unit__first/0/1/source"] =
            std::string{"unsent local work"};
        state.preferences["narration_settings"]["voice"] = std::string{"ef_dora"};
    }
};

void invalid_selected_coverage_is_atomic() {
    for (const auto suffix : {"v1", "v2_es", "v2_en", "v2_fr"})
        for (const auto field : {"_read_pages_", "_read_blocks_"}) {
            Fixture fixture;
            const auto invalid = "lesson_" + fixture.scope.lessons.back() + field + suffix;
            // A type mismatch in the SECOND lesson must not erase any receipt
            // from the first lesson, even when it sorts earlier in the map.
            fixture.state.preferences["learning_progress"][invalid] = std::int64_t{2};
            const auto original = fixture.state;
            const auto original_bytes = lgaed::state::encode(original);
            bool rejected = false;
            try {
                (void)reset_catalog_progress(fixture.state, fixture.scope);
            } catch (const std::runtime_error& error) {
                rejected = std::string_view(error.what()) ==
                    "Cobertura de lectura no válida; no se reinicia el estado.";
            }
            check(rejected, "An invalid selected read-coverage receipt did not reject the reset");
            check(fixture.state == original &&
                lgaed::state::encode(fixture.state) == original_bytes,
                "A rejected reset partially modified the student's serialized progress");
        }
}

void accepted_reset_keeps_foreign_state_and_is_idempotent() {
    Fixture fixture;
    const auto previous = fixture.state;
    check(reset_catalog_progress(fixture.state, fixture.scope) == 10,
        "The selected reset did not clear exactly its eight records and two reading slots");
    const auto& progress = fixture.state.preferences.at("learning_progress");
    const auto& native = fixture.state.preferences.at("native_windows");
    check(!progress.contains(fixture.completion) &&
        !progress.contains(fixture.answer) &&
        !progress.contains("lesson_cpp_fundamentals__unit__second_started") &&
        !progress.contains("unit_cpp_fundamentals__unit_completed") &&
        !progress.contains("course_cpp_fundamentals_completed") &&
        !native.contains("scroll/cpp_fundamentals__unit__first/reader") &&
        !native.contains("compiler-puzzle/v1/es/cpp_fundamentals__unit__second/0/1") &&
        !native.contains("reference-proof/v1/es/programming/cpp_fundamentals__unit__first/0/1"),
        "Scoped completion, execution receipts or native bookmarks survived the reset");
    check(std::get<std::vector<std::string>>(progress.at(fixture.first_read)).empty() &&
        std::get<std::vector<std::string>>(progress.at(fixture.second_read)).empty(),
        "The selected reading coverage was not cleared in-place");
    for (const auto& domain : {"cpp_lesson_drafts", "narration_settings"})
        check(fixture.state.preferences.at(domain) == previous.preferences.at(domain),
            "The reset modified saved drafts or general preferences");
    for (const auto& id : {
            "lesson_other_course__unit__first_completed",
            "lesson_other_course__unit__first_read_pages_v2_es",
            "lesson_cpp_fundamentals__unit__first_extra_completed",
            "lesson_cpp_fundamentals__unit__first_state_lineage_v2_es"})
        check(progress.at(id) == previous.preferences.at("learning_progress").at(id),
            "The reset crossed its course boundary or deleted edition provenance");

    const auto committed = lgaed::state::encode(fixture.state);
    check(reset_catalog_progress(fixture.state, fixture.scope) == 0 &&
        lgaed::state::encode(fixture.state) == committed,
        "Repeating a completed reset changed its previous result");
}

void foreign_corruption_does_not_block_selected_course() {
    Fixture fixture;
    check(reset_catalog_progress(fixture.state, fixture.scope) == 10,
        "Foreign invalid coverage prevented a reset of the authorized course");
    check(std::get<std::int64_t>(fixture.state.preferences.at("learning_progress").at(
        "lesson_other_course__unit__first_read_pages_v2_es")) == 12,
        "A scoped reset rewrote a foreign course's unknown historical coverage");
}

} // namespace

int main() {
    try {
        invalid_selected_coverage_is_atomic();
        accepted_reset_keeps_foreign_state_and_is_idempotent();
        foreign_corruption_does_not_block_selected_course();
        std::cout << "PASS catalog reset: 8 malformed coverage slots rejected atomically; "
                     "exact scope, drafts, provenance and binary state preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL catalog reset: " << error.what() << '\n';
        return 1;
    }
}
