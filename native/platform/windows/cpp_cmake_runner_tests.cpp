#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
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
    check(!migrated.cmake_graph,
        "An ordinary CMake compile incorrectly captured structure without opting in");
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
    request.capture_cmake_graph=true;
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
    auto linked_declaration=linked;
    linked_declaration.cmake_graph_check=expected_graph;
    const auto linked_capture=capture_cpp_execution(linked_declaration,linked_result);
    check(linked_capture.cmake_graph_correct,
        "The genuine CMake graph did not reach the first-case execution capture");
    auto false_declaration=linked_declaration;
    false_declaration.cmake_graph_check->targets[0].links_to.clear();
    false_declaration.cmake_graph_check->targets[0].sources={"src/value.cpp"};
    check(!capture_cpp_execution(false_declaration,linked_result).cmake_graph_correct,
        "An incorrect authored graph obtained the same execution proof");

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
    const auto graph_rejected=evaluate_cmake_graph(ordering,order_run,relaxed);
    check(!graph_rejected.correct&&
        graph_rejected.reason.find("enlaza")!=std::string::npos,
        "An add_dependencies ordering edge was misrepresented as a library link");
    auto config_only=linked;
    config_only.action=CppRunRequest::Action::configure;
    check(!evaluate_cmake_graph(config_only,linked_result,expected_graph).correct,
        "An earlier CMake configure was treated as a compiled and executed objective");
    // A real published U2 editor can receive the new root-only graph
    // declaration without changing any historical source, address or draft.
    constexpr auto relative=
        "app/src/main/assets/content/es/programming/programming_practice_tools/"
        "construccion_reproducible/lesson_objetivos_dependencias_y_enlace.json";
    const auto original=lgaed::windows::load_lesson(
        std::filesystem::path(build::source_root)/relative);
    check(original.pages[4].blocks[4].stable_id=="block_0004_0004"&&
        original.pages[4].blocks[4].cmake&&
        original.pages[4].blocks[4].output_check&&
        !original.pages[4].blocks[4].cmake_graph_check,
        "The actual historical U2 CMake project owner moved or is already declared");
    std::string projected=original.raw_json;
    constexpr auto anchor="  \"stateLayout\": {";
    const auto at=projected.find(anchor);
    check(at!=projected.npos&&projected.find(anchor,at+1)==projected.npos,
        "The historical CMake lesson must have one root state map");
    projected.insert(at,R"json(  "compilerCmakeGraphChecks": [
    {"activity":"block_0004_0004","project":"Preparacion",
     "targets":[
       {"name":"revisar","type":"EXECUTABLE",
        "sources":["main.cpp"],"linksTo":["faltantes"],"exactSources":true},
       {"name":"faltantes","type":"STATIC_LIBRARY",
        "sources":["faltantes.cpp"],"exactSources":true}
     ]},
    {"activity":"block_0008_0003","project":"Pases",
     "targets":[
       {"name":"portero","type":"EXECUTABLE",
        "sources":["acceso.cpp"],"linksTo":["regla"],"exactSources":true},
       {"name":"regla","type":"STATIC_LIBRARY",
        "sources":["regla.cpp"],"exactSources":true}
     ]}
  ],
)json");
    const auto mapped=lgaed::windows::parse_lesson_json(projected,"es");
    const auto metadata=lgaed::windows::parse_lesson_progress_metadata(projected,"es");
    check(mapped.state_layout.lineage==original.state_layout.lineage&&
        mapped.state_layout.signatures==original.state_layout.signatures&&
        metadata.state_layout.lineage==original.state_layout.lineage&&
        metadata.state_layout.signatures==original.state_layout.signatures,
        "A root CMake graph declaration changed stable learner state identity");
    const auto& before=original.pages[4].blocks[4];
    const auto& after=mapped.pages[4].blocks[4];
    const auto& lean=metadata.pages[4].blocks[4];
    check(after.cmake_graph_check.has_value()&&
        lean.cmake_graph_check==after.cmake_graph_check&&
        after.initial_code==before.initial_code&&
        after.project_files==before.project_files&&
        curriculum::exercise_key(mapped,after)==
            curriculum::exercise_key(original,before)&&
        curriculum::draft_key("programming",mapped,after)==
            curriculum::draft_key("programming",original,before)&&
        curriculum::reference_objective_frame(mapped,after)!=
            curriculum::reference_objective_frame(original,before),
        "The root CMake graph contract did not bind to its owner");
    CppRunRequest student;
    student.cmake=after.cmake;
    student.project_files=after.project_files;
    student.source_files=after.source_files;
    student.action=CppRunRequest::Action::compile_run;
    student.cmake_graph_check=after.cmake_graph_check;
    student.capture_cmake_graph=true;
    check(compiler_runtime_request_allowed(after,student),
        "An authentic CMake student's current structure was not eligible to run");
    student.capture_cmake_graph=false;
    check(!compiler_runtime_request_allowed(after,student),
        "The lesson accepted a CMake request without structural capture");
    student.capture_cmake_graph=true;
    student.cmake_graph_check.reset();
    check(!compiler_runtime_request_allowed(after,student),
        "The lesson accepted a request omitting its authenticated graph rubric");
    std::cout<<"PASS root CMake graph declaration: original U2 source, maps, drafts and responses unchanged; new source-bound rubric required\\n";
    // Construct the actual historical U2 student project with its missing
    // link repaired. A real MSVC run, not an in-memory matching string, must
    // grant exactly the new first-case structural proof.
    auto repaired_project=student;
    repaired_project.capture_cmake_graph=true;
    repaired_project.cmake_graph_check=after.cmake_graph_check;
    repaired_project.request_id=1401;
    repaired_project.revision=140;
    repaired_project.project_key="qa/p2-u2-cmake-link-repair";
    repaired_project.projects_directory=environment.paths.projects;
    bool added_link=false;
    for(auto& file:repaired_project.project_files)
        if(file.first=="CMakeLists.txt") {
            file.second+="target_link_libraries(revisar PRIVATE faltantes)\n";
            added_link=true;
        }
    check(added_link&&compiler_runtime_request_allowed(after,repaired_project),
        "A student CMake link edit was incorrectly refused before execution");
    const auto first_case=run_cpp(repaired_project);
    ran(first_case);
    check(first_case.execution.stdout_bytes=="3:0:0"&&
        first_case.execution.stderr_bytes.empty(),
        "The actual published U2 project did not preserve its first-case output");
    const auto credited_capture=capture_cpp_execution(repaired_project,first_case);
    check(credited_capture.cmake_graph_correct&&
        evaluate_cpp_output_check(*after.output_check,credited_capture,
            repaired_project.revision).verdict==curriculum::OutputVerdict::correct,
        "The real repaired project did not satisfy both graph and output evidence");
    auto legacy_request=repaired_project;
    legacy_request.capture_cmake_graph=false;
    legacy_request.cmake_graph_check.reset();
    const auto old_capture=capture_cpp_execution(legacy_request,first_case);
    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    check(record_compiler_puzzle_case(saved,original,before,legacy_request,
            old_capture,"Salida histórica")&&
        compiler_puzzle_completed(saved,original,before),
        "A valid historical stdout receipt could not be represented");
    check(!compiler_puzzle_completed(saved,mapped,after),
        "The new CMake structure inherited an old stdout-only achievement");
    state::prepare_lesson_state(saved,mapped);
    check(record_compiler_puzzle_case(saved,mapped,after,repaired_project,
            credited_capture,"Estructura y salida acreditadas")&&
        compiler_puzzle_completed(saved,mapped,after),
        "The first verified CMake case did not unlock its new objective");
    const auto reopened=state::decode(state::encode(saved));
    check(compiler_puzzle_completed(reopened,mapped,after)&&
        compiler_puzzle_completed(reopened,original,before)&&
        compiler_puzzle_case_count(reopened,mapped,after)==1,
        "Reopening changed either historical stdout or new graph accreditation");
    std::cout<<"PASS actual published U2 CMake editor: MSVC library link, first-case success, historical receipts and reopening\\n";

    // The second original U2L2 exercise begins with an empty
    // CMakeLists.txt. The student must build the static library and link it
    // to the portero executable. The same stdout alone does not prove this.
    const auto& passes_before=original.pages.at(8).blocks.at(3);
    const auto& passes_after=mapped.pages.at(8).blocks.at(3);
    const auto& passes_metadata=metadata.pages.at(8).blocks.at(3);
    check(passes_before.stable_id=="block_0008_0003"&&
        !passes_before.cmake_graph_check&&
        passes_after.cmake_graph_check&&
        passes_after.cmake_graph_check==passes_metadata.cmake_graph_check&&
        passes_after.project_files==passes_before.project_files&&
        curriculum::exercise_key(mapped,passes_after)==
            curriculum::exercise_key(original,passes_before)&&
        curriculum::draft_key("programming",mapped,passes_after)==
            curriculum::draft_key("programming",original,passes_before)&&
        curriculum::reference_objective_frame(mapped,passes_after)!=
            curriculum::reference_objective_frame(original,passes_before),
        "The second U2L2 CMake contract changed historical sources or addresses");
    CppRunRequest passes;
    passes.request_id=1440;
    passes.revision=440;
    passes.project_key="qa/p2-u2-new-linked-library";
    passes.projects_directory=environment.paths.projects;
    passes.project_files=passes_after.project_files;
    passes.cmake=passes_after.cmake;
    passes.cmake_graph_check=passes_after.cmake_graph_check;
    passes.capture_cmake_graph=true;
    passes.action=CppRunRequest::Action::compile_run;
    for(auto& file:passes.project_files)
        if(file.first=="CMakeLists.txt")
            file.second=R"cmake(cmake_minimum_required(VERSION 3.20)
project(Pases LANGUAGES CXX)
add_library(regla STATIC regla.cpp)
add_executable(portero acceso.cpp)
target_link_libraries(portero PRIVATE regla)
target_compile_features(regla PRIVATE cxx_std_20)
target_compile_features(portero PRIVATE cxx_std_20)
set_target_properties(regla portero PROPERTIES CXX_EXTENSIONS OFF)
)cmake";
    check(compiler_runtime_request_allowed(passes_after,passes),
        "The original Pases editor refused a valid new library and client");
    const auto passes_result=run_cpp(passes);
    ran(passes_result);
    const auto passes_fresh=capture_cpp_execution(passes,passes_result);
    check(passes_result.execution.stdout_bytes=="1:1:0"&&
        passes_fresh.cmake_graph_correct&&
        evaluate_cpp_output_check(*passes_after.output_check,passes_fresh,
            passes.revision).verdict==curriculum::OutputVerdict::correct,
        "Real MSVC failed the Pases graph or its three prepared observations");
    auto passes_saved=reopened;
    state::prepare_lesson_state(passes_saved,mapped);
    check(!compiler_puzzle_completed(passes_saved,mapped,passes_after)&&
        record_compiler_puzzle_case(passes_saved,mapped,passes_after,
            passes,passes_fresh,"Pases enlazado")&&
        compiler_puzzle_completed(passes_saved,mapped,passes_after)&&
        compiler_puzzle_case_count(passes_saved,mapped,passes_after)==1,
        "The correct first Pases case did not unlock its new contract");
    auto duplicate=passes;
    duplicate.request_id=1441;
    duplicate.revision=441;
    duplicate.project_key+="/duplicate-implementation";
    for(auto& file:duplicate.project_files)
        if(file.first=="CMakeLists.txt")
            file.second=R"cmake(cmake_minimum_required(VERSION 3.20)
project(Pases LANGUAGES CXX)
add_library(regla STATIC regla.cpp)
add_executable(portero acceso.cpp regla.cpp)
target_compile_features(regla PRIVATE cxx_std_20)
target_compile_features(portero PRIVATE cxx_std_20)
set_target_properties(regla portero PROPERTIES CXX_EXTENSIONS OFF)
)cmake";
    const auto duplicate_result=run_cpp(duplicate);
    ran(duplicate_result);
    const auto duplicate_fresh=capture_cpp_execution(duplicate,duplicate_result);
    check(duplicate_result.execution.stdout_bytes=="1:1:0"&&
        evaluate_cpp_output_check(*passes_after.output_check,duplicate_fresh,
            duplicate.revision).verdict==curriculum::OutputVerdict::correct&&
        !duplicate_fresh.cmake_graph_correct&&
        !record_compiler_puzzle_case(passes_saved,mapped,passes_after,
            duplicate,duplicate_fresh,"Fuentes duplicadas"),
        "The identical stdout from a duplicate source earned a library-link receipt");
    const auto persisted=state::decode(state::encode(passes_saved));
    check(compiler_puzzle_completed(persisted,mapped,passes_after)&&
        compiler_puzzle_completed(persisted,mapped,after)&&
        compiler_puzzle_completed(persisted,original,before),
        "A failed alternative invalidated earlier achievements on reopening");
    std::cout<<"PASS both original U2L2 CMake editors: linked library, duplicate source rejected, preserved first-case and history\\n";
    // U2L1 starts with an empty CMakeLists.txt. Its already-published
    // inventoried source and first output can receive a separate, authentic
    // generated-graph requirement without replacing any historical exercise.
    constexpr auto inventory_relative=
        "app/src/main/assets/content/es/programming/programming_practice_tools/"
        "construccion_reproducible/lesson_fuentes_objetivos_y_fases.json";
    const auto inventory_original=lgaed::windows::load_lesson(
        std::filesystem::path(build::source_root)/inventory_relative);
    const auto& inventory_before=inventory_original.pages.at(9).blocks.at(3);
    check(inventory_before.stable_id=="block_0009_0003"&&
        inventory_before.cmake&&inventory_before.output_check&&
        !inventory_before.cmake_graph_check,
        "Original U2L1 inventory editor moved or already received a graph");
    std::string inventory_projected=inventory_original.raw_json;
    const auto inventory_at=inventory_projected.find(anchor);
    check(inventory_at!=inventory_projected.npos&&
        inventory_projected.find(anchor,inventory_at+1)==inventory_projected.npos,
        "Original U2L1 lesson changed its root stateLayout anchor");
    inventory_projected.insert(inventory_at,R"json(  "compilerCmakeGraphChecks": [
      {"activity":"block_0009_0003","project":"InventarioLocal",
       "minimumCmakeVersion":"3.20",
       "targets":[{"name":"comprobar","type":"EXECUTABLE",
                   "sources":["inventario.cpp"],"exactSources":true,
                   "explicitCxx20":true,"explicitCxxExtensionsOff":true}]}
    ],
)json");
    const auto inventory_mapped=lgaed::windows::parse_lesson_json(
        inventory_projected,"es");
    const auto inventory_metadata=lgaed::windows::parse_lesson_progress_metadata(
        inventory_projected,"es");
    const auto& inventory_after=inventory_mapped.pages.at(9).blocks.at(3);
    check(inventory_mapped.state_layout.lineage==
            inventory_original.state_layout.lineage&&
        inventory_mapped.state_layout.signatures==
            inventory_original.state_layout.signatures&&
        inventory_metadata.state_layout.lineage==
            inventory_original.state_layout.lineage&&
        inventory_after.cmake_graph_check&&
        inventory_after.cmake_graph_check==
            inventory_metadata.pages.at(9).blocks.at(3).cmake_graph_check&&
        inventory_after.project_files==inventory_before.project_files&&
        curriculum::exercise_key(inventory_mapped,inventory_after)==
            curriculum::exercise_key(inventory_original,inventory_before)&&
        curriculum::draft_key("programming",inventory_mapped,inventory_after)==
            curriculum::draft_key("programming",inventory_original,inventory_before)&&
        curriculum::reference_objective_frame(inventory_mapped,inventory_after)!=
            curriculum::reference_objective_frame(inventory_original,inventory_before),
        "U2L1 source, saved state, draft or graph provenance changed");
    CppRunRequest inventory;
    inventory.request_id=1450;inventory.revision=450;
    inventory.project_key="qa/p2-u2-first-inventory";
    inventory.projects_directory=environment.paths.projects;
    inventory.cmake=inventory_after.cmake;
    inventory.project_files=inventory_after.project_files;
    inventory.cmake_graph_check=inventory_after.cmake_graph_check;
    inventory.capture_cmake_graph=true;
    inventory.action=CppRunRequest::Action::compile_run;
    bool supplied_inventory_cmake=false;
    for(auto& file:inventory.project_files)
        if(file.first=="CMakeLists.txt") {
            check(file.second.empty(),"The original U2L1 editor is not empty");
            file.second=R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
add_executable(comprobar inventario.cpp)
target_compile_features(comprobar PRIVATE cxx_std_20)
set_target_properties(comprobar PROPERTIES CXX_EXTENSIONS OFF)
)cmake";
            supplied_inventory_cmake=true;
        }
    check(supplied_inventory_cmake&&
        compiler_runtime_request_allowed(inventory_after,inventory),
        "Correct U2L1 CMake source cannot enter the original student editor");
    const auto inventory_run=run_cpp(inventory);
    ran(inventory_run);
    const auto inventory_fresh=capture_cpp_execution(inventory,inventory_run);
    check(inventory_run.execution.stdout_bytes=="0:1:1"&&
        inventory_run.execution.stderr_bytes.empty()&&
        inventory_fresh.cmake_graph_correct&&
        evaluate_cpp_output_check(*inventory_after.output_check,inventory_fresh,
            inventory.revision).verdict==curriculum::OutputVerdict::correct,
        "Real U2L1 MSVC failed project name, compiled source or first output");
    // A project that omits either explicit requirement still compiles under
    // this runner's C++23 / extensions-OFF defaults. The generated program
    // must not be enough to unlock an authored C++20/extension exercise.
    struct InventoryVariant {
        std::string suffix,cmake_source;
        bool should_pass;
    };
    const std::vector<InventoryVariant> variants={
        {"defaults-only",R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
add_executable(comprobar inventario.cpp)
)cmake",false},
        {"feature-without-property",R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
add_executable(comprobar inventario.cpp)
target_compile_features(comprobar PRIVATE cxx_std_20)
)cmake",false},
        {"property-without-feature",R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
add_executable(comprobar inventario.cpp)
set_target_properties(comprobar PROPERTIES CXX_EXTENSIONS OFF)
)cmake",false},
        {"property-overwritten",R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
add_executable(comprobar inventario.cpp)
target_compile_features(comprobar PRIVATE cxx_std_20)
set_target_properties(comprobar PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(comprobar PROPERTIES CXX_EXTENSIONS ON)
)cmake",false},
        {"equivalent-target-properties",R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
add_executable(comprobar inventario.cpp)
set_target_properties(comprobar PROPERTIES CXX_STANDARD 20)
set_property(TARGET comprobar PROPERTY CXX_EXTENSIONS OFF)
)cmake",true},
        {"expanded-target-feature",R"cmake(cmake_minimum_required(VERSION 3.20)
project(InventarioLocal LANGUAGES CXX)
set(elegido comprobar)
add_executable(comprobar inventario.cpp)
target_compile_features(${elegido} PRIVATE cxx_std_20)
set_property(TARGET ${elegido} PROPERTY CXX_EXTENSIONS OFF)
)cmake",true}
    };
    std::uint64_t variant_id=1460;
    for(const auto& variant:variants) {
        auto candidate=inventory;
        candidate.request_id=variant_id;
        candidate.revision=variant_id++;
        candidate.project_key+="/"+variant.suffix;
        bool changed=false;
        for(auto& file:candidate.project_files)
            if(file.first=="CMakeLists.txt") {
                file.second=variant.cmake_source;
                changed=true;
            }
        check(changed,"The original U2L1 CMake file vanished");
        const auto output=run_cpp(candidate);
        ran(output);
        const auto observed=capture_cpp_execution(candidate,output);
        check(output.execution.stdout_bytes=="0:1:1"&&
            output.execution.stderr_bytes.empty()&&
            evaluate_cpp_output_check(*inventory_after.output_check,observed,
                candidate.revision).verdict==curriculum::OutputVerdict::correct&&
            observed.cmake_graph_correct==variant.should_pass,
            "Authored CMake language rules earned wrong first-case evidence: "+
                variant.suffix);
    }
    // A previous valid stdout-only achievement remains historical. It must
    // not silently complete the newly authenticated construction goal.
    auto inventory_legacy=inventory;
    inventory_legacy.capture_cmake_graph=false;
    inventory_legacy.cmake_graph_check.reset();
    const auto inventory_old_capture=capture_cpp_execution(
        inventory_legacy,inventory_run);
    state::StateData inventory_saved;
    state::prepare_lesson_state(inventory_saved,inventory_original);
    check(record_compiler_puzzle_case(inventory_saved,inventory_original,
            inventory_before,inventory_legacy,inventory_old_capture,
            "Salida previa")&&
        compiler_puzzle_completed(inventory_saved,inventory_original,
            inventory_before)&&
        !compiler_puzzle_completed(inventory_saved,inventory_mapped,
            inventory_after),
        "U2L1 graph incorrectly inherited an old stdout-only achievement");
    state::prepare_lesson_state(inventory_saved,inventory_mapped);
    check(record_compiler_puzzle_case(inventory_saved,inventory_mapped,
            inventory_after,inventory,inventory_fresh,
            "Fuente y proyecto")&&
        compiler_puzzle_completed(inventory_saved,inventory_mapped,
            inventory_after)&&
        compiler_puzzle_case_count(inventory_saved,inventory_mapped,
            inventory_after)==1,
        "Correct first U2L1 case did not earn its new construction objective");
    const auto inventory_reopened=state::decode(state::encode(inventory_saved));
    check(compiler_puzzle_completed(inventory_reopened,inventory_original,
            inventory_before)&&
        compiler_puzzle_completed(inventory_reopened,inventory_mapped,
            inventory_after),
        "Reopening the U2L1 achievement lost its old or new provenance");
    // The same executable output from a different CMake project is not proof
    // of the original project name, even when source and target are identical.
    auto inventory_impostor=inventory;
    inventory_impostor.request_id=1451;inventory_impostor.revision=451;
    inventory_impostor.project_key+="/wrong-project";
    for(auto& file:inventory_impostor.project_files)
        if(file.first=="CMakeLists.txt") {
            constexpr auto correct_name="project(InventarioLocal LANGUAGES CXX)";
            const auto at=file.second.find(correct_name);
            check(at!=file.second.npos,"The U2L1 CMake project name moved");
            file.second.replace(at,std::char_traits<char>::length(correct_name),
                "project(ProyectoAjeno LANGUAGES CXX)");
        }
    const auto impostor_run=run_cpp(inventory_impostor);
    ran(impostor_run);
    const auto impostor_fresh=capture_cpp_execution(inventory_impostor,
        impostor_run);
    check(impostor_run.execution.stdout_bytes==
            inventory_run.execution.stdout_bytes&&
        evaluate_cpp_output_check(*inventory_after.output_check,impostor_fresh,
            inventory_impostor.revision).verdict==
                curriculum::OutputVerdict::correct&&
        !impostor_fresh.cmake_graph_correct&&
        !record_compiler_puzzle_case(inventory_saved,inventory_mapped,
            inventory_after,inventory_impostor,impostor_fresh,
            "Proyecto ajeno"),
        "U2L1 accepted matching stdout from the wrong CMake project");
    // CMake's generated top-level directory records the minimum actually
    // declared by the project, not the newer CMake executable in the runner.
    auto inventory_old_min=inventory;
    inventory_old_min.request_id=1452;inventory_old_min.revision=452;
    inventory_old_min.project_key+="/older-minimum";
    for(auto& file:inventory_old_min.project_files)
        if(file.first=="CMakeLists.txt") {
            constexpr auto original_version="cmake_minimum_required(VERSION 3.20)";
            const auto at=file.second.find(original_version);
            check(at!=file.second.npos,"The original inventory minimum moved");
            file.second.replace(at,std::char_traits<char>::length(original_version),
                "cmake_minimum_required(VERSION 3.19)");
        }
    const auto old_min_run=run_cpp(inventory_old_min);
    ran(old_min_run);
    const auto old_min_fresh=capture_cpp_execution(inventory_old_min,old_min_run);
    check(old_min_run.execution.stdout_bytes=="0:1:1"&&
        old_min_run.cmake_graph&&
        old_min_run.cmake_graph->minimum_cmake_version=="3.19"&&
        evaluate_cpp_output_check(*inventory_after.output_check,old_min_fresh,
            inventory_old_min.revision).verdict==curriculum::OutputVerdict::correct&&
        !old_min_fresh.cmake_graph_correct&&
        !record_compiler_puzzle_case(inventory_saved,inventory_mapped,
            inventory_after,inventory_old_min,old_min_fresh,
            "Requisito mínimo alterado"),
        "U2L1 accepted a different CMake minimum with matching stdout");
    check(curriculum::cmake_numeric_version("3.20")==
        curriculum::cmake_numeric_version("3.20.0")&&
        curriculum::cmake_numeric_version("3.20")!=
        curriculum::cmake_numeric_version("3.21"),
        "Equivalent CMake version spelling or ordering changed");
    std::cout<<"PASS original U2L1 CMake: real source, project, exact minimum, first-case history, wrong project/minimum refused\\n";
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
