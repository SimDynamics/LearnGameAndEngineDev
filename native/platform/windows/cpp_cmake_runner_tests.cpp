#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "native_build_config.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#include <exception>
#include <map>
#include <string_view>

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
    std::filesystem::path root,owned;bool removed=false;
    explicit OwnedQa(const std::filesystem::path& value):root(value){
        check(root.is_absolute()&&root.parent_path().filename()==L"qa"&&root.filename().wstring().starts_with(L"cmake-runner-"+std::to_wstring(GetCurrentProcessId())+L"-")&&!std::filesystem::exists(root),
            "CMake QA cleanup must own a new isolated profile");
        lgaed::state::require_safe_path(root);
        std::filesystem::create_directories(lgaed::state::native_io_path(root.parent_path()));
        lgaed::state::require_safe_path(root.parent_path());
        check(std::filesystem::create_directory(lgaed::state::native_io_path(root)),"CMake QA cleanup must create its own new profile");
        // MSIX can redirect this new child while its pre-existing parent stays
        // physical. Pin the exact created directory, not the parent's old view.
        owned=std::filesystem::weakly_canonical(root);
        check(owned.filename()==root.filename()&&owned.parent_path().filename()==L"qa","The created CMake QA profile changed its identity");
        lgaed::state::require_safe_path(owned);
    }
    void finish(){
        if(removed)return;
        const auto resolved=std::filesystem::weakly_canonical(root);
        check(resolved==owned,"Refuse cleanup outside this exact isolated CMake QA profile");
        lgaed::state::require_safe_path(root);
        lgaed::state::require_safe_path(resolved);
        for(const auto& item:std::filesystem::recursive_directory_iterator(lgaed::state::native_io_path(resolved)))lgaed::state::require_safe_path(item.path());
        std::filesystem::remove_all(lgaed::state::native_io_path(resolved));removed=true;
        check(!std::filesystem::exists(root),"The successful CMake QA profile retained generated artifacts");
    }
    ~OwnedQa() noexcept(false){if(!removed&&std::uncaught_exceptions()==0)finish();}
};
std::string utf8(std::wstring_view text){
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    check(size>0,"Cannot encode a pinned historical toolchain path");std::string result(static_cast<std::size_t>(size),'\0');
    check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr)==size,
        "Cannot encode the complete historical toolchain path");return result;
}
void write(const std::filesystem::path& path,std::string_view bytes){
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);
    check(static_cast<bool>(file)&&static_cast<bool>(file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()))),"Cannot create an owned C++20 cache fixture");
}
using Files=std::map<std::filesystem::path,std::pair<std::string,std::filesystem::file_time_type>>;
Files snapshot_files(const std::filesystem::path& root){Files result;
    for(const auto& file:std::filesystem::recursive_directory_iterator(root))if(file.is_regular_file())
        result.emplace(file.path().lexically_relative(root),std::pair{read(file.path()),file.last_write_time()});return result;
}
void cpp23_cmake_migration(const lgaed::WindowsEnvironment& environment){
    using namespace lgaed;using namespace windows;
    CppRunRequest request;request.projects_directory=environment.paths.projects/L"C++23 conservado";
    request.request_id=23001;request.revision=23;request.project_key="cpp23/default-language/personal-draft";
    request.stdin_utf8="40";
    request.project_files={{"CMakeLists.txt","cmake_minimum_required(VERSION 3.20)\nproject(Cpp23Default LANGUAGES CXX)\nadd_executable(cpp23 main.cpp calculation.cpp)\n"},
        {"contract.hpp",R"cpp(#include <expected>
#include <iostream>
#include <utility>
static_assert(__cplusplus == 202302L, "CMake must expose C++23, not the historical20 or latest mode");
static_assert(_MSVC_LANG == 202302L, "CMake must compile in the actual C++23 MSVC mode");
constexpr int selected_phase(int value) {
    if consteval { return value + 1; }
    else { return value + 2; }
}
static_assert(selected_phase(40) == 41);
enum class Answer : int { accepted = 42 };
std::expected<int,int> calculate(int value, bool accept);
)cpp"},
        {"calculation.cpp",R"cpp(#include "contract.hpp"
std::expected<int,int> calculate(int value, bool accept) {
    if (!accept) return std::unexpected(-9);
    return selected_phase(value);
}
)cpp"},
        {"main.cpp",R"cpp(#include "contract.hpp"
int main() {
    constexpr int compile_phase = selected_phase(40);
    int input{};
    if (!(std::cin >> input)) return 1;
    const auto accepted = calculate(input, true), rejected = calculate(input, false);
    if (!accepted || rejected || rejected.error() != -9) return 2;
    std::cout << __cplusplus << ':' << _MSVC_LANG << ':' << compile_phase
        << ':' << *accepted << ':' << rejected.error() << ':' << std::to_underlying(Answer::accepted);
}
)cpp"}};
    request.cmake=curriculum::CmakeConfig{"cpp23",{},"Debug"};
    const auto original=request.project_files;
    // Pin the exact pre-C++23 addressing algorithm. This is an explicit
    // restored-cache fixture, not a claim that its C++23 draft ran under20.
    const auto old_configuration=state::sha256(curriculum::cpp_project_frame(request.project_files,request.source_files,request.cmake,true)
        +utf8(build::educational_vc_root)+utf8(build::educational_sdk_version)+utf8(build::educational_cmake)+utf8(build::educational_ninja));
    const auto old_identity=state::sha256("cpp-cmake-workspace-v1/"+std::to_string(request.project_key.size())+":"+request.project_key+old_configuration);
    const auto historical=request.projects_directory/std::filesystem::path("c"+old_identity.substr(0,32));
    check(!std::filesystem::exists(historical),"The historical C++20 fixture must be new and private");
    write(historical/L"identity.sha256",old_identity+'\n');
    write(historical/L"CMakeCache.txt","CMAKE_CXX_STANDARD:UNINITIALIZED=20\n");
    write(historical/L"build.ninja","# Preserved C++20 restored-cache fixture; never executed.\n");
    write(historical/L"personal-notes.txt"," \t conservar el trabajo anterior\r\n");
    for(const auto& [name,text]:request.project_files)write(historical/L"p"/std::filesystem::path(name),text);
    const auto old_files=snapshot_files(historical);
    CppRunRequest earlier;earlier.projects_directory=request.projects_directory;earlier.request_id=23000;earlier.revision=1;
    earlier.source_utf8="#include <cstdio>\nint main(){std::printf(\"existing independent run\");}\n";
    const auto prior_run=run_cpp(earlier);ran(prior_run);
    check(prior_run.execution.stdout_bytes=="existing independent run","The independent prior run did not really execute");
    const auto prior_run_files=snapshot_files(prior_run.run_directory);
    state::StateStore store(environment.paths.root);auto data=store.load();
    state::Draft draft;draft.source="// fuente personal sin modificar\r\nint main() { return 7; }\n";
    draft.input=" \t entrada guardada\r\n";draft.indentation_json=state::make_indentation_metadata(draft.source,{});
    draft.selection_start=3;draft.selection_end=11;state::put_draft(data,request.project_key,draft);
    data.preferences["cpp23_private_migration"]["opaque"]=std::string(" conservar\t\r\n保存");store.save(data);
    const auto preserved_data=data;
    const auto pending=inspect_cpp_project(request);
    check(pending.workspace!=historical&&!pending.configured&&!pending.built&&pending.configuration_fingerprint!=old_configuration,
        "The C++23 profile reused the C++20 address or adopted a historical configured artifact");
    const auto migrated=run_cpp(request);ran(migrated);
    check(migrated.configuration.succeeded()&&migrated.execution.stdout_bytes=="202302:202302:41:42:-9:42"&&migrated.execution.stderr_bytes.empty(),
        "CMake did not configure and execute the real default C++23 program with its exact library and language semantics");
    const auto available=inspect_cpp_project(request);
    check(available.workspace==pending.workspace&&available.configured&&available.built,
        "The current C++23 workspace failed normal inspection/reopening");
    const auto commands=read(available.workspace/L"compile_commands.json");
    check(commands.find("std:c++23")!=std::string::npos&&commands.find("std:c++20")==std::string::npos&&commands.find("std:c++latest")==std::string::npos,
        "The CMake rule graph selected historical20 or an open-ended latest compiler mode");
    for(const auto& [name,text]:original)check(read(available.workspace/L"p"/std::filesystem::path(name))==text,
        "C++23 configuration changed the authored project instead of supplying the default language profile");
    const auto artifact_time=std::filesystem::last_write_time(migrated.artifact);
    const auto first_run_files=snapshot_files(migrated.run_directory);
    ++request.request_id;const auto reused=run_cpp(request);ran(reused);
    check(reused.configuration.status==CppPhaseStatus::not_started&&reused.execution.stdout_bytes==migrated.execution.stdout_bytes
        &&std::filesystem::last_write_time(reused.artifact)==artifact_time,
        "Reopening an unchanged C++23 project reconfigured, relinked or executed another result");
    state::StateStore reopened(environment.paths.root);
    check(reopened.load()==preserved_data&&request.project_files==original&&snapshot_files(historical)==old_files
        &&snapshot_files(prior_run.run_directory)==prior_run_files&&snapshot_files(migrated.run_directory)==first_run_files,
        "Migrating C++20 cache identity changed a saved draft, prior cache/source, existing run or original project");
    std::cout<<"PASS C++23 default CMake: exact language macros, real if-consteval/expected/to-underlying execution, separate restored-C++20 cache identity, unchanged prior cache/draft/runs and durable current reuse\n";
}
}
int main(int argc,char** argv){using namespace lgaed;using namespace windows;try{
    const bool cpp23_only=argc==2&&std::string_view(argv[1])=="--cpp23";
    const bool graph_only=argc==2&&std::string_view(argv[1])=="--graph";
    check(argc==1||cpp23_only||graph_only,"Unknown CMake runner test selector");
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"cmake-runner-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});OwnedQa qa(environment.paths.root);initialize_profile_directories(environment.paths);
    if(!graph_only)cpp23_cmake_migration(environment);
    if(cpp23_only){qa.finish();return 0;}
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
    // Both programs print 42, but the original executable compiles value.cpp
    // directly while the second project links the static library. Generated
    // File API facts must distinguish those structures independently of stdout.
    check(configured.cmake_graph&&
        configured.cmake_graph->project_name=="PersonalNative"&&
        configured.cmake_graph->project_fingerprint==configured.project_fingerprint&&
        configured.cmake_graph->configuration_fingerprint==configured.configuration_fingerprint,
        "A successful configure lacked source-bound generated CMake graph evidence");
    const auto find_target=[](const CppCmakeGraphEvidence& graph,
            std::string_view name)->const CppCmakeTargetEvidence& {
        const auto found=std::ranges::find_if(graph.targets,
            [&](const auto& one){return one.name==name;});
        check(found!=graph.targets.end(),"No generated target: "+std::string(name));
        return *found;
    };
    const auto contains=[](const auto& items,std::string_view value) {
        return std::ranges::find(items,std::string(value))!=items.end();
    };
    const auto links=[](const CppCmakeTargetEvidence& target,
            std::string_view name) {
        return std::ranges::any_of(target.link_libraries,[&](const auto& fragment){
            return fragment.find(name)!=std::string::npos;
        });
    };
    const auto& original_first=find_target(*configured.cmake_graph,"first");
    const auto& original_parts=find_target(*configured.cmake_graph,"parts");
    check(original_first.type=="EXECUTABLE"&&
        contains(original_first.compiled_sources,"src/main.cpp")&&
        contains(original_first.compiled_sources,"src/value.cpp")&&
        !contains(original_first.build_dependencies,"parts")&&
        !links(original_first,"parts.lib")&&
        original_parts.type=="STATIC_LIBRARY"&&
        contains(original_parts.compiled_sources,"src/value.cpp"),
        "An unrelated library and a duplicate .cpp source were mistaken for a linked target");
    auto linked=request;
    linked.project_key+="/real-link-graph";
    linked.request_id=1201;
    linked.action=CppRunRequest::Action::compile_run;
    linked.project_files[0].second=R"cmake(cmake_minimum_required(VERSION 3.20)
project(GraphLinked LANGUAGES CXX)
add_library(parts STATIC src/value.cpp)
target_include_directories(parts PRIVATE include)
target_compile_definitions(parts PRIVATE CHOSEN=42)
add_executable(first src/main.cpp)
target_include_directories(first PRIVATE include)
target_link_libraries(first PRIVATE parts)
)cmake";
    const auto linked_result=run_cpp(linked);
    ran(linked_result);
    check(linked_result.execution.stdout_bytes=="42"&&
        linked_result.cmake_graph&&
        linked_result.cmake_graph->project_name=="GraphLinked",
        "Actual linked library failed to build or expose the generated CMake graph");
    const auto& linked_first=find_target(*linked_result.cmake_graph,"first");
    check(linked_first.type=="EXECUTABLE"&&
        contains(linked_first.compiled_sources,"src/main.cpp")&&
        !contains(linked_first.compiled_sources,"src/value.cpp")&&
        contains(linked_first.build_dependencies,"parts")&&
        links(linked_first,"parts.lib")&&
        find_target(*linked_result.cmake_graph,"parts").type=="STATIC_LIBRARY",
        "The generated library link graph did not differ from direct-source compilation");
    const CppCmakeGraphRequirement expected_graph{
        "GraphLinked",{
            {"first","EXECUTABLE",{"src/main.cpp"},{"parts"},true},
            {"parts","STATIC_LIBRARY",{"src/value.cpp"},{},true}
        }};
    const auto accepted=evaluate_cmake_graph(linked,linked_result,expected_graph);
    check(accepted.correct,"A real source-bound linked CMake project failed its graph criteria: "+accepted.reason);
    const auto late_id=linked_result;
    auto wrong_request=linked;
    ++wrong_request.revision;
    check(!evaluate_cmake_graph(wrong_request,late_id,expected_graph).correct,
        "A File API graph from an older revision earned a new execution receipt");
    wrong_request=linked;
    wrong_request.project_files[0].second+="\n# changed source revision\n";
    check(!evaluate_cmake_graph(wrong_request,linked_result,expected_graph).correct,
        "The structure of a different project source earned current graph credit");
    auto wrong_type=expected_graph;
    wrong_type.targets[1].type="SHARED_LIBRARY";
    check(!evaluate_cmake_graph(linked,linked_result,wrong_type).correct,
        "A static library was accepted as the wrong kind of target");
    auto wrong_source=expected_graph;
    wrong_source.targets[0].sources={"src/value.cpp"};
    check(!evaluate_cmake_graph(linked,linked_result,wrong_source).correct,
        "A wrong compiled translation unit earned credit");
    auto ordering=linked;
    ordering.project_key+="/order-only-not-link";
    ordering.request_id=1202;
    ordering.project_files[0].second=R"cmake(cmake_minimum_required(VERSION 3.20)
project(GraphLinked LANGUAGES CXX)
add_library(parts STATIC src/value.cpp)
target_include_directories(parts PRIVATE include)
add_executable(first src/main.cpp src/value.cpp)
target_include_directories(first PRIVATE include)
target_compile_definitions(first PRIVATE CHOSEN=42)
add_dependencies(first parts)
)cmake";
    const auto order_run=run_cpp(ordering);
    ran(order_run);
    check(order_run.execution.stdout_bytes=="42"&&order_run.cmake_graph,
        "The independent build-order-only example did not execute correctly");
    const auto& order_first=find_target(*order_run.cmake_graph,"first");
    check(contains(order_first.build_dependencies,"parts")&&
        !links(order_first,"parts.lib"),
        "The negative CMake graph does not have a real dependency without linking");
    auto relaxed=expected_graph;
    relaxed.targets[0].exact_sources=false;
    const auto rejected=evaluate_cmake_graph(ordering,order_run,relaxed);
    check(!rejected.correct&&rejected.reason.find("enlaza")!=std::string::npos,
        "An add_dependencies ordering edge was misrepresented as a library link");
    auto config_only=linked;
    config_only.action=CppRunRequest::Action::configure;
    check(!evaluate_cmake_graph(config_only,linked_result,expected_graph).correct,
        "An earlier CMake configure was treated as a compiled and executed objective");
    if(graph_only) {
        qa.finish();
        std::cout<<"PASS real CMake File API: source duplication is not library linking; target types, sources, dependency and link fragments verified\n";
        return 0;
    }
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
