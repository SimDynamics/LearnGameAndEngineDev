#include "core/crt_output_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include <iostream>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const Block& editor(const Lesson& lesson){const auto at=stable_block_position(lesson,test::crt_output_editor_id);
    if(!at)throw std::runtime_error("Missing CRT fixture owner");return lesson.pages[at->page].blocks[at->block];}
state::StateData reopen(const state::StateStore& store){state::StateStore reopened(store.root().parent_path());return reopened.load();}
state::Preferences receipts(const state::StateData& data){state::Preferences result;
    if(const auto found=data.preferences.find("native_windows");found!=data.preferences.end())
        for(const auto& [key,value]:found->second)if(key.starts_with("compiler-puzzle/v1/")&&key.find("/review/")==key.npos)
            result.emplace(key,value);
    return result;
}
void seed(state::StateData& data,const Lesson& lesson){
    state::prepare_lesson_state(data,lesson);
    data.preferences["crt-feedback-qa"]["opaque"]=std::string("Conservar\t\r\n");
    state::Draft draft;draft.source=editor(lesson).initial_code+"\n// QA draft\n";
    draft.input="";draft.selection_start=3;draft.selection_end=7;
    state::put_draft(data,draft_key("programming",lesson,editor(lesson)),draft);
    state::put_response(data,"crt-feedback-qa-other-response",state::Response{"Unsubmitted QA answer",false});
    narration::History history;
    for(unsigned n=0;n<3;++n)history.entries.push_back({"crt-qa-audio-"+std::to_string(n),"crt-qa-stable-"+std::to_string(n),
        narration::Position{1,static_cast<std::int64_t>(20+n),0,false},"crt-qa-page-"+std::to_string(n)});
    narration::write_history(data,history);state::save_current_page(data,lesson,5);
}
}
int main()try{
    // Model adapter fixtures, never a claim of new compilation or listening.
    // All persistence belongs to a fresh directory under this checkout's QA.
    const auto original=test::load_pre_crt_output_lesson();
    const auto variant=test::with_crt_output_variant(original);
    const std::vector<OutputReviewFeedback> guides{{{""},"QA: no text was produced."},{{"one sample"},"QA: the full report is required."}};
    const auto guided=test::with_crt_output_guidance(variant,"QA: the exact report matches.",guides);
    const auto metadata=parse_lesson_progress_metadata(guided.raw_json);
    const auto& old=editor(original);const auto& current=editor(guided);const auto& projected=editor(metadata);
    check(old.initial_code==current.initial_code&&current.output_check->cases.size()==1&&
        current.output_check->cases.front().outputs==editor(variant).output_check->cases.front().outputs,
        "Guidance changed executable source or exact raw output alternatives");
    check(current.crt_output_original==editor(variant).crt_output_original&&projected.crt_output_original==current.crt_output_original,
        "Full/catalog loading lost closed CRT provenance after adding guidance");
    check(reference_objective_frame(original,old)==reference_objective_frame(guided,current)&&
        reference_objective_frame(metadata,projected)==reference_objective_frame(original,old),
        "Presentation-only guidance changed an execution objective");
    const auto qa=std::filesystem::path(build::source_root)/"out/qa"/
        ("crt-feedback-state-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64()));
    for(const bool earned:{false,true}){
        state::StateStore store(qa/(earned?"earned":"unattempted"));auto data=store.load();seed(data,original);
        CppRunRequest capture;capture.revision=41;capture.source_utf8="// QA captured reference source";
        FreshExecution execution;execution.source_revision=41;execution.compilation_succeeded=true;
        execution.execution_succeeded=true;execution.execution_completed=true;execution.exit_code=0;
        execution.standard_output=old.output_check->cases.front().outputs.front();
        if(earned)check(record_compiler_puzzle_case(data,original,old,capture,execution,"Correcto\nQA original guide"),
            "Could not create isolated adapter completion fixture");
        const auto prior_receipts=receipts(data);const auto prior_markers=narration::read_history(data);
        const auto prior_draft=state::get_draft(data,draft_key("programming",original,old),old.initial_code);
        store.save(data);check(reopen(store)==data,"Original isolated state did not round trip");
        state::prepare_lesson_state(data,variant);state::prepare_lesson_state(data,guided);
        state::validate_lesson_state(data,guided);state::validate_lesson_state(data,metadata);
        check(compiler_puzzle_completed(data,guided,current)==earned&&compiler_puzzle_completed(data,metadata,projected)==earned,
            "Adding guidance created or removed an achievement");
        check(receipts(data)==prior_receipts&&narration::read_history(data)==prior_markers,
            "Adding guidance rewrote execution receipts or the three paused bookmarks");
        const auto draft=state::get_draft(data,draft_key("programming",guided,current),current.initial_code);
        check(draft.source==prior_draft.source&&draft.input==prior_draft.input&&draft.selection_start==prior_draft.selection_start&&
            draft.selection_end==prior_draft.selection_end&&state::get_response(data,"crt-feedback-qa-other-response")->answer=="Unsubmitted QA answer",
            "Guidance adoption changed a draft, selection or unrelated answer");
        check(state::string_value(data,"crt-feedback-qa","opaque")=="Conservar\t\r\n","Guidance changed unrelated preferences");
        store.save(data);auto restored=reopen(store);check(restored==data,"Guidance state did not round trip exactly");
        auto wrong=capture;wrong.revision=42;wrong.source_utf8=current.initial_code;
        auto rejected=execution;rejected.source_revision=42;rejected.standard_output="";
        const auto evaluated=evaluate_cpp_output_check(*current.output_check,rejected,42);
        check(evaluated.verdict==OutputVerdict::incorrect&&evaluated.feedback==guides.front().text&&
            evaluated.feedback_path=="/outputCheck/cases/0/reviewFeedback/0/text","Typed new guidance was not evaluated");
        check(record_compiler_puzzle_review(restored,guided,current,wrong,rejected,"Incorrecto\n"+evaluated.feedback),
            "A valid isolated rejected run did not retain its new guidance");
        store.save(restored);restored=reopen(store);
        const auto latest=compiler_puzzle_current_feedback(restored,guided,current,wrong);
        check(latest&&!latest->correct&&latest->text=="Incorrecto\n"+guides.front().text&&
            compiler_puzzle_completed(restored,guided,current)==earned&&receipts(restored)==prior_receipts&&
            narration::read_history(restored)==prior_markers,"Reopening guidance lost its review or changed historical state");
        auto stale=rejected;--stale.source_revision;const auto protected_state=restored;
        check(!record_compiler_puzzle_review(restored,guided,current,wrong,stale,"stale")&&restored==protected_state,
            "A stale result altered the reviewed state");
        const auto revised=test::with_crt_output_guidance(guided,"QA: revised report wording.",guides);
        state::prepare_lesson_state(restored,revised);state::validate_lesson_state(restored,revised);
        check(receipts(restored)==prior_receipts&&narration::read_history(restored)==prior_markers&&
            compiler_puzzle_completed(restored,revised,editor(revised))==earned,
            "A second guidance revision replaced the original proof or bookmarks");
        store.save(restored);check(reopen(store)==restored,"Second guidance revision failed exact reopen");
        if(earned){
            // A real recheck is modelled here after an older success without
            // these guides. Its historical receipt remains byte-for-byte old;
            // reopening must still show the current sole-case explanation.
            auto annotated=guided;const auto at=stable_block_position(annotated,test::crt_output_editor_id);
            auto& shown=annotated.pages[at->page].blocks[at->block];
            const auto& explanation=shown.output_check->cases.front().explanation;
            constexpr auto path="/outputCheck/cases/0/explanation";
            shown.text_semantics[path]=TextSemantics{explanation,{{0,explanation.size(),explanation,SemanticRole::output}}};
            auto checked=capture;checked.revision=43;auto accepted=execution;accepted.source_revision=43;
            accepted.standard_output=shown.output_check->cases.front().outputs.at(1);
            check(record_compiler_puzzle_case(restored,annotated,shown,checked,accepted,"Correcto\n"+explanation),
                "A fresh CRT recheck failed to retain its original achievement");
            store.save(restored);restored=reopen(store);
            const auto current_feedback=compiler_puzzle_current_feedback(restored,annotated,shown,checked);
            check(current_feedback&&current_feedback->correct&&current_feedback->text=="Correcto\n"+explanation&&
                current_feedback->source_path==path,"Reopening an earned CRT recheck lost its current success explanation or exact semantic origin");
            check(receipts(restored)==prior_receipts&&narration::read_history(restored)==prior_markers,
                "Restoring the current explanation rewrote original CRT receipts or paused page slots");
            auto incorrect=accepted;incorrect.standard_output="";
            check(record_compiler_puzzle_review(restored,annotated,shown,checked,incorrect,"Incorrecto\n"+guides.front().text),
                "The CRT success explanation prevented a later rejected attempt");
            store.save(restored);restored=reopen(store);const auto rejected_again=compiler_puzzle_current_feedback(restored,annotated,shown,checked);
            check(rejected_again&&!rejected_again->correct&&rejected_again->text=="Incorrecto\n"+guides.front().text&&
                receipts(restored)==prior_receipts,"An old achievement's explanation overrode the latest actual rejection");
        }
    }
    std::cout<<"PASS CRT guidance adoption/revision: raw controls and objective preserved, earned/unattempted state, exact reopen, draft and three bookmarks; adapter fixtures only, no compilation/UI/audio.\n";
    return 0;
}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
