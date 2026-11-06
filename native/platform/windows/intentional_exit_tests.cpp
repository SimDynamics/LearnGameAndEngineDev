#include "core/lesson.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/base.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    require(bool(input),"Missing owned intentional-exit QA artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::string replace_once(std::string source,std::string_view before,std::string_view after) {
    const auto at=source.find(before);
    require(at!=source.npos&&source.find(before,at+before.size())==source.npos,
        "The intentional-exit contrast no longer selects one exact source fragment");
    source.replace(at,before.size(),after);return source;
}
void compiled(const CppRunResult& result) {
    require(result.error_utf8.empty(),"Intentional-exit adapter error: "+result.error_utf8);
    require(result.compilation.succeeded(),"Intentional-exit compilation failed: "+
        result.compilation.stdout_bytes+result.compilation.stderr_bytes);
}
struct Captured {CppRunRequest request;CppRunResult result;FreshExecution execution;};
}

int main() try {
    const LaunchOptions options{ProfileId::Codex,L"intentional-exit-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"Código & (salida intencional)";
    const auto file=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/"
        "lesson_compartir_flujos_sin_acoplar_el_calculo.json";
    const auto asset=read(file),asset_hash=state::sha256(asset);
    const auto lesson=load_lesson(file);
    const auto position=stable_block_position(lesson,"block_0005_0002");
    require(position.has_value(),"The real U12L13 intentional-exit editor was not found");
    const auto original=lesson.pages[position->page].blocks[position->block];
    require(original.type==BlockType::cpp_compiler&&
        original.initial_code.find("std::istringstream entrada{\"7 x\"}")!=std::string::npos&&
        original.initial_code.find("return 1;")!=std::string::npos,
        "The real U12L13 input-failure source changed");

    // This exploratory editor has no authored goal. Give only the QA clone a
    // process objective; never rewrite the lesson or claim curricular credit.
    auto objective=original;
    OutputCase item;item.outputs={"Entrada invalida|9:4"};item.exit_code=1;
    item.explanation="QA: invalid input leaves both destination values intact.";
    objective.output_check=OutputCheck{TextComparison::exact,{item}};
    auto legacy=objective;legacy.output_check->cases.front().exit_code=0;

    std::uint64_t sequence=0;unsigned passed=0,failed=0;
    const auto test=[&](std::string_view name,const std::function<void()>& action){
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    const auto request=[&](std::string source){
        CppRunRequest value;value.request_id=++sequence;value.revision=70+sequence;
        value.source_utf8=std::move(source);value.projects_directory=projects;
        value.compile_timeout=30s;value.execution_timeout=2s;return value;
    };
    const auto evaluate=[&](const Captured& capture){
        return evaluate_cpp_output_check(*objective.output_check,capture.execution,capture.request.revision);
    };
    const auto run=[&](CppRunRequest value){
        Captured capture;capture.request=std::move(value);capture.result=run_cpp(capture.request);
        capture.execution=capture_cpp_execution(capture.request,capture.result);return capture;
    };
    const auto unavailable=[&](const CppRunRequest& value,const CppRunResult& result){
        const auto fresh=capture_cpp_execution(value,result);
        require(evaluate_cpp_output_check(*objective.output_check,fresh,value.revision).verdict==OutputVerdict::unavailable,
            "A stale, incomplete or failed adapter capture became an educational answer");
        state::StateData data;
        require(!record_compiler_puzzle_case(data,lesson,objective,value,fresh,"not an earned proof")&&data.preferences.empty(),
            "A technical capture created a completion receipt");
    };
    std::optional<Captured> accepted,zero;

    test("actual U12L13 source completes normally with exit 1 and exact preserved output",[&]{
        auto capture=run(request(original.initial_code));compiled(capture.result);
        require(capture.result.execution.status==CppPhaseStatus::completed&&capture.result.execution.exit_code==1u,
            "The real invalid-input path did not return the intended exit code");
        require(capture.execution.execution_completed&&!capture.execution.execution_succeeded&&
            capture.execution.exit_code==1&&capture.execution.standard_output=="Entrada invalida|9:4"&&
            capture.execution.standard_error.empty(),"The nonzero capture lost completion or exact stream bytes");
        const auto verdict=evaluate(capture);
        require(verdict.verdict==OutputVerdict::correct&&verdict.stdout_correct&&verdict.stderr_correct&&verdict.exit_code_correct,
            "The real intentional exit was not accepted by the complete output objective");
        require(evaluate_output_check(*objective.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::correct,
            "The shared evaluator disagreed with the Windows adapter");
        require(read(capture.result.run_directory/L"main.cpp")==original.initial_code&&
            read(capture.result.run_directory/L"execution-stdout.bin")==capture.execution.standard_output,
            "The runner changed the source or its actual captured output");
        accepted=std::move(capture);
    });

    test("return 0 and return 2 cannot satisfy an explicit return 1 objective",[&]{
        for(const int code:{0,2}){
            auto capture=run(request(replace_once(original.initial_code,"return 1;","return "+std::to_string(code)+";")));
            compiled(capture.result);
            require(capture.execution.execution_completed&&capture.execution.exit_code==code,
                "The exit-code contrast did not execute its requested normal return");
            const auto verdict=evaluate(capture);
            require(verdict.verdict==OutputVerdict::incorrect&&verdict.stdout_correct&&!verdict.exit_code_correct,
                "Correct stdout hid an incorrect process exit code");
            state::StateData data;
            require(!record_compiler_puzzle_case(data,lesson,objective,capture.request,capture.execution,"incorrect exit")&&
                data.preferences.empty(),"A wrong normal exit earned a completion proof");
            if(code==0)zero=std::move(capture);
        }
    });

    test("normal exit 1 with different stdout is an evaluated rejection",[&]{
        const auto capture=run(request(replace_once(original.initial_code,"Entrada invalida|","Entrada modificada|")));
        compiled(capture.result);const auto verdict=evaluate(capture);
        require(capture.execution.execution_completed&&capture.execution.exit_code==1&&
            verdict.verdict==OutputVerdict::incorrect&&!verdict.stdout_correct&&verdict.exit_code_correct,
            "An intended exit code overrode the required stdout");
        state::StateData data;
        require(!record_compiler_puzzle_case(data,lesson,objective,capture.request,capture.execution,"incorrect stdout"),
            "Incorrect stdout earned the intentional-exit objective");
        require(record_compiler_puzzle_review(data,lesson,objective,capture.request,capture.execution,"Revisa la salida exacta.")&&
            compiler_puzzle_review(data,lesson,objective,capture.request)=="Revisa la salida exacta.",
            "An educational rejection could not retain its own bounded retry guidance");
        require(!compiler_puzzle_completed(data,lesson,objective),"A saved rejection became a completed objective");
    });

    test("real compilation failure has no execution or completion credit",[&]{
        const auto capture=run(request("int main(){ deliberate compile failure; }\n"));
        require(!capture.result.compilation.succeeded()&&capture.result.execution.status==CppPhaseStatus::not_started,
            "The invalid source unexpectedly executed");unavailable(capture.request,capture.result);
    });

    test("real timeout after exact stdout cannot satisfy intentional termination",[&]{
        auto value=request("#include <iostream>\n#include <windows.h>\nint main(){"
            "std::cout<<\"Entrada invalida|9:4\"<<std::flush;Sleep(INFINITE);return 1;}\n");
        value.execution_timeout=300ms;
        const auto capture=run(std::move(value));compiled(capture.result);
        require(capture.result.execution.status==CppPhaseStatus::timed_out&&!capture.execution.execution_completed,
            "The bounded timeout did not remain a technical outcome");unavailable(capture.request,capture.result);
    });

    test("real abnormal Windows exit code cannot masquerade as an intended small return",[&]{
        // ExitProcess returns the Windows status without raising an exception
        // or invoking a crash dialog on the user's desktop.
        const auto capture=run(request("#include <iostream>\n#include <windows.h>\nint main(){"
            "std::cout<<\"Entrada invalida|9:4\"<<std::flush;ExitProcess(0xc0000005u);}\n"));
        compiled(capture.result);
        require(capture.result.execution.status==CppPhaseStatus::completed&&
            capture.result.execution.exit_code==0xc0000005u&&!capture.execution.execution_completed,
            "A Windows abnormal status was narrowed into a normal educational return");
        unavailable(capture.request,capture.result);
    });

    test("capture rejects stale request revision action target diagnostic and technical errors",[&]{
        require(accepted.has_value(),"The real accepted execution is missing");
        const auto& good=*accepted;
        auto result=good.result;++result.request_id;unavailable(good.request,result);
        result=good.result;++result.revision;unavailable(good.request,result);
        auto value=good.request;++value.request_id;unavailable(value,good.result);
        value=good.request;++value.revision;unavailable(value,good.result);
        result=good.result;result.error_utf8="owned QA technical failure";unavailable(good.request,result);
        for(const auto action:{CppRunRequest::Action::configure,CppRunRequest::Action::build}){
            value=good.request;value.action=action;unavailable(value,good.result);
        }
        value=good.request;value.diagnostic=CppDiagnosticRequest{};unavailable(value,good.result);
        value=good.request;value.cmake=CmakeConfig{};value.cmake->target="owned_qa_target";
        unavailable(value,good.result);
        result=good.result;result.compilation.status=CppPhaseStatus::launch_failed;unavailable(good.request,result);
        result=good.result;result.configuration.status=CppPhaseStatus::timed_out;unavailable(good.request,result);
        result=good.result;result.execution.exit_code.reset();unavailable(good.request,result);
        for(const auto status:{CppPhaseStatus::not_started,CppPhaseStatus::cancelled,CppPhaseStatus::timed_out,
                CppPhaseStatus::output_limit,CppPhaseStatus::launch_failed}){
            result=good.result;result.execution.status=status;unavailable(good.request,result);
        }
        for(const std::uint32_t code:{256u,0xc0000005u,0xffffffffu}){
            result=good.result;result.execution.exit_code=code;unavailable(good.request,result);
        }
        auto fresh=good.execution;fresh.execution_completed=false;fresh.execution_succeeded=true;
        require(evaluate_output_check(*objective.output_check,fresh,good.request.revision).verdict==OutputVerdict::unavailable,
            "A nonzero exit without authenticated normal completion was accepted");
    });

    test("legacy zero defaults and saved zero receipts do not accredit the new exit 1 goal",[&]{
        require(accepted.has_value()&&zero.has_value(),"The real zero and nonzero captures are missing");
        require(OutputCase{}.exit_code==0,"An omitted exit code changed the historical objective");
        require(evaluate_cpp_output_check(*legacy.output_check,zero->execution,zero->request.revision).verdict==OutputVerdict::correct,
            "A normal successful legacy output objective regressed");
        auto old_capture=zero->execution;old_capture.execution_completed=false;
        require(evaluate_output_check(*legacy.output_check,old_capture,zero->request.revision).verdict==OutputVerdict::correct,
            "A historical zero-exit FreshExecution now requires a newly added completion flag");
        require(evaluate_cpp_output_check(*legacy.output_check,accepted->execution,accepted->request.revision).verdict==OutputVerdict::unavailable,
            "A legacy zero-exit goal accepted a normal nonzero termination");
        require(reference_objective_frame(lesson,legacy).find("process-exit-code/v1")==std::string::npos&&
            reference_objective_frame(lesson,legacy)!=reference_objective_frame(lesson,objective),
            "The new process contract reused the historical zero-exit objective identity");

        state::StateStore store(environment.paths.root);auto data=store.load();
        require(data.preferences.empty(),"The unique headless profile unexpectedly contained old data");
        data.preferences["qa-unrelated"]["progress"]=std::int64_t{19};
        data.preferences["qa-unrelated"]["draft"]=std::string{"preserve my isolated draft"};
        require(record_compiler_puzzle_case(data,lesson,legacy,zero->request,zero->execution,"legacy exact output")&&
            compiler_puzzle_completed(data,lesson,legacy),"The actual zero-exit run did not earn its own old proof");
        const auto before=data;
        require(!compiler_puzzle_completed(data,lesson,objective)&&compiler_puzzle_case_count(data,lesson,objective)==0,
            "A saved zero-exit receipt credited an unexecuted intentional-exit objective");
        require(!record_compiler_puzzle_case(data,lesson,objective,zero->request,zero->execution,"incorrect exit")&&data==before,
            "Rejecting an old zero capture rewrote its historical receipt");
        store.save(data);
        state::StateStore reopened(environment.paths.root);data=reopened.load();
        require(data==before&&compiler_puzzle_completed(data,lesson,legacy)&&!compiler_puzzle_completed(data,lesson,objective),
            "A profile round trip changed old proof identity or invented new completion");
        require(record_compiler_puzzle_case(data,lesson,objective,accepted->request,accepted->execution,"intentional exact output")&&
            compiler_puzzle_completed(data,lesson,objective)&&compiler_puzzle_completed(data,lesson,legacy),
            "The real intentional exit could not earn a separate current receipt");
        const auto unrelated=data.preferences.at("qa-unrelated");
        reset_compiler_puzzle(data,lesson,objective);
        require(!compiler_puzzle_completed(data,lesson,objective)&&!compiler_puzzle_completed(data,lesson,legacy)&&
            compiler_puzzle_case_count(data,lesson,objective)==0&&data.preferences.at("qa-unrelated")==unrelated,
            "Reset left an owned proof behind or changed unrelated isolated preferences");
        reopened.save(data);
    });

    test("a completed objective re-evaluates a new wrong run and restores its latest rejection",[&]{
        require(accepted.has_value()&&zero.has_value(),"The real accepted and rejected captures are missing");
        // The first success and rejection were actually executed in that order.
        // Compile the repaired source again after restoring the latest review.
        const auto profile=environment.paths.root/L"repeat-revalidation";
        state::StateStore store(profile);auto data=store.load();
        require(data.preferences.empty(),"The private repeat-check profile unexpectedly contained old data");
        data.preferences["qa-unrelated"]["progress"]=std::int64_t{23};
        const std::string success="Correcto\nQA: actual exit 1 and exact output.";
        const std::string rejected="Incorrecto\nQA: stdout matches, but the new run returned 0 instead of 1.";
        require(record_compiler_puzzle_case(data,lesson,objective,accepted->request,accepted->execution,success),
            "The real initial execution did not earn its objective");
        const auto achievement=data;
        require(evaluate(*zero).verdict==OutputVerdict::incorrect&&
            !record_compiler_puzzle_case(data,lesson,objective,zero->request,zero->execution,rejected)&&data==achievement,
            "A previous achievement bypassed evaluation of a later incorrect run or lost its old proof");
        require(record_compiler_puzzle_review(data,lesson,objective,zero->request,zero->execution,rejected)&&
            compiler_puzzle_review(data,lesson,objective,zero->request)==rejected,
            "The latest rejected run was hidden by a previously completed objective");
        require(compiler_puzzle_completed(data,lesson,objective)&&compiler_puzzle_case_count(data,lesson,objective)==1&&
            compiler_puzzle_feedback(data,lesson,objective)==success,
            "Rechecking a completed objective revoked or rewrote its earned achievement");
        const auto reviewed=data;
        require(!record_compiler_puzzle_case(data,lesson,objective,zero->request,zero->execution,"another wrong verdict")&&
            data==reviewed,"A repeated wrong check discarded its current review or changed old completion");
        store.save(data);
        state::StateStore reopened(profile);data=reopened.load();
        require(data==reviewed&&compiler_puzzle_completed(data,lesson,objective)&&
            compiler_puzzle_review(data,lesson,objective,zero->request)==rejected&&
            compiler_puzzle_review(data,lesson,objective,accepted->request).empty(),
            "Reopening lost the current rejection or attached it to a different program");
        auto changed=data;
        invalidate_compiler_puzzle_attempt(changed,lesson,objective,accepted->request);
        require(changed==achievement,"Changing only the current program failed to clear its old rejection independently");
        const auto repaired=run(request(original.initial_code));compiled(repaired.result);
        require(repaired.request.revision>zero->request.revision&&evaluate(repaired).verdict==OutputVerdict::correct,
            "The repaired program did not produce a new successful execution after the rejection");
        require(record_compiler_puzzle_case(data,lesson,objective,repaired.request,repaired.execution,
                "Correcto\nQA: compiled and checked the repaired program again.")&&
            compiler_puzzle_review(data,lesson,objective,zero->request).empty()&&data==achievement,
            "A new correct check failed to clear the current rejection while retaining the original achievement");
        reopened.save(data);
        state::StateStore final_reopen(profile);
        require(final_reopen.load()==achievement,"A subsequent successful recheck did not survive reopening");
    });

    test("the original U12L13 curriculum and exploratory editor remain unchanged",[&]{
        require(read(file)==asset&&state::sha256(read(file))==asset_hash,
            "Intentional-exit QA changed the curricular asset");
        const auto reloaded=load_lesson(file);
        const auto& block=reloaded.pages[position->page].blocks[position->block];
        require(block.stable_id==original.stable_id&&block.initial_code==original.initial_code&&
            block.output_check.has_value()==original.output_check.has_value(),
            "The in-memory process goal escaped into the exploratory editor");
    });
    std::cout<<"Headless intentional exits: "<<passed<<" passed, "<<failed<<" failed.\n";
    return failed?1:0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL intentional-exit setup or teardown: "<<error.what()<<'\n';
    return 1;
} catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL intentional-exit Windows setup or teardown: "<<error.code().value<<'\n';
    return 1;
} catch(...) {
    std::cerr<<"FAIL intentional-exit setup or teardown: unexpected exception\n";
    return 1;
}
