#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using namespace lgaed::curriculum;
void require(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
const Block& editor(const Lesson& lesson,std::string_view identity){const auto at=stable_block_position(lesson,identity);
    require(at.has_value(),"Missing current diagnostic owner");return lesson.pages[at->page].blocks[at->block];}
std::filesystem::path content(){return std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";}
Lesson single_lesson(){return load_lesson(content()/"datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json");}
CppRunRequest single_request(const Block& block,bool correct){
    CppRunRequest request;request.request_id=correct?11:12;request.revision=correct?37:38;request.source_utf8=block.initial_code;
    if(correct){const auto at=request.source_utf8.rfind("    return 0;");require(at!=std::string::npos,"Missing main insertion anchor");
        request.source_utf8.insert(at,"    fija.cambiar_a(4,2);\n");}
    request.diagnostic=prepare_cpp_diagnostic_request(block,request);return request;
}
CppRunResult accepted_capture(const CppRunRequest& request){
    // Explicit model fixture: these phase records test admission and state,
    // and are not evidence that MSVC ran. --real below uses the actual runner.
    CppRunResult result;result.request_id=request.request_id;result.revision=request.revision;
    result.run_directory=L"C:/qa-diagnostic-capture";result.compilation={CppPhaseStatus::completed,0};
    result.diagnostic.emplace();auto& evidence=*result.diagnostic;evidence.control={CppPhaseStatus::completed,0};
    if(request.diagnostic->project){const auto& project=*request.diagnostic->project;
        evidence.source_sha256=state::sha256(cpp_project_frame(request.project_files,request.source_files,std::nullopt));
        evidence.control_sha256=state::sha256(cpp_project_frame(project.control_files,project.control_sources,std::nullopt));
        for(const auto& source:project.control_sources)evidence.control_units.push_back({source,{CppPhaseStatus::completed,0}});
        for(const auto& source:request.source_files)evidence.candidate_units.push_back({source,{CppPhaseStatus::completed,0}});
        if(project.link){evidence.control_link={CppPhaseStatus::completed,0};evidence.candidate_link={CppPhaseStatus::completed,0};}
    }else{
        evidence.source_sha256=state::sha256(request.source_utf8);
        evidence.control_sha256=state::sha256(request.diagnostic->control_source_utf8);
    }
    return result;
}
CppRunResult correct_capture(const CppRunRequest& request){auto result=accepted_capture(request);const auto& check=*request.diagnostic;
    require(!check.expected_error_code.empty()&&check.first_line,"The model must use an actual prepared single-source rejection");
    const auto path=result.run_directory/L"candidate/main.cpp";const auto bytes=path.u8string();
    result.compilation.exit_code=2;result.compilation.stderr_bytes=std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())
        +'('+std::to_string(check.first_line)+",1): error "+check.expected_error_code+": QA model diagnostic.\n";
    result.diagnostic->errors.push_back({check.expected_error_code,path,check.first_line,1});return result;
}
state::StateData without_review(state::StateData data){
    for(auto& [domain,values]:data.preferences)if(domain=="native_windows")
        std::erase_if(values,[](const auto& pair){return pair.first.find("/review/")!=std::string::npos;});
    return data;
}
void seed_independent_state(state::StateData& data){
    data.preferences["diagnostic-review-qa"]["unrelated"]=std::string(" exact\r\n保存 ");
    data.preferences["native_windows"]["diagnostic-review-qa/unrelated"]=std::string("preserved native preference");
    auto history=narration::begin({},"qa-page-one","qa-owner-one",{2,123,1,false},{},"first-page");
    history=narration::begin(std::move(history),"qa-page-two","qa-owner-two",{3,456,2,false},{},"second-page");
    history=narration::begin(std::move(history),"qa-page-three","qa-owner-three",{4,789,3,true},{},"third-page");
    narration::write_history(data,history);
}
void check_sequence(state::StateStore& store,const Lesson& lesson,const Block& block,
        const CppRunRequest& correct,const CppRunResult& correct_result,
        const CppRunRequest& accepted,const CppRunResult& accepted_result){
    auto data=store.load();seed_independent_state(data);const auto initial=data;const auto markers=narration::read_history(data);
    require(!compiler_puzzle_current_feedback(data,lesson,block,accepted),"An untouched goal already has feedback");
    auto unearned=data;
    require(record_compiler_diagnostic_review(unearned,lesson,block,accepted,accepted_result,"Incorrecto\nFalta el rechazo solicitado.")&&
        !compiler_puzzle_completed(unearned,lesson,block),"A rejected attempt granted an unexecuted diagnostic achievement");
    require(record_compiler_diagnostic_case(data,lesson,block,correct,correct_result,"Correcto\nDiagnóstico comprobado."),
        "The correct capture did not earn the diagnostic objective");
    const auto achievement=data;
    require(!record_compiler_diagnostic_case(data,lesson,block,accepted,accepted_result,"false")&&data==achievement,
        "An accepted candidate was credited or rewrote the earned achievement");
    require(record_compiler_diagnostic_review(data,lesson,block,accepted,accepted_result,"Incorrecto\nFalta el rechazo solicitado."),
        "The latest diagnostic rejection was not persisted after an achievement");
    const auto reviewed=data;const auto feedback=compiler_puzzle_current_feedback(data,lesson,block,accepted);
    require(feedback&&!feedback->correct&&feedback->text=="Incorrecto\nFalta el rechazo solicitado."&&feedback->source_path.empty(),
        "The old success or its authored guide overrode the exact current rejection");
    require(compiler_puzzle_completed(data,lesson,block)&&without_review(data)==achievement&&narration::read_history(data)==markers,
        "The new rejection erased history or changed the three exact page markers");
    store.save(data);state::StateStore reopen(store.root().parent_path());const auto restored=reopen.load();
    const auto reopened=compiler_puzzle_current_feedback(restored,lesson,block,accepted);
    require(restored==reviewed&&reopened&&!reopened->correct&&compiler_puzzle_completed(restored,lesson,block)&&
        narration::read_history(restored)==markers,"Reopening lost the rejection, achievement or independent markers");
    auto next=accepted;next.request_id+=100;next.revision=0;
    require(!compiler_puzzle_review(restored,lesson,block,next).empty(),"Session-local counters invalidated the exact restored draft");
    next.source_utf8+="\n// a different untested draft";next.diagnostic=prepare_cpp_diagnostic_request(block,next);
    require(compiler_puzzle_review(restored,lesson,block,next).empty(),"A different draft borrowed an old rejected verdict");
    data=restored;invalidate_compiler_puzzle_attempt(data,lesson,block,next);
    require(data==achievement,"Editing cleared the achievement or retained an unrelated rejected verdict");
    data=restored;require(record_compiler_diagnostic_case(data,lesson,block,correct,correct_result,"Correcto\nNew recheck")&&
        data==achievement,"A later correct recheck failed to clear only its latest rejection");
    data=restored;reset_compiler_puzzle(data,lesson,block);
    require(!compiler_puzzle_completed(data,lesson,block)&&!compiler_puzzle_current_feedback(data,lesson,block,accepted)&&data==initial,
        "Explicit reset left diagnostic state or changed unrelated progress");
    // A review is never reconstructed from matching wording or a typed flag.
    data=reviewed;
    for(auto& [key,value]:data.preferences["native_windows"])if(key.ends_with("/review/capture")){
        auto capture=std::get<std::vector<std::string>>(value);capture[3]="link";value=std::move(capture);}
    require(compiler_puzzle_review(data,lesson,block,accepted).empty()&&compiler_puzzle_completed(data,lesson,block),
        "A tampered review phase was trusted or damaged the original achievement");
}
void model_tests(const WindowsEnvironment& environment){
    const auto lesson=single_lesson();const auto& block=editor(lesson,"block_0006_0006");
    const auto correct=single_request(block,true),accepted=single_request(block,false);
    const auto correct_result=correct_capture(correct),accepted_result=accepted_capture(accepted);
    require(expected_compile_diagnostic(correct,correct_result)&&accepted_compile_diagnostic(accepted,accepted_result),
        "The model phase fixtures do not satisfy the production predicates");
    state::StateStore store(environment.paths.root/L"model");check_sequence(store,lesson,block,correct,correct_result,accepted,accepted_result);
    state::StateData untouched;
    const auto reject=[&](const CppRunRequest& request,const CppRunResult& result){auto data=untouched;
        require(!accepted_compile_diagnostic(request,result)&&
            !record_compiler_diagnostic_review(data,lesson,block,request,result,"Incorrecto")&&data==untouched,
            "A stale, technical, executed or source-mismatched capture produced a pedagogical rejection");};
    for(const auto mutate:std::vector<std::function<void(CppRunResult&)>>{
        [](auto& value){++value.request_id;},[](auto& value){++value.revision;},
        [](auto& value){value.error_utf8="technical failure";},[](auto& value){value.run_directory.clear();},
        [](auto& value){value.execution={CppPhaseStatus::completed,0};},[](auto& value){value.configuration={CppPhaseStatus::completed,0};},
        [](auto& value){value.artifact=L"not-a-diagnostic.exe";},[](auto& value){value.executable="wrong-target";},
        [](auto& value){value.diagnostic->source_sha256="changed";},[](auto& value){value.diagnostic->control_sha256="changed";},
        [](auto& value){value.diagnostic->control.exit_code=2;},[](auto& value){value.compilation.exit_code=2;},
        [](auto& value){value.diagnostic->errors.push_back({"C2143",L"main.cpp",1,1});},
        [](auto& value){value.compilation.stderr_bytes="main.cpp(1,1): error C2143: unrelated.\n";},
        [](auto& value){value.diagnostic->control.stderr_bytes="main.cpp(1,1): error C2143: control failed.\n";},
        [](auto& value){value.diagnostic->candidate_units.push_back({"injected.cpp",{CppPhaseStatus::completed,0}});},
        [](auto& value){value.diagnostic->candidate_link={CppPhaseStatus::completed,0};}}){
        auto changed=accepted_result;mutate(changed);reject(accepted,changed);
    }
    for(const auto phase:{CppPhaseStatus::not_started,CppPhaseStatus::cancelled,CppPhaseStatus::timed_out,
            CppPhaseStatus::output_limit,CppPhaseStatus::launch_failed}){
        auto changed=accepted_result;changed.compilation.status=phase;reject(accepted,changed);
        changed=accepted_result;changed.diagnostic->control.status=phase;reject(accepted,changed);
    }
    auto changed=accepted;changed.action=CppRunRequest::Action::execute;reject(changed,accepted_result);
    changed=accepted;changed.stdin_utf8="undeclared";reject(changed,accepted_result);
    changed=accepted;changed.source_utf8+=' ';reject(changed,accepted_result);
    changed=accepted;changed.diagnostic->control_source_utf8+=' ';reject(changed,accepted_result);
    changed=accepted;changed.diagnostic->expected_error_code="C2143";
    auto data=untouched;require(!record_compiler_diagnostic_review(data,lesson,block,changed,accepted_capture(changed),"Incorrecto")&&data==untouched,
        "A substituted objective descriptor stored a rejection");
    std::cout<<"PASS model current diagnostic review, exact reopen, original achievement, three untouched markers and bounded negative captures\n";

    // Actual U6 owners supply the phase and control recipe. Candidate = known
    // valid control project: this must be a wrong answer, never a new success.
    const auto project_lesson=load_lesson(content()/"funciones_y_contratos/lesson_declaraciones_y_firmas.json");
    for(const auto identity:{"block_0014_0002","block_0015_0002"}){
        const auto& project_block=editor(project_lesson,identity);CppRunRequest request;
        request.request_id=51;request.revision=71;request.source_utf8=project_block.initial_code;
        request.project_files=project_block.project_files;request.source_files=project_block.source_files;
        request.diagnostic=prepare_cpp_diagnostic_request(project_block,request);require(request.diagnostic->project.has_value(),"Missing U6 phase recipe");
        request.project_files=request.diagnostic->project->control_files;request.source_files=request.diagnostic->project->control_sources;
        request.diagnostic=prepare_cpp_diagnostic_request(project_block,request);const auto result=accepted_capture(request);
        require(accepted_compile_diagnostic(request,result),"An intact accepted U6 compile/link capture was not evaluable");
        state::StateData draft;require(record_compiler_diagnostic_review(draft,project_lesson,project_block,request,result,"Incorrecto")&&
            !compiler_puzzle_completed(draft,project_lesson,project_block)&&
            !compiler_puzzle_review(draft,project_lesson,project_block,request).empty(),"The actual U6 owner lost its current rejection or gained credit");
        const auto no_review=[&](CppRunResult other){auto preserved=draft;
            require(!accepted_compile_diagnostic(request,other)&&
                !record_compiler_diagnostic_review(preserved,project_lesson,project_block,request,other,"wrong")&&preserved==draft,
                "An incomplete or different U6 unit/phase borrowed acceptance");};
        auto altered=result;altered.diagnostic->candidate_units.clear();no_review(altered);
        altered=result;altered.diagnostic->control_units.front().source="other.cpp";no_review(altered);
        altered=result;altered.diagnostic->candidate_units.front().phase.stdout_bytes="changed aggregate";no_review(altered);
        altered=result;altered.diagnostic->source_sha256="different project";no_review(altered);
        altered=result;altered.diagnostic->candidate_link=request.diagnostic->project->link?CppPhaseResult{}:CppPhaseResult{CppPhaseStatus::completed,0};no_review(altered);
        altered=result;altered.diagnostic->control_link=request.diagnostic->project->link?CppPhaseResult{}:CppPhaseResult{CppPhaseStatus::completed,0};no_review(altered);
        auto changed_request=request;changed_request.diagnostic->project->link=!request.diagnostic->project->link;
        auto preserved=draft;require(!record_compiler_diagnostic_review(preserved,project_lesson,project_block,changed_request,
            accepted_capture(changed_request),"wrong")&&preserved==draft,"Changing the authored phase stored an unrelated review");
        if(request.diagnostic->project->link)for(const bool control:{false,true})for(const bool standard_error:{false,true}){
            auto bounded=request;bounded.output_limit_bytes=4;auto overflow=result;
            auto& units=control?overflow.diagnostic->control_units:overflow.diagnostic->candidate_units;
            auto& aggregate=control?overflow.diagnostic->control:overflow.compilation;
            require(units.size()>=2,"The U6 link regression needs its two actual translation units");
            (standard_error?units[0].phase.stderr_bytes:units[0].phase.stdout_bytes)="AAAA";
            (standard_error?units[1].phase.stderr_bytes:units[1].phase.stdout_bytes)="x";
            (standard_error?aggregate.stderr_bytes:aggregate.stdout_bytes)="AAAA";
            // This is the exact previously accepted shape: aggregate streams
            // were truncated, then a successful empty link reset its status.
            require(aggregate.succeeded(),"The cumulative-overflow regression lost its misleading terminal status");
            auto retained=draft;require(!accepted_compile_diagnostic(bounded,overflow)&&
                !record_compiler_diagnostic_review(retained,project_lesson,project_block,bounded,overflow,"wrong")&&retained==draft,
                "Cumulative control/candidate stream overflow was treated as a pedagogical rejection");
            (standard_error?units[1].phase.stderr_bytes:units[1].phase.stdout_bytes).clear();
            require(accepted_compile_diagnostic(bounded,overflow),"A complete capture exactly at its stream budget was rejected");
        }
    }
    std::cout<<"PASS actual U6 compile/link owner recipes: complete units, exact aggregates, phase binding and no invented achievements\n";
}
void real_tests(const WindowsEnvironment& environment){
    const auto lesson=single_lesson();const auto& block=editor(lesson,"block_0006_0006");const auto asset_before=state::sha256(state::read_bytes(lesson.source_path));
    auto correct=single_request(block,true),accepted=single_request(block,false);
    correct.projects_directory=accepted.projects_directory=environment.paths.projects/L"Real diagnostic recheck";
    const auto correct_result=run_cpp(correct),accepted_result=run_cpp(accepted);
    require(expected_compile_diagnostic(correct,correct_result)&&accepted_compile_diagnostic(accepted,accepted_result),
        "The two real MSVC captures did not prove requested rejection followed by accepted candidate");
    require(correct_result.execution==CppPhaseResult{}&&accepted_result.execution==CppPhaseResult{},"A diagnostic recheck executed a program");
    state::StateStore store(environment.paths.root/L"real");check_sequence(store,lesson,block,correct,correct_result,accepted,accepted_result);
    require(state::sha256(state::read_bytes(lesson.source_path))==asset_before,"Diagnostic review QA changed a curricular asset");
    std::cout<<"PASS two real MSVC compilation-only captures: correct -> rejected recheck -> reopened rejection -> correct, retained achievement/markers\n";
}
}
int main(int argc,char** argv) try {
    const bool real=argc==2&&std::string_view(argv[1])=="--real";
    require(argc==1||real,"Usage: compiler_diagnostic_review_tests [--real]");
    const LaunchOptions options{ProfileId::Codex,L"diagnostic-review-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    if(real)real_tests(environment);else model_tests(environment);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
