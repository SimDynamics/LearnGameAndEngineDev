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

void retired_catalog_location_recovers_nearest_published_parent() {
    using namespace lgaed::curriculum;
    const CatalogLocation active_course{"fr","programming","cpp_fundamentals",{},{}};
    const CatalogLocation stale_unit{"fr","programming","cpp_fundamentals",
        "cpp_fundamentals__edition_fr_1__retired",{}};
    std::vector<CatalogLocation> attempted;
    const auto published=[&](const CatalogLocation& location) {
        attempted.push_back(location);
        return location == active_course;
    };
    check(catalog_recover_location(stale_unit,published)==active_course &&
          attempted==std::vector<CatalogLocation>{stale_unit,active_course},
          "Retired edition reset the entire saved course instead of reopening its parent");
    attempted.clear();
    check(catalog_recover_location(active_course,published)==active_course &&
          attempted==std::vector<CatalogLocation>{active_course},
          "An active course was unnecessarily redirected on restoration");

    const CatalogLocation active_route{"fr","programming",{},{},{}};
    const auto route_only=[&](const CatalogLocation& location) {
        return location==active_route;
    };
    check(catalog_recover_location(stale_unit,route_only)==active_route,
          "Missing course did not retain its still-published route and language");
    const CatalogLocation french_home{"fr",{},{},{},{}};
    check(catalog_recover_location(stale_unit,[&](const CatalogLocation& location) {
        return location==french_home;
    })==french_home,"Missing route changed a valid reading language");
    check(catalog_recover_location(
            CatalogLocation{"invalid","other_route","other_course","other_unit",{}},
            [](const CatalogLocation& location) { return location==CatalogLocation{}; })
          ==CatalogLocation{},"Corrupted saved location did not recover the safe home");
    check(catalog_recover_location(active_course,[](const CatalogLocation&) -> bool {
        throw std::runtime_error("An index is not available");
    })==CatalogLocation{},"Repeated index errors did not have a bounded fallback");
    Fixture fixture;
    const auto immutable=lgaed::state::encode(fixture.state);
    (void)catalog_recover_location(stale_unit,published);
    check(lgaed::state::encode(fixture.state)==immutable,
          "Recovering a new edition rewrote real or unrelated saved progress");
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
          catalog_restore_entry_index(page,top,1,3)==1 &&
          !catalog_scroll_anchor_changed(page,top,1,1,3),
        "Reordering one edition selected a different unit from its old ordinal");
    std::swap(page.entries[1],page.entries[2]);
    check(catalog_restore_entry_index(page,top,1,3)==2 &&
          catalog_restore_entry_index(page,selected,2,3)==0 &&
          catalog_restore_entry_index(page,{},1,3)==1 &&
          catalog_scroll_anchor_changed(page,top,1,2,3),
        "The first visible unit or restored scroll anchor did not follow its identity");
    // A removed top row may be replaced at the SAME zero-based index.
    // Ordinal equality must not authorize its stale absolute pixel offset.
    check(catalog_restore_entry_index(page,prefix+"retirada",0,3)==0 &&
          catalog_scroll_anchor_changed(page,prefix+"retirada",0,0,3) &&
          catalog_scroll_anchor_changed(page,prefix+"otra_edicion",0,0,3) &&
          !catalog_scroll_anchor_changed(page,{},0,0,3),
        "A retired top-row identity wrongly reused an unrelated row's saved pixels");
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



void invalid_edition_locations_recover_without_reusing_old_selection() {
    using namespace lgaed::curriculum;
    const CatalogLocation active_course{"fr","programming","cpp_fundamentals",{},{}};
    const CatalogLocation valid_unit{"fr","programming","cpp_fundamentals",
        "cpp_fundamentals__edition_fr_1__inicio",{}};
    const CatalogLocation valid_lesson{"fr","programming","cpp_fundamentals",
        valid_unit.unit,valid_unit.unit+"__primera"};
    validate_catalog_location(active_course);
    validate_catalog_location(valid_unit);
    validate_catalog_location(valid_lesson);
    const auto invalid=[&](CatalogLocation stale) {
        bool rejected=false;
        try {validate_catalog_location(stale);}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"A malformed edition identity was accepted as catalog navigation");
        // A previously persisted invalid entry must return to its published
        // ancestor, not appear available just because the probe is permissive.
        const auto original=stale;
        const auto nearest=stale.unit==valid_unit.unit?valid_unit:active_course;
        auto recovered=catalog_recover_location(std::move(stale),
            [&](const CatalogLocation& place) {
                // A buggy validator would accept the malformed saved leaf.
                // Only the published unit and course are valid ancestors.
                return place==original||place==valid_unit||place==active_course;
            });
        check(recovered==nearest,
            "An invalid saved edition location reopened the wrong ancestor");
    };
    for(const auto* bad:{
            "cpp_fundamentals__edition_fr_0__inicio",
            "cpp_fundamentals__edition_fr_01__inicio",
            "cpp_fundamentals__edition_fr_1000001__inicio",
            "cpp_fundamentals__edition_de_1__inicio",
            "cpp_fundamentals__edition_fr_1__inicio__parte",
            "cpp_fundamentals__edition_fr_1__-inicio",
            "cpp_fundamentals__edition_fr_1__inicio_"}) {
        auto stale=valid_unit;
        stale.unit=bad;
        invalid(stale);
    }
    for(const auto* bad:{
            "cpp_fundamentals__edition_fr_1__inicio__-parte",
            "cpp_fundamentals__edition_fr_1__inicio__parte_",
            "cpp_fundamentals__edition_fr_1__inicio__parte__extra"}) {
        auto stale=valid_lesson;
        stale.lesson=bad;
        invalid(stale);
    }
    invalid({"fr","programming","cpp_fundamentals",
        "cpp_fundamentals__legado",
        "cpp_fundamentals__legado__edition_fr_1__primera"});
}

void malformed_or_cross_edition_scope_is_atomic() {
    const std::string course="cpp_fundamentals";
    const std::string first_unit=course+"__edition_es_1__inicio";
    const std::string first_lesson=first_unit+"__primera";
    const CatalogScope original_scope{{course},{first_unit},{first_lesson}};

    const std::vector<std::string> invalid_units{
        course+"__edition_es_0__otra",
        course+"__edition_es_01__otra",
        course+"__edition_es_1000001__otra",
        course+"__edition_de_1__otra",
        course+"__edition_es_1__otra__subunidad",
        course+"__edition_es_1__",
        course+"__edition_es_1__otra_",
        course+"__edition_es_2__otra",
        course+"__edition_fr_1__otra"
    };
    for(const auto& invalid:invalid_units) {
        Fixture fixture;
        auto scope=original_scope;
        scope.units.push_back(invalid);
        const auto prior=lgaed::state::encode(fixture.state);
        bool reset_rejected=false,progress_rejected=false;
        try {(void)reset_catalog_progress(fixture.state,scope);}
        catch(const std::invalid_argument&) {reset_rejected=true;}
        try {(void)lgaed::curriculum::catalog_progress(fixture.state,scope);}
        catch(const std::invalid_argument&) {progress_rejected=true;}
        check(reset_rejected&&progress_rejected&&
              lgaed::state::encode(fixture.state)==prior,
              "A malformed or cross-generation unit altered historical progress");
    }
    for(const auto& invalid_lesson:{
            course+"__edition_es_1__inicio__primera__anexo",
            course+"__edition_es_1__inicio__",
            course+"__edition_es_2__inicio__primera"}) {
        Fixture fixture;
        auto scope=original_scope;
        scope.lessons.push_back(invalid_lesson);
        const auto prior=lgaed::state::encode(fixture.state);
        bool rejected=false;
        try {(void)reset_catalog_progress(fixture.state,scope);}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected&&lgaed::state::encode(fixture.state)==prior,
              "An invalid edition lesson was permitted to erase saved receipts");
    }

    // More than one unit is legal when both belong to the SAME active
    // generation. Their new successes may be reset, not the legacy course.
    Fixture fixture;
    const auto next_unit=course+"__edition_es_1__siguiente";
    const auto next_lesson=next_unit+"__primera";
    auto valid=original_scope;
    valid.units.push_back(next_unit);
    valid.lessons.push_back(next_lesson);
    auto& progress=fixture.state.preferences["learning_progress"];
    progress["lesson_"+first_lesson+"_completed"]=true;
    progress["lesson_"+next_lesson+"_completed"]=true;
    check(lgaed::curriculum::catalog_progress(fixture.state,valid).completed==2,
          "Two valid units of the same edition did not retain their own progress");
    check(reset_catalog_progress(fixture.state,valid)==2&&
          !progress.contains("lesson_"+first_lesson+"_completed")&&
          !progress.contains("lesson_"+next_lesson+"_completed")&&
          progress.contains("course_cpp_fundamentals_completed"),
          "An edition-only reset deleted the historic course or retained new answers");
}

void malformed_progress_scopes_never_count_an_edition_twice() {
    using namespace lgaed::curriculum;
    const auto first=std::string{"cpp_fundamentals__edition_es_1__inicio__primera"};
    const auto second=std::string{"cpp_fundamentals__edition_es_1__inicio__segunda"};
    const auto unit=std::string{"cpp_fundamentals__edition_es_1__inicio"};
    const CatalogScope good{{"cpp_fundamentals"},{unit},{first,second}};
    Fixture fixture;
    fixture.state.preferences["learning_progress"]["lesson_"+first+"_completed"]=true;
    const auto original=lgaed::state::encode(fixture.state);
    check(catalog_progress(fixture.state,good).completed==1 &&
          catalog_progress(fixture.state,good).total==2,
          "A correct new-edition index lost or duplicated its legitimate credit");
    for(int variant=0;variant<6;++variant) {
        auto bad=good;
        switch(variant) {
        case 0: bad.lessons.push_back(first); break;
        case 1: bad.units.push_back(unit); break;
        case 2: bad.courses.push_back("cpp_fundamentals"); break;
        case 3: bad.lessons.push_back("other_course__unidad__ajena"); break;
        case 4: bad.units.push_back("other_course__unidad"); break;
        case 5: bad.lessons.push_back("cpp_fundamentals__edition_es_2__inicio__ajena"); break;
        }
        bool rejected=false;
        try {(void)catalog_progress(fixture.state,bad);}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"Malformed catalog identities fabricated lesson completion");
        check(lgaed::state::encode(fixture.state)==original,
              "Rejected progress validation changed a learner's serialized data");
    }
    // A unit's weighted progress also uses the guarded counter. Supplying
    // plausible percentages must not bypass the invalid owner list.
    auto duplicated=good;
    duplicated.lessons.push_back(first);
    bool rejected=false;
    try {
        (void)catalog_unit_learning_progress(fixture.state,duplicated,
            std::vector<CatalogProgress>{{1,1},{0,1},{1,1}});
    }catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&lgaed::state::encode(fixture.state)==original,
          "An aggregate unit progress bar accepted duplicate lesson owners");
}

void new_edition_page_progress_uses_only_its_own_reading_and_answers() {
    using namespace lgaed::curriculum;
    Fixture fixture;
    const auto old_state=fixture.state;
    const std::string unit="cpp_fundamentals__edition_es_1__inicio";
    const std::string lesson_id=unit+"__primera";
    Lesson lesson;
    lesson.id=lesson_id;
    lesson.locale="es";
    lesson.state_layout.version=2;
    lesson.state_layout.locale="es";
    const auto add_page=[&](std::size_t index,BlockType type) {
        Page page;
        page.stable_id="page_000"+std::to_string(index);
        Block block;
        block.type=type;
        block.stable_id="block_000"+std::to_string(index)+"_0000";
        page.blocks.push_back(std::move(block));
        lesson.state_layout.origin.push_back({page.stable_id,{page.blocks.front().stable_id}});
        lesson.state_layout.current.push_back({page.stable_id,{page.blocks.front().stable_id}});
        lesson.pages.push_back(std::move(page));
    };
    add_page(0,BlockType::paragraph);
    add_page(1,BlockType::multiple_choice);
    add_page(2,BlockType::paragraph);
    const auto new_base="lesson_"+lesson_id;
    const auto answer_key="exercise_"+lesson_id+"_page_1_block_0";
    const auto read_key=new_base+"_read_blocks_v2_es";
    const CatalogScope scope{{"cpp_fundamentals"},{unit},{lesson_id}};
    const auto progress_for=[&](const StateData& data) {
        return catalog_lesson_page_progress(data,lesson,[&](const Block& block) {
            if(block.type!=BlockType::multiple_choice)return false;
            const auto evidence=lgaed::state::find(data,"learning_progress",answer_key);
            return evidence && std::get_if<bool>(evidence) && std::get<bool>(*evidence);
        });
    };

    // The previous course is complete, including an answered activity and
    // historical reading. The new three-page lesson has no new evidence.
    check(progress_for(fixture.state).completed==0&&progress_for(fixture.state).total==3,
        "A new edition inherited historical page reading or a graded response");
    auto& learning=fixture.state.preferences["learning_progress"];
    learning[new_base+"_started"]=true;
    learning[read_key]=std::vector<std::string>{
        "block_0000_0000","block_0002_0000"};
    lgaed::state::set_saved_page(fixture.state,lesson_id,2);
    check(progress_for(fixture.state).completed==2&&
        progress_for(fixture.state).percentage()==66,
        "New theory pages did not require their own reading receipts");
    learning[answer_key]=true;
    learning[new_base+"_completed"]=true;
    check(progress_for(fixture.state).completed==3&&
        progress_for(fixture.state).percentage()==100,
        "The new graded page did not require the new edition's response");
    const auto encoded=lgaed::state::encode(fixture.state);
    auto recovered=lgaed::state::decode(encoded);
    check(recovered==fixture.state&&lgaed::state::encode(recovered)==encoded&&
        progress_for(recovered).completed==3,
        "Importing a profile changed its new edition's page evidence");
    check(reset_catalog_progress(recovered,scope)==5,
        "Scoped reset did not clear the new reading, page, answer and completion receipts");
    check(progress_for(recovered).completed==0&&
        !catalog_lesson_started(recovered,lesson_id)&&
        lgaed::state::saved_page(recovered,lesson_id)==0&&
        !recovered.preferences.at("learning_progress").contains(answer_key)&&
        !recovered.preferences.at("learning_progress").contains(new_base+"_completed")&&
        std::get<std::vector<std::string>>(recovered.preferences.at("learning_progress").at(read_key)).empty(),
        "A reset new edition retained old reading or awarded a phantom page");
    for(const auto& [key,value]:old_state.preferences.at("learning_progress")) {
        check(recovered.preferences.at("learning_progress").at(key)==value,
            "A new edition page reset rewrote the previous course's progress");
    }
    check(recovered.preferences.at("cpp_lesson_drafts")==old_state.preferences.at("cpp_lesson_drafts")&&
        recovered.preferences.at("narration_settings")==old_state.preferences.at("narration_settings"),
        "Edition page reset rewrote the learner's drafts or general preferences");
}

void new_edition_progress_and_reset_preserve_retired_course() {
    using namespace lgaed::curriculum;
    Fixture fixture;
    const auto original=fixture.state;
    const std::string new_unit="cpp_fundamentals__edition_es_1__inicio";
    const std::string first=new_unit+"__primera";
    const std::string second=new_unit+"__segunda";
    const CatalogScope new_edition{{"cpp_fundamentals"},{new_unit},{first,second}};

    // The original course is complete, but its replacement has not been
    // studied. Do not reuse a completed flag, draft or page from its ancestor.
    const auto initial=catalog_progress(fixture.state,new_edition);
    check(initial.completed==0&&initial.total==2&&
        !catalog_lesson_completed(fixture.state,first)&&
        !catalog_lesson_started(fixture.state,first)&&
        !catalog_lesson_started(fixture.state,second),
        "A published new edition inherited the original course's successes");
    auto& progress=fixture.state.preferences["learning_progress"];
    progress["lesson_"+first+"_started"]=true;
    progress["lesson_"+first+"_completed"]=true;
    progress["exercise_"+first+"_page_0_block_1"]=std::string{"edition-specific answer"};
    progress["unit_"+new_unit+"_completed"]=true;
    check(catalog_progress(fixture.state,new_edition).completed==1&&
        catalog_lesson_started(fixture.state,first)&&
        !catalog_lesson_started(fixture.state,second),
        "The replacement's own receipts did not advance independently");

    const auto saved=lgaed::state::encode(fixture.state);
    auto restored=lgaed::state::decode(saved);
    check(restored==fixture.state&&
        lgaed::state::encode(restored)==saved&&
        catalog_progress(restored,new_edition).completed==1,
        "Saving and importing a replacement edition lost its independent progress");
    check(reset_catalog_progress(restored,new_edition)==4,
        "Reset of the new edition did not remove exactly its four new receipts");
    const auto& clean=restored.preferences.at("learning_progress");
    check(catalog_progress(restored,new_edition).completed==0&&
        !catalog_lesson_started(restored,first)&&
        !clean.contains("exercise_"+first+"_page_0_block_1")&&
        !clean.contains("unit_"+new_unit+"_completed"),
        "The new edition still had an answer or accomplishment after its reset");
    for(const auto& key : std::vector<std::string>{
            fixture.completion,
            fixture.answer,
            fixture.first_read,
            fixture.second_read,
            "unit_cpp_fundamentals__unit_completed",
            "course_cpp_fundamentals_completed",
            "lesson_other_course__unit__first_completed"}) {
        check(clean.at(key)==original.preferences.at("learning_progress").at(key),
            "An edition-only reset deleted a retired course or unrelated history");
    }
    for(const auto& domain:{"cpp_lesson_drafts","narration_settings"})
        check(restored.preferences.at(domain)==original.preferences.at(domain),
            "An edition-only reset modified drafts or global preferences");
    check(reset_catalog_progress(restored,new_edition)==0,
        "A repeat edition-only reset changed preserved historical history");

    // An explicitly mixed scope, in contrast, really includes legacy units.
    // It must still clear the selected historical course completion.
    auto mixed=fixture.state;
    CatalogScope selected_legacy=new_edition;
    selected_legacy.units.push_back(fixture.scope.units.front());
    selected_legacy.lessons.push_back(fixture.scope.lessons.front());
    (void)reset_catalog_progress(mixed,selected_legacy);
    check(!mixed.preferences.at("learning_progress").contains(
                "course_cpp_fundamentals_completed")&&
        !mixed.preferences.at("learning_progress").contains(fixture.completion),
        "Explicitly selecting the historical unit failed to reset its completion");
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
        retired_catalog_location_recovers_nearest_published_parent();
        invalid_edition_locations_recover_without_reusing_old_selection();
        catalog_unit_selection_is_scoped_to_published_edition();
        catalog_entry_identity_survives_reordering_and_retirement();
        malformed_or_cross_edition_scope_is_atomic();
        malformed_progress_scopes_never_count_an_edition_twice();
        new_edition_page_progress_uses_only_its_own_reading_and_answers();
        new_edition_progress_and_reset_preserve_retired_course();
        malformed_or_cross_course_scope_is_atomic();
        accepted_reset_keeps_foreign_state_and_is_idempotent();
        foreign_corruption_does_not_block_selected_course();
        std::cout << "PASS catalog reset: 8 malformed coverage slots and 9 invalid scopes rejected atomically; new-edition receipts isolated; "
                     "exact scope, drafts, provenance and binary state preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL catalog reset: " << error.what() << '\n';
        return 1;
    }
}
