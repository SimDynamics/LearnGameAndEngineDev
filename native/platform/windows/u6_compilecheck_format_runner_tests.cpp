#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;

void require(bool condition,std::string_view reason){
    if(!condition)throw std::runtime_error(std::string(reason));
}

const Block& owner(const Lesson& lesson,std::size_t page,std::size_t index){
    require(page<lesson.pages.size()&&index<lesson.pages[page].blocks.size(),"Missing U6 compiler block");
    return lesson.pages[page].blocks[index];
}

void exercise(const Lesson& lesson,const Block& block,
              const WindowsEnvironment& environment,std::uint64_t sequence,bool is_link){
    require(block.compile_check.has_value(),"U6 diagnostic goal missing");
    CppRunRequest request;
    request.request_id=sequence;
    request.revision=250+sequence;
    request.source_utf8=block.initial_code;
    request.project_files=block.project_files;
    request.source_files=block.source_files;
    request.projects_directory=environment.paths.projects/L"U6 compileCheck physical format";
    request.compile_timeout=75s;
    request.execution_timeout=2s;
    request.diagnostic=prepare_cpp_diagnostic_request(block,request);
    require(request.diagnostic&&request.diagnostic->project&&
        request.diagnostic->project->link==is_link&&
        !request.diagnostic->project->error_codes.empty(),
        "The formatted U6 source no longer prepares its exact compile/link objective");
    const auto result=run_cpp(request);
    require(result.error_utf8.empty()&&result.diagnostic&&
        expected_compile_diagnostic(request,result)&&
        result.execution==CppPhaseResult{}&&result.configuration==CppPhaseResult{}&&
        result.artifact.empty()&&!result.executable,
        "The fresh MSVC compiler did not verify the expected U6 rejection");
    if(is_link) {
        require(result.diagnostic->control_link.succeeded()&&
            result.diagnostic->candidate_link.status==CppPhaseStatus::completed&&
            !result.diagnostic->candidate_link.succeeded(),
            "The missing-definition test was not a real link failure");
    } else {
        require(result.diagnostic->control_link==CppPhaseResult{}&&
            result.diagnostic->candidate_link==CppPhaseResult{}&&
            !result.diagnostic->errors.empty(),
            "The invalid return/header was not rejected at compilation");
    }
    state::StateData progress;
    state::prepare_lesson_state(progress,lesson);
    progress.preferences["student_unrelated"]["value"]=std::string("unchanged");
    require(!compiler_puzzle_completed(progress,lesson,block),
        "A U6 diagnostic was credited before a real compiler result");
    require(record_compiler_diagnostic_case(progress,lesson,block,request,result,"Correcto"),
        "A correct MSVC diagnostic failed to credit its original compiler owner");
    require(compiler_puzzle_completed(progress,lesson,block)&&
        state::string_value(progress,"student_unrelated","value")=="unchanged",
        "A formatted compiler result lost the student's credit or unrelated data");
    std::cout<<"PASS U6 MSVC "<<block.compile_check->diagnostic
        <<" phase="<<(is_link?"link":"compile")
        <<" source="<<(block.source_format_original_initial_code?"formatted":"original")<<'\n';
}

void exercise_single(const Lesson& lesson,const Block& block,
        const WindowsEnvironment& environment,std::uint64_t sequence) {
    require(block.compile_check&&block.compile_check->diagnostic=="const-member-call"&&
        block.project_files.empty()&&block.source_files.empty(),
        "The formatted U9 sample is not the authentic source-only diagnostic");
    CppRunRequest request;
    request.request_id=sequence;
    request.revision=250+sequence;
    request.source_utf8=block.initial_code;
    request.projects_directory=environment.paths.projects/L"u9-format";
    request.compile_timeout=75s;
    const auto insertion=request.source_utf8.rfind("    return 0;");
    require(insertion!=std::string::npos,"U9 diagnostic insertion point was removed");
    request.source_utf8.insert(insertion,"    fija.cambiar_a(4,2);\n");
    request.diagnostic=prepare_cpp_diagnostic_request(block,request);
    require(request.diagnostic&&!request.diagnostic->project&&
        request.diagnostic->control_source_utf8.size()==request.source_utf8.size()&&
        request.diagnostic->control_source_utf8!=request.source_utf8&&
        request.diagnostic->expected_error_code=="C2662"&&
        request.diagnostic->first_line>0&&
        request.diagnostic->last_line==request.diagnostic->first_line,
        "U9 diagnostic lost its source-matched, compilation-only C2662 contract");
    const auto result=run_cpp(request);
    require(result.error_utf8.empty()&&result.diagnostic&&
        result.diagnostic->control.succeeded()&&
        expected_compile_diagnostic(request,result)&&
        result.execution==CppPhaseResult{}&&
        result.configuration==CppPhaseResult{}&&
        result.artifact.empty()&&!result.executable,
        "Real MSVC did not reproduce the U9 expected C2662 control and candidate");
    state::StateData progress;
    state::prepare_lesson_state(progress,lesson);
    require(!compiler_puzzle_completed(progress,lesson,block),
        "The formatted U9 compiler objective received credit before any test");
    require(record_compiler_diagnostic_case(progress,lesson,block,request,result,"Correcto")&&
        compiler_puzzle_completed(progress,lesson,block),
        "A real U9 diagnostic was not credited under its historical identity");
    std::cout<<"PASS U9 actual MSVC C2662 source="
        <<(block.source_format_original_initial_code?"formatted":"original")<<'\n';
}
}

int main(int argc,char** argv)try{
    require(argc==2,"Supply an isolated U6 format fixture directory");
    const std::filesystem::path fixture(argv[1]);
    const auto before=load_lesson(fixture/"u6.before.json","es");
    const auto after=load_lesson(fixture/"u6.after.json","es");
    require(before.id==after.id&&before.state_layout.signatures==after.state_layout.signatures,
        "The formatted U6 files changed their historical ownership");
    const LaunchOptions options{ProfileId::Codex,
        L"native-u6-formatted-diagnostic-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);

    const std::array<std::pair<std::size_t,std::size_t>,3> positions{{{14,2},{15,2},{17,2}}};
    std::uint64_t sequence=1;
    for(std::size_t i=0;i<positions.size();++i){
        const auto [page,index]=positions[i];
        const auto& original=owner(before,page,index);
        const auto& formatted=owner(after,page,index);
        require(original.compile_check&&formatted.compile_check&&
            original.compile_check->diagnostic==formatted.compile_check->diagnostic&&
            original.source_files==formatted.source_files,
            "U6 format changed the intended diagnostic or selected source files");
        exercise(before,original,environment,sequence++,i==0);
        exercise(after,formatted,environment,sequence++,i==0);
    }
    const auto u9_before=load_lesson(fixture/"u9.before.json","es");
    const auto u9_after=load_lesson(fixture/"u9.after.json","es");
    require(u9_before.id==u9_after.id&&
        u9_before.state_layout.signatures==u9_after.state_layout.signatures&&
        u9_before.state_layout.lineage==u9_after.state_layout.lineage,
        "The authentic U9 before/after format did not preserve student ownership");
    exercise_single(u9_before,owner(u9_before,6,6),environment,sequence++);
    exercise_single(u9_after,owner(u9_after,6,6),environment,sequence++);
    std::cout<<"PASS six U6 and two U9 actual MSVC compile/link diagnoses before/after safe format\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL U6L2 real MSVC formatted diagnostic: "<<error.what()<<'\n';
    return 1;
}
