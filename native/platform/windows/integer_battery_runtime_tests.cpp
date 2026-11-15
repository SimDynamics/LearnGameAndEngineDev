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
using winrt::Windows::Data::Json::JsonValue;
void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing authentic integer battery fixture or artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const Block& owner(const Lesson& lesson,std::string_view id) {
    const Block* result=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.stable_id==id) {
            require(!result,"Duplicated integer battery owner");
            result=&block;
        }
    require(result!=nullptr,"Missing integer battery owner");
    return *result;
}
std::string& file(CppProjectFiles& files,std::string_view path) {
    const auto found=std::ranges::find_if(files,[&](const auto& item){return item.first==path;});
    require(found!=files.end(),"Missing authentic integer project file");
    return found->second;
}
const std::string& file(const CppProjectFiles& files,std::string_view path) {
    const auto found=std::ranges::find_if(files,[&](const auto& item){return item.first==path;});
    require(found!=files.end(),"Missing authentic integer project file");
    return found->second;
}
void replace_once(std::string& source,std::string_view from,std::string_view to) {
    const auto at=source.find(from);
    require(at!=source.npos,"Authentic integer fixture changed");
    source.replace(at,from.size(),to);
}
constexpr std::string_view first_id="block_0004_0003";
constexpr std::string_view second_id="block_0010_0003";
constexpr std::string_view first_reference="block_0005_0003";
constexpr std::string_view second_reference="block_0011_0002";
constexpr std::string_view declaration=R"json([
 {"activity":"block_0004_0003","kind":"cmake-int-unary-v1",
  "libraryPath":"src/cajas.cpp","function":"cajas_para",
  "testPath":"pruebas/cajas_casos.cpp","target":"comprobar_cajas",
  "publishedResult":"failure","failureExitCode":"nonzero",
  "cases":[{"input":0,"expected":0},{"input":4,"expected":1},
           {"input":8,"expected":2},{"input":20,"expected":5},
           {"input":1,"expected":1},{"input":5,"expected":2},{"input":17,"expected":5}],
  "prerequisite":{"target":"consultar_cajas",
    "calls":[{"input":4,"result":1},{"input":8,"result":2}],
    "standardInput":"","standardOutput":"1:2","standardError":"","exitCode":0}},
 {"activity":"block_0010_0003","kind":"cmake-int-unary-v1",
  "libraryPath":"motor/cantidad.cpp","function":"lotes_para",
  "testPath":"pruebas/lotes_casos.cpp","target":"verificar_lotes",
  "publishedResult":"success","failureExitCode":1,
  "cases":[{"input":0,"expected":0},{"input":6,"expected":1},
           {"input":7,"expected":2},{"input":18,"expected":3}]}])json";


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

void rejected_loaders(std::string_view source) {
    for(const bool progress:{false,true}) {
        bool rejected=false;
        try {
            if(progress)(void)parse_lesson_progress_metadata(source,"es");
            else (void)parse_lesson_json(source,"es");
        }catch(const std::exception&){rejected=true;}
        require(rejected,"Invalid integer declaration survived a full/progress loader");
    }
}
void public_execution(const CppRunResult& result) {
    require(result.error_utf8.empty(),"Runner error: "+result.error_utf8);
    require(result.compilation.succeeded(),"Candidate compilation failed: "+
        result.compilation.stdout_bytes+result.compilation.stderr_bytes);
    require(result.execution.status==CppPhaseStatus::completed&&result.execution.exit_code,
        "Candidate did not complete an actual native execution");
}
void check_restored(const CppRunRequest& request,const CppRunResult& result) {
    const auto availability=inspect_cpp_project(request);
    require(availability.built&&availability.artifact==result.artifact,
        "Private instrumentation damaged the public CMake build or artifact digest");
    for(const auto& [path,source]:request.project_files)
        require(read(availability.workspace/L"p"/std::filesystem::path(winrt::to_hstring(path).c_str()))==source,
            "A private source remained in the public CMake workspace");
    require(read(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
            read(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "Private output replaced the public program logs");
    for(const auto& entry:std::filesystem::directory_iterator(result.run_directory)) {
        const auto name=entry.path().filename().string();
        require(name.find("private")==name.npos&&name.find("mutation")==name.npos,
            "A phase-labelled private artifact remained visible");
    }
}
void checked_run(const CppRunRequest& request,const CppRunResult& result,
        const FreshExecution& execution,const Block& block,bool expected=true) {
    public_execution(result);
    require(result.integer_battery_evidence.has_value(),"Missing independent integer evidence");
    const auto& evidence=*result.integer_battery_evidence;
    const auto& spec=*request.integer_battery_probe;
    const auto plan=integer_battery_plan(spec);
    require(plan&&plan->size()==spec.cases.size()+2&&evidence.runs.size()==plan->size(),
        "The control, every single-case mutation and the published phase did not all run");
    for(std::size_t index=0;index<plan->size();++index) {
        const auto& run=evidence.runs[index];
        require(run.compiled&&run.executed&&run.completed_normally&&run.phase==(*plan)[index],
            "A private integer phase failed to compile, execute or return normally");
    }
    require(execution.execution_completed&&
            execution.integer_battery_request_fingerprint==
                cpp_integer_battery_request_fingerprint(request)&&
            execution.integer_battery_probe_correct==expected,
            "Unexpected integer verdict: "+execution.integer_battery_probe_feedback);
    require(evaluate_cpp_output_check(*block.output_check,execution,request.revision).verdict==
            (expected?OutputVerdict::correct:OutputVerdict::incorrect),
            "The independent integer verdict disagreed with its output objective");
    require(execution.standard_output==result.execution.stdout_bytes&&
            execution.standard_error==result.execution.stderr_bytes,
            "Private observations replaced the student's public output");
    check_restored(request,result);
}
void flow(const state::StateData& data,const Lesson& lesson,std::string_view activity,
        std::string_view reference,bool expected) {
    const auto& block=owner(lesson,activity);
    const auto& comparison=owner(lesson,reference);
    const auto completed=[&](const Block& candidate) {
        return compiler_puzzle_completed(data,lesson,candidate);
    };
    const auto snapshot=compiler_completion_snapshot(lesson,completed);
    require(compiler_requires_completion(block)&&snapshot.at(block.stable_id)==expected,
        "The integer editor lost its mandatory completion");
    const auto at=stable_block_position(lesson,reference);
    require(at&&compiler_reference_available(lesson,at->page,comparison,snapshot,completed)==expected,
        "The later comparison ignored the integer battery achievement");
    const auto start=stable_block_position(lesson,activity);
    require(start.has_value(),"The integer owner lost its stable position");
    const std::array requirements{EntryRequirement{start->page,
        expected?EntryCompletion::complete:EntryCompletion::incomplete}};
    require(lesson_entry_decision(at->page,lesson.pages.size(),requirements).page==
            (expected?at->page:start->page),"Entry policy bypassed the integer battery");
}
std::string reversed_solution() {
    return R"cpp(#include "envio/cajas.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    std::ifstream input("toolchain.txt", std::ios::binary);
    const std::string toolchain((std::istreambuf_iterator<char>(input)), {});
    std::string identity = std::string(__FILE__) + "|" + std::to_string(__LINE__) +
        "|" + std::to_string(argc) + "|" + argv[0] + "|" +
        std::filesystem::current_path().string() + "|" + toolchain;
    if (const char* path = std::getenv("PATH")) { identity += path; }
    if (const char* temp = std::getenv("TEMP")) { identity += temp; }
    identity += std::filesystem::exists(__FILE__) ? "|source-visible" : "|source-hidden";
    std::string encoded;
    for (const unsigned char byte : identity) {
        encoded += static_cast<char>('A' + byte / 16);
        encoded += static_cast<char>('A' + byte % 16);
    }
    std::cout << "Identidad=" << encoded << '\n';
    std::ofstream("public-proof.txt", std::ios::binary) << identity;
    std::ofstream("toolchain.txt", std::ios::binary | std::ios::app) << "PUBLIC-MODIFICATION\n";
    const int inputs[]{17, 5, 1, 20, 8, 4, 0};
    const int expected[]{5, 2, 1, 5, 2, 1, 0};
    int failures = 0;
    for (int index = 0; index != 7; ++index) {
        const int obtained = cajas_para(inputs[index]);
        const bool matches = obtained == expected[index];
        std::printf("dato=%d esperado=%d obtenido=%d %s\n",
            inputs[index], expected[index], obtained, matches ? "BIEN" : "DIFIERE");
        if (!matches) { ++failures; }
    }
    std::printf("Resumen: %s\n", failures ? "DIFIERE" : "BIEN");
    return failures ? 257 : 0;
}
)cpp";
}
} // namespace


int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const LaunchOptions options{ProfileId::Codex,
            L"integer-battery-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        const auto projects=environment.paths.projects/L"Baterías & (CMake)";
        const auto original_text=read(std::filesystem::path(argv[1]));
        const auto original=parse_lesson_json(original_text,"es");
        auto document=JsonObject::Parse(winrt::to_hstring(original_text));
        auto declarations=document.HasKey(L"compilerOpenBatteryChecks")
            ?document.GetNamedArray(L"compilerOpenBatteryChecks"):JsonArray{};
        for(const auto& desired:JsonArray::Parse(winrt::to_hstring(declaration))) {
            const auto activity=desired.GetObject().GetNamedString(L"activity");
            bool present=false;
            for(const auto& existing:declarations)
                if(existing.GetObject().GetNamedString(L"activity")==activity) {
                    require(!present&&same_json(existing,desired),
                        "An existing integer declaration has an incompatible contract");
                    present=true;
                }
            if(!present)declarations.Append(desired);
        }
        document.Insert(L"compilerOpenBatteryChecks",declarations);
        auto references=document.HasKey(L"referenceAfter")
            ?document.GetNamedArray(L"referenceAfter"):JsonArray{};
        for(const auto& [activity,reference]:std::array{
                std::pair{first_id,first_reference},std::pair{second_id,second_reference}}) {
            bool present=false;
            for(const auto& value:references) {
                const auto relation=value.GetObject();
                if(relation.GetNamedString(L"activity")!=winrt::to_hstring(activity))continue;
                require(!present,"Duplicated existing reference activity");
                present=true;
                const auto blocks=relation.GetNamedArray(L"blocks");
                bool linked=false;
                for(const auto& id:blocks)
                    if(id.GetString()==winrt::to_hstring(reference))linked=true;
                if(!linked)blocks.Append(JsonValue::CreateStringValue(winrt::to_hstring(reference)));
            }
            if(!present)references.Append(JsonObject::Parse(winrt::to_hstring(
                "{\"activity\":\""+std::string(activity)+"\",\"blocks\":[\""+
                std::string(reference)+"\"]}")));
        }
        document.Insert(L"referenceAfter",references);
        const auto reviewed=winrt::to_string(document.Stringify());
        const auto lesson=parse_lesson_json(reviewed,"es");
        const auto progress=parse_lesson_progress_metadata(reviewed,"es");
        auto decimal=reviewed;
        replace_once(decimal,"\"failureExitCode\":1","\"failureExitCode\":1.0");
        rejected_loaders(decimal);
        auto unmapped=document;
        unmapped.Remove(L"stateLayout");
        rejected_loaders(winrt::to_string(unmapped.Stringify()));
        require(original.state_layout.raw_json==lesson.state_layout.raw_json&&
                original.state_layout.raw_json==progress.state_layout.raw_json&&
                original.state_layout.lineage==lesson.state_layout.lineage&&
                original.state_layout.signatures==lesson.state_layout.signatures&&
                original.pages.size()==lesson.pages.size()&&lesson.pages.size()==progress.pages.size(),
            "Integer declarations changed an authentic state layout or page");
        for(const auto id:{first_id,second_id}) {
            const auto& before=owner(original,id);
            const auto& block=owner(lesson,id);
            const auto& restored=owner(progress,id);
            require(block.integer_battery_probe&&restored.integer_battery_probe&&
                    block.integer_battery_probe==restored.integer_battery_probe&&
                    block.output_check&&restored.output_check&&
                    block.output_check->integer_battery_check&&restored.output_check->integer_battery_check&&
                    block.output_check->cases.size()==1&&restored.output_check->cases.size()==1&&
                    block.project_files==before.project_files&&restored.project_files==before.project_files&&
                    block.cmake==before.cmake&&restored.cmake==before.cmake&&
                    block.initial_code==before.initial_code&&restored.initial_code==before.initial_code&&
                    block.stable_id==before.stable_id&&restored.stable_id==before.stable_id&&
                    block.address.page==before.address.page&&block.address.block==before.address.block&&
                    restored.address.page==before.address.page&&restored.address.block==before.address.block&&
                    JsonObject::Parse(winrt::to_hstring(block.raw_json)).Stringify()==
                        JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify()&&
                    block.raw_json==restored.raw_json&&
                    exercise_key(original,before)==exercise_key(lesson,block)&&
                    draft_key("programming",original,before)==draft_key("programming",lesson,block)&&
                    reference_objective_frame(lesson,block)==reference_objective_frame(progress,restored),
                "Integer declarations replaced a source, target, owner, address, key or full/progress objective");
        }
        const auto& first=owner(lesson,first_id);
        const auto& second=owner(lesson,second_id);
        const auto first_solution=owner(original,first_reference).code;
        const auto second_solution=owner(original,second_reference).code;
        require(!first_solution.empty()&&!second_solution.empty(),"Missing authentic later checker solutions");
        for(const auto reference:{first_reference,second_reference}) {
            const auto original_position=stable_block_position(original,reference);
            const auto reviewed_position=stable_block_position(lesson,reference);
            const auto restored_position=stable_block_position(progress,reference);
            require(original_position&&reviewed_position&&restored_position&&
                    original_position->page==reviewed_position->page&&
                    original_position->block==reviewed_position->block&&
                    original_position->page==restored_position->page&&
                    original_position->block==restored_position->block&&
                    lesson.pages[reviewed_position->page].blocks[reviewed_position->block].stable_id==reference&&
                    owner(lesson,reference).code==owner(original,reference).code,
                "A comparison no longer resolves to its original stable state-layout identity");
        }
        state::StateData data;
        data.preferences["qa"]["keep"]=std::string("unrelated achievement and draft");
        state::prepare_lesson_state(data,original);
        const auto draft_identity=draft_key("programming",lesson,first);
        JsonObject saved_project_object;
        JsonArray saved_files;
        for(const auto& [path,source]:first.project_files) {
            JsonObject item;
            item.Insert(L"path",JsonValue::CreateStringValue(winrt::to_hstring(path)));
            item.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(source)));
            saved_files.Append(item);
        }
        saved_project_object.Insert(L"selected",JsonValue::CreateStringValue(L"pruebas/cajas_casos.cpp"));
        saved_project_object.Insert(L"sources",JsonArray{});
        saved_project_object.Insert(L"files",saved_files);
        saved_project_object.Insert(L"indentationV2",JsonObject{});
        saved_project_object.Insert(L"cursors",JsonObject{});
        const auto saved_project=winrt::to_string(saved_project_object.Stringify());
        data.preferences["cpp_lesson_drafts"][draft_identity+"/project"]=saved_project;
        state::Draft saved_draft;
        saved_draft.source=first_solution;
        saved_draft.input="";
        saved_draft.selection_start=3;
        saved_draft.selection_end=9;
        saved_draft.has_saved=true;
        state::put_draft(data,draft_identity,saved_draft);
        state::prepare_lesson_state(data,lesson);
        require(state::string_value(data,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
                state::get_draft(data,draft_identity,first.initial_code).source==first_solution,
            "Root declaration replaced an existing project or editor draft");
        flow(data,lesson,first_id,first_reference,false);
        flow(data,progress,second_id,second_reference,false);
        std::cout << "PASS authentic full/progress owners, preserved sources, targets, drafts and reference gates\n";

        std::uint64_t sequence=0;
        const auto request=[&](const Block& block,std::string_view driver,
                std::string_view target,CppRunRequest::Action action=CppRunRequest::Action::compile_run) {
            CppRunRequest result;
            result.revision=700+(++sequence);
            result.projects_directory=projects;
            result.project_key=draft_key("programming",lesson,block);
            result.project_files=block.project_files;
            result.source_files=block.source_files;
            result.cmake=block.cmake;
            result.cmake->target=target;
            result.integer_battery_probe=block.integer_battery_probe;
            file(result.project_files,block.integer_battery_probe->test_path)=driver;
            result.stdin_utf8=block.standard_input.value_or("");
            result.compile_timeout=60s;
            result.execution_timeout=5s;
            // The reader looks up provenance before assigning its run id/action.
            result.integer_battery_prerequisite=
                compiler_integer_battery_prerequisite(data,lesson,block,result);
            result.request_id=sequence;
            result.action=action;
            return result;
        };
        const auto phase=[&](CppRunRequest& candidate,CppRunRequest::Action action) {
            candidate.request_id=++sequence;
            candidate.action=action;
            return run_cpp(candidate);
        };

        // This complete checker was captured before any real client run.
        auto premature=request(first,first_solution,first.integer_battery_probe->test_target);
        require(premature.integer_battery_prerequisite.empty(),"A client receipt existed before its execution");
        const auto premature_result=run_cpp(premature);
        const auto premature_capture=capture_cpp_execution(premature,premature_result);
        checked_run(premature,premature_result,premature_capture,first,false);
        require(integer_battery_verify(*premature.integer_battery_probe,
                    premature_result.integer_battery_evidence->runs,
                    cpp_integer_battery_request_fingerprint(premature)).accepted,
            "The premature checker failed a criterion other than its missing client");
        const auto no_client=data;
        require(!record_compiler_puzzle_case(data,lesson,first,premature,premature_capture,"premature")&&
                data==no_client,"A checker without the earlier client earned progress");

        auto client=request(first,file(first.project_files,first.integer_battery_probe->test_path),
            first.integer_battery_probe->prerequisite->target);
        const auto configured=phase(client,CppRunRequest::Action::configure);
        require(configured.error_utf8.empty()&&configured.configuration.succeeded()&&
                !configured.integer_battery_evidence,"The client configure phase claimed runtime evidence");
        const auto built=phase(client,CppRunRequest::Action::build);
        require(built.error_utf8.empty()&&built.compilation.succeeded()&&!built.integer_battery_evidence&&
                inspect_cpp_project(client).built,"The client build did not leave its real executable ready");
        const auto client_result=phase(client,CppRunRequest::Action::execute);
        public_execution(client_result);
        const auto client_capture=capture_cpp_execution(client,client_result);
        require(client_capture.integer_battery_client_correct&&!client_capture.integer_battery_probe_correct&&
                client_capture.execution_completed&&client_capture.execution_succeeded&&
                client_result.execution.stdout_bytes=="1:2"&&
                client_result.integer_battery_evidence&&
                client_result.integer_battery_evidence->runs.size()==1&&
                client_result.integer_battery_evidence->runs.front().calls==
                    std::vector<IntegerBatteryObservation>{{4,1},{8,2}},
            "The real client target did not prove its own two calls and normal output");
        check_restored(client,client_result);
        require(record_compiler_integer_battery_client(data,lesson,first,client,client_capture),
            "The real client could not preserve its prerequisite receipt");
        require(!compiler_puzzle_completed(data,lesson,first),
            "A client observation alone completed the checker activity");
        const auto prerequisite_state=data;
        require(!record_compiler_puzzle_case(data,lesson,first,premature,premature_capture,"old checker")&&
                data==prerequisite_state,"An earlier checker was promoted after recording the client");
        auto rebound=premature;
        rebound.integer_battery_prerequisite=
            compiler_integer_battery_prerequisite(data,lesson,first,rebound);
        require(!rebound.integer_battery_prerequisite.empty()&&
                !capture_cpp_execution(rebound,premature_result).integer_battery_probe_correct&&
                !record_compiler_puzzle_case(data,lesson,first,rebound,premature_capture,"rebound")&&
                data==prerequisite_state,"A new prerequisite rebound an old checker capture");
        std::cout << "PASS real configure/build/execute client and nonretroactive checker prerequisite\n";

        auto checker=request(first,first_solution,first.integer_battery_probe->test_target,
            CppRunRequest::Action::execute);
        const auto token=checker.integer_battery_prerequisite;
        require(!token.empty(),"Editing only tests discarded the client's evidence");
        invalidate_compiler_puzzle_attempt(data,lesson,first,checker);
        require(compiler_integer_battery_prerequisite(data,lesson,first,checker)==token,
            "Invalidating an incomplete test attempt erased its earlier client");
        state::StateStore storage(environment.paths.root);
        (void)storage.load();
        storage.save(data);
        state::StateStore next_storage(environment.paths.root);
        data=next_storage.load();
        require(data==prerequisite_state&&
                compiler_integer_battery_prerequisite(data,progress,owner(progress,first_id),checker)==token,
            "Physical reopening lost the prerequisite or project draft");
        validate_compiler_puzzle_state(data,progress,owner(progress,first_id));
        const auto checker_configure=phase(checker,CppRunRequest::Action::configure);
        require(checker_configure.error_utf8.empty()&&checker_configure.configuration.succeeded(),
            "The checker could not configure its real project");
        const auto checker_build=phase(checker,CppRunRequest::Action::build);
        require(checker_build.error_utf8.empty()&&checker_build.compilation.succeeded(),
            "The checker could not build its selected executable");
        const auto checker_result=phase(checker,CppRunRequest::Action::execute);
        const auto checker_capture=capture_cpp_execution(checker,checker_result);
        checked_run(checker,checker_result,checker_capture,first);
        require(checker_result.execution.exit_code==std::uint32_t{1},
            "The authentic seven-case checker did not retain its published failure");
        require(record_compiler_puzzle_case(data,lesson,first,checker,checker_capture,"Batería ampliada comprobada"),
            "The real checker and its earlier client could not earn a sealed achievement");
        flow(data,lesson,first_id,first_reference,true);
        flow(data,progress,first_id,first_reference,true);
        std::cout << "PASS edited tests and reopened prerequisite, real checker phase workflow and sealed achievement\n";

        auto equivalent=request(first,reversed_solution(),first.integer_battery_probe->test_target);
        require(equivalent.integer_battery_prerequisite==token,"An equivalent driver discarded the client receipt");
        const auto equivalent_result=run_cpp(equivalent);
        const auto equivalent_capture=capture_cpp_execution(equivalent,equivalent_result);
        checked_run(equivalent,equivalent_result,equivalent_capture,first);
        require(equivalent_result.execution.exit_code==std::uint32_t{257},
            "The allowed wide nonzero return was narrowed to one byte");
        const auto identity=equivalent_result.execution.stdout_bytes.substr(0,
            equivalent_result.execution.stdout_bytes.find('\n'));
        require(identity.starts_with("Identidad="),"Missing phase identity fixture");
        for(const auto& run:equivalent_result.integer_battery_evidence->runs)
            require(run.stdout_bytes.substr(0,run.stdout_bytes.find('\n'))==identity,
                "A private CMake phase exposed another file, argv, cwd, environment or toolchain input");
        const auto toolchain=read(equivalent_result.run_directory/L"toolchain.txt");
        const auto marker=toolchain.find("PUBLIC-MODIFICATION");
        require(marker!=toolchain.npos&&toolchain.find("PUBLIC-MODIFICATION",marker+1)==toolchain.npos&&
                !read(equivalent_result.run_directory/L"public-proof.txt").empty(),
            "Private work lost or repeated a public filesystem side effect");
        const auto earned_first=data;
        require(record_compiler_puzzle_case(data,lesson,first,equivalent,equivalent_capture,"Otra organización")&&
                data==earned_first,"An equivalent later checker replaced the earlier earned achievement");
        std::cout << "PASS reversed loop, printf diagnostics, normal257, equal phase identity and public filesystem restoration\n";

        auto lots=request(second,second_solution,second.integer_battery_probe->test_target);
        const auto lots_result=run_cpp(lots);
        const auto lots_capture=capture_cpp_execution(lots,lots_result);
        checked_run(lots,lots_result,lots_capture,second);
        require(lots_result.execution.succeeded()&&lots.integer_battery_prerequisite.empty()&&
                !lots_capture.integer_battery_client_correct,
            "The independent four-case transfer gained an undocumented client prerequisite");
        require(record_compiler_puzzle_case(data,lesson,second,lots,lots_capture,"Transferencia comprobada"),
            "The real four-case transfer did not earn its objective");
        flow(data,progress,second_id,second_reference,true);
        std::cout << "PASS authentic four-case transfer with strict failure1 and no client prerequisite\n";

        const auto earned=data;
        const auto reject_reuse=[&](const CppRunRequest& changed) {
            auto state=earned;
            require(!capture_cpp_execution(changed,checker_result).integer_battery_probe_correct&&
                    !record_compiler_puzzle_case(state,lesson,first,changed,checker_capture,"stale")&&state==earned,
                "Old FreshExecution rebound to a changed integer request");
        };
        auto changed=checker;
        file(changed.project_files,first.integer_battery_probe->test_path)+="\n// changed driver\n";
        reject_reuse(changed);
        for(const auto& path:{"src/cajas.cpp","app/consulta.cpp","CMakeLists.txt","publico/envio/cajas.h"}) {
            changed=checker;file(changed.project_files,path)+="\n// protected edit\n";
            require(compiler_integer_battery_prerequisite(data,lesson,first,changed).empty()&&
                    !compiler_runtime_request_allowed(first,changed),
                "A protected component, client, interface or CMake edit reused the client receipt");
            reject_reuse(changed);
        }
        changed=checker;changed.cmake->build_type="Release";reject_reuse(changed);
        changed=checker;changed.cmake->target="consultar_cajas";reject_reuse(changed);
        changed=checker;changed.integer_battery_prerequisite="forged token";reject_reuse(changed);
        changed=checker;++changed.request_id;reject_reuse(changed);
        changed=checker;++changed.revision;reject_reuse(changed);
        changed=checker;changed.project_key="another profile";reject_reuse(changed);
        changed=checker;changed.stdin_utf8="unexpected";reject_reuse(changed);
        changed=checker;changed.integer_battery_probe->cases.back().expected=4;reject_reuse(changed);
        changed=checker;changed.integer_battery_probe->failure_exit_code=257;reject_reuse(changed);
        changed=checker;changed.integer_battery_probe->cases.clear();
        require(cpp_integer_battery_request_fingerprint(changed).empty(),"Invalid spec retained a fingerprint");
        reject_reuse(changed);
        for(const auto part:{0,1,2,3,4}) {
            auto altered=checker_result;
            if(part==0)altered.integer_battery_evidence.reset();
            if(part==1)altered.integer_battery_evidence->runs.back().completed_normally=false;
            if(part==2)altered.integer_battery_evidence->runs.back().calls.clear();
            if(part==3)altered.execution.stdout_bytes+="forged";
            if(part==4)altered.project_fingerprint="stale";
            require(!capture_cpp_execution(checker,altered).integer_battery_probe_correct,
                "Missing, stale or altered integer observations acquired credit");
        }
        auto missing=checker_capture;
        missing.integer_battery_request_fingerprint.clear();
        require(!record_compiler_puzzle_case(data,lesson,first,checker,missing,"unbound")&&data==earned,
            "A summary boolean without request binding earned progress");
        std::cout << "PASS source, target, configuration, token, policy and direct-recorder binding guards\n";

        std::vector<std::pair<const Block*,std::string>> negatives{
            {&first,file(first.project_files,first.integer_battery_probe->test_path)},
            {&second,file(second.project_files,second.integer_battery_probe->test_path)}};
        auto always_ok=second_solution;
        replace_once(always_ok,"obtenido == esperado","true");
        negatives.emplace_back(&second,std::move(always_ok));
        auto copied_expected=second_solution;
        replace_once(copied_expected,"const bool coincide{obtenido == esperado};",
            "esperado = obtenido;\n    const bool coincide{obtenido == esperado};");
        negatives.emplace_back(&second,std::move(copied_expected));
        for(std::size_t i=0;i<negatives.size();++i) {
            const auto& block=*negatives[i].first;
            auto candidate=request(block,negatives[i].second,block.integer_battery_probe->test_target);
            const auto result=run_cpp(candidate);
            const auto execution=capture_cpp_execution(candidate,result);
            checked_run(candidate,result,execution,block,false);
            require(!execution.integer_battery_probe_feedback.empty(),
                "A real rejected battery lost its independent feedback");
            if(i==1)require(result.integer_battery_evidence->runs.back().calls.empty(),
                "The initial empty main gained an artificial component call");
            auto state=earned;
            require(!record_compiler_puzzle_case(state,lesson,block,candidate,execution,"incorrect")&&state==earned,
                "A real rejected checker erased a prior achievement");
            const auto evaluation=evaluate_cpp_output_check(*block.output_check,execution,candidate.revision);
            require(record_compiler_puzzle_review(state,lesson,block,candidate,execution,evaluation.feedback),
                "An authentic rejected checker could not store its feedback");
            const auto reopened=state::decode(state::encode(state));
            require(compiler_puzzle_completed(reopened,progress,owner(progress,block.stable_id))&&
                    compiler_puzzle_review(reopened,progress,owner(progress,block.stable_id),candidate)==evaluation.feedback,
                "Reopening lost either an earlier achievement or a real integer rejection");
        }
        std::cout << "PASS real initial drivers, empty-main normality, always-OK and copied expected values\n";

        auto abort_source=second_solution;
        replace_once(abort_source,"#include <iostream>","#include <iostream>\n#include <cstdlib>");
        replace_once(abort_source,"    if (!correcto) {",
            "    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);\n    std::abort();\n    if (!correcto) {");
        auto abnormal=request(second,abort_source,second.integer_battery_probe->test_target);
        const auto abnormal_result=run_cpp(abnormal);
        public_execution(abnormal_result);
        const auto abnormal_capture=capture_cpp_execution(abnormal,abnormal_result);
        require(abnormal_result.execution.exit_code==std::uint32_t{3}&&
                !abnormal_capture.execution_completed&&!abnormal_capture.integer_battery_probe_correct&&
                !abnormal_capture.integer_battery_probe_feedback.empty(),
            "Abort was confused with a normal failure return");
        require(!record_compiler_puzzle_case(data,lesson,second,abnormal,abnormal_capture,"aborted")&&data==earned,
            "An abnormal checker changed prior progress");
        storage.save(data);
        state::StateStore reopened_storage(environment.paths.root);
        const auto reopened=reopened_storage.load();
        require(reopened==earned&&compiler_puzzle_completed(reopened,progress,owner(progress,first_id))&&
                compiler_puzzle_completed(reopened,progress,owner(progress,second_id)),
            "Physical reopening lost a sealed integer achievement");
        validate_compiler_puzzle_state(reopened,progress,owner(progress,first_id));
        validate_compiler_puzzle_state(reopened,progress,owner(progress,second_id));
        state::validate_lesson_state(reopened,progress);
        auto tampered=reopened;
        const auto client_key="compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
            exercise_key(lesson,first)+"/integer-client/v1/"+
            state::sha256(reference_objective_frame(lesson,first));
        auto& fields=std::get<std::vector<std::string>>(tampered.preferences["native_windows"].at(client_key));
        fields.back()="tampered";
        require(compiler_integer_battery_prerequisite(tampered,progress,owner(progress,first_id),checker).empty(),
            "A tampered client receipt supplied a checker prerequisite");
        bool rejected=false;
        try {validate_compiler_puzzle_state(tampered,progress,owner(progress,first_id));}
        catch(const std::exception&){rejected=true;}
        require(rejected,"State validation accepted a malformed client seal");
        auto reset=reopened;
        reset_compiler_puzzle(reset,lesson,first);
        require(compiler_integer_battery_prerequisite(reset,lesson,first,checker).empty()&&
                !compiler_puzzle_completed(reset,lesson,first)&&
                compiler_puzzle_completed(reset,lesson,second)&&
                state::string_value(reset,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
                state::string_value(reset,"qa","keep")=="unrelated achievement and draft",
            "Explicit reset did not isolate the owner receipt from other achievements and drafts");
        require(read(std::filesystem::path(argv[1]))==original_text,
            "The integer runtime test rewrote its authentic curricular asset");
        std::cout << "PASS abort rejection, physical reopening, receipt tamper, isolated reset and preserved prior state\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr << "FAIL integer battery runtime: " << error.what() << '\n';
        return 1;
    }catch(const winrt::hresult_error& error) {
        std::cerr << "FAIL integer battery runtime: " << winrt::to_string(error.message()) << '\n';
        return 1;
    }
}
