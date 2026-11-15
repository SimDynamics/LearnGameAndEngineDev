#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "core/lesson_entry_policy.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;
using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing authentic reduction fixture or runner artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const Block& owner(const Lesson& lesson,std::string_view id) {
    const Block* found=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.stable_id==id) {
            require(!found,"Duplicate stable reduction owner");
            found=&block;
        }
    require(found!=nullptr,"Missing stable reduction owner");
    return *found;
}
void replace_once(std::string& source,std::string_view before,std::string_view after) {
    const auto position=source.find(before);
    require(position!=std::string::npos,"Authentic reduction source fixture changed");
    source.replace(position,before.size(),after);
}
void rejected_loaders(std::string_view source,std::string_view reason) {
    for(const bool metadata:{false,true}) {
        bool rejected=false;
        try {
            if(metadata)(void)parse_lesson_progress_metadata(source,"es");
            else (void)parse_lesson_json(source,"es");
        }catch(const std::exception&){rejected=true;}
        require(rejected,std::string(reason)+(metadata?" (progress)":" (full)"));
    }
}
constexpr std::string_view activity_id="block_0002_0003";
constexpr std::string_view reference_id="block_0003_0002";
constexpr std::string_view declaration=R"json([
 {"activity":"block_0002_0003","kind":"stateful-vector-int-v1",
  "function":"contar_altos","state":"acumulado","initialState":0,
  "calls":[{"values":[2],"argument":2,"result":1,"stateAfter":1},
           {"values":[],"argument":2,"result":1,"stateAfter":1}],
  "observedCall":1,"requiredResult":0,"exitCode":0,
  "dependencies":[
   {"header":"vector","symbols":["std::vector"]},
   {"header":"iostream","symbols":["std::cout","std::cerr","std::clog"]},
   {"header":"string","symbols":["std::string","std::to_string"]},
   {"header":"cstdio","symbols":["std::printf","std::puts"]}]}])json";

std::string program(const Block& block,std::string_view main,
                    std::string_view headers="#include <iostream>\n#include <vector>\n") {
    return std::string(headers)+"\nint acumulado{0};\n\n"+
        block.reduction_probe->protected_definition+"\n\n"+std::string(main);
}
std::string ignored_first_solution(const Block& block) {
    return program(block,R"cpp(int main() {
    contar_altos({2}, 2);
    const int segunda = contar_altos({}, 2);
    std::printf(segunda ? "Segunda=%d\n" : "Sin valores=%d\n", segunda);
    return segunda == 1 ? 0 : 7;
}
)cpp","#include <cstdio>\n#include <vector>\n");
}

void public_execution(const CppRunResult& result) {
    require(result.error_utf8.empty(),"Runner error: "+result.error_utf8);
    require(result.compilation.succeeded(),"Candidate compilation failed: "+
        result.compilation.stdout_bytes+result.compilation.stderr_bytes);
    require(result.execution.status==CppPhaseStatus::completed&&result.execution.exit_code,
        "Candidate did not complete an actual Windows execution");
}
void checked_run(const CppRunRequest& request,const CppRunResult& result,
                 const FreshExecution& execution,const Block& block) {
    public_execution(result);
    require(result.execution.succeeded()&&result.reduction_evidence,
        "Missing independent reduction observations or public zero exit");
    const auto& evidence=*result.reduction_evidence;
    const auto& spec=*request.reduction_probe;
    const auto plan=reduction_plan(spec);
    require(plan&&plan->size()==3&&evidence.runs.size()==plan->size(),
        "The published phase and both result interventions did not all run");
    for(std::size_t phase=0;phase<plan->size();++phase) {
        const auto& run=evidence.runs[phase];
        require(run.compiled&&run.executed&&run.completed_normally&&run.phase==(*plan)[phase],
            "A private phase failed to compile, execute or finish normally");
        require(run.calls.size()==spec.calls.size(),"A private phase omitted a required call");
        for(std::size_t index=0;index<spec.calls.size();++index) {
            const auto& actual=run.calls[index];
            const auto& expected=spec.calls[index];
            const auto before=index?spec.calls[index-1].state_after:spec.initial_state;
            const auto delivered=index==spec.observed_call&&run.phase.replacement
                ?*run.phase.replacement:expected.result;
            require(actual.values==expected.values&&actual.argument==expected.argument&&
                    actual.state_before==before&&actual.state_after==expected.state_after&&
                    actual.result==expected.result&&actual.delivered==delivered,
                    "An intervention changed the protected calculation, state or call sequence");
        }
    }
    require(execution.reduction_probe_correct&&execution.execution_completed&&
            execution.execution_succeeded&&execution.exit_code==0&&
            execution.reduction_request_fingerprint==cpp_reduction_request_fingerprint(request),
            "Authentic reduction proof was rejected: "+execution.reduction_probe_feedback);
    require(evaluate_cpp_output_check(*block.output_check,execution,request.revision).verdict==
            OutputVerdict::correct,"The verified reduction did not earn its declared result");
    require(execution.standard_output==result.execution.stdout_bytes&&
            execution.standard_error==result.execution.stderr_bytes,
            "A private trace replaced the user's visible public streams");
    require(read(result.run_directory/L"main.cpp")==request.source_utf8&&
            std::filesystem::is_regular_file(result.artifact)&&
            read(result.run_directory/L"compilation-stdout.bin")==result.compilation.stdout_bytes&&
            read(result.run_directory/L"compilation-stderr.bin")==result.compilation.stderr_bytes&&
            read(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
            read(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "The public source, executable or captured logs were not restored");
    for(const auto& entry:std::filesystem::directory_iterator(result.run_directory)) {
        const auto name=entry.path().filename().string();
        require(name.find("private")==std::string::npos&&name.find("replacement")==std::string::npos,
            "A phase-labelled private artifact escaped into the public workspace");
    }
}
void rejected_capture(const CppRunRequest& request,const CppRunResult& result) {
    require(!capture_cpp_execution(request,result).reduction_probe_correct,
        "Stale, missing or altered evidence acquired a reduction proof");
}
void flow(const state::StateData& data,const Lesson& lesson,bool expected) {
    const auto& block=owner(lesson,activity_id);
    const auto& reference=owner(lesson,reference_id);
    const auto completed=[&](const Block& candidate) {
        return compiler_puzzle_completed(data,lesson,candidate);
    };
    const auto proofs=compiler_completion_snapshot(lesson,completed);
    require(compiler_requires_completion(block)&&proofs.at(block.stable_id)==expected,
        "The reduction editor lost its mandatory, evidence-backed completion");
    const auto position=stable_block_position(lesson,reference_id);
    require(position&&compiler_reference_available(
            lesson,position->page,reference,proofs,completed)==expected,
            "The linked reference ignored the current reduction achievement");
    const auto activity=stable_block_position(lesson,activity_id);
    require(activity.has_value(),"Missing authentic reduction page");
    const std::array requirements{EntryRequirement{activity->page,
        expected?EntryCompletion::complete:EntryCompletion::incomplete}};
    const auto decision=lesson_entry_decision(position->page,lesson.pages.size(),requirements);
    require(decision.page==(expected?position->page:activity->page)&&!decision.pending_math,
        "The shared entry policy bypassed the reduction gate");
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const LaunchOptions options{ProfileId::Codex,
            L"reduction-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        const auto projects=environment.paths.projects/L"Reducción & (fuente)";
        const auto original_text=read(std::filesystem::path(argv[1]));
        const auto original=parse_lesson_json(original_text,"es");
        auto document=JsonObject::Parse(winrt::to_hstring(original_text));
        document.Insert(L"compilerReductionChecks",JsonArray::Parse(winrt::to_hstring(declaration)));
        // This root relation links the authentic later comparison; neither
        // block's bytes nor its historical state slot is rewritten.
        auto references=document.HasKey(L"referenceAfter")
            ?document.GetNamedArray(L"referenceAfter"):JsonArray{};
        bool linked=false;
        for(const auto& value:references) {
            const auto relation=value.GetObject();
            if(relation.GetNamedString(L"activity")==winrt::to_hstring(activity_id))
                for(const auto& id:relation.GetNamedArray(L"blocks"))
                    if(id.GetString()==winrt::to_hstring(reference_id))linked=true;
        }
        if(!linked)references.Append(JsonObject::Parse(
            LR"json({"activity":"block_0002_0003","blocks":["block_0003_0002"]})json"));
        document.Insert(L"referenceAfter",references);
        const auto reviewed=winrt::to_string(document.Stringify());
        const auto lesson=parse_lesson_json(reviewed,"es");
        const auto progress=parse_lesson_progress_metadata(reviewed,"es");
        // Preserve the numeric lexeme: JsonValue(double{0}) would stringify
        // back to an integer and would never exercise the raw-number guard.
        auto decimal=reviewed;
        const auto exit_name=decimal.find("\"exitCode\":");
        require(exit_name!=std::string::npos,"Missing declared reduction exit field");
        const auto exit_value=decimal.find_first_not_of(" \t\r\n",exit_name+11);
        require(exit_value!=std::string::npos&&decimal[exit_value]=='0',"Unexpected declared reduction exit value");
        decimal.replace(exit_value,1,"0.0");
        rejected_loaders(decimal,"An integral-looking floating JSON value bypassed the integer guard");
        auto unmapped=JsonObject::Parse(LR"json({"id":"reduction_unmapped","title":"QA",
          "pages":[{"blocks":[{"type":"cpp_compiler","language":"cpp",
          "initialCode":"#include <vector>\nint acumulado{0};\nint contar_altos(std::vector<int> datos,int minimo){return acumulado;}\nint main(){return 0;}"}]}]})json");
        unmapped.Insert(L"compilerReductionChecks",JsonArray::Parse(winrt::to_hstring(declaration)));
        rejected_loaders(winrt::to_string(unmapped.Stringify()),
            "A reduction declaration without stateLayout was not rejected cleanly");
        const auto& before=owner(original,activity_id);
        const auto& block=owner(lesson,activity_id);
        const auto& restored=owner(progress,activity_id);
        require(original.pages.size()==lesson.pages.size()&&lesson.pages.size()==progress.pages.size()&&
                original.state_layout.raw_json==lesson.state_layout.raw_json&&
                original.state_layout.raw_json==progress.state_layout.raw_json&&
                original.state_layout.lineage==lesson.state_layout.lineage&&
                original.state_layout.lineage==progress.state_layout.lineage&&
                original.state_layout.signatures==lesson.state_layout.signatures&&
                original.state_layout.signatures==progress.state_layout.signatures,
                "Reduction declarations changed an authentic state map or family");
        require(block.output_check&&restored.output_check&&block.output_check->reduction_check&&
                restored.output_check->reduction_check&&block.reduction_probe&&restored.reduction_probe&&
                block.reduction_probe==restored.reduction_probe&&
                block.initial_code==before.initial_code&&restored.initial_code==before.initial_code&&
                JsonObject::Parse(winrt::to_hstring(block.raw_json)).Stringify()==
                    JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify()&&
                restored.raw_json==block.raw_json&&
                block.stable_id==before.stable_id&&restored.stable_id==before.stable_id&&
                block.address.page==before.address.page&&block.address.block==before.address.block&&
                restored.address.page==before.address.page&&restored.address.block==before.address.block&&
                block.output_check->cases.size()==1&&restored.output_check->cases.size()==1&&
                reference_objective_frame(lesson,block)==reference_objective_frame(progress,restored)&&
                exercise_key(original,before)==exercise_key(lesson,block)&&
                exercise_key(lesson,block)==exercise_key(progress,restored)&&
                draft_key("programming",original,before)==draft_key("programming",lesson,block)&&
                draft_key("programming",lesson,block)==draft_key("programming",progress,restored),
                "Reduction metadata changed a source, owner, address, draft or objective");
        const auto reference_source=owner(original,reference_id).code;
        require(!reference_source.empty(),"Missing authentic published reduction");
        state::StateData untouched;
        untouched.preferences["qa"]["keep"]=std::string("unrelated state");
        state::prepare_lesson_state(untouched,original);
        state::Draft draft;
        draft.source=reference_source;draft.input="";draft.selection_start=3;draft.selection_end=12;
        draft.has_saved=true;
        const auto draft_identity=draft_key("programming",original,before);
        state::put_draft(untouched,draft_identity,draft);
        state::prepare_lesson_state(untouched,lesson);
        const auto preserved=state::get_draft(untouched,draft_identity,block.initial_code);
        require(preserved.source==draft.source&&preserved.input==draft.input&&
                preserved.selection_start==3&&preserved.selection_end==12&&preserved.has_saved,
                "The root declaration replaced the original student's draft");
        flow(untouched,lesson,false);
        flow(untouched,progress,false);
        std::cout << "PASS authentic full/progress metadata, preserved draft and blocked reference\n";

        std::uint64_t sequence=0;
        const auto request=[&](std::string source) {
            CppRunRequest result;
            result.request_id=++sequence;result.revision=100+sequence;
            result.projects_directory=projects;result.source_utf8=std::move(source);
            result.stdin_utf8=block.standard_input.value_or("");
            result.reduction_probe=block.reduction_probe;
            result.compile_timeout=60s;result.execution_timeout=5s;
            return result;
        };
        const auto reference_request=request(reference_source);
        require(compiler_runtime_request_allowed(block,reference_request),
            "The authentic reference was refused before execution");
        const auto reference_result=run_cpp(reference_request);
        const auto reference_capture=capture_cpp_execution(reference_request,reference_result);
        checked_run(reference_request,reference_result,reference_capture,block);
        std::cout << "PASS authentic reference, preserved state and causal second result\n";

        const auto equivalent_request=request(ignored_first_solution(block));
        require(compiler_runtime_request_allowed(block,equivalent_request),
            "Ignoring the first return or using printf was rejected");
        const auto equivalent_result=run_cpp(equivalent_request);
        const auto equivalent_capture=capture_cpp_execution(equivalent_request,equivalent_result);
        checked_run(equivalent_request,equivalent_result,equivalent_capture,block);
        for(std::size_t index=1;index<equivalent_result.reduction_evidence->runs.size();++index)
            require(equivalent_result.reduction_evidence->runs[index].exit_code==7,
                "A legitimate nonzero private return was narrowed to public zero");
        std::cout << "PASS ignored first result, printf, dependent text and normal private nonzero exits\n";

        auto recorded=untouched;
        auto changed=reference_request;
        changed.source_utf8+="\n// another captured source\n";
        require(changed.request_id==reference_request.request_id&&changed.revision==reference_request.revision,
            "The stale-source regression accidentally changed request identity");
        rejected_capture(changed,reference_result);
        require(!record_compiler_puzzle_case(recorded,lesson,block,changed,reference_capture,"stale")&&recorded==untouched,
            "Direct consumption rebound an old FreshExecution to new source bytes");
        changed=reference_request;changed.reduction_probe->required_result=2;
        auto another_goal=block;another_goal.reduction_probe=changed.reduction_probe;
        rejected_capture(changed,reference_result);
        require(!record_compiler_puzzle_case(recorded,lesson,another_goal,changed,reference_capture,"stale")&&recorded==untouched,
            "Direct consumption rebound an old FreshExecution to another reduction contract");
        changed=reference_request;changed.reduction_probe->calls.clear();
        require(cpp_reduction_request_fingerprint(changed).empty(),
            "An invalid reduction contract retained a capture fingerprint");
        rejected_capture(changed,reference_result);
        another_goal=block;another_goal.reduction_probe=changed.reduction_probe;
        require(!record_compiler_puzzle_case(recorded,lesson,another_goal,changed,reference_capture,"invalid")&&recorded==untouched,
            "An invalid reduction contract escaped the direct-consumption guard");
        changed=reference_request;changed.stdin_utf8="unexpected";rejected_capture(changed,reference_result);
        changed=reference_request;++changed.request_id;rejected_capture(changed,reference_result);
        changed=reference_request;++changed.revision;rejected_capture(changed,reference_result);
        changed=reference_request;changed.backend=CppRunRequest::Backend::wasi;rejected_capture(changed,reference_result);
        changed=reference_request;changed.compile_timeout+=1ms;rejected_capture(changed,reference_result);
        changed=reference_request;changed.project_key="different profile";rejected_capture(changed,reference_result);
        changed=reference_request;changed.action=CppRunRequest::Action::execute;rejected_capture(changed,reference_result);
        changed=reference_request;changed.rejection_probe=RejectionOperation::boxes;rejected_capture(changed,reference_result);
        require(!compiler_runtime_request_allowed(block,changed),"An unrelated probe changed the reduction context");
        changed=reference_request;changed.reduction_probe.reset();
        require(!compiler_runtime_request_allowed(block,changed),"An absent reduction contract was accepted");
        auto altered=reference_result;altered.reduction_evidence.reset();rejected_capture(reference_request,altered);
        altered=reference_result;altered.execution.stdout_bytes+="forged";rejected_capture(reference_request,altered);
        altered=reference_result;altered.execution.exit_code=1;rejected_capture(reference_request,altered);
        altered=reference_result;altered.reduction_evidence->runs[1].calls.pop_back();rejected_capture(reference_request,altered);
        altered=reference_result;altered.reduction_evidence->runs.front().completed_normally=false;rejected_capture(reference_request,altered);
        altered=reference_result;altered.reduction_evidence->original_program_fingerprint="stale";rejected_capture(reference_request,altered);
        auto missing=reference_capture;missing.reduction_probe_correct=false;
        require(!record_compiler_puzzle_case(recorded,lesson,block,reference_request,missing,"incorrect")&&recorded==untouched,
            "A missing independent proof changed progress");
        missing=reference_capture;missing.reduction_request_fingerprint.clear();
        require(!record_compiler_puzzle_case(recorded,lesson,block,reference_request,missing,"forged")&&recorded==untouched,
            "A summary without the captured request binding earned credit");
        std::cout << "PASS source, contract, profile, snapshot and direct-recorder binding guards\n";

        require(record_compiler_puzzle_case(recorded,lesson,block,reference_request,reference_capture,
                    "Reproducción reducida comprobada"),"Real reduction evidence could not be sealed");
        require(state::string_value(recorded,"qa","keep")=="unrelated state",
            "An unrelated preference was overwritten");
        flow(recorded,lesson,true);
        state::StateStore storage(environment.paths.root);
        (void)storage.load();
        storage.save(recorded);
        state::StateStore new_storage(environment.paths.root);
        const auto reopened=new_storage.load();
        require(reopened==recorded,"Physical state reopening changed a reduction receipt or draft");
        require(compiler_puzzle_completed(reopened,progress,restored)&&
                compiler_puzzle_case_count(reopened,progress,restored)==1,
                "A sealed first-case reduction achievement was lost on progress reopening");
        validate_compiler_puzzle_state(reopened,progress,restored);
        state::validate_lesson_state(reopened,progress);
        flow(reopened,progress,true);
        require(state::get_draft(reopened,draft_identity,restored.initial_code).source==draft.source,
            "Physical reopening replaced the existing editor draft");
        const auto receipt_key="compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
            exercise_key(lesson,block)+"/"+state::sha256(reference_objective_frame(lesson,block))+"/";
        auto tampered=reopened;
        auto& proofs=std::get<std::vector<std::string>>(
            tampered.preferences["native_windows"][receipt_key+"cases"]);
        require(proofs.size()==1,"The authentic first-case reduction receipt was not found");
        proofs[0]="unverified reduction";
        require(!compiler_puzzle_completed(tampered,progress,restored),
            "A tampered receipt unlocked the reduction editor");
        flow(tampered,progress,false);
        const auto earned=recorded;
        require(record_compiler_puzzle_case(recorded,lesson,block,equivalent_request,equivalent_capture,
                    "Variante equivalente")&&recorded==earned,
                "A later correct variant rewrote the original earned achievement");
        std::cout << "PASS sealed receipt, physical reopening, reference admission and tamper rejection\n";

        std::vector<std::pair<std::string,std::string>> negatives;
        negatives.emplace_back("unchanged initial",block.initial_code);
        negatives.emplace_back("literal instead of second result",program(block,R"cpp(int main() {
    contar_altos({2}, 2);
    contar_altos({}, 2);
    std::cout << 1 << '\n';
    return 0;
}
)cpp"));
        negatives.emplace_back("omitted preparatory call",program(block,R"cpp(int main() {
    std::cout << contar_altos({}, 2) << '\n';
    return 0;
}
)cpp"));
        auto repaired=reference_source;
        replace_once(repaired,"return acumulado;","return 0;");
        negatives.emplace_back("repaired protected definition",std::move(repaired));
        auto missing_header=block.initial_code;
        replace_once(missing_header,"#include <string>\n","");
        replace_once(missing_header,"    std::cout << \"Informe terminado\\n\";\n","");
        negatives.emplace_back("missing direct string dependency",std::move(missing_header));
        negatives.emplace_back("no calls",program(block,"int main() { return 0; }\n"));
        for(const auto& [name,source]:negatives) {
            const auto candidate=request(source);
            const auto result=run_cpp(candidate);
            public_execution(result);
            require(result.execution.succeeded(),name+": public candidate did not return zero");
            const auto execution=capture_cpp_execution(candidate,result);
            const auto evaluation=evaluate_cpp_output_check(*block.output_check,execution,candidate.revision);
            require(!execution.reduction_probe_correct&&evaluation.verdict==OutputVerdict::incorrect&&
                    !evaluation.feedback.empty(),name+": invalid reduction acquired credit or lost feedback");
            if(name!="repaired protected definition") {
                require(execution.execution_completed&&result.reduction_evidence&&
                        result.reduction_evidence->runs.size()==3,
                        name+": rejected source lost its real normal-completion evidence");
                for(const auto& run:result.reduction_evidence->runs)
                    require(run.compiled&&run.executed&&run.completed_normally,
                        name+": a technical failure replaced the intended objective rejection");
            }
            if(name=="unchanged initial"||name=="missing direct string dependency"||
               name=="repaired protected definition")
                require(!reduction_source_check(source,*block.reduction_probe).accepted,
                    name+": a forbidden source edit escaped the source criterion");
            else require(reduction_source_check(source,*block.reduction_probe).accepted,
                name+": the causal/call-sequence regression was accidentally rejected before execution");
            auto feedback_state=earned;
            require(!record_compiler_puzzle_case(feedback_state,lesson,block,candidate,execution,evaluation.feedback)&&
                    feedback_state==earned,name+": a rejected reduction changed its earlier achievement");
            require(record_compiler_puzzle_review(feedback_state,lesson,block,candidate,execution,evaluation.feedback),
                name+": an authentic rejection could not preserve its feedback");
            const auto again=state::decode(state::encode(feedback_state));
            require(compiler_puzzle_completed(again,progress,restored)&&
                    compiler_puzzle_review(again,progress,restored,candidate)==evaluation.feedback,
                    name+": reopening lost the prior achievement or the actual rejection");
        }
        std::cout << "PASS six real rejected programs: initial, literal, preparation, repair, direct header and no calls\n";

        const auto abnormal=request(program(block,R"cpp(int main() {
    contar_altos({2}, 2);
    std::cout << contar_altos({}, 2) << std::endl;
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::abort();
}
)cpp","#include <cstdlib>\n#include <iostream>\n#include <vector>\n"));
        const auto abnormal_result=run_cpp(abnormal);
        public_execution(abnormal_result);
        require(abnormal_result.execution.exit_code==std::uint32_t{3},
            "The abnormal fixture did not actually execute abort(3)");
        const auto abnormal_capture=capture_cpp_execution(abnormal,abnormal_result);
        const auto rejection=evaluate_cpp_output_check(*block.output_check,abnormal_capture,abnormal.revision);
        require(!abnormal_capture.execution_completed&&!abnormal_capture.reduction_probe_correct&&
                rejection.verdict==OutputVerdict::incorrect&&!rejection.feedback.empty(),
                "Abort acquired a normal-return witness or lost its rejection feedback");
        require(!record_compiler_puzzle_case(recorded,lesson,block,abnormal,abnormal_capture,rejection.feedback)&&
                recorded==earned,"A crashing reduction changed its earlier achievement");
        require(record_compiler_puzzle_review(recorded,lesson,block,abnormal,abnormal_capture,rejection.feedback),
            "A genuine abnormal attempt could not preserve its feedback");
        const auto review_state=state::decode(state::encode(recorded));
        require(compiler_puzzle_completed(review_state,progress,restored)&&
                compiler_puzzle_review(review_state,progress,restored,abnormal)==rejection.feedback,
                "Reopening lost either the achievement or the abnormal attempt's feedback");
        auto other_request=abnormal;other_request.reduction_probe->calls.front().argument=3;
        require(compiler_puzzle_review(review_state,lesson,block,other_request).empty(),
            "A rejection explanation was reused for another reduction contract");
        const auto review_untouched=recorded;
        require(!record_compiler_puzzle_review(recorded,lesson,block,other_request,abnormal_capture,"stale")&&
                recorded==review_untouched,"Old FreshExecution rebound feedback to a new current contract");
        require(read(std::filesystem::path(argv[1]))==original_text,
            "Runtime validation rewrote the authentic curricular fixture");
        std::cout << "PASS real abnormal termination, feedback reopening and preserved achievement\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr << "FAIL reduction runtime: " << error.what() << '\n';
        return 1;
    }catch(const winrt::hresult_error& error) {
        std::cerr << "FAIL reduction runtime: " << winrt::to_string(error.message()) << '\n';
        return 1;
    }
}
