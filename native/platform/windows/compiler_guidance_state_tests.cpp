#include "core/compile_check_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include <functional>
#include <iostream>

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
        require(data==unchanged,"The current presentation getter mutated an original receipt");
        if(!earned)continue;
        auto second=guidance(revised,id,"QA next guide N=2");const auto& latest=editor(second,id);
        state::prepare_lesson_state(data,second);store.save(data);data=reopen(store);
        feedback=compiler_puzzle_current_feedback(data,second,latest,captured);
        require(feedback&&feedback->text=="Correcto\nQA next guide N=2"&&feedback->source_path==explanation_path&&
            receipts(data)==saved_receipts&&narration::read_history(data)==saved_markers,"A second guide revision lost immutable state");
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
        require(feedback&&feedback->text=="Correcto\nQA historical body"&&feedback->source_path.empty(),
            "An unvalidated legacy model acquired the new V2 source path");
        if(id=="block_0009_0002"){auto copy=latest;feedback=compiler_puzzle_current_feedback(data,second,copy,captured);
            require(feedback&&feedback->text=="Correcto\nQA historical body"&&feedback->source_path.empty(),
                "An unbound block copy acquired an authoring source path");}
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
    auto hostile=revised;editor(hostile,id).compile_check->diagnostic="other-diagnostic";
    require(!compiler_puzzle_current_feedback(data,hostile,editor(hostile,id),capture),"A changed diagnostic borrowed an earned explanation");
}
void multicase_isolation(const Lesson& actual){
    constexpr std::string_view id="block_0009_0002";auto lesson=actual;auto& block=editor(lesson,id);
    auto extra=block.output_check->cases.front();extra.standard_input+="QA other input";extra.explanation="QA second authored guide";
    block.output_check->cases.front().explanation="QA first authored guide";block.output_check->cases.push_back(extra);
    state::StateData data;auto captured=request(block);auto result=execution(block,captured);
    require(!record_compiler_puzzle_case(data,lesson,block,captured,result,"Correcto\nQA first authored guide")&&
        compiler_puzzle_case_count(data,lesson,block)==1&&!compiler_puzzle_completed(data,lesson,block),
        "The first admitted multicase input either lost its partial proof or completed two cases at once");
    captured.stdin_utf8=extra.standard_input;result.standard_input=extra.standard_input;result.standard_output=extra.outputs.front();
    require(record_compiler_puzzle_case(data,lesson,block,captured,result,"Correcto\nQA second authored guide"),"Second multicase model was rejected");
    auto revised=lesson;editor(revised,id).output_check->cases.front().explanation="QA arbitrary new first guide";
    const auto before=data;const auto feedback=compiler_puzzle_current_feedback(data,revised,editor(revised,id),captured);
    require(feedback&&feedback->text=="Correcto\nQA second authored guide"&&feedback->source_path.empty()&&data==before,
        "A multicase goal arbitrarily selected the first current explanation");
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
}
int main(int argc,char** argv)try{
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    const auto qa=std::filesystem::path(build::source_root)/"out/qa"/
        ("compiler-guidance-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64()));
    if(argc==2&&std::string_view(argv[1])=="--review-priority")return review_priority_models(content,qa);
    if(argc!=1)return 2;
    const auto lesson=load_lesson(content/"archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json");
    sole_output_sequence(lesson,"block_0005_0004",qa);sole_output_sequence(lesson,"block_0009_0002",qa);
    diagnostic_sequence(content,qa);multicase_isolation(lesson);
    std::cout<<"PASS current V2 compiler guidance: two L20 editors, diagnostic guide, exact reopen, immutable receipts/drafts/three pauses, rejected rechecks, protected contracts and legacy/multicase isolation; model fixtures only.\n";
    return 0;
}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
