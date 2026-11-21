// Regression for explicit selection of BOTH historic and new course content.
// Such a scope is intentional and may reset both generations. A scope that
// mixes different *new* epochs remains forbidden, preserving all stored data.
// This exercises the production Catalog and StateData on hosted Windows.
#include "core/catalog.hpp"
#include "core/lesson_state.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;

void require(bool correct,std::string_view reason) {
    if(!correct)throw std::runtime_error(std::string(reason));
}
template<class Action> void rejected(Action&& action,std::string_view reason) {
    bool did_reject=false;
    try { action(); } catch(const std::invalid_argument&) { did_reject=true; }
    require(did_reject,reason);
}
void verify_explicit_mixed_scope(state::StateData& data,
                                 const curriculum::CatalogScope& scope) {
    const auto original=data;
    auto selected=data;
    const auto summary=curriculum::catalog_progress(selected,scope);
    require(summary.total==scope.lessons.size(),
        "An explicitly selected mixed course miscounted its exact lessons");
    require(curriculum::reset_catalog_progress(selected,scope)>0,
        "An explicit legacy/new scope did not reset its selected receipts");
    require(state::find(selected,"learning_progress",
        "lesson_qa_other_course__unit__primero_completed"),
        "An explicit mixed reset erased another course's completion");
    for(const auto& domain:{"cpp_lesson_drafts","narration_settings"})
        require(selected.preferences.at(domain)==original.preferences.at(domain),
            "An explicit mixed reset changed source drafts or global audio preferences");
    // Selected reader scroll offsets ARE progress and must be removed. A
    // position from another course is not selected and must survive.
    const std::string historic="qa_course__original__primero";
    const auto scroll="scroll/"+historic+"/p0";
    const bool selected_historic=
        std::ranges::find(scope.lessons,historic)!=scope.lessons.end();
    const auto actual_scroll=state::find(selected,"native_windows",scroll);
    const auto original_scroll=state::find(original,"native_windows",scroll);
    require(original_scroll&&(selected_historic?!actual_scroll:
            actual_scroll&&*actual_scroll==*original_scroll),
        "Explicit course reset did not match the selected lesson's scroll ownership");
    const std::string foreign="scroll/qa_other_course__unit__primero/p0";
    const auto other=state::find(selected,"native_windows",foreign);
    const auto other_before=state::find(original,"native_windows",foreign);
    require(other&&other_before&&*other==*other_before,
        "A mixed scoped reset changed an unrelated course's scroll position");
    require(data==original,
        "A test-owned explicit mixed scope changed the source student state");
}
void verify_rejected_scope_unchanged(state::StateData& data,
                                     const curriculum::CatalogScope& scope) {
    const auto original=data;
    rejected([&]{ (void)curriculum::reset_catalog_progress(data,scope); },
        "An invalid cross-epoch or cross-parent course scope was admitted");
    rejected([&]{ (void)curriculum::catalog_progress(data,scope); },
        "An invalid course scope counted stale progress");
    require(data==original,
        "A rejected course scope changed the student's historical bytes");
}
void test_course_scope() {
    constexpr std::string_view course="qa_course";
    const std::string historic_unit="qa_course__original";
    const std::string historic_lesson="qa_course__original__primero";
    const std::string edition_unit="qa_course__edition_es_1__inicio";
    const std::string edition_lesson="qa_course__edition_es_1__inicio__primero";
    const std::string other_lesson="qa_other_course__unit__primero";

    state::StateData data;
    auto& progress=data.preferences["learning_progress"];
    progress["lesson_"+historic_lesson+"_completed"]=true;
    progress["lesson_"+edition_lesson+"_completed"]=true;
    progress["lesson_"+other_lesson+"_completed"]=true;
    progress["course_"+std::string(course)+"_completed"]=true;
    progress["unit_"+edition_unit+"_completed"]=true;
    data.preferences["cpp_lesson_drafts"]["programming/"+historic_lesson+"/0/0/source"]=
        std::string("historical source retained");
    data.preferences["cpp_lesson_drafts"]["programming/"+edition_lesson+"/0/0/source"]=
        std::string("new source retained");
    data.preferences["narration_settings"]["voice"]=std::string("ef_dora");
    data.preferences["native_windows"]["scroll/"+historic_lesson+"/p0"]=std::int64_t{17};
    data.preferences["native_windows"]["scroll/"+other_lesson+"/p0"]=std::int64_t{19};

    // Explicitly selecting both generations IS authorized: do not prevent
    // a user-requested reset of the old and new course in one operation.
    verify_explicit_mixed_scope(data,
        {{std::string(course)},{historic_unit,edition_unit},
            {historic_lesson,edition_lesson}});
    // Even without parent lists the selected unit/lesson owners are exact.
    verify_explicit_mixed_scope(data,
        {{},{historic_unit,edition_unit},{}});
    verify_explicit_mixed_scope(data,
        {{},{},{historic_lesson,edition_lesson}});
    // Selecting the new unit but a historic lesson of a DIFFERENT parent
    // must still fail atomically.
    verify_rejected_scope_unchanged(data,
        {{std::string(course)},{edition_unit},
            {historic_lesson,edition_lesson}});

    // Two NEW generations or locales of the SAME course remain forbidden.
    const std::string later="qa_course__edition_es_2__inicio";
    verify_rejected_scope_unchanged(data,
        {{std::string(course)},{edition_unit,later},{}});
    verify_rejected_scope_unchanged(data,
        {{std::string(course)},{edition_unit,"qa_course__edition_fr_1__debut"},{}});

    // Other courses may legitimately use different editions in the same
    // broad index. A selected SINGLE edition may clear only its own
    // learning progress while keeping historical drafts and preferences.
    const curriculum::CatalogScope other_course{
        {"qa_course","qa_other_course"},
        {edition_unit,"qa_other_course__edition_fr_2__debut"},
        {edition_lesson,"qa_other_course__edition_fr_2__debut__premier"}};
    require(curriculum::catalog_progress(data,other_course).total==2,
        "Independent courses were incorrectly forced into one edition epoch");
    const curriculum::CatalogScope current{
        {std::string(course)},{edition_unit},{edition_lesson}};
    const auto snapshot=data;
    require(curriculum::reset_catalog_progress(data,current)==2,
        "The valid new-edition scope did not reset exactly its own progress");
    require(!state::find(data,"learning_progress","lesson_"+edition_lesson+"_completed")&&
            !state::find(data,"learning_progress","unit_"+edition_unit+"_completed")&&
            state::find(data,"learning_progress","lesson_"+historic_lesson+"_completed")&&
            state::find(data,"learning_progress","course_"+std::string(course)+"_completed")&&
            state::find(data,"learning_progress","lesson_"+other_lesson+"_completed"),
        "A valid edition reset erased historical or other-course completion");
    for(const auto& domain:{"cpp_lesson_drafts","narration_settings","native_windows"})
        require(data.preferences.at(domain)==snapshot.preferences.at(domain),
            "Reset changed unrelated draft, narration or native preferences");
    require(curriculum::reset_catalog_progress(data,current)==0,
        "A repeated new-edition scope reset was not idempotent");

    // Existing legacy-only course resets still work. No new edition in their
    // requested scope may silently keep a legacy completion flag.
    const curriculum::CatalogScope old{{std::string(course)},{historic_unit},{historic_lesson}};
    // Two completion receipts AND the lesson's selected scroll marker are
    // cleared. Neither general preferences nor an unrelated course's scroll
    // may be counted as selected historical progress.
    require(curriculum::reset_catalog_progress(data,old)==3&&
            !state::find(data,"learning_progress","lesson_"+historic_lesson+"_completed")&&
            !state::find(data,"learning_progress","course_"+std::string(course)+"_completed")&&
            !state::find(data,"native_windows","scroll/"+historic_lesson+"/p0")&&
            state::integer_value(data,"native_windows",
                "scroll/"+other_lesson+"/p0")==19,
        "A legacy-only scoped reset lost its selected receipts or changed unrelated scroll");
    for(const auto& domain:{"cpp_lesson_drafts","narration_settings"})
        require(data.preferences.at(domain)==snapshot.preferences.at(domain),
            "Reset unexpectedly deleted a draft or changed global audio preferences");
    const auto completed=data;
    require(curriculum::reset_catalog_progress(data,old)==0&&data==completed,
        "Repeating the explicitly scoped historic reset changed preserved data");
}
}
int main() {
    try {
        test_course_scope();
        std::cout<<"PASS Windows course edition scope: explicitly mixed "
            "historical/new reset remains authorized, invalid new epochs "
            "reject atomically, drafts and other courses remain untouched.\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL course edition reset scope: "<<error.what()<<'\n';
        return 1;
    }
}
