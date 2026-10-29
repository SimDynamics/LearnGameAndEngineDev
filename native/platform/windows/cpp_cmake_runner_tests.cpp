#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#include <exception>

namespace {
void check(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
void built(const lgaed::windows::CppRunResult& result){check(result.error_utf8.empty()&&result.compilation.succeeded(),"Real build failed: "+result.error_utf8+result.configuration.stdout_bytes+result.configuration.stderr_bytes+result.compilation.stdout_bytes+result.compilation.stderr_bytes);}
void ran(const lgaed::windows::CppRunResult& result){built(result);check(result.execution.succeeded(),"Real execution failed: "+result.error_utf8+result.execution.stderr_bytes);}
bool visible_tool_console(){bool visible=false;EnumWindows([](HWND window,LPARAM context)->BOOL{
    wchar_t title[160]{};GetWindowTextW(window,title,160);
    if(std::wstring_view(title).starts_with(L"LearnGameAndEngineDev.CppTool.")&&IsWindowVisible(window))*reinterpret_cast<bool*>(context)=true;
    return TRUE;
},reinterpret_cast<LPARAM>(&visible));return visible;}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);check(static_cast<bool>(file),"Read the existing owned CMake fixture");return std::string((std::istreambuf_iterator<char>(file)),{});}
struct OwnedQa {
    std::filesystem::path root,parent;bool removed=false;
    explicit OwnedQa(const std::filesystem::path& value):root(value),parent(std::filesystem::weakly_canonical(value.parent_path())){
        check(root.is_absolute()&&root.parent_path().filename()==L"qa"&&root.filename().wstring().starts_with(L"cmake-runner-"+std::to_wstring(GetCurrentProcessId())+L"-")&&!std::filesystem::exists(root),
            "CMake QA cleanup must own a new isolated profile");
    }
    void finish(){
        if(removed)return;
        const auto resolved=std::filesystem::weakly_canonical(root);
        check(resolved.parent_path()==parent&&resolved.filename()==root.filename(),"Refuse cleanup outside this exact isolated CMake QA profile");
        lgaed::state::require_safe_path(resolved);
        for(const auto& item:std::filesystem::recursive_directory_iterator(lgaed::state::native_io_path(resolved)))lgaed::state::require_safe_path(item.path());
        std::filesystem::remove_all(lgaed::state::native_io_path(resolved));removed=true;
        check(!std::filesystem::exists(root),"The successful CMake QA profile retained generated artifacts");
    }
    ~OwnedQa() noexcept(false){if(!removed&&std::uncaught_exceptions()==0)finish();}
};
}
int main(){using namespace lgaed;using namespace windows;try{
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"cmake-runner-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});OwnedQa qa(environment.paths.root);initialize_profile_directories(environment.paths);
    CppRunRequest request;request.projects_directory=environment.paths.projects/L"Códigos & nombres";request.project_key="original/exercise/draft";request.request_id=1;request.revision=7;
    request.project_files={{"CMakeLists.txt",R"(cmake_minimum_required(VERSION 3.20)
project(PersonalNative LANGUAGES CXX)
message(STATUS "LGAED stdout áé你好")
message(WARNING "LGAED stderr áé你好")
option(USE_FIRST "Select value" ON)
add_executable(first src/main.cpp src/value.cpp)
target_include_directories(first PRIVATE include)
if(USE_FIRST)
  target_compile_definitions(first PRIVATE CHOSEN=42)
else()
  target_compile_definitions(first PRIVATE CHOSEN=9)
endif()
set_target_properties(first PROPERTIES OUTPUT_NAME "real-file-api-name" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_executable(second src/second.cpp)
add_library(parts STATIC src/value.cpp)
target_include_directories(parts PRIVATE include)
)"},{"include/value.hpp","int number();\r\n"},{"src/main.cpp","#include <cstdio>\n#include \"value.hpp\"\nint main(){std::printf(\"%d\",number());}\n"},{"src/value.cpp","#include \"value.hpp\"\n#ifndef CHOSEN\n#define CHOSEN 0\n#endif\nint number(){return CHOSEN;}\n"},{"src/second.cpp","#include <cstdio>\nint main(){std::printf(\"42\");}\n"}};
    request.cmake=curriculum::CmakeConfig{"first",{{"USE_FIRST","ON"}},"Debug"};request.action=CppRunRequest::Action::configure;
    const auto initial=inspect_cpp_project(request);check(!initial.configured,"Inspection invented a configured workspace");
    check(initial.workspace.parent_path()==request.projects_directory&&initial.workspace.filename().wstring().size()==33&&initial.workspace.filename().wstring().front()==L'c',
        "The CMake workspace exhausted Windows path space by nesting identity digests");
    auto independent=request;independent.project_key+="/other";check(inspect_cpp_project(independent).workspace!=initial.workspace,"Independent editor identities shared a CMake workspace");
    {
        // Simulate a different full identity at this shortened address. Neither
        // configure nor inspection may adopt or overwrite its existing files.
        std::filesystem::create_directories(initial.workspace);
        const auto marker=initial.workspace/L"identity.sha256",kept=initial.workspace/L"existing.txt";
        {std::ofstream stream(marker,std::ios::binary);stream<<std::string(64,'0')<<'\n';}
        {std::ofstream stream(kept,std::ios::binary);stream<<"other owner data";}
        const auto refused=run_cpp(request);check(!refused.error_utf8.empty()&&!refused.configuration.succeeded(),"A shortened-address collision configured another owner's project");
        bool inspection_rejected=false;try{(void)inspect_cpp_project(request);}catch(const std::exception&){inspection_rejected=true;}
        std::ifstream owner(marker,std::ios::binary),data(kept,std::ios::binary);
        check(inspection_rejected&&std::string((std::istreambuf_iterator<char>(owner)),{})==std::string(64,'0')+"\n"&&std::string((std::istreambuf_iterator<char>(data)),{})=="other owner data"&&!std::filesystem::exists(initial.workspace/L"p"),
            "An ownership collision altered another project's manifest or files");
        owner.close();data.close();std::filesystem::remove(kept);std::filesystem::remove(marker);std::filesystem::remove(initial.workspace);
    }
    const auto configured=run_cpp(request);check(configured.error_utf8.empty()&&configured.configuration.succeeded()&&!configured.compilation.succeeded()&&!configured.execution.succeeded(),"Configure did not run real CMake/Ninja generation only: "+configured.error_utf8+configured.configuration.stderr_bytes);
    check(configured.configuration.stdout_bytes.find("LGAED stdout áé你好")!=std::string::npos&&configured.configuration.stderr_bytes.find("LGAED stderr áé你好")!=std::string::npos,
        "The private tool host mixed channels or corrupted real Unicode diagnostics");
    check(configured.configuration.stdout_bytes.find("Detecting CXX compiler ABI info - done")!=std::string::npos&&configured.configuration.stderr_bytes.find("CMAKE_OBJECT_PATH_MAX")==std::string::npos,
        "The native tool workspace still exhausts MSVC paths during the real ABI check");
    auto available=inspect_cpp_project(request);check(available.configured&&!available.built&&available.target_type=="EXECUTABLE","File API target/configuration inspection failed");
    request.action=CppRunRequest::Action::build;const auto build=run_cpp(request);built(build);check(!build.execution.succeeded()&&build.artifact.filename()==L"real-file-api-name.exe","Build guessed a filename or automatically executed the program");
    available=inspect_cpp_project(request);check(available.built&&available.artifact==build.artifact,"Reopening lost the verified current built artifact");
    request.action=CppRunRequest::Action::execute;const auto first=run_cpp(request);ran(first);check(first.execution.stdout_bytes=="42"&&first.executable=="first"&&first.configuration.status==CppPhaseStatus::not_started,"Execute did not use the selected real CMake target without reconfiguring");
    const auto unchanged_time=std::filesystem::last_write_time(first.artifact);request.action=CppRunRequest::Action::compile_run;const auto reused=run_cpp(request);ran(reused);check(reused.configuration.status==CppPhaseStatus::not_started&&std::filesystem::last_write_time(reused.artifact)==unchanged_time,"An unchanged snapshot unnecessarily reconfigured or relinked a valid executable");
    auto second=request;second.cmake->target="second";check(inspect_cpp_project(second).configured&&!inspect_cpp_project(second).built,"Changing the target reused another executable or unnecessarily invalidated configure state");const auto second_run=run_cpp(second);ran(second_run);check(second_run.execution.stdout_bytes=="42"&&second_run.executable=="second","The second real File API executable did not run");
    curriculum::OutputCheck output;output.cases.push_back({"",{"42"},"",{},"first"});curriculum::FreshExecution execution{request.revision,true,true,0,"",second_run.execution.stdout_bytes,second_run.executable};
    check(curriculum::evaluate_output_check(output,execution,request.revision).verdict==curriculum::OutputVerdict::unavailable,"Matching stdout from the wrong executable was credited as the authored output");execution.executable=first.executable;check(curriculum::evaluate_output_check(output,execution,request.revision).verdict==curriculum::OutputVerdict::correct,"The matching fresh executable was not checked");
    request.project_files[3].second="#include \"value.hpp\"\n#ifndef CHOSEN\n#define CHOSEN 0\n#endif\nint number(){return CHOSEN+1;}\n";++request.revision;available=inspect_cpp_project(request);check(available.configured&&!available.built,"Source edits did not invalidate the old built result");request.action=CppRunRequest::Action::execute;const auto stale=run_cpp(request);check(!stale.error_utf8.empty()&&!stale.execution.succeeded(),"Execute ran a stale binary after a source edit");request.action=CppRunRequest::Action::compile_run;const auto changed=run_cpp(request);ran(changed);check(changed.execution.stdout_bytes=="43"&&changed.configuration.status==CppPhaseStatus::not_started,"A source-only change did not rebuild using valid configure state");
    {std::ofstream tamper(changed.artifact,std::ios::binary|std::ios::app);tamper<<"owned QA changed artifact";}check(!inspect_cpp_project(request).built,"A modified executable bypassed SHA256 verification");const auto repaired=run_cpp(request);ran(repaired);check(repaired.execution.stdout_bytes=="43","A changed generated artifact was not rebuilt safely");
    auto option=request;option.cmake->options["USE_FIRST"]="OFF";check(!inspect_cpp_project(option).configured,"Changed CMake options reused old generated rules");option.action=CppRunRequest::Action::build;const auto unconfigured=run_cpp(option);check(!unconfigured.error_utf8.empty()&&!unconfigured.compilation.succeeded(),"Build skipped required configure after options changed");
    auto library=request;library.cmake->target="parts";library.action=CppRunRequest::Action::build;const auto parts=run_cpp(library);built(parts);check(parts.target_type=="STATIC_LIBRARY"&&!parts.execution.succeeded()&&!inspect_cpp_project(library).built,"A real library target was misidentified as executable");
    auto invalid=request;invalid.project_files.push_back({"../outside.cpp","int main(){}"});const auto rejected=run_cpp(invalid);check(!rejected.error_utf8.empty()&&rejected.run_directory.empty(),"An escaping virtual path was materialized");
    auto unicode=request;unicode.project_files={{"Á.cpp","int x;"},{"á.cpp","int y;"}};unicode.cmake.reset();unicode.source_files={"Á.cpp"};const auto collision=run_cpp(unicode);check(!collision.error_utf8.empty()&&collision.run_directory.empty(),"Windows Unicode case collision overwrote a source file");
    auto direct=request;direct.cmake.reset();direct.source_files={"one/value.cpp","two/value.cpp","main.cpp"};direct.project_files={{"one/value.cpp","int first(){return 6;}"},{"two/value.cpp","int second(){return 7;}"},{"main.cpp","#include <cstdio>\nint first();int second();int main(){std::printf(\"%d\",first()+second());}"}};direct.action=CppRunRequest::Action::compile_run;const auto multiple=run_cpp(direct);ran(multiple);check(multiple.execution.stdout_bytes=="13","Distinct same-basename translation units collided or failed to link");
    {
        // Exercise both real project execution paths with ordinary C++ LF
        // output. The Windows alternative belongs to comparison, not capture.
        auto multiline=direct;multiline.request_id=100;multiline.project_key+="/stdout-line-endings";
        multiline.project_files[2].second=
            "#include <iostream>\nint first();\nint second();\n"
            "int main() {\n    int input{}, extra_space{};\n    std::cin >> input >> extra_space;\n"
            "    std::cout << input + first() + second() << \"\\nFin\";\n"
            "    if (extra_space) std::cout << ' ';\n    std::cerr << \" \\tdiagnostico\\n \";\n}\n";
        const std::string inputs[]{" \t7 0\n "," \t7 1\n "};
        for(const bool use_cmake:{false,true}) {
            auto current=multiline;const std::string kind=use_cmake?"CMake":"Direct multi-file";
            if(use_cmake) {
                current.project_files.push_back({"CMakeLists.txt",
                    "cmake_minimum_required(VERSION 3.20)\nproject(LineEndings LANGUAGES CXX)\n"
                    "add_executable(line_endings main.cpp one/value.cpp two/value.cpp)\n"});
                current.source_files.clear();current.cmake=curriculum::CmakeConfig{"line_endings",{},"Debug"};
            }
            curriculum::OutputCheck goal;
            for(const auto& input:inputs) {
                curriculum::OutputCase item;item.standard_input=input;item.outputs={"20\nFin"};
                if(use_cmake)item.executable="line_endings";
                item.standard_error=curriculum::StandardErrorExpectation::non_blank;goal.cases.push_back(item);
            }
            const auto original_files=current.project_files;
            for(unsigned extra_space=0;extra_space<2;++extra_space) {
                ++current.request_id;current.stdin_utf8=inputs[extra_space];
                const auto result=run_cpp(current);ran(result);
                const auto raw=std::string("20\r\nFin")+(extra_space?" ":"");
                const std::string error=" \tdiagnostico\r\n ";
                check(result.execution.stdout_bytes==raw&&result.execution.stderr_bytes==error&&
                    read(result.run_directory/L"execution-stdout.bin")==raw&&
                    read(result.run_directory/L"execution-stderr.bin")==error&&
                    read(result.run_directory/L"stdin.bin")==current.stdin_utf8,
                    kind+" rewrote raw CRLF, significant spaces/TAB, stdin or separate stderr");
                const auto source=use_cmake?inspect_cpp_project(current).workspace/L"p":result.run_directory/L"project";
                for(const auto& [name,text]:original_files)check(read(source/std::filesystem::path(name))==text,
                    kind+" rewrote an authored project source or configuration");
                curriculum::FreshExecution observed;observed.source_revision=result.revision;
                observed.compilation_succeeded=result.compilation.succeeded();observed.execution_succeeded=result.execution.succeeded();
                observed.exit_code=static_cast<int>(result.execution.exit_code.value_or(0xffffffffu));
                observed.standard_input=current.stdin_utf8;observed.standard_output=result.execution.stdout_bytes;
                observed.standard_error=result.execution.stderr_bytes;observed.executable=result.executable;
                check(evaluate_cpp_output_check(goal,observed,current.revision).verdict==
                    (extra_space?curriculum::OutputVerdict::incorrect:curriculum::OutputVerdict::correct),
                    kind+" rejected native CRLF for an LF goal or accepted an added final space");
                if(!extra_space) {
                    auto tabbed=observed;tabbed.standard_output+='\t';
                    check(evaluate_cpp_output_check(goal,tabbed,current.revision).verdict==curriculum::OutputVerdict::incorrect,
                        kind+" accepted an extra TAB in its exact multiline output");
                }
                check(curriculum::evaluate_output_check(goal,observed,current.revision).verdict==curriculum::OutputVerdict::incorrect,
                    kind+" changed the shared exact LF contract to accept raw CRLF");
                check(observed.standard_output==raw&&observed.standard_error==error&&observed.standard_input==inputs[extra_space]&&
                    current.project_files==original_files&&goal.cases.size()==2,
                    kind+" comparison changed captured bytes, original sources, input or LF goals");
                for(unsigned index=0;index<2;++index)check(goal.cases[index].standard_input==inputs[index]&&
                    goal.cases[index].outputs==std::vector<std::string>{"20\nFin"}&&goal.cases[index].standard_error==curriculum::StandardErrorExpectation::non_blank&&
                    goal.cases[index].executable==(use_cmake?std::optional<std::string>{"line_endings"}:std::nullopt),
                    kind+" comparison changed an exact LF output, input, target or stderr requirement");
            }
        }
        std::cout<<"PASS real direct multi-file and CMake multiline CRLF capture, Windows LF verdict, final-space rejection and exact source/stdin/stdout/stderr preservation\n";
    }
    {
        const auto workspace=inspect_cpp_project(request).workspace,manifest=workspace/L"identity.sha256",record=workspace/L"retained.fixture";
        const auto identity=read(manifest);const auto source_stamp=std::filesystem::last_write_time(workspace/L"p/src/main.cpp");
        {std::ofstream kept(record,std::ios::binary);kept<<"owned record preserved";}
        auto fresh=request;fresh.action=CppRunRequest::Action::configure;const auto generated=run_cpp(fresh);
        check(generated.error_utf8.empty()&&generated.configuration.succeeded()&&std::filesystem::is_regular_file(workspace/L"build.ninja")&&read(manifest)==identity&&read(record)=="owned record preserved",
            "CMake --fresh removed the source-independent ownership manifest or an existing record");
        for(const auto& [name,text]:request.project_files)check(read(workspace/L"p"/std::filesystem::path(name))==text,"CMake --fresh removed or rewrote a student project file");
        check(std::filesystem::last_write_time(workspace/L"p/src/main.cpp")==source_stamp,"Configure rewrote unchanged source text in its out-of-source p directory");
    }
    {
        using namespace std::chrono_literals;
        auto cancellable=request;cancellable.request_id=99;cancellable.project_key+="/cancel";cancellable.action=CppRunRequest::Action::configure;
        cancellable.project_files[0].second="cmake_minimum_required(VERSION 3.20)\nproject(CancelHost LANGUAGES CXX)\nmessage(STATUS \"LGAED_HOST_READY\")\nexecute_process(COMMAND \"${CMAKE_COMMAND}\" -E sleep 30)\nadd_executable(first src/main.cpp src/value.cpp)\ntarget_include_directories(first PRIVATE include)\n";
        const auto parent_input=GetConsoleCP(),parent_output=GetConsoleOutputCP();std::stop_source stop;
        auto future=std::async(std::launch::async,[&]{return run_cpp(cancellable,stop.get_token());});
        bool ready=false,visible=false;const auto deadline=std::chrono::steady_clock::now()+10s;
        while(std::chrono::steady_clock::now()<deadline&&!ready&&future.wait_for(0ms)!=std::future_status::ready){
            visible=visible||visible_tool_console();
            for(const auto& directory:std::filesystem::directory_iterator(request.projects_directory/L"cpp-runs"))if(directory.path().filename().wstring().starts_with(L"run-99-")){
                std::ifstream captured_output(directory.path()/L"configuration-stdout.bin",std::ios::binary);
                if(captured_output){const std::string bytes((std::istreambuf_iterator<char>(captured_output)),{});ready=bytes.find("LGAED_HOST_READY")!=std::string::npos;}
            }
            if(!ready)std::this_thread::sleep_for(10ms);
        }
        stop.request_stop();check(future.wait_for(5s)==std::future_status::ready,"Cancellation did not stop the tool host and its real CMake child");const auto stopped=future.get();
        check(ready&&!visible&&stopped.configuration.status==CppPhaseStatus::cancelled&&!stopped.execution.succeeded(),"The active native host was visible, failed to cancel, or executed stale output");
        check(GetConsoleCP()==parent_input&&GetConsoleOutputCP()==parent_output,"A tool host changed its launching terminal's console encoding");
    }
    qa.finish();std::cout<<"PASS real pinned native CMake configure/build/File API targets/execute, unchanged reuse, fresh per-executable checks, stale/tampered rejection and repair, source/options invalidation, library distinction, safe paths and multi-file MSVC\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
