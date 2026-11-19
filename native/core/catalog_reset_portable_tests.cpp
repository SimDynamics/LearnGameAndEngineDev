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

void catalog_unit_selection_is_scoped_to_published_edition() {
    using namespace lgaed::curriculum;
    CatalogPage page;
    page.location={"es","programming","cpp_fundamentals","",""};
    page.entries.resize(1);
    const auto historical=catalog_position_key(page.location);
    page.entries[0].id="cpp_fundamentals__original";
    page.entries[0].asset_locale="es";
    check(catalog_position_key(page)==historical,
          "An original course changed its historical list position key");

    page.entries[0].id="cpp_fundamentals__edition_es_1__inicio";
    const auto first=catalog_position_key(page);
    check(first==historical+"/edition/es_1"&&first!=historical,
          "A new edition reused a retired course unit's ordinal");
    page.entries[0].id="cpp_fundamentals__edition_es_2__inicio";
    const auto second=catalog_position_key(page);
    check(second==historical+"/edition/es_2"&&second!=first,
          "A second edition reused the first edition's saved unit position");
    page.entries[0].id="cpp_fundamentals__edition_es_2__otra_unidad";
    check(catalog_position_key(page)==second,
          "A compatible unit reordering erased navigation within the same edition");
    page.location.locale="fr";
    check(catalog_position_key(page)==
              catalog_position_key(page.location)+"/edition/es_2",
          "The UI language was mistaken for the new edition's asset locale");
    page.entries.clear();
    check(catalog_position_key(page)==catalog_position_key(page.location),
          "An empty unit index fabricated an edition key");

    // Lesson-list position keys already include the new unit identity.
    page.location.locale="es";
    page.location.unit="cpp_fundamentals__edition_es_2__otra_unidad";
    page.entries.resize(1);
    page.entries[0].id=page.location.unit+"__primera";
    check(catalog_position_key(page)==catalog_position_key(page.location),
          "A lesson list acquired an unrelated second epoch key");
}

void catalog_entry_identity_survives_reordering_and_retirement() {
    using namespace lgaed::curriculum;
    CatalogPage page;
    page.location={"es","programming","cpp_fundamentals","",""};
    const auto prefix=std::string("cpp_fundamentals__edition_es_1__");
    for(const auto* slug : {"primera","segunda","tercera"}) {
        CatalogEntry entry;
        entry.id=prefix+slug; entry.asset_locale="es";
        page.entries.push_back(entry);
    }
    const auto edition_key=catalog_position_key(page);
    const auto selected=catalog_saved_entry_id(page,2);
    const auto top=catalog_saved_entry_id(page,1);
    check(selected==prefix+"tercera"&&top==prefix+"segunda",
        "A real unit was not saved under its stable edition identity");
    std::swap(page.entries[0],page.entries[2]);
    check(catalog_position_key(page)==edition_key &&
          catalog_restore_entry_index(page,selected,2,3)==0 &&
          catalog_restore_entry_index(page,top,1,3)==1,
        "Reordering one edition selected a different unit from its old ordinal");
    std::swap(page.entries[1],page.entries[2]);
    check(catalog_restore_entry_index(page,top,1,3)==2 &&
          catalog_restore_entry_index(page,selected,2,3)==0 &&
          catalog_restore_entry_index(page,{},1,3)==1,
        "The first visible unit or restored scroll anchor did not follow its identity");
    check(catalog_restore_entry_index(page,prefix+"retirada",2,3)==0,
        "A retired unit inherited the former slot's unrelated unit");
    check(catalog_restore_entry_index(page,prefix+"otra_edicion",2,3)==0,
        "An unmatched edition ID silently borrowed another generation");
    // Old profiles store only positions. Their navigation still works,
    // including an auxiliary home action beyond the catalog entries.
    check(catalog_restore_entry_index(page,{},2,5)==2 &&
          catalog_restore_entry_index(page,{},4,5)==4 &&
          catalog_restore_entry_index(page,{},99,5)==4 &&
          catalog_restore_entry_index(page,{},-1,5)==0 &&
          catalog_restore_entry_index(page,{},2,0)==0,
        "Old numerical positions, home actions or an empty list regressed");
    check(catalog_saved_entry_id(page,-1).empty() &&
          catalog_saved_entry_id(page,3).empty(),
        "An empty selection or home action borrowed a unit identity");
    page.entries.clear();
    check(catalog_restore_entry_index(page,selected,2,3)==0 &&
          catalog_restore_entry_index(page,{},2,3)==2,
        "An empty lesson list fabricated a selected entry");
}

void malformed_or_cross_course_scope_is_atomic() {
    for (int variant = 0; variant < 9; ++variant) {
        Fixture fixture;
        auto scope = fixture.scope;
        switch (variant) {
        case 0: scope.courses.push_back("cpp_fundamentals"); break;
        case 1: scope.units.push_back("cpp_fundamentals__unit"); break;
        case 2: scope.lessons.push_back(scope.lessons.front()); break;
        case 3: scope.courses = {"other_course"}; break;
        case 4: scope.units.push_back("other_course__unit"); break;
        case 5: scope.lessons.push_back("other_course__unit__first"); break;
        case 6: scope.lessons = {"cpp_fundamentals__another_unit__first"}; break;
        case 7: scope.courses.push_back("../another_course"); break;
        case 8: scope.units = {"cpp_fundamentals__unknown"};
                scope.lessons = {fixture.scope.lessons.front()};
                break;
        }
        const auto original = fixture.state;
        const auto original_bytes = lgaed::state::encode(original);
        bool rejected = false;
        try { (void)reset_catalog_progress(fixture.state, scope); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "An invalid, duplicate or cross-course reset scope was accepted");
        check(fixture.state == original &&
              lgaed::state::encode(fixture.state) == original_bytes,
              "A rejected reset changed serialized student progress or unrelated preferences");
    }

    // A legitimate route-wide reset can include more than one course. The
    // other course's deliberately corrupt receipt is replaced only in this
    // isolated fixture; real student state is never touched.
    Fixture fixture;
    fixture.scope.courses.push_back("other_course");
    fixture.scope.units.push_back("other_course__unit");
    fixture.scope.lessons.push_back("other_course__unit__first");
    fixture.state.preferences["learning_progress"][
        "lesson_other_course__unit__first_read_pages_v2_es"] =
            std::vector<std::string>{};
    const auto drafts = fixture.state.preferences.at("cpp_lesson_drafts");
    check(reset_catalog_progress(fixture.state, fixture.scope) == 11,
        "Valid two-course reset failed to clear its own selected records");
    check(!fixture.state.preferences.at("learning_progress").contains(
              "lesson_other_course__unit__first_completed") &&
          fixture.state.preferences.at("cpp_lesson_drafts") == drafts,
        "A valid multi-course reset did not preserve unrelated drafts");
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
        catalog_unit_selection_is_scoped_to_published_edition();
        catalog_entry_identity_survives_reordering_and_retirement();
        malformed_or_cross_course_scope_is_atomic();
        accepted_reset_keeps_foreign_state_and_is_idempotent();
        foreign_corruption_does_not_block_selected_course();
        std::cout << "PASS catalog reset: 8 malformed coverage slots and 9 invalid scopes rejected atomically; "
                     "exact scope, drafts, provenance and binary state preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL catalog reset: " << error.what() << '\n';
        return 1;
    }
}
