#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "core/open_battery_check.hpp"
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
#include <cstdint>
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
using winrt::Windows::Data::Json::JsonValue;
constexpr std::string_view activity="block_0006_0011";
constexpr std::string_view previous_activity="block_0006_0008";
constexpr std::string_view test_path="pruebas/casos.cpp";
constexpr std::string_view declaration=R"json({
  "activity":"block_0006_0011",
  "library":"src/ajuste.cpp","test":"pruebas/casos.cpp",
  "function":"intentar_ajuste","comparison":"comprobar","target":"comprobar_ajustes",
  "originalCases":[
    {"value":2,"lower":3,"upper":7,"initial":77},
    {"value":5,"lower":3,"upper":7,"initial":-8},
    {"value":9,"lower":3,"upper":7,"initial":19},
    {"value":4,"lower":4,"upper":4,"initial":-4},
    {"value":-20,"lower":20,"upper":20,"initial":8},
    {"value":20,"lower":-20,"upper":-20,"initial":17},
    {"value":0,"lower":7,"upper":3,"initial":77},
    {"value":21,"lower":3,"upper":7,"initial":-8},
    {"value":-21,"lower":3,"upper":7,"initial":19},
    {"value":0,"lower":-21,"upper":7,"initial":-4},
    {"value":0,"lower":3,"upper":21,"initial":8}
  ]
})json";

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing authentic open battery fixture or artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const Block& owner(const Lesson& lesson,std::string_view id=activity) {
    const Block* result=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.stable_id==id) {
            require(!result,"Duplicated mapped U3L10 open battery owner");
            result=&block;
        }
    require(result!=nullptr,"Missing mapped U3L10 open battery owner");
    return *result;
}
std::string& file(CppProjectFiles& files,std::string_view path) {
    const auto found=std::ranges::find_if(files,[&](const auto& item){return item.first==path;});
    require(found!=files.end(),"Missing authentic open battery project file");
    return found->second;
}
const std::string& file(const CppProjectFiles& files,std::string_view path) {
    const auto found=std::ranges::find_if(files,[&](const auto& item){return item.first==path;});
    require(found!=files.end(),"Missing authentic open battery project file");
    return found->second;
}
void replace_once(std::string& source,std::string_view from,std::string_view to) {
    const auto at=source.find(from);
    require(at!=source.npos&&source.find(from,at+from.size())==source.npos,
        "The authentic open battery fixture changed");
    source.replace(at,from.size(),to);
}
bool same_json(const winrt::Windows::Data::Json::IJsonValue& left,
        const winrt::Windows::Data::Json::IJsonValue& right) {
    using winrt::Windows::Data::Json::JsonValueType;
    if(left.ValueType()!=right.ValueType())return false;
    switch(left.ValueType()) {
    case JsonValueType::String:return left.GetString()==right.GetString();
    case JsonValueType::Number:return left.GetNumber()==right.GetNumber();
    case JsonValueType::Boolean:return left.GetBoolean()==right.GetBoolean();
    case JsonValueType::Null:return true;
    case JsonValueType::Array: {
        const auto a=left.GetArray(),b=right.GetArray();
        if(a.Size()!=b.Size())return false;
        for(std::uint32_t i=0;i<a.Size();++i)
            if(!same_json(a.GetAt(i),b.GetAt(i)))return false;
        return true;
    }
    case JsonValueType::Object: {
        const auto a=left.GetObject(),b=right.GetObject();
        if(a.Size()!=b.Size())return false;
        for(const auto& entry:a)
            if(!b.HasKey(entry.Key())||!same_json(entry.Value(),b.GetNamedValue(entry.Key())))
                return false;
        return true;
    }
    }
    return false;
}
std::string portable_warning_fixture(std::string source) {
    // This QA-only projection changes the three warning-option statements.
    // It does not certify an editorial CMake migration and never writes the
    // published asset. All component, interface, client and test bytes remain
    // authentic; both branches retain their original target relationships.
    if(source.find("if(MSVC)")!=source.npos)return source;
    for(const auto target:{"ajustes","consultar_ajustes","comprobar_ajustes"}) {
        const auto old="target_compile_options("+std::string(target)+
            " PRIVATE -Wall -Wextra -Wpedantic)\n";
        const auto replacement="if(MSVC)\n  target_compile_options("+
            std::string(target)+" PRIVATE /W4)\nelse()\n  "+old+"endif()\n";
        replace_once(source,old,replacement);
    }
    return source;
}
std::string extended_driver(const Block& block,std::string_view call) {
    auto source=file(block.project_files,test_path);
    const std::string summary="    std::cout << \"Fallos=\" << fallos << \"\\n\";";
    replace_once(source,summary,std::string(call)+summary);
    return source;
}
void public_success(const CppRunResult& result) {
    require(result.error_utf8.empty(),"Runner error: "+result.error_utf8);
    require(result.compilation.succeeded(),"Real CMake compilation failed: "+
        result.compilation.stdout_bytes+result.compilation.stderr_bytes);
    require(result.execution.succeeded(),"The actual checker did not finish with exit 0");
    require(result.executable==std::optional<std::string>{"comprobar_ajustes"},
        "The wrong executable supplied the public observation");
}
void preserved_public_project(const CppRunRequest& request,const CppRunResult& result) {
    const auto available=inspect_cpp_project(request);
    require(available.built&&available.artifact==result.artifact,
        "Private probes replaced the public target or invalidated its build");
    for(const auto& [path,source]:request.project_files)
        require(read(available.workspace/L"p"/
                    std::filesystem::path(winrt::to_hstring(path).c_str()))==source,
            "A private source replaced a submitted public file");
    require(read(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
            read(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
        "Private streams replaced the public execution logs");
}
void checked_battery(const CppRunRequest& request,const CppRunResult& result,
        const FreshExecution& capture,const Block& block,OpenBatteryCall added,bool accepted) {
    public_success(result);
    require(result.open_battery_evidence.has_value(),"Missing actual independent CMake phases");
    const auto& evidence=*result.open_battery_evidence;
    const auto& spec=*block.open_battery_probe;
    const auto plan=open_battery_mutation_plan(spec.original_cases,added);
    require(plan&&plan->size()==25&&evidence.runs.size()==plan->size(),
        "The real eleven-case extension did not execute all 25 independent phases");
    auto calls=spec.original_cases;
    calls.push_back(added);
    for(std::size_t i=0;i<plan->size();++i) {
        const auto& observed=evidence.runs[i];
        require(observed.compiled&&observed.executed&&
                observed.program_frame==evidence.original_project_fingerprint&&
                observed.mutation==(*plan)[i].mutation&&observed.target==(*plan)[i].target&&
                observed.calls==calls,
            "A real phase omitted an original/new call or used another mutation");
        if(accepted)require(observed.exit_code==(i==0?0:1),
            "A passing checker failed to report an independently introduced defect");
    }
    require(evidence.runs.front().stdout_bytes==result.execution.stdout_bytes&&
            evidence.runs.front().stderr_bytes==result.execution.stderr_bytes&&
            capture.standard_output==result.execution.stdout_bytes&&
            capture.standard_error==result.execution.stderr_bytes,
        "The private baseline or captured feedback differed from the public checker");
    require(capture.open_battery_probe_correct==accepted&&
            evaluate_cpp_output_check(*block.output_check,capture,request.revision).verdict==
                (accepted?OutputVerdict::correct:OutputVerdict::incorrect),
        "The observed mutation result disagreed with the first-case objective");
    preserved_public_project(request,result);
}
void completion(const state::StateData& data,const Lesson& lesson,bool expected) {
    const auto& block=owner(lesson);
    const auto solved=[&](const Block& candidate) {
        return compiler_puzzle_completed(data,lesson,candidate);
    };
    require(compiler_requires_completion(block)&&
            compiler_completion_snapshot(lesson,solved).at(block.stable_id)==expected,
        "The open battery lost its mandatory completion gate");
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const LaunchOptions options{ProfileId::Codex,
            L"open-battery-action-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        const auto projects=environment.paths.projects/L"Open battery & (U3L10)";
        const auto fixture_path=std::filesystem::path(argv[1]);
        const auto original_text=read(fixture_path);
        const auto original_sha=state::sha256(original_text);
        const auto original=parse_lesson_json(original_text,"es");
        const auto& before=owner(original);
        require(before.standard_input&&!before.standard_input->empty()&&
                before.project_files.size()==5&&before.cmake&&
                before.cmake->target=="comprobar_ajustes",
            "The authentic initial project or historical client batch changed");
        const auto position=stable_block_position(original,activity);
        require(position.has_value(),"The authentic owner lost its stable map address");
        auto document=JsonObject::Parse(winrt::to_hstring(original_text));
        const auto original_document=JsonObject::Parse(winrt::to_hstring(original_text));
        auto projected_files=before.project_files;
        file(projected_files,"CMakeLists.txt")=
            portable_warning_fixture(file(before.project_files,"CMakeLists.txt"));
        const auto raw_owner=document.GetNamedArray(L"pages").
            GetObjectAt(static_cast<std::uint32_t>(position->page)).
            GetNamedArray(L"blocks").GetObjectAt(static_cast<std::uint32_t>(position->block));
        for(const auto& value:raw_owner.GetNamedArray(L"projectFiles")) {
            const auto item=value.GetObject();
            if(item.GetNamedString(L"path")==L"CMakeLists.txt")
                item.Insert(L"initialCode",JsonValue::CreateStringValue(
                    winrt::to_hstring(file(projected_files,"CMakeLists.txt"))));
        }
        auto declarations=document.HasKey(L"compilerOpenBatteryChecks")
            ?document.GetNamedArray(L"compilerOpenBatteryChecks"):JsonArray{};
        const auto desired=JsonObject::Parse(winrt::to_hstring(declaration));
        bool present=false;
        for(const auto& existing:declarations)
            if(existing.GetObject().GetNamedString(L"activity")==winrt::to_hstring(activity)) {
                require(!present&&same_json(existing,desired),
                    "An existing open battery declaration has an incompatible contract");
                present=true;
            }
        if(!present)declarations.Append(desired);
        document.Insert(L"compilerOpenBatteryChecks",declarations);
        if(original_document.HasKey(L"compilerOpenBatteryChecks")) {
            const auto previous=original_document.GetNamedArray(L"compilerOpenBatteryChecks");
            require(declarations.Size()>=previous.Size(),"Existing root declarations disappeared");
            for(std::uint32_t i=0;i<previous.Size();++i)
                require(same_json(previous.GetAt(i),declarations.GetAt(i)),
                    "The QA addition changed another existing root declaration");
        }
        const auto reviewed=winrt::to_string(document.Stringify());
        const auto lesson=parse_lesson_json(reviewed,"es");
        const auto progress=parse_lesson_progress_metadata(reviewed,"es");
        const auto& block=owner(lesson);
        const auto& restored=owner(progress);
        require(original.pages.size()==lesson.pages.size()&&
                lesson.pages.size()==progress.pages.size()&&
                original.state_layout.raw_json==lesson.state_layout.raw_json&&
                original.state_layout.raw_json==progress.state_layout.raw_json&&
                original.state_layout.signatures==lesson.state_layout.signatures&&
                original.state_layout.signatures==progress.state_layout.signatures&&
                original.state_layout.lineage==lesson.state_layout.lineage&&
                original.state_layout.lineage==progress.state_layout.lineage,
            "The QA root goal changed the original stable state layout");
        require(block.open_battery_probe&&restored.open_battery_probe&&
                block.open_battery_probe==restored.open_battery_probe&&
                block.open_battery_probe->original_cases.size()==11&&
                block.output_check&&restored.output_check&&
                block.output_check->open_battery_check&&restored.output_check->open_battery_check&&
                block.output_check->cases.size()==1&&restored.output_check->cases.size()==1&&
                block.output_check->input_comparison==TextComparison::exact&&
                restored.output_check->input_comparison==TextComparison::exact&&
                block.output_check->cases.front().standard_input.empty()&&
                restored.output_check->cases.front().standard_input.empty()&&
                block.output_check->cases.front().executable==
                    std::optional<std::string>{block.open_battery_probe->test_target}&&
                restored.output_check->cases.front().executable==
                    block.output_check->cases.front().executable,
            "Full/progress loading did not require the instructed empty checker input");
        require(block.project_files==projected_files&&restored.project_files==projected_files&&
                block.initial_code==before.initial_code&&restored.initial_code==before.initial_code&&
                block.standard_input==before.standard_input&&restored.standard_input==before.standard_input&&
                block.cmake==before.cmake&&restored.cmake==before.cmake&&
                block.stable_id==before.stable_id&&restored.stable_id==before.stable_id&&
                block.address.page==before.address.page&&block.address.block==before.address.block&&
                restored.address.page==before.address.page&&restored.address.block==before.address.block&&
                exercise_key(original,before)==exercise_key(lesson,block)&&
                draft_key("programming",original,before)==draft_key("programming",lesson,block)&&
                reference_objective_frame(lesson,block)==reference_objective_frame(progress,restored),
            "The new goal changed source, initial input, target, identity or full/progress evidence");
        auto former_input_contract=block;
        former_input_contract.output_check->cases.front().standard_input=*before.standard_input;
        require(reference_objective_frame(lesson,former_input_contract)!=
                reference_objective_frame(lesson,block),
            "The corrected input reused the former objective's evidence identity");
        require(read(fixture_path)==original_text&&state::sha256(read(fixture_path))==original_sha,
            "Preparing the QA fixture rewrote the published source or SHA");

        state::StateData data;
        data.preferences["qa"]["keep"]=std::string("unrelated saved state");
        state::prepare_lesson_state(data,original);
        const auto draft_identity=draft_key("programming",lesson,block);
        JsonObject project_draft;
        JsonArray draft_files;
        for(const auto& [path,source]:before.project_files) {
            JsonObject item;
            item.Insert(L"path",JsonValue::CreateStringValue(winrt::to_hstring(path)));
            item.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(source)));
            draft_files.Append(item);
        }
        project_draft.Insert(L"selected",JsonValue::CreateStringValue(winrt::to_hstring(test_path)));
        project_draft.Insert(L"files",draft_files);
        project_draft.Insert(L"sources",JsonArray{});
        project_draft.Insert(L"indentationV2",JsonObject{});
        project_draft.Insert(L"cursors",JsonObject{});
        const auto saved_project=winrt::to_string(project_draft.Stringify());
        data.preferences["cpp_lesson_drafts"][draft_identity+"/project"]=saved_project;
        state::Draft saved_draft;
        saved_draft.source=before.initial_code;
        saved_draft.input=*before.standard_input;
        saved_draft.selection_start=3;
        saved_draft.selection_end=9;
        saved_draft.has_saved=true;
        state::put_draft(data,draft_identity,saved_draft);
        state::prepare_lesson_state(data,lesson);
        require(state::string_value(data,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
                state::get_draft(data,draft_identity,before.initial_code).input==*before.standard_input,
            "Adding the root goal replaced the historical project or prepared input draft");
        completion(data,lesson,false);
        completion(data,progress,false);
        std::cout << "PASS authentic owner, full/progress empty-input goal, source maps and historical drafts\n";

        std::uint64_t sequence=0;
        const auto request=[&](std::string_view driver,CppRunRequest::Action action) {
            CppRunRequest value;
            value.request_id=++sequence;
            value.revision=900+sequence;
            value.source_utf8=block.initial_code;
            value.projects_directory=projects;
            value.project_key=draft_identity;
            value.project_files=block.project_files;
            file(value.project_files,test_path)=driver;
            value.source_files=block.source_files;
            value.cmake=block.cmake;
            value.open_battery_probe=block.open_battery_probe;
            value.stdin_utf8="";
            value.action=action;
            value.compile_timeout=90s;
            value.execution_timeout=5s;
            return value;
        };
        const auto phase=[&](CppRunRequest& value,CppRunRequest::Action action) {
            value.request_id=++sequence;
            value.action=action;
            return run_cpp(value);
        };
        auto initial=request(file(block.project_files,test_path),CppRunRequest::Action::compile_run);
        // Keep activityAfter intact. The preceding editor asks for this
        // repaired library and the same eleven comparisons, without an open
        // probe. Earn its prerequisite through its own real request first.
        const auto& previous=owner(lesson,previous_activity);
        require(previous.cmake==block.cmake&&previous.output_check&&
                previous.output_check->cases.size()==1&&
                previous.output_check->cases.front().standard_input.empty()&&
                !previous.open_battery_probe&&previous.source_files==block.source_files,
            "The preceding authentic repair objective changed");
        auto previous_files=previous.project_files;
        auto repaired_library=file(previous_files,"src/ajuste.cpp");
        replace_once(repaired_library,"ajuste = valor - superior;","ajuste = superior - valor;");
        file(previous_files,"src/ajuste.cpp")=std::move(repaired_library);
        file(previous_files,"CMakeLists.txt")=
            portable_warning_fixture(file(previous.project_files,"CMakeLists.txt"));
        require(previous_files==initial.project_files,
            "The extension's starting project no longer supplies the preceding legitimate repair");
        auto prerequisite=initial;
        prerequisite.request_id=++sequence;
        prerequisite.revision=900+sequence;
        prerequisite.project_key=draft_key("programming",lesson,previous);
        prerequisite.source_utf8=previous.initial_code;
        prerequisite.open_battery_probe.reset();
        require(compiler_runtime_request_allowed(previous,prerequisite),
            "The preceding editor rejected its legitimate repaired candidate");
        const auto prerequisite_result=run_cpp(prerequisite);
        public_success(prerequisite_result);
        const auto prerequisite_capture=capture_cpp_execution(prerequisite,prerequisite_result);
        require(!prerequisite_result.open_battery_evidence&&
                evaluate_cpp_output_check(*previous.output_check,prerequisite_capture,
                    prerequisite.revision).verdict==OutputVerdict::correct&&
                record_compiler_puzzle_case(data,lesson,previous,prerequisite,
                    prerequisite_capture,"repaired eleven-case checker")&&
                compiler_puzzle_completed(data,progress,owner(progress,previous_activity)),
            "The preceding repair did not earn its own authentic first-case receipt");
        preserved_public_project(prerequisite,prerequisite_result);
        completion(data,lesson,false);
        const auto seed=data;
        std::cout << "PASS real repaired eleven-case prerequisite without altering activityAfter\n";

        const auto initial_result=run_cpp(initial);
        public_success(initial_result);
        const auto initial_capture=capture_cpp_execution(initial,initial_result);
        require(!initial_result.open_battery_evidence&&!initial_capture.open_battery_probe_correct&&
                initial_result.execution.stdout_bytes.find("Fallos=0")!=std::string::npos&&
                evaluate_cpp_output_check(*block.output_check,initial_capture,initial.revision).verdict==
                    OutputVerdict::incorrect&&
                !record_compiler_puzzle_case(data,lesson,block,initial,initial_capture,"initial")&&
                data==seed,
            "The unchanged eleven-case program earned extension credit");
        preserved_public_project(initial,initial_result);
        std::cout << "PASS actual unchanged eleven-case checker runs successfully without earning extension credit\n";

        constexpr OpenBatteryCall first_added{3,3,7,59};
        const auto first_source=extended_driver(block,
            "    if (!comprobar(12, 3, 3, 7, 59, true, 0)) { ++fallos; }\n");
        auto first=request(first_source,CppRunRequest::Action::execute);
        require(compiler_runtime_request_allowed(block,first),
            "The instructed execute action was rejected before running the built checker");
        const auto configured=phase(first,CppRunRequest::Action::configure);
        require(configured.error_utf8.empty()&&configured.configuration.succeeded()&&
                configured.execution.status==CppPhaseStatus::not_started&&
                !configured.open_battery_evidence&&!compiler_runtime_request_allowed(block,first)&&
                !record_compiler_puzzle_case(data,lesson,block,first,
                    capture_cpp_execution(first,configured),"configure")&&data==seed,
            "Configuring claimed runtime credit or failed to generate the real project");
        const auto built=phase(first,CppRunRequest::Action::build);
        require(built.error_utf8.empty()&&built.compilation.succeeded()&&
                built.execution.status==CppPhaseStatus::not_started&&
                !built.open_battery_evidence&&!compiler_runtime_request_allowed(block,first)&&
                !record_compiler_puzzle_case(data,lesson,block,first,
                    capture_cpp_execution(first,built),"build")&&data==seed&&
                inspect_cpp_project(first).built,
            "Building claimed runtime credit or failed to leave the executable ready");
        const auto first_result=phase(first,CppRunRequest::Action::execute);
        require(compiler_runtime_request_allowed(block,first),
            "Executing the configured and built target still failed its curricular gate");
        const auto first_capture=capture_cpp_execution(first,first_result);
        checked_battery(first,first_result,first_capture,block,first_added,true);
        require(record_compiler_puzzle_case(data,lesson,block,first,first_capture,"checked extension"),
            "The real configure/build/execute sequence did not earn a sealed receipt");
        completion(data,lesson,true);
        const auto earned=data;
        std::cout << "PASS actual configure build execute, 25 private phases and first-case completion\n";

        auto old_input=first;
        old_input.request_id=++sequence;
        old_input.stdin_utf8=*before.standard_input;
        require(!compiler_runtime_request_allowed(block,old_input),
            "The historical client input was accepted by the checker request gate");
        const auto old_input_result=run_cpp(old_input);
        public_success(old_input_result);
        const auto old_input_capture=capture_cpp_execution(old_input,old_input_result);
        require(!old_input_result.open_battery_evidence&&!old_input_capture.open_battery_probe_correct&&
                old_input_result.execution.stdout_bytes==first_result.execution.stdout_bytes&&
                old_input_result.execution.stderr_bytes==first_result.execution.stderr_bytes&&
                evaluate_cpp_output_check(*block.output_check,old_input_capture,old_input.revision).verdict==
                    OutputVerdict::unavailable&&
                !record_compiler_puzzle_case(data,lesson,block,old_input,old_input_capture,"old input")&&
                data==earned,
            "The obsolete input ran private probes, earned credit or erased the existing receipt");
        std::cout << "PASS historical client input executes without private probes or checker credit\n";

        constexpr OpenBatteryCall second_added{0,-3,-4,-29};
        const auto second_source=extended_driver(block,
            "    if (!comprobar(12, 0, -3, -4, -29, false, -29)) { ++fallos; }\n");
        auto second=request(second_source,CppRunRequest::Action::compile_run);
        require(compiler_runtime_request_allowed(block,second),
            "The existing compile_run API was rejected");
        const auto second_result=run_cpp(second);
        const auto second_capture=capture_cpp_execution(second,second_result);
        checked_battery(second,second_result,second_capture,block,second_added,true);
        auto second_data=seed;
        require(record_compiler_puzzle_case(second_data,lesson,block,second,second_capture,"second extension")&&
                compiler_program_frame(first)!=compiler_program_frame(second)&&second_data!=earned,
            "A different valid ternary case did not earn its own source-bound receipt");
        completion(second_data,progress,true);
        std::cout << "PASS independent rejected-input extension through the existing compile_run API\n";

        auto fixed_source=first_source;
        replace_once(fixed_source,"return fallos == 0 ? 0 : 1;","return 0;");
        auto fixed=request(fixed_source,CppRunRequest::Action::compile_run);
        const auto fixed_result=run_cpp(fixed);
        const auto fixed_capture=capture_cpp_execution(fixed,fixed_result);
        checked_battery(fixed,fixed_result,fixed_capture,block,first_added,false);
        require(std::ranges::all_of(fixed_result.open_battery_evidence->runs,
                    [](const auto& observed){return observed.exit_code==0;})&&
                !record_compiler_puzzle_case(data,lesson,block,fixed,fixed_capture,"fixed return")&&
                data==earned,
            "A fixed successful return credited an insensitive battery or erased prior success");
        std::cout << "PASS actual fixed-return checker is rejected while its earlier achievement survives\n";

        state::StateStore storage(environment.paths.root);
        (void)storage.load();
        storage.save(data);
        state::StateStore reopened_storage(environment.paths.root);
        const auto reopened=reopened_storage.load();
        require(reopened==earned,"Physical reopening changed the sealed receipt or saved drafts");
        completion(reopened,progress,true);
        validate_compiler_puzzle_state(reopened,progress,restored);
        state::validate_lesson_state(reopened,progress);
        auto tampered=reopened;
        const auto key="compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
            exercise_key(lesson,block)+"/"+
            state::sha256(reference_objective_frame(lesson,block))+"/";
        tampered.preferences["native_windows"][key+"cases"]=std::vector<std::string>{"unverified"};
        completion(tampered,progress,false);
        auto reset=reopened;
        reset_compiler_puzzle(reset,lesson,block);
        completion(reset,progress,false);
        require(compiler_puzzle_completed(reset,progress,owner(progress,previous_activity))&&
                state::string_value(reset,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
                state::get_draft(reset,draft_identity,before.initial_code).source==before.initial_code&&
                state::get_draft(reset,draft_identity,before.initial_code).input==*before.standard_input&&
                state::string_value(reset,"qa","keep")=="unrelated saved state",
            "Explicit reset removed a project, initial input or unrelated saved state");
        const auto after=read(fixture_path);
        require(after==original_text&&state::sha256(after)==original_sha,
            "The runtime test rewrote the authentic curricular source or SHA");
        std::cout << "PASS physical reopening, tamper rejection, isolated reset and unchanged published source SHA\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr << "FAIL open battery action runtime: " << error.what() << '\n';
        return 1;
    }catch(const winrt::hresult_error& error) {
        std::cerr << "FAIL open battery action runtime: " << winrt::to_string(error.message()) << '\n';
        return 1;
    }
}
