// Regression for edition-scoped course progress: a mixed historical/new
// selection is not a publishable course index, and must NEVER erase answers.
// This uses the production catalog and StateData implementations on Windows.
#include "core/catalog.hpp"
#include "platform/windows/state_store.hpp"

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
void verify_scope_rejected_unchanged(state::StateData& data,
                                     const curriculum::CatalogScope& scope) {
    const auto original=data;
    rejected([&]{ (void)curriculum::reset_catalog_progress(data,scope); },
        "A mixed historical/new course reset was admitted");
    require(data==original,"Rejecting a mixed scope changed a student's stored data");
    rejected([&]{ (void)curriculum::catalog_progress(data,scope); },
        "An invalid mixed course was still counted as published progress");
    require(data==original,"A read-only course summary mutated stored data");
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

    // A course-level scope from one published index cannot contain both
    // historical and edition resources, even with valid parent-child names.
    verify_scope_rejected_unchanged(data,
        {{std::string(course)},{historic_unit,edition_unit},{historic_lesson,edition_lesson}});

    // Do not trust missing parent arrays: the exact same course distinction
    // holds when the caller passes only unit or only lesson identities.
    verify_scope_rejected_unchanged(data,
        {{},{historic_unit,edition_unit},{}});
    verify_scope_rejected_unchanged(data,
        {{},{},{historic_lesson,edition_lesson}});
    verify_scope_rejected_unchanged(data,
        {{std::string(course)},{edition_unit},{historic_lesson,edition_lesson}});

    // Two new generations or locales are also forbidden; no historical
    // entry can disguise a cross-epoch set.
    const std::string later="qa_course__edition_es_2__inicio";
    verify_scope_rejected_unchanged(data,
        {{std::string(course)},{edition_unit,later},{}});
    verify_scope_rejected_unchanged(data,
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
    require(curriculum::reset_catalog_progress(data,old)==2&&
            !state::find(data,"learning_progress","lesson_"+historic_lesson+"_completed")&&
            !state::find(data,"learning_progress","course_"+std::string(course)+"_completed"),
        "A legacy-only scoped reset was broken by the edition guard");
    require(data.preferences.at("cpp_lesson_drafts")==snapshot.preferences.at("cpp_lesson_drafts"),
        "Reset unexpectedly deleted a saved source from either generation");
}
}
int main() {
    try {
        test_course_scope();
        std::cout<<"PASS Windows course edition scope: mixed legacy/new identities are "
            "rejected atomically; isolated editions and historical-only resets "
            "retain unrelated completions, drafts and narration.\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL course edition reset scope: "<<error.what()<<'\n';
        return 1;
    }
}
