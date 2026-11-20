#include "core/compile_check_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "presentation/windows/text_semantic_composition.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <regex>
#include <set>

namespace lgaed::test {
int math_matching_text_live_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int indexed_unique_transfer_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int defaulted_move_policy_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int independent_copy_assignment_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int move_assignment_invariant_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int array_owner_read_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int array_copy_move_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
int array_return_take_state_windows(
    const std::filesystem::path& content,const std::filesystem::path& qa);
}

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
constexpr std::string_view explanation_path="/outputCheck/cases/0/explanation";
void require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
void rejects_both(const JsonObject& root,std::string_view expected){
    const auto raw=winrt::to_string(root.Stringify());
    for(const bool metadata:{false,true}){bool rejected=false;
        try{if(metadata)(void)parse_lesson_progress_metadata(raw,"es");else(void)parse_lesson_json(raw,"es");}
        catch(const std::exception& error){rejected=true;require(std::string_view(error.what()).find(expected)!=std::string_view::npos,
            "A malformed snapshot failed for an unrelated reason");}
        require(rejected,"An unpublished or changed diagnostic snapshot was admitted");}
}
Block& editor(Lesson& lesson,std::string_view id){const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"Missing current QA compiler owner");return lesson.pages[at->page].blocks[at->block];}
const Block& editor(const Lesson& lesson,std::string_view id){const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"Missing current QA compiler owner");return lesson.pages[at->page].blocks[at->block];}
JsonObject editor(const JsonObject& root,const Lesson& lesson,std::string_view id){const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"Missing JSON QA compiler owner");return root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block));}
Lesson guidance(const Lesson& before,std::string_view id,std::string_view text){
    const auto root=JsonObject::Parse(winrt::to_hstring(before.raw_json)),block=editor(root,before,id);
    const auto old=test::compile_check_test_fingerprint(block);
    const auto goal=block.HasKey(L"compileCheck")?block.GetNamedObject(L"compileCheck"):
        block.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
    if(text.empty()){if(goal.HasKey(L"explanation"))goal.Remove(L"explanation");}
    else goal.Insert(L"explanation",JsonValue::CreateStringValue(winrt::to_hstring(text)));
    const auto now=test::compile_check_test_fingerprint(block);
    if(old!=now){JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));
        edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(old)));
        edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(now)));
        root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(edge);
        // An annotation authenticates the whole exact source field. Retire
        // only the changed QA guide's descriptor in this in-memory revision;
        // leave every unrelated authored source and annotation intact.
        if(root.HasKey(L"textSemantics")){
            const auto fields=root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
            const auto path=block.HasKey(L"compileCheck")?L"/compileCheck/explanation":L"/outputCheck/cases/0/explanation";
            for(unsigned n=fields.Size();n>0;--n){const auto field=fields.GetObjectAt(n-1);
                if(field.GetNamedString(L"block")==winrt::to_hstring(id)&&field.GetNamedString(L"path")==path)fields.RemoveAt(n-1);}
        }
    }
    auto result=parse_lesson_json(winrt::to_string(root.Stringify()),before.locale);result.source_path=before.source_path;return result;
}
Lesson retire_heading(const Lesson& source){
    const auto root=JsonObject::Parse(winrt::to_hstring(source.raw_json)),layout=root.GetNamedObject(L"stateLayout");
    const auto pages=root.GetNamedArray(L"pages"),positions=layout.GetNamedArray(L"current");
    const auto blocks=pages.GetObjectAt(0).GetNamedArray(L"blocks"),ids=positions.GetObjectAt(0).GetNamedArray(L"blocks");
    require(ids.Size()>1&&blocks.GetObjectAt(0).GetNamedString(L"type")==L"heading","QA retirement needs its actual leading heading");
    const auto id=ids.GetStringAt(0);JsonArray retired;retired.Append(JsonValue::CreateStringValue(id));JsonObject snapshot;
    snapshot.Insert(L"retired",retired);snapshot.Insert(L"current",JsonArray::Parse(positions.Stringify()));
    snapshot.Insert(L"pages",JsonArray::Parse(pages.Stringify()));snapshot.Insert(L"compilerPuzzlesRequired",
        root.HasKey(L"compilerPuzzlesRequired")?root.GetNamedValue(L"compilerPuzzlesRequired"):JsonValue::CreateBooleanValue(false));
    for(const auto name:{L"referenceAfter",L"activityAfter"})snapshot.Insert(name,
        root.HasKey(name)?JsonArray::Parse(root.GetNamedArray(name).Stringify()):JsonArray{});
    // A retired heading can have real authored semantic spans. Capture the
    // full former reading overlay before removing its active owner. Leaving a
    // live text descriptor for a retired block is not a valid publication.
    if(root.HasKey(L"textSemantics")) {
        const auto overlay=root.GetNamedObject(L"textSemantics");
        snapshot.Insert(L"textSemantics",JsonObject::Parse(overlay.Stringify()));
        const auto fields=overlay.GetNamedArray(L"fields");
        for(unsigned n=fields.Size();n>0;--n)
            if(fields.GetObjectAt(n-1).GetNamedString(L"block")==id)
                fields.RemoveAt(n-1);
    }
    JsonObject record;record.Insert(L"id",JsonValue::CreateStringValue(id));record.Insert(L"page",positions.GetObjectAt(0).GetNamedValue(L"id"));
    record.Insert(L"next",ids.GetAt(1));record.Insert(L"block",JsonObject::Parse(blocks.GetObjectAt(0).Stringify()));
    if(!layout.HasKey(L"retired"))layout.Insert(L"retired",JsonArray{});layout.GetNamedArray(L"retired").Append(record);
    if(!layout.HasKey(L"retirementHistory"))layout.Insert(L"retirementHistory",JsonArray{});layout.GetNamedArray(L"retirementHistory").Append(snapshot);
    ids.RemoveAt(0);blocks.RemoveAt(0);return parse_lesson_json(winrt::to_string(root.Stringify()),source.locale);
}
state::Preferences receipts(const state::StateData& data){state::Preferences result;
    if(const auto values=data.preferences.find("native_windows");values!=data.preferences.end())
        for(const auto& [key,value]:values->second)if(key.starts_with("compiler-puzzle/v1/")&&key.find("/review/")==key.npos)
            result.emplace(key,value);
    return result;
}
state::StateData reopen(const state::StateStore& store){state::StateStore opened(store.root().parent_path());return opened.load();}
CppRunRequest request(const Block& block){CppRunRequest result;result.request_id=11;result.revision=71;result.source_utf8=block.initial_code;
    if(block.output_check){result.stdin_utf8=block.output_check->cases.front().standard_input;
        if(block.cmake){result.cmake=block.cmake;result.cmake->target=block.output_check->cases.front().executable.value_or("");}}
    return result;
}
FreshExecution execution(const Block& block,const CppRunRequest& capture){FreshExecution result;result.source_revision=capture.revision;
    result.compilation_succeeded=true;result.execution_succeeded=true;result.execution_completed=true;
    const auto& item=block.output_check->cases.front();result.standard_output=item.outputs.front();
    result.standard_input=capture.stdin_utf8;result.executable=item.executable;
    result.standard_error=item.standard_error==StandardErrorExpectation::non_blank?"QA stderr":"";result.exit_code=item.exit_code;
    result.execution_succeeded=item.exit_code==0;return result;
}
void seed(state::StateData& data,const Lesson& lesson,std::string_view id){state::prepare_lesson_state(data,lesson);
    state::Draft draft;draft.source=editor(lesson,id).initial_code+"\n// QA saved draft\n";draft.input="QA draft input";
    draft.selection_start=2;draft.selection_end=8;state::put_draft(data,draft_key("programming",lesson,editor(lesson,id)),draft);
    state::put_response(data,"guidance-qa-unrelated-response",state::Response{"Unsubmitted answer",false});
    data.preferences["guidance-qa"]["opaque"]=std::string(" exact\t\r\n");
    narration::History history;for(unsigned i=0;i<3;++i)history.entries.push_back({"qa-audio-"+std::to_string(i),
        "qa-owner-"+std::to_string(i),{i,static_cast<std::int64_t>(120+i),0,false},"qa-page-"+std::to_string(i)});
    narration::write_history(data,history);state::save_current_page(data,lesson,3);
}
void sole_output_sequence(const Lesson& actual,std::string_view id,const std::filesystem::path& qa){
    // This captures only the model boundary. It never compiles, plays audio or
    // accesses a learner profile, and cannot certify a student's real program.
    const auto original=guidance(actual,id,"");const auto& old=editor(original,id);
    require(old.output_check&&old.output_check->cases.size()==1,"QA requires one actual authored output case");
    auto revised=guidance(original,id,"QA current guide N=1");auto& current=editor(revised,id);
    current.text_semantics[std::string(explanation_path)]={"QA current guide N=1",{{0,20,"QA current guide N=1",SemanticRole::output}}};
    // Correct UTF-16 descriptor text, without guessing an origin from wording.
    auto& descriptor=current.text_semantics.at(std::string(explanation_path));descriptor.spans.front().end=descriptor.source.size();
    validate_text_semantics(descriptor);
    const auto metadata=parse_lesson_progress_metadata(revised.raw_json,revised.locale);
    require(reference_objective_frame(original,old)==reference_objective_frame(revised,current)&&
        reference_objective_frame(metadata,editor(metadata,id))==reference_objective_frame(original,old),
        "A guide-only revision changed the executable objective");
    const auto old_layout=JsonObject::Parse(winrt::to_hstring(original.state_layout.raw_json));
    const auto new_layout=JsonObject::Parse(winrt::to_hstring(revised.state_layout.raw_json));
    require(original.state_layout.signatures==revised.state_layout.signatures&&
        test::compile_check_test_fingerprint(old_layout.GetNamedArray(L"origin"))==test::compile_check_test_fingerprint(new_layout.GetNamedArray(L"origin"))&&
        test::compile_check_test_fingerprint(old_layout.GetNamedObject(L"family"))==test::compile_check_test_fingerprint(new_layout.GetNamedObject(L"family")),
        "Guide-only authoring changed IDs, signatures or family");
    for(const bool earned:{false,true}){
        state::StateStore store(qa/(std::string(id)+(earned?"-earned":"-unattempted")));auto data=store.load();seed(data,original,id);
        auto captured=request(old);const auto accepted=execution(old,captured);
        if(earned)require(record_compiler_puzzle_case(data,original,old,captured,accepted,"Correcto\nQA historical body"),
            "Could not seed the explicit adapter achievement");
        const auto saved_receipts=receipts(data);const auto saved_markers=narration::read_history(data);
        const auto saved_draft=state::get_draft(data,draft_key("programming",original,old),old.initial_code);
        store.save(data);require(reopen(store)==data,"Original state failed exact round trip");
        state::prepare_lesson_state(data,revised);state::validate_lesson_state(data,revised);state::validate_lesson_state(data,metadata);
        require(compiler_puzzle_completed(data,revised,current)==earned&&compiler_puzzle_completed(data,metadata,editor(metadata,id))==earned,
            "Current guide granted or revoked a completion");
        require(receipts(data)==saved_receipts&&narration::read_history(data)==saved_markers,"Guide adoption rewrote receipts or three bookmark slots");
        const auto draft=state::get_draft(data,draft_key("programming",revised,current),current.initial_code);
        require(draft.source==saved_draft.source&&draft.input==saved_draft.input&&draft.selection_start==saved_draft.selection_start&&
            draft.selection_end==saved_draft.selection_end&&state::get_response(data,"guidance-qa-unrelated-response")->answer=="Unsubmitted answer"&&
            state::string_value(data,"guidance-qa","opaque")==" exact\t\r\n","Guide adoption changed independent state");
        store.save(data);data=reopen(store);const auto unchanged=data;
        auto feedback=compiler_puzzle_current_feedback(data,revised,current,captured);
        require((feedback.has_value()==earned)&&(!earned||(feedback->correct&&feedback->text=="Correcto\nQA current guide N=1"&&
            feedback->source_path==explanation_path)),"A reopened sole-case goal lost its current typed guide");
        require(compiler_puzzle_feedback(data,revised,current)==
                (earned?std::string("Correcto\nQA current guide N=1"):std::string{}),
            "The direct feedback getter did not honor authenticated current guidance");
        require(data==unchanged,"The current presentation getter mutated an original receipt");
        if(!earned)continue;
        auto second=guidance(revised,id,"QA next guide N=2");const auto& latest=editor(second,id);
        state::prepare_lesson_state(data,second);store.save(data);data=reopen(store);
        feedback=compiler_puzzle_current_feedback(data,second,latest,captured);
        require(feedback&&feedback->text=="Correcto\nQA next guide N=2"&&feedback->source_path==explanation_path&&
            receipts(data)==saved_receipts&&narration::read_history(data)==saved_markers,"A second guide revision lost immutable state");
        require(compiler_puzzle_feedback(data,second,latest)=="Correcto\nQA next guide N=2",
            "A second authenticated guide revision retained stale direct feedback");
        auto rejected=accepted;rejected.standard_output="QA output that cannot satisfy the exact report";
        require(record_compiler_puzzle_review(data,second,latest,captured,rejected,"Incorrecto\nQA latest rejection"),"Could not seed exact rejected recheck");
        store.save(data);data=reopen(store);feedback=compiler_puzzle_current_feedback(data,second,latest,captured);
        require(feedback&&!feedback->correct&&feedback->text=="Incorrecto\nQA latest rejection"&&receipts(data)==saved_receipts,
            "A derived success guide overrode the latest rejected attempt");
        require(record_compiler_puzzle_case(data,second,latest,captured,accepted,"Correcto\nignored new receipt"),"A successful recheck was rejected");
        require(receipts(data)==saved_receipts,"A successful recheck rewrote the historical receipt");
        // Contract changes cannot reuse the earned proof. Each mutation is of
        // the actual current model rather than a disconnected Block copy.
        for(unsigned change=0;change<7;++change){auto hostile=second;auto& bad=editor(hostile,id);
            if(change==0)bad.initial_code+="\n// changed authored source";
            if(change==1)bad.output_check->cases.front().outputs.front()+='x';
            if(change==2)bad.output_check->cases.front().standard_input+='x';
            if(change==3)bad.output_check->cases.push_back(bad.output_check->cases.front());
            if(change==4)bad.output_check->cases.front().exit_code=1;
            if(change==5)bad.output_check->cases.front().executable="another-target";
            if(change==6)bad.output_check->cases.front().standard_error=bad.output_check->cases.front().standard_error==StandardErrorExpectation::ignored?
                StandardErrorExpectation::non_blank:StandardErrorExpectation::ignored;
            require(!compiler_puzzle_completed(data,hostile,bad)&&!compiler_puzzle_current_feedback(data,hostile,bad,captured),
                "A changed source/input/output/count/status/target/stderr borrowed an old success");
        }
        auto legacy=second;legacy.state_layout={};feedback=compiler_puzzle_current_feedback(data,legacy,editor(legacy,id),captured);
        require(feedback&&feedback->text=="Correcto\nQA historical body"&&feedback->source_path.empty()&&
            compiler_puzzle_feedback(data,legacy,editor(legacy,id))=="Correcto\nQA historical body",
            "An unvalidated legacy model borrowed current-authored direct feedback");
        if(id=="block_0009_0002"){auto copy=latest;feedback=compiler_puzzle_current_feedback(data,second,copy,captured);
            require(feedback&&feedback->text=="Correcto\nQA historical body"&&feedback->source_path.empty()&&
                compiler_puzzle_feedback(data,second,copy)=="Correcto\nQA historical body",
                "An unbound block copy acquired active-author feedback");}
    }
}
CppRunRequest diagnostic_request(const Block& block){auto captured=request(block);const auto at=captured.source_utf8.rfind("    return 0;");
    require(at!=std::string::npos,"Diagnostic fixture lost its source anchor");captured.source_utf8.insert(at,"    fija.cambiar_a(4,2);\n");
    captured.diagnostic=prepare_cpp_diagnostic_request(block,captured);return captured;
}
CppRunResult diagnostic_capture(const CppRunRequest& captured){CppRunResult result;result.request_id=captured.request_id;result.revision=captured.revision;
    result.run_directory=L"C:/qa-guidance-diagnostic";result.compilation={CppPhaseStatus::completed,2};result.diagnostic.emplace();
    auto& evidence=*result.diagnostic;evidence.control={CppPhaseStatus::completed,0};
    evidence.source_sha256=state::sha256(captured.source_utf8);evidence.control_sha256=state::sha256(captured.diagnostic->control_source_utf8);
    const auto file=result.run_directory/L"candidate/main.cpp";const auto name=file.u8string();
    result.compilation.stderr_bytes=std::string(reinterpret_cast<const char*>(name.data()),name.size())+'('
        +std::to_string(captured.diagnostic->first_line)+",1): error "+captured.diagnostic->expected_error_code+": QA model only.\n";
    evidence.errors.push_back({captured.diagnostic->expected_error_code,file,captured.diagnostic->first_line,1});return result;
}
void diagnostic_sequence(const std::filesystem::path& content,const std::filesystem::path& qa){
    constexpr std::string_view id="block_0006_0006";
    const auto actual=load_lesson(content/"datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json");
    const auto original=guidance(actual,id,""),revised=guidance(original,id,"QA current diagnostic guide");
    const auto& old=editor(original,id);const auto& current=editor(revised,id);
    require(reference_objective_frame(original,old)==reference_objective_frame(revised,current)&&
        original.state_layout.signatures==revised.state_layout.signatures,"A diagnostic guide changed its protected contract");
    const auto metadata=parse_lesson_progress_metadata(revised.raw_json,revised.locale);
    // An authentic old snapshot protects the diagnostic, while allowing a
    // later guide-only edit. It is not a forged reading revision from hashes.
    const auto retired=retire_heading(original),archived=guidance(retired,id,"QA revised guide with an intact old snapshot");
    require(!archived.state_layout.retirement_history.empty()&&
        editor(archived,id).compile_check->diagnostic==old.compile_check->diagnostic,
        "A guide-only edit invalidated an authentic diagnostic reading snapshot");
    (void)parse_lesson_progress_metadata(archived.raw_json,archived.locale);
    const auto snapshot_editor=[&](const JsonObject& root){const auto at=stable_block_position(original,id);
        return root.GetNamedObject(L"stateLayout").GetNamedArray(L"retirementHistory").GetObjectAt(0)
            .GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page)).GetNamedArray(L"blocks")
            .GetObjectAt(static_cast<unsigned>(at->block)).GetNamedObject(L"compileCheck");};
    const auto forged=JsonObject::Parse(winrt::to_hstring(archived.raw_json));
    snapshot_editor(forged).Insert(L"explanation",JsonValue::CreateStringValue(L"QA fabricated unpublished historical guide"));
    rejects_both(forged,"exact occurrence");
    const auto wrong_diagnostic=JsonObject::Parse(winrt::to_hstring(archived.raw_json));
    snapshot_editor(wrong_diagnostic).Insert(L"diagnostic",JsonValue::CreateStringValue(L"unknown-qa-diagnostic"));
    rejects_both(wrong_diagnostic,"Unknown reviewed compilation diagnostic");
    state::StateStore store(qa/"diagnostic");auto data=store.load();seed(data,original,id);const auto capture=diagnostic_request(old);
    require(record_compiler_diagnostic_case(data,original,old,capture,diagnostic_capture(capture),"Correcto\nQA original diagnostic guide"),
        "Could not seed explicit accepted diagnostic model");
    const auto saved_receipts=receipts(data);const auto markers=narration::read_history(data);state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);state::validate_lesson_state(data,metadata);store.save(data);data=reopen(store);
    const auto before=data;const auto feedback=compiler_puzzle_current_feedback(data,revised,current,capture);
    require(feedback&&feedback->correct&&feedback->text=="Correcto\nQA current diagnostic guide"&&feedback->source_path=="/compileCheck/explanation"&&
        receipts(data)==saved_receipts&&narration::read_history(data)==markers&&data==before,"A diagnostic guide failed exact immutable restoration");
    require(compiler_puzzle_feedback(data,revised,current)=="Correcto\nQA current diagnostic guide",
        "An authenticated compiler diagnostic guide retained its old direct feedback");
    auto hostile=revised;editor(hostile,id).compile_check->diagnostic="other-diagnostic";
    require(!compiler_puzzle_current_feedback(data,hostile,editor(hostile,id),capture),"A changed diagnostic borrowed an earned explanation");
}
void multicase_isolation(const Lesson& actual){
    constexpr std::string_view id="block_0009_0002";auto lesson=actual;auto& block=editor(lesson,id);
    auto extra=block.output_check->cases.front();extra.standard_input+="QA other input";extra.explanation="QA second authored guide";
    block.output_check->cases.front().explanation="QA first authored guide";block.output_check->cases.push_back(extra);
    state::StateData data;auto captured=request(block);auto result=execution(block,captured);
    require(record_compiler_puzzle_case(data,lesson,block,captured,result,"Correcto\nQA first authored guide")&&
        compiler_puzzle_case_count(data,lesson,block)==1&&compiler_puzzle_completed(data,lesson,block),
        "The first verified case did not unlock the editor on its own");
    const auto earned=compiler_puzzle_feedback(data,lesson,block);
    captured.stdin_utf8=extra.standard_input;result.standard_input=extra.standard_input;result.standard_output=extra.outputs.front();
    require(record_compiler_puzzle_case(data,lesson,block,captured,result,"Correcto\nQA second authored guide")&&
        compiler_puzzle_case_count(data,lesson,block)==2&&compiler_puzzle_completed(data,lesson,block)&&
        compiler_puzzle_feedback(data,lesson,block)==earned,
        "A voluntary second case was rejected or rewrote the first achievement");
    auto revised=lesson;editor(revised,id).output_check->cases.front().explanation="QA arbitrary new first guide";
    const auto before=data;const auto feedback=compiler_puzzle_current_feedback(data,revised,editor(revised,id),captured);
    require(feedback&&feedback->text=="Correcto\nQA first authored guide"&&feedback->source_path.empty()&&data==before,
        "A multicase goal rewrote the earned first-case guidance after an optional run");
}
std::string legacy_review_token(std::string_view text){return std::to_string(text.size())+':'+std::string(text);}
state::StateData legacy_review_fixture(state::StateData data,const Block& block,const CppRunRequest& request){
    // Frozen V1 framing, independently reproduced from the historical format.
    // This is a compatibility model, not a migration or a real execution.
    const auto token=[](std::string_view value){return legacy_review_token(value);};
    std::string rules;
    if(block.compile_check){const auto& plan=*request.diagnostic;require(!plan.project,"The legacy diagnostic fixture must remain single-source");
        rules="compiler-diagnostic-review-rules-v1/"+token(block.compile_check->diagnostic)+token(block.compile_check->explanation)
            +token("compile")+token(plan.control_source_utf8)+token(plan.expected_error_code)
            +token(std::to_string(plan.first_line))+token(std::to_string(plan.last_line));
    }else{const auto& check=*block.output_check;rules="compiler-review-rules-v1/"+
        token(check.input_comparison==TextComparison::tokens?"tokens":"exact")+token(std::to_string(check.cases.size()));
        for(const auto& item:check.cases){rules+=token(item.standard_input)+token(item.executable.value_or(""))+token(item.explanation)
                +token(item.standard_error==StandardErrorExpectation::non_blank?"nonBlank":"ignored")+token(std::to_string(item.outputs.size()));
            if(item.exit_code!=0)rules+=token("process-exit-code/v1")+token(std::to_string(item.exit_code));
            for(const auto& output:item.outputs)rules+=token(output);rules+=token(std::to_string(item.review_feedback.size()));
            for(const auto& guide:item.review_feedback){rules+=token(std::to_string(guide.outputs.size()));
                for(const auto& output:guide.outputs)rules+=token(output);rules+=token(guide.text);}
        }
    }
    auto& values=data.preferences.at("native_windows");std::string prefix;
    for(const auto& [key,value]:values)if(key.ends_with("/review/rules")){(void)value;require(prefix.empty(),"Ambiguous legacy review fixture");prefix=key.substr(0,key.size()-5);}
    require(!prefix.empty(),"Missing model rejection for V1 compatibility");
    const auto text=[&](std::string_view suffix){return std::get<std::string>(values.at(prefix+std::string(suffix)));};
    values[prefix+"rules"]=state::sha256(rules);
    const auto seal=state::sha256("compiler-review-v1/"+token(prefix)+token(text("program"))+token(text("input"))+
        token(text("target"))+token(text("rules"))+token(text("feedback")));
    auto execution_seal=seal;
    if(block.compile_check){auto frame="compiler-diagnostic-review-v1/"+token(seal);
        for(const auto& field:std::get<std::vector<std::string>>(values.at(prefix+"capture")))frame+=token(field);
        execution_seal=state::sha256(frame);require(!values.contains(prefix+"feedback-origin/v1"),
            "A generated diagnostic rejection acquired an authored success origin");
    }
    values[prefix+"seal"]=execution_seal;
    if(const auto found=values.find(prefix+"feedback-origin/v1");found!=values.end()){
        auto fields=std::get<std::vector<std::string>>(found->second);require(fields.size()==3,"Malformed model feedback origin");
        fields[2]=state::sha256("compiler-feedback-origin-v1/"+token(prefix)+token(execution_seal)+token(text("feedback"))+token(fields[0])+token(fields[1]));
        found->second=std::move(fields);
    }
    return data;
}
void review_priority_after_success_edit(const std::filesystem::path& content,const std::filesystem::path& qa,bool diagnostic){
    // Explicit adapter models only. These captures exercise admission and
    // persisted presentation; no compiler or educational program runs here.
    const std::string id=diagnostic?"block_0006_0006":"block_0005_0004";
    const auto actual=load_lesson(content/(diagnostic?
        "datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json":
        "archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json"));
    auto original=guidance(actual,id,"QA original success explanation");
    auto revised=guidance(original,id,"QA revised success explanation only");
    auto& old=editor(original,id);auto& current=editor(revised,id);
    std::string review_text,review_path;
    if(!diagnostic){
        require(old.output_check->cases.size()==1&&!old.output_check->cases.front().review_feedback.empty(),
            "The reviewed CRT fixture lost its actual authored rejection guide");
        const auto& guide=old.output_check->cases.front().review_feedback.front();
        require(!guide.outputs.empty()&&current.output_check->cases.front().review_feedback.front().outputs==guide.outputs&&
            current.output_check->cases.front().review_feedback.front().text==guide.text,
            "The success-only edit changed the rejection's authored conditions or guide");
        review_text="Incorrecto\n"+guide.text;review_path="/outputCheck/cases/0/reviewFeedback/0/text";
        const auto length=winrt::to_hstring(guide.text).size();
        const TextSemantics descriptor{guide.text,{{0,length,guide.text,SemanticRole::output}}};validate_text_semantics(descriptor);
        old.text_semantics[review_path]=descriptor;current.text_semantics[review_path]=descriptor;
    }else review_text="Incorrecto\nQA accepted compilation; the requested diagnostic is still pending.";
    require(reference_objective_frame(original,old)==reference_objective_frame(revised,current),
        "A success explanation changed the reviewed execution objective");
    state::StateStore store(qa/(diagnostic?"diagnostic-review-priority":"stdout-review-priority"));auto data=store.load();seed(data,original,id);
    auto accepted=request(old),rejected=accepted;rejected.request_id=12;rejected.revision=72;
    if(diagnostic){
        accepted=diagnostic_request(old);
        require(record_compiler_diagnostic_case(data,original,old,accepted,diagnostic_capture(accepted),"Correcto\n"+old.compile_check->explanation),
            "Could not seed the explicit diagnostic achievement model");
        rejected.diagnostic=prepare_cpp_diagnostic_request(old,rejected);
        CppRunResult result;result.request_id=rejected.request_id;result.revision=rejected.revision;
        result.run_directory=L"C:/qa-guidance-accepted-diagnostic";result.compilation={CppPhaseStatus::completed,0};result.diagnostic.emplace();
        result.diagnostic->control={CppPhaseStatus::completed,0};result.diagnostic->source_sha256=state::sha256(rejected.source_utf8);
        result.diagnostic->control_sha256=state::sha256(rejected.diagnostic->control_source_utf8);
        require(record_compiler_diagnostic_review(data,original,old,rejected,result,review_text),"The accepted diagnostic candidate did not record its model rejection");
    }else{
        require(record_compiler_puzzle_case(data,original,old,accepted,execution(old,accepted),"Correcto\n"+old.output_check->cases.front().explanation),
            "Could not seed the explicit stdout achievement model");
        rejected.source_utf8+="\n// QA rejected draft";auto failure=execution(old,rejected);
        failure.standard_output=old.output_check->cases.front().review_feedback.front().outputs.front();
        require(record_compiler_puzzle_review(data,original,old,rejected,failure,review_text),"The actual authored rejection fixture was not admitted");
    }
    auto draft=state::get_draft(data,draft_key("programming",original,old),old.initial_code);
    draft.source=rejected.source_utf8;draft.input=rejected.stdin_utf8;state::put_draft(data,draft_key("programming",original,old),draft);
    const auto check_feedback=[&](const Lesson& model){const auto snapshot=data;
        const auto saved=state::get_draft(data,draft_key("programming",model,editor(model,id)),editor(model,id).initial_code);
        require(saved.source==rejected.source_utf8&&saved.input==rejected.stdin_utf8,"The reviewed fixture does not match its exact reopened draft");
        const auto value=compiler_puzzle_current_feedback(data,model,editor(model,id),rejected);
        require(value&&!value->correct&&value->text==review_text&&value->source_path==review_path,
            diagnostic?"A success-only diagnostic explanation edit displaced the exact latest rejection":
                       "A success-only stdout explanation edit displaced the exact latest rejection or its typed guide");
        require(data==snapshot,"Restoring a reviewed attempt mutated StateData");};
    check_feedback(original);store.save(data);data=reopen(store);check_feedback(original);
    const auto current_format=data;data=legacy_review_fixture(data,old,rejected);const auto legacy=data;
    check_feedback(original);store.save(data);data=reopen(store);require(data==legacy,"An unchanged V1 rejection failed exact reopen");check_feedback(original);
    require(compiler_puzzle_review(data,revised,current,rejected).empty(),
        "A V1 rejection with changed complete rules was reinterpreted as a V2 rejection");
    require(receipts(data)==receipts(current_format)&&narration::read_history(data)==narration::read_history(current_format),
        "The isolated V1 fixture altered original achievement or page slots");
    data=current_format;
    const auto original_state=data;const auto original_receipts=receipts(data);const auto original_markers=narration::read_history(data);
    state::prepare_lesson_state(data,revised);state::validate_lesson_state(data,revised);
    require(data==original_state,"A success-only guide adoption changed immutable attempts, draft or three bookmark slots");
    store.save(data);data=reopen(store);require(data==original_state&&receipts(data)==original_receipts&&
        narration::read_history(data)==original_markers&&compiler_puzzle_completed(data,revised,current),
        "Reopening a success-only edit changed achievement, reviewed state or bookmarks");
    check_feedback(revised);
    const auto preserved=data;
    for(unsigned change=0;change<5;++change){auto query=rejected;auto altered=revised;
        if(change==0)query.source_utf8+="\n// different untested editor";
        if(change==1)query.stdin_utf8+='x';
        if(change==2){query.cmake=CmakeConfig{};query.cmake->target="different-target";}
        if(change==3){if(diagnostic)query.diagnostic->first_line+=1;
            else editor(altered,id).output_check->cases.front().review_feedback.front().outputs.front()+='x';}
        if(change==4){if(diagnostic)editor(altered,id).compile_check->diagnostic="other-qa-diagnostic";
            else editor(altered,id).output_check->cases.front().exit_code=1;}
        require(compiler_puzzle_review(data,altered,editor(altered,id),query).empty()&&data==preserved,
            "Changed source/input/target/selector/objective reused the current rejection");
    }
    if(!diagnostic){auto changed_error=revised;editor(changed_error,id).output_check->cases.front().review_feedback.front().text+=" QA changed error guide";
        require(compiler_puzzle_review(data,changed_error,editor(changed_error,id),rejected).empty()&&data==preserved,
            "A changed rejection guide resurrected obsolete feedback without an actual output capture");}
    auto tampered=data;for(auto& [key,value]:tampered.preferences.at("native_windows"))if(key.ends_with("/review/feedback"))value=std::string("Correcto\nQA tampered verdict");
    require(compiler_puzzle_review(tampered,revised,current,rejected).empty(),"An altered verdict bypassed its captured review seal");
}
int review_priority_models(const std::filesystem::path& content,const std::filesystem::path& qa){
    unsigned failures=0;for(const bool diagnostic:{false,true}){
        try{review_priority_after_success_edit(content,qa,diagnostic);std::cout<<"PASS "<<(diagnostic?"diagnostic":"stdout")<<" latest rejection survives success-only guide edit\n";}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<(diagnostic?"diagnostic":"stdout")<<" latest rejection priority: "<<error.what()<<'\n';}
    }return failures?1:0;
}
TextSemantics authored_guide_semantics(const JsonObject& root,std::string_view owner,
        std::string_view path,std::string_view source){
    std::optional<TextSemantics> result;
    for(const auto& item:root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields")){
        const auto field=item.GetObject();
        if(field.GetNamedString(L"block")!=winrt::to_hstring(owner)||field.GetNamedString(L"path")!=winrt::to_hstring(path))continue;
        require(!result,"The authored guide has ambiguous semantic owners");
        require(winrt::to_string(field.GetNamedString(L"sourceSha256"))==state::sha256(source),"The authored guide source digest changed");
        TextSemantics value{std::string(source),{}};
        for(const auto& entry:field.GetNamedArray(L"spans")){const auto span=entry.GetObject();
            value.spans.push_back({static_cast<std::size_t>(span.GetNamedNumber(L"start")),
                static_cast<std::size_t>(span.GetNamedNumber(L"end")),winrt::to_string(span.GetNamedString(L"text")),
                parse_semantic_role(winrt::to_string(span.GetNamedString(L"role"))),
                parse_semantic_reading(winrt::to_string(span.GetNamedString(L"reading",L"auto")))});}
        validate_text_semantics(value);result=std::move(value);
    }
    require(result.has_value(),"A current authored L24 guide lost its exact semantic field");return *result;
}
int authored_l24_guides(const std::filesystem::path& content,const std::filesystem::path& qa){
    using namespace lgaed::presentation;
    const auto path=content/"archivos_y_programa_completo/lesson_escribir_un_formato_que_pueda_leerse.json";
    const auto lesson=load_lesson(path),metadata=parse_lesson_progress_metadata(lesson.raw_json,lesson.locale);
    const auto raw=JsonObject::Parse(winrt::to_hstring(lesson.raw_json));
    const std::array<std::string_view,2> owners{"block_0002_0004","block_0006_0002"};
    constexpr std::string_view review_path="/outputCheck/cases/0/reviewFeedback/0/text";
    std::set<std::pair<std::string,std::string>> expected_fields,actual_fields;
    for(const auto owner:owners)for(const auto field:{explanation_path,review_path})expected_fields.emplace(owner,field);
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(block.type==BlockType::cpp_compiler)
        for(const auto& [field,value]:block.text_semantics){(void)value;actual_fields.emplace(block.stable_id,field);}
    // The current course may legitimately contain additional authored guide
    // fields or newer compiler exercises. Never discard those to satisfy this
    // fixture's two selected owners. Validate the four originals exactly,
    // together with all independently authored fields through the loader.
    require(std::ranges::all_of(expected_fields,[&](const auto& item){
        return actual_fields.contains(item);
    }),"The four focused L24 compiler guide fields no longer exist");
    state::StateStore store(qa/"authored-l24-guides");auto data=store.load();
    require(data.preferences.empty(),"The isolated L24 guide profile contains existing data");
    for(const auto owner:owners){seed(data,lesson,owner);const auto& block=editor(lesson,owner);
        auto draft=state::get_draft(data,draft_key("programming",lesson,block),block.initial_code);
        draft.input=block.output_check->cases.front().standard_input;
        state::put_draft(data,draft_key("programming",lesson,block),draft);}
    const auto independent=data;const auto pauses=narration::read_history(data);
    require(pauses.entries.size()==3,"The L24 model fixture lost one of its storage-only bookmark slots");
    const auto preserve=[&](const state::StateData& before){
        for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
            require(state::find(data,domain,key)&&*state::find(data,domain,key)==value,
                "A guide model overwrote an achievement, draft, bookmark or unrelated preference");};
    std::size_t checked=0;
    for(const auto owner:owners){const auto& block=editor(lesson,owner);
        require(block.output_check&&block.output_check->cases.size()==1,"L24 guide QA needs its current sole output case");
        const auto raw_case=editor(raw,lesson,owner).GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
        const auto raw_reviews=raw_case.GetNamedArray(L"reviewFeedback");
        require(raw_reviews.Size()==1&&!block.output_check->cases.front().review_feedback.front().outputs.empty(),
            "L24 guide QA lost its one authored rejection rule");
        auto captured=request(block);const auto draft=state::get_draft(data,draft_key("programming",lesson,block),block.initial_code);
        captured.source_utf8=draft.source;captured.stdin_utf8=draft.input;
        for(const bool correct:{true,false}){
            const auto field_path=correct?explanation_path:review_path;
            const auto source=winrt::to_string(correct?raw_case.GetNamedString(L"explanation"):raw_reviews.GetObjectAt(0).GetNamedString(L"text"));
            const auto authored=authored_guide_semantics(raw,owner,field_path,source);
            require(block.text_semantics.at(std::string(field_path))==authored&&
                editor(metadata,owner).text_semantics.at(std::string(field_path))==authored,
                "Runtime or catalog lost raw authored guide roles, readings or UTF-16 ranges");
            // Explicit adapter models, never executions of the educational code.
            auto model=execution(block,captured);
            if(!correct)model.standard_output=winrt::to_string(raw_reviews.GetObjectAt(0).GetNamedArray(L"outputs").GetStringAt(0));
            const auto evaluation=evaluate_cpp_output_check(*block.output_check,model,captured.revision);
            require(evaluation.verdict==(correct?OutputVerdict::correct:OutputVerdict::incorrect)&&
                evaluation.feedback==source&&evaluation.feedback_path==field_path,
                "The L24 evaluated model selected another authored guide or path");
            const auto before=data;const auto earned=receipts(data);const auto verdict=correct?"Correcto":"Incorrecto";
            const auto recorded=std::string(verdict)+'\n'+source;
            require(correct?record_compiler_puzzle_case(data,lesson,block,captured,model,recorded):
                record_compiler_puzzle_review(data,lesson,block,captured,model,recorded),"The L24 evaluated guide model was not recorded");
            preserve(before);if(!correct)require(receipts(data)==earned,"An incorrect L24 review altered its independent achievement");
            store.save(data);const auto saved=data;data=reopen(store);require(data==saved,"Reopening changed the exact authored L24 guide state");
            const auto unchanged=data;const auto restored=compiler_puzzle_current_feedback(data,lesson,block,captured);
            require(restored&&restored->correct==correct&&restored->text==recorded&&restored->source_path==field_path&&data==unchanged,
                "Restored L24 feedback lost its latest verdict, exact source or typed semantic origin");
            const auto prefix=std::string(correct?"✓ ":"✗ ")+verdict;
            const auto full=prefix+"\n\n"+source;
            const auto composed=verdict_text_semantics(full,prefix,&block.text_semantics.at(restored->source_path));
            auto expected=authored;expected.source=full;const auto offset=semantic_utf8_boundaries(prefix+"\n\n").size()-1;
            for(auto& span:expected.spans){span.start+=offset;span.end+=offset;}
            require(composed&&*composed==expected,"Native verdict composition changed authored L24 roles, readings or source owners");
            const auto units=semantic_utf8_boundaries(full).size()-1;std::vector<std::size_t> identity(units+1);
            for(std::size_t i=0;i<identity.size();++i)identity[i]=i;
            const auto runs=semantic_display_runs(*composed,identity,units);
            require(!runs.empty()&&runs.front().start==0&&runs.front().end>=offset&&runs.front().role==SemanticRole::prose,
                "A generated L24 verdict borrowed its guide's technical background");
            for(const auto& span:expected.spans)require(std::ranges::find(runs,SemanticDisplayRun{span.start,span.end,span.role,span.reading})!=runs.end(),
                "A displayed L24 guide lost an exact authored range or its reading intent");
            require(compiler_puzzle_completed(data,lesson,block),"A rejected guide model revoked a prior L24 achievement");++checked;
        }
    }
    preserve(independent);require(checked==4&&narration::read_history(data)==pauses,"L24 guide QA omitted a field or changed one of the three storage-only pauses");
    require(load_lesson(path).raw_json==lesson.raw_json,"L24 guide QA changed an active curricular asset");
    std::cout<<"PASS four authored U12L24 compiler guides: raw roles/readings/ranges, typed evaluator origins, exact StateStore reopen and native verdict projection; achievements/drafts/three storage-only pauses retained. Adapter models only, no compiler/JVM/UI/audio.\n";
    return 0;
}

int identifier_rename_live_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    // The Spanish curriculum stays unchanged. Generate six proofs only in
    // memory from its actual final formatted C++ owners.
    constexpr std::array<std::pair<std::string_view,std::string_view>,6> owners{{
        {"block_0006_0002","etiquetas_mas_frecuentes"},
        {"recuperacion_entrada_editor","etiquetas_mas_frecuentes"},
        {"block_0006_0009","etiquetas_mas_frecuentes"},
        {"block_0007_0002","etiquetas_mas_frecuentes"},
        {"block_0007_0004","etiquetas_mas_frecuentes"},
        {"frecuencias_numericas_referencia_programa","valores_mas_frecuentes"}
    }};
    const auto path=content/
        "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json";
    const auto baseline=load_lesson(path);
    const auto root=JsonObject::Parse(winrt::to_hstring(baseline.raw_json));
    const auto layout=root.GetNamedObject(L"stateLayout");
    JsonObject prefix;
    prefix.Insert(L"lessonId",JsonValue::CreateStringValue(winrt::to_hstring(baseline.id)));
    prefix.Insert(L"locale",JsonValue::CreateStringValue(L"es"));
    prefix.Insert(L"editorial",JsonArray::Parse(layout.GetNamedArray(L"editorial").Stringify()));
    const auto history_sha=test::compile_check_test_fingerprint(prefix);
    JsonArray records;
    const auto replace_word=[](std::string code,std::string_view old,std::string_view newer) {
        return std::regex_replace(code,
            std::regex("\\b"+std::string(old)+"\\b"),std::string(newer));
    };
    for(const auto& [identity,destination]:owners) {
        const auto& old=editor(baseline,identity);
        const auto block=editor(root,baseline,identity);
        const auto snapshot=JsonObject::Parse(block.Stringify());
        const auto slot=old.type==BlockType::cpp_compiler?L"initialCode":L"code";
        const auto source=winrt::to_string(block.GetNamedString(slot));
        const auto old_hash=test::compile_check_test_fingerprint(snapshot);
        const auto count_word=[&](std::string_view name) {
            const std::regex pattern("\\b"+std::string(name)+"\\b");
            return static_cast<unsigned>(
                std::distance(std::sregex_iterator(source.begin(),source.end(),pattern),
                              std::sregex_iterator{}));
        };
        require(count_word("maxima")==5&&count_word("mayores")==5,
            "A published frequency example lost its five distinct identifier occurrences");
        const auto revised=replace_word(
            replace_word(source,"maxima","frecuencia_maxima_encontrada"),
            "mayores",destination);
        block.Insert(slot,JsonValue::CreateStringValue(winrt::to_hstring(revised)));
        const auto after=test::compile_check_test_fingerprint(block);
        require(old_hash!=after&&revised.find("Maxima:")!=revised.npos&&
            revised.find("Mayores:")!=revised.npos,
            "A native C++ rename changed a published output label");

        const auto format=std::ranges::find(
            baseline.state_layout.source_format_revisions,identity,&SourceFormatRevision::id);
        require(format!=baseline.state_layout.source_format_revisions.end(),
            "The real frequency owner lost its original formatted source");
        JsonObject replacements,counts,affirmed,names,occurrences,confirmed;
        replacements.Insert(L"maxima",
            JsonValue::CreateStringValue(L"frecuencia_maxima_encontrada"));
        replacements.Insert(L"mayores",
            JsonValue::CreateStringValue(winrt::to_hstring(destination)));
        for(const auto field:{L"maxima",L"mayores"}) {
            counts.Insert(field,JsonValue::CreateNumberValue(5));
            affirmed.Insert(field,JsonValue::CreateNumberValue(5));
        }
        names.Insert(slot,replacements);
        occurrences.Insert(slot,counts);
        confirmed.Insert(slot,affirmed);
        JsonObject storage;
        storage.Insert(L"page",JsonValue::CreateNumberValue(old.address.page));
        storage.Insert(L"block",JsonValue::CreateNumberValue(old.address.block));
        JsonObject item;
        const auto string_field=[&](const wchar_t* field,std::string_view value) {
            item.Insert(field,JsonValue::CreateStringValue(winrt::to_hstring(value)));
        };
        string_field(L"lessonId",baseline.id);
        string_field(L"locale","es");
        string_field(L"historicalFamilySha256",
            baseline.state_layout.family.at("es").sha256);
        string_field(L"originalStateSha256",
            baseline.state_layout.signatures.at(std::string(identity)));
        string_field(L"editorialPrefixSha256",history_sha);
        string_field(L"id",identity);
        item.Insert(L"stableStorage",storage);
        item.Insert(L"block",snapshot);
        item.Insert(L"names",names);
        item.Insert(L"occurrences",occurrences);
        item.Insert(L"confirmed",confirmed);
        string_field(L"beforeSha256",old_hash);
        string_field(L"afterSha256",after);
        string_field(L"priorSourceFormatSha256",format->sha256);
        records.Append(item);
        JsonObject edge;
        edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
        edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(old_hash)));
        edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(after)));
        layout.GetNamedArray(L"editorial").Append(edge);
    }
    layout.Insert(L"identifierRenameRevisions",records);
    const auto serialized=winrt::to_string(root.Stringify());
    const auto changed=parse_lesson_json(serialized,"es");
    const auto metadata=parse_lesson_progress_metadata(serialized,"es");
    require(changed.state_layout.identifier_rename_revisions.size()==6&&
        metadata.state_layout.identifier_rename_revisions.size()==6&&
        changed.state_layout.signatures==baseline.state_layout.signatures&&
        metadata.state_layout.signatures==baseline.state_layout.signatures&&
        changed.state_layout.origin.size()==baseline.state_layout.origin.size()&&
        changed.state_layout.additions.size()==baseline.state_layout.additions.size(),
        "Renaming C++ sources changed historical signatures, owners or storage");
    for(const auto& [identity,destination]:owners) {
        const auto& old=editor(baseline,identity);
        const auto& current=editor(changed,identity);
        const auto& progress=editor(metadata,identity);
        const auto& previous_source=old.type==BlockType::cpp_compiler?
            old.initial_code:old.code;
        const auto& new_source=current.type==BlockType::cpp_compiler?
            current.initial_code:current.code;
        require(current.address.page==old.address.page&&
            current.address.block==old.address.block&&
            progress.address.page==old.address.page&&
            progress.address.block==old.address.block&&
            new_source!=previous_source&&
            new_source.find("frecuencia_maxima_encontrada")!=new_source.npos&&
            new_source.find(std::string(destination))!=new_source.npos&&
            new_source.find("Maxima:")!=new_source.npos&&
            new_source.find("Mayores:")!=new_source.npos,
            "Authentic Windows rename changed storage or missed current code");
    }

    // Exercise a real draft and a foreign preference through persistent
    // StateStore reopening. A new name is not another learner achievement.
    constexpr std::string_view editor_id="recuperacion_entrada_editor";
    const auto& old_editor=editor(baseline,editor_id);
    const auto& new_editor=editor(changed,editor_id);
    state::StateStore store(qa/"identifier-rename-live");
    auto data=store.load();
    state::prepare_lesson_state(data,baseline);
    state::Draft draft;draft.source=old_editor.initial_code+"\n// Historical learner draft";
    draft.input="6 patio sala patio faro sala patio";
    draft.selection_start=2;draft.selection_end=4;
    const auto original_key=draft_key("programming",baseline,old_editor);
    state::put_draft(data,original_key,draft);
    const auto capture=request(old_editor);
    require(record_compiler_puzzle_case(data,baseline,old_editor,capture,
            execution(old_editor,capture),"Correcto: QA U5L13 histórico"),
        "The original frequency editor could not save its real-case success receipt");
    require(compiler_puzzle_completed(data,baseline,old_editor),
        "A successful original frequency run was not credited");
    const auto prior_receipts=receipts(data);
    narration::History audible;
    audible.entries.push_back({"u5l13-original-reading","u5l13-original-owner",
        {3,149,0,false},"page_0006"});
    narration::write_history(data,audible);
    const auto previous_audio=narration::read_history(data);
    data.preferences["qa-independent"]["unchanged"]=std::string("opaque");
    store.save(data);
    data=reopen(store);
    const auto stored=state::get_draft(data,original_key,old_editor.initial_code);
    require(stored.source==draft.source&&stored.input==draft.input,
        "Original C++ renamed-owner draft could not reopen");
    state::prepare_lesson_state(data,changed);
    state::validate_lesson_state(data,changed);
    state::validate_lesson_state(data,metadata);
    require(draft_key("programming",changed,new_editor)==original_key&&
        state::get_draft(data,original_key,new_editor.initial_code).source==draft.source&&
        state::string_value(data,"qa-independent","unchanged")=="opaque"&&
        compiler_puzzle_completed(data,changed,new_editor)&&
        compiler_puzzle_completed(data,metadata,editor(metadata,editor_id))&&
        receipts(data)==prior_receipts&&
        narration::read_history(data)==previous_audio,
        "Renaming changed a learner's saved draft, receipt or audio bookmark");
    store.save(data);
    const auto persisted=reopen(store);
    state::validate_lesson_state(persisted,changed);
    require(persisted==data&&compiler_puzzle_completed(persisted,changed,new_editor)&&
        receipts(persisted)==prior_receipts&&
        narration::read_history(persisted)==previous_audio,
        "Renaming changed an original achievement or reading marker after durable reopen");

    const auto rejects=[&](std::string_view cause,const auto& mutation) {
        const auto forged=JsonObject::Parse(winrt::to_hstring(serialized));
        mutation(forged);
        rejects_both(forged,cause);
    };
    rejects("C++ rename belongs to another stable response address",
        [](const JsonObject& forged) {
            const auto record=forged.GetNamedObject(L"stateLayout").
                GetNamedArray(L"identifierRenameRevisions").GetObjectAt(0);
            record.GetNamedObject(L"stableStorage").Insert(
                L"page",JsonValue::CreateNumberValue(999));
        });
    rejects("C++ rename changed its authenticated occurrence count",
        [](const JsonObject& forged) {
            const auto record=forged.GetNamedObject(L"stateLayout").
                GetNamedArray(L"identifierRenameRevisions").GetObjectAt(0);
            record.GetNamedObject(L"occurrences").GetNamedObject(L"code").Insert(
                L"maxima",JsonValue::CreateNumberValue(4));
        });
    rejects("C++ rename archive changed its lesson, source or format provenance",
        [](const JsonObject& forged) {
            const auto record=forged.GetNamedObject(L"stateLayout").
                GetNamedArray(L"identifierRenameRevisions").GetObjectAt(0);
            record.GetNamedObject(L"block").Insert(
                L"code",JsonValue::CreateStringValue(L"int main() { return 9; }\n"));
        });
    rejects("C++ rename needs its unique original-to-revised editorial segment",
        [](const JsonObject& forged) {
            const auto proofs=forged.GetNamedObject(L"stateLayout").
                GetNamedArray(L"editorial");
            proofs.RemoveAt(proofs.Size()-1);
        });
    require(load_lesson(path).raw_json==baseline.raw_json,
        "A Windows rename regression wrote to a published curriculum asset");
    std::cout<<"PASS real frequency lesson Windows identifier rename: six exact "
             <<"source/progress owners, native + metadata loader, reopened learner "
             <<"draft and earned receipt, authenticated original format "
             <<"and four hostile archives.\n";
    return 0;
}

int general_choice_live_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    // The actual published lesson is read only. Authorship and malicious
    // changes below exist exclusively as in-memory JSON projections.
    constexpr std::string_view id="block_0007_0002";
    constexpr std::string_view original_text=
        "La operación incumple su precondición aunque conserve una cantidad válida.";
    constexpr std::string_view replacement=
        "La operación incumple la condición para aceptar la transición, aunque conserve una cantidad válida.";
    const auto path=content/"datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json";
    const auto original=load_lesson(path);
    const auto& old=editor(original,id);
    require(old.type==BlockType::multiple_choice&&old.correct_index==0&&
        old.options.size()==3&&old.options.front()==original_text,
        "The authentic U9L13 option, owner or original correct answer changed");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto block=editor(root,original,id);
    const auto archived=JsonObject::Parse(block.Stringify());
    const auto before=test::compile_check_test_fingerprint(archived);
    const auto overlay=root.GetNamedObject(L"textSemantics");
    const auto fields=overlay.GetNamedArray(L"fields");
    JsonObject original_marker, revised_marker;
    unsigned marker_index=0,found=0;
    for(unsigned i=0;i<fields.Size();++i) {
        const auto field=fields.GetObjectAt(i);
        if(field.GetNamedString(L"block")!=winrt::to_hstring(id)||
                field.GetNamedString(L"path")!=L"/options/0")continue;
        original_marker=JsonObject::Parse(field.Stringify());
        revised_marker=JsonObject::Parse(field.Stringify());
        marker_index=i;++found;
    }
    require(found==1&&original_marker.GetNamedArray(L"spans").Size()==0,
        "The original unhighlighted option has no unique semantic marker");
    revised_marker.Insert(L"sourceSha256",
        JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(replacement))));
    fields.SetAt(marker_index,revised_marker);
    block.GetNamedArray(L"options").SetAt(0,
        JsonValue::CreateStringValue(winrt::to_hstring(replacement)));
    const auto after=test::compile_check_test_fingerprint(block);
    require(before!=after,"A true editorial option change was not recorded");
    const auto layout=root.GetNamedObject(L"stateLayout");
    const auto existing=layout.HasKey(L"choiceRevisions")?
        layout.GetNamedArray(L"choiceRevisions"):JsonArray{};
    JsonObject revision;
    revision.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));
    revision.Insert(L"block",archived);
    revision.Insert(L"optionIndex",JsonValue::CreateNumberValue(0));
    revision.Insert(L"replacement",JsonValue::CreateStringValue(winrt::to_hstring(replacement)));
    revision.Insert(L"semanticBefore",original_marker);
    revision.Insert(L"semanticAfter",revised_marker);
    revision.Insert(L"semanticRootBefore",JsonValue::CreateBooleanValue(true));
    existing.Append(revision);
    layout.Insert(L"choiceRevisions",existing);
    JsonObject proof;
    proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(before)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(after)));
    layout.GetNamedArray(L"editorial").Append(proof);
    const auto serialized=winrt::to_string(root.Stringify());
    const auto actual=parse_lesson_json(serialized,"es");
    const auto metadata=parse_lesson_progress_metadata(serialized,"es");
    const auto& revised=editor(actual,id);
    require(actual.id==original.id&&metadata.id==original.id&&
        metadata.pages.size()==original.pages.size()&&
        revised.correct_index==old.correct_index&&
        revised.options.size()==old.options.size()&&
        revised.options.front()==replacement&&revised.options[1]==old.options[1]&&
        revised.options[2]==old.options[2]&&revised.explanation==old.explanation&&
        revised.review_feedback_options==old.review_feedback_options&&
        revised.choice_revision&&revised.choice_revision->general_option_index==0&&
        revised.choice_revision->original_options==old.options,
        "Native Windows ignored the genuine compatible choice edit or changed its answer");
    require(state::response_storage_key(original,old)==
        state::response_storage_key(actual,revised),
        "A general editorial option created another student's storage key");

    state::StateData data;
    state::prepare_lesson_state(data,original);
    state::Response completed;completed.selected=0;completed.correct=true;
    state::put_response(data,original,old,completed);
    data.preferences["guidance-qa"]["foreign"]=std::string("preserve");
    const auto preserved=data;
    state::StateStore store(qa/"general-choice-live");
    // StateStore deliberately forbids a write until this instance has loaded
    // the profile generation. Reproduce the real first-use sequence in QA.
    const auto empty=store.load();
    require(empty.preferences.empty(),
        "The isolated general-choice QA profile already contains student data");
    store.save(data);
    data=reopen(store);
    const auto response=state::get_response(data,actual,revised);
    require(response&&response->correct&&response->selected==0&&
        !response->historical_choice&&data==preserved&&
        curriculum::evaluate_choice(revised,0).correct,
        "Reopening failed to preserve the original correct choice, foreign state or storage");

    // Opening the revised lesson adopts its new authenticated choice archive
    // without moving the original receipt or manufacturing extra achievements.
    const auto saved_response=state::string_value(
        data,"learning_progress",state::response_storage_key(original,old));
    state::prepare_lesson_state(data,actual);
    require(state::string_value(
            data,"learning_progress",state::response_storage_key(actual,revised))==
                saved_response&&
        state::string_value(data,"guidance-qa","foreign")=="preserve",
        "Edition adoption rewrote the historical correct receipt or other preferences");
    const auto adopted=data;
    store.save(data);
    data=reopen(store);
    require(data==adopted,
        "The adopted choice revision was not durable through a real StateStore reopen");
    state::prepare_lesson_state(data,actual);
    require(data==adopted,
        "Reopening a reviewed choice applied its state migration twice");
    // Validate the *actual import/reopen gate*, not just its component
    // response reader. Import validation must never rewrite the profile.
    state::validate_lesson_state(data,actual,"programming");
    require(data==adopted,
        "Valid revised-choice import changed the saved state");

    // An imported answer claiming an unearned success must fail closed even
    // when its historical owner and revision match the current lesson.
    auto false_positive_data=adopted;
    state::Response unearned;
    unearned.selected=2;unearned.correct=true;
    state::put_response(false_positive_data,
        state::response_storage_key(actual,revised),unearned);
    const auto false_positive_snapshot=false_positive_data;
    bool refused_false_positive=false;
    try{(void)state::get_response(false_positive_data,actual,revised);}
    catch(const std::runtime_error& error) {
        refused_false_positive=std::string_view(error.what()).find(
            "El acierto guardado no corresponde")!=std::string_view::npos;
    }
    require(refused_false_positive&&false_positive_data==false_positive_snapshot,
        "The revised choice accepted a forged achievement or modified its original bytes");
    bool rejected_false_import=false;
    try{state::validate_lesson_state(false_positive_data,actual,"programming");}
    catch(const std::runtime_error&){rejected_false_import=true;}
    require(rejected_false_import&&false_positive_data==false_positive_snapshot,
        "Native import validation accepted a forged choice achievement");

    // Nor may a resumed profile adopt a fabricated archive entry as its
    // authentic editorial provenance. Rejection must leave all bytes intact.
    auto forged_journal=adopted;
    const auto revision_key="lesson_"+actual.id+"_state_choice_revisions_v1_"+actual.locale;
    require(state::find(forged_journal,"learning_progress",revision_key)!=nullptr,
        "The first revised lesson did not adopt its choice history");
    forged_journal.preferences["learning_progress"][revision_key]=
        std::string(R"([["forged-owner","forged-hash","forged-representation"]])");
    const auto journal_snapshot=forged_journal;
    bool refused_journal=false;
    try{state::prepare_lesson_state(forged_journal,actual);}
    catch(const std::runtime_error& error) {
        refused_journal=std::string_view(error.what()).find(
            "opción guardada")!=std::string_view::npos;
    }
    require(refused_journal&&forged_journal==journal_snapshot,
        "A forged choice-revision archive mutated the profile or passed adoption");
    bool rejected_journal_import=false;
    try{state::validate_lesson_state(forged_journal,actual,"programming");}
    catch(const std::runtime_error&){rejected_journal_import=true;}
    require(rejected_journal_import&&forged_journal==journal_snapshot,
        "Native import validation accepted a forged choice revision journal");

    // An old incorrect selection is just as important as an old achievement:
    // neither the wording correction nor a binary reopen may re-grade it.
    state::StateData incorrect_data;
    state::prepare_lesson_state(incorrect_data,original);
    state::Response incorrect;
    incorrect.selected=2;incorrect.correct=false;
    state::put_response(incorrect_data,original,old,incorrect);
    const auto incorrect_before=incorrect_data;
    state::StateStore incorrect_store(qa/"general-choice-incorrect");
    require(incorrect_store.load().preferences.empty(),
        "The isolated incorrect-choice QA profile already contains student data");
    incorrect_store.save(incorrect_data);
    incorrect_data=reopen(incorrect_store);
    const auto preserved_incorrect=state::get_response(incorrect_data,actual,revised);
    require(preserved_incorrect&&preserved_incorrect->selected==2&&
        !preserved_incorrect->correct&&!preserved_incorrect->historical_choice&&
        incorrect_data==incorrect_before&&!curriculum::evaluate_choice(revised,2).correct,
        "Reopening credited a previously incorrect response or changed its storage");
    const auto wrong_receipt=state::string_value(
        incorrect_data,"learning_progress",state::response_storage_key(original,old));
    state::prepare_lesson_state(incorrect_data,actual);
    const auto still_incorrect=state::get_response(incorrect_data,actual,revised);
    require(state::string_value(
            incorrect_data,"learning_progress",state::response_storage_key(actual,revised))==
                wrong_receipt&&still_incorrect&&!still_incorrect->correct&&
        still_incorrect->selected==2,
        "Edition adoption regraded an existing incorrect selection");
    const auto accepted_incorrect=incorrect_data;
    state::validate_lesson_state(incorrect_data,actual,"programming");
    require(incorrect_data==accepted_incorrect,
        "Native import validation altered an honestly incorrect selection");

    // The revised edition must also survive a *second* durable save/reopen,
    // not merely the first reopen of its original, unreviewed response.
    incorrect_store.save(incorrect_data);
    incorrect_data=reopen(incorrect_store);
    require(incorrect_data==accepted_incorrect,
        "Reopening after editorial adoption changed the prior incorrect receipt");
    state::prepare_lesson_state(incorrect_data,actual);
    const auto reopened_incorrect=state::get_response(incorrect_data,actual,revised);
    require(incorrect_data==accepted_incorrect&&reopened_incorrect&&
        reopened_incorrect->selected==2&&!reopened_incorrect->correct&&
        !reopened_incorrect->historical_choice&&
        state::string_value(incorrect_data,"learning_progress",
            state::response_storage_key(actual,revised))==wrong_receipt,
        "A second reviewed-choice reopen regraded the error or changed its stored owner");
    state::validate_lesson_state(incorrect_data,actual,"programming");
    require(incorrect_data==accepted_incorrect,
        "Native import validation mutated the reopened incorrect choice");
    require(load_lesson(path).raw_json==original.raw_json,
        "The editable QA copy changed the published curricular file");

    const auto reject_mutation=[&](std::string_view expected_error,const auto& mutation) {
        const auto forged=JsonObject::Parse(winrt::to_hstring(serialized));
        mutation(forged);
        // A rejection for unrelated parsing/metadata failures is not proof
        // that the protected choice or semantic contract was enforced.
        rejects_both(forged,expected_error);
    };
    reject_mutation("general choice revision changed another answer",[&](const JsonObject& forged){
        editor(forged,original,id).Insert(L"correctIndex",JsonValue::CreateNumberValue(1));
    });
    reject_mutation("general choice revision changed another answer",[&](const JsonObject& forged){
        editor(forged,original,id).GetNamedArray(L"options").SetAt(1,
            JsonValue::CreateStringValue(L"Respuesta adulterada"));
    });
    reject_mutation("active choice semantic marker differs",[&](const JsonObject& forged){
        const auto items=forged.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
        for(unsigned i=0;i<items.Size();++i) {
            const auto entry=items.GetObjectAt(i);
            if(entry.GetNamedString(L"block")==winrt::to_hstring(id)&&
                    entry.GetNamedString(L"path")==L"/options/0"){
                entry.Insert(L"sourceSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
                return;
            }
        }
        throw std::runtime_error("The forged semantic-marker fixture lost its owner");
    });
    reject_mutation("General choice archive must belong",[&](const JsonObject& forged){
        const auto records=forged.GetNamedObject(L"stateLayout").GetNamedArray(L"choiceRevisions");
        const auto last=records.GetObjectAt(records.Size()-1);
        last.GetNamedObject(L"block").Insert(L"correctIndex",JsonValue::CreateNumberValue(2));
    });
    std::cout<<"PASS real U9L13 choice on Windows: native and metadata loaders, "
        "prior receipt, StateStore reopen, exact owner/semantics, four hostile variants; "
        "no learner data or published lesson changed.\n";
    return 0;
}
}
int main(int argc,char** argv)try{
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    const auto qa=std::filesystem::path(build::source_root)/"out/qa"/
        ("compiler-guidance-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64()));
    if(argc==2&&std::string_view(argv[1])=="--review-priority")return review_priority_models(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--authored-l24-guides")return authored_l24_guides(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--general-choice-live")return general_choice_live_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--identifier-rename-live")return identifier_rename_live_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--math-matching-text-live")
        return test::math_matching_text_live_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--indexed-unique-transfer-state-live")
        return test::indexed_unique_transfer_state_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--defaulted-move-policy-state-live")
        return test::defaulted_move_policy_state_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--independent-copy-assignment-state-live")
        return test::independent_copy_assignment_state_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--move-assignment-invariant-state-live")
        return test::move_assignment_invariant_state_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--array-owner-read-state-live")
        return test::array_owner_read_state_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--array-copy-move-state-live")
        return test::array_copy_move_state_windows(content,qa);
    if(argc==2&&std::string_view(argv[1])=="--array-return-take-state-live")
        return test::array_return_take_state_windows(content,qa);
    if(argc!=1)return 2;
    const auto lesson=load_lesson(content/"archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json");
    const auto stage=[&](std::string_view name,const auto& work){
        try{work();}catch(const std::exception& error){
            throw std::runtime_error(std::string(name)+": "+error.what());
        }
    };
    stage("L20 original guide", [&]{sole_output_sequence(lesson,"block_0005_0004",qa);});
    stage("L20 independent owner", [&]{sole_output_sequence(lesson,"block_0009_0002",qa);});
    stage("U9 diagnostic reading", [&]{diagnostic_sequence(content,qa);});
    stage("L20 multicase history", [&]{multicase_isolation(lesson);});
    std::cout<<"PASS current V2 compiler guidance: two L20 editors, diagnostic guide, exact reopen, immutable receipts/drafts/three pauses, rejected rechecks, protected contracts and legacy/multicase isolation; model fixtures only.\n";
    return 0;
}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
