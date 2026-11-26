#include "core/compiler_flow.hpp"
#include "core/adjustment_project_check.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "core/open_battery_check.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/published_adoption_test_source.hpp"
#include "native_build_config.hpp"

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
constexpr std::string_view lesson_relative=
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_integrar_un_proyecto_y_sostener_su_evidencia.json";
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
        "The authentic open battery fixture changed at anchor: "+std::string(from));
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
std::string portable_warning_recipe(std::string source) {
    // Authored source for the in-memory editorial transition below. The
    // component, interface, client, checker and target relationships stay exact.
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
std::string recipe_revision_json(const Lesson& previous,std::string_view source) {
    const auto position=stable_block_position(previous,activity);
    require(position.has_value(),"A recipe review lost its original mapped owner");
    auto document=JsonObject::Parse(winrt::to_hstring(previous.raw_json));
    const auto layout=document.GetNamedObject(L"stateLayout");
    const auto block=document.GetNamedArray(L"pages").
        GetObjectAt(static_cast<std::uint32_t>(position->page)).
        GetNamedArray(L"blocks").GetObjectAt(static_cast<std::uint32_t>(position->block));
    const auto archived=JsonObject::Parse(block.Stringify());
    const auto editorial=layout.GetNamedArray(L"editorial");
    const auto fingerprint=[](const auto& value) {
        return test::compile_check_test_fingerprint(value);
    };
    JsonObject prefix;
    prefix.Insert(L"lessonId",document.GetNamedValue(L"id"));
    prefix.Insert(L"locale",layout.GetNamedValue(L"locale"));
    prefix.Insert(L"editorial",JsonArray::Parse(editorial.Stringify()));
    const auto history=fingerprint(prefix);
    JsonArray semantics;
    if(document.HasKey(L"textSemantics"))
        for(const auto& value:document.GetNamedObject(L"textSemantics").GetNamedArray(L"fields")) {
            const auto field=value.GetObject();
            if(field.GetNamedString(L"block")==winrt::to_hstring(activity)&&
                    winrt::to_string(field.GetNamedString(L"path")).starts_with("/outputCheck/cases/"))
                semantics.Append(JsonObject::Parse(field.Stringify()));
        }
    JsonObject record,recipe;
    record.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(activity)));
    record.Insert(L"block",archived);
    if(archived.HasKey(L"outputCheck"))
        record.Insert(L"cases",archived.GetNamedObject(L"outputCheck").GetNamedValue(L"cases"));
    else record.Insert(L"cases",JsonValue::Parse(L"null"));
    record.Insert(L"editorialIndex",JsonValue::CreateNumberValue(editorial.Size()));
    record.Insert(L"historySha256",JsonValue::CreateStringValue(winrt::to_hstring(history)));
    record.Insert(L"semanticBefore",semantics);
    record.Insert(L"semanticAfter",JsonArray::Parse(semantics.Stringify()));
    recipe.Insert(L"path",JsonValue::CreateStringValue(L"CMakeLists.txt"));
    recipe.Insert(L"source",JsonValue::CreateStringValue(winrt::to_hstring(source)));
    record.Insert(L"recipe",recipe);
    bool replaced=false;
    for(const auto& value:block.GetNamedArray(L"projectFiles")) {
        const auto item=value.GetObject();
        if(item.GetNamedString(L"path")==L"CMakeLists.txt") {
            require(!replaced&&item.GetNamedString(L"initialCode")!=winrt::to_hstring(source),
                "A recipe review must replace exactly one existing source");
            item.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(source)));
            replaced=true;
        }
    }
    require(replaced,"A recipe review cannot invent a CMake file");
    JsonObject proof;
    proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(activity)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(archived))));
    const auto after=fingerprint(block);
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(after)));
    std::vector<std::string> nodes;
    for(const auto& value:editorial) {
        const auto edge=value.GetObject();
        if(edge.GetNamedString(L"id")!=winrt::to_hstring(activity))continue;
        if(nodes.empty())nodes.push_back(winrt::to_string(edge.GetNamedString(L"beforeSha256")));
        nodes.push_back(winrt::to_string(edge.GetNamedString(L"afterSha256")));
    }
    for(std::size_t i=0;i+1<nodes.size();++i)if(nodes[i]==after) {
        JsonObject returned;
        returned.Insert(L"revision",JsonValue::CreateNumberValue(static_cast<double>(i)));
        returned.Insert(L"historySha256",JsonValue::CreateStringValue(winrt::to_hstring(history)));
        returned.Insert(L"previousBlock",JsonObject::Parse(archived.Stringify()));
        proof.Insert(L"returnTo",returned);
        break;
    }
    auto records=layout.HasKey(L"compilerCaseRevisions")
        ?layout.GetNamedArray(L"compilerCaseRevisions"):JsonArray{};
    records.Append(record);
    layout.Insert(L"compilerCaseRevisions",records);
    editorial.Append(proof);
    return winrt::to_string(document.Stringify());
}
void rejected_recipe(const JsonObject& invalid,std::string_view message) {
    const auto source=winrt::to_string(invalid.Stringify());
    for(const bool progress_only:{false,true}) {
        bool rejected=false;
        try {
            if(progress_only)(void)parse_lesson_progress_metadata(source,"es");
            else (void)parse_lesson_json(source,"es");
        }catch(const std::exception&) {rejected=true;}
        catch(const winrt::hresult_error&) {rejected=true;}
        require(rejected,message);
    }
}
std::string extended_driver(const Block& block,std::string_view call) {
    auto source=file(block.project_files,test_path);
    const std::string summary="    std::cout << \"Fallos=\" << fallos << \"\\n\";";
    replace_once(source,summary,std::string(call)+summary);
    return source;
}
std::string_view phase_status_name(CppPhaseStatus status) {
    switch(status) {
    case CppPhaseStatus::not_started:return "not_started";
    case CppPhaseStatus::completed:return "completed";
    case CppPhaseStatus::cancelled:return "cancelled";
    case CppPhaseStatus::timed_out:return "timed_out";
    case CppPhaseStatus::output_limit:return "output_limit";
    case CppPhaseStatus::launch_failed:return "launch_failed";
    }
    return "unknown";
}
std::string run_diagnostic(const CppRunRequest& request,const CppRunResult& result) {
    std::string details="\nrequest="+std::to_string(request.request_id)+
        " revision="+std::to_string(request.revision)+
        " action="+std::to_string(static_cast<int>(request.action))+
        "\nprojects="+winrt::to_string(request.projects_directory.wstring())+
        "\nrun_directory="+winrt::to_string(result.run_directory.wstring())+
        "\nrequested_target="+(request.cmake?request.cmake->target:std::string{})+
        "\nobserved_target="+result.executable.value_or("")+
        "\nrunner_error="+result.error_utf8+"\n";
    try {
        const auto workspace=inspect_cpp_project(request).workspace;
        details+="workspace="+winrt::to_string(workspace.wstring())+
            "\nworkspace_characters="+std::to_string(workspace.native().size())+"\n";
    }catch(const std::exception& error) {
        details+="workspace_inspection_error="+std::string(error.what())+"\n";
    }catch(const winrt::hresult_error& error) {
        details+="workspace_inspection_error="+winrt::to_string(error.message())+"\n";
    }
    const auto append=[&](std::string_view name,const CppPhaseResult& phase) {
        details+=std::string(name)+": status="+std::string(phase_status_name(phase.status))+
            " exit="+(phase.exit_code?std::to_string(*phase.exit_code):"none")+
            "\nstdout:\n"+phase.stdout_bytes+"\nstderr:\n"+phase.stderr_bytes+"\n";
    };
    append("configuration",result.configuration);
    append("compilation",result.compilation);
    append("execution",result.execution);
    return details;
}
void public_success(const CppRunRequest& request,const CppRunResult& result) {
    const auto diagnostic=run_diagnostic(request,result);
    require(result.error_utf8.empty(),"Runner error: "+diagnostic);
    require(result.configuration.status==CppPhaseStatus::not_started||
            result.configuration.succeeded(),"Real CMake configuration failed: "+diagnostic);
    require(result.compilation.succeeded(),"Real CMake compilation failed: "+diagnostic);
    require(result.execution.succeeded(),"The actual checker did not finish with exit 0: "+diagnostic);
    require(result.executable==std::optional<std::string>{"comprobar_ajustes"},
        "The wrong executable supplied the public observation: "+diagnostic);
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
        const FreshExecution& capture,const Block& block,OpenBatteryCall added,bool accepted,
        int added_case_number=12) {
    public_success(request,result);
    require(result.open_battery_evidence.has_value(),"Missing actual independent CMake phases");
    const auto& evidence=*result.open_battery_evidence;
    const auto& spec=*block.open_battery_probe;
    const auto plan=open_battery_mutation_plan(spec.original_cases,added);
    require(plan&&plan->size()==37&&evidence.runs.size()==plan->size(),
        "The real eleven-case extension did not execute all 37 independent phases");
    auto calls=spec.original_cases;
    calls.push_back(added);
    auto case_numbers=spec.original_case_numbers;
    case_numbers.push_back(added_case_number);
    for(std::size_t i=0;i<plan->size();++i) {
        const auto& observed=evidence.runs[i];
        require(observed.compiled&&observed.executed&&
                observed.program_frame==evidence.original_project_fingerprint&&
                observed.mutation==(*plan)[i].mutation&&observed.target==(*plan)[i].target&&
                observed.calls==calls&&observed.case_numbers==case_numbers,
            "A real phase omitted a call, changed its number or used another mutation");
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
void verify_recipe_return(const Lesson& earned_lesson,const state::StateData& earned,
        const std::filesystem::path& profile) {
    const auto& credited=owner(earned_lesson);
    require(compiler_puzzle_completed(earned,earned_lesson,credited)&&
            compiler_puzzle_case_count(earned,earned_lesson,credited)==1,
        "Recipe history QA requires a real earned first-case receipt");
    const auto original_recipe=file(credited.project_files,"CMakeLists.txt");
    const auto changed_text=recipe_revision_json(earned_lesson,
        original_recipe+"\n# QA: exercise an explicit later recipe publication.\n");
    const auto changed=parse_lesson_json(changed_text,"es");
    const auto returned_text=recipe_revision_json(changed,original_recipe);
    const auto returned=parse_lesson_json(returned_text,"es");
    const auto& identical=owner(returned);
    require(identical.project_files==credited.project_files&&
            identical.initial_code==credited.initial_code&&
            identical.compiler_client_epoch!=credited.compiler_client_epoch&&
            reference_objective_frame(returned,identical)!=
                reference_objective_frame(earned_lesson,credited),
        "Returning to identical recipe bytes collapsed two authenticated source epochs");
    const auto layout=JsonObject::Parse(winrt::to_hstring(returned.raw_json)).GetNamedObject(L"stateLayout");
    const auto edges=layout.GetNamedArray(L"editorial");
    require(edges.GetObjectAt(edges.Size()-1).HasKey(L"returnTo"),
        "The recipe return did not archive its explicit earlier editorial node");
    const auto invalid_return=JsonObject::Parse(winrt::to_hstring(returned.raw_json));
    const auto invalid_edges=invalid_return.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    invalid_edges.GetObjectAt(invalid_edges.Size()-1).Remove(L"returnTo");
    rejected_recipe(invalid_return,"A recipe return without its explicit earlier revision was accepted");
    auto migrated=earned;
    for(const auto* version:{&changed,&returned}) {
        const auto compact=parse_lesson_progress_metadata(version->raw_json,"es");
        const auto before=migrated;
        state::prepare_lesson_state(migrated,*version);
        for(const auto* model:{version,&compact}) {
            const auto& current=owner(*model);
            require(!compiler_puzzle_completed(migrated,*model,current)&&
                    compiler_puzzle_case_count(migrated,*model,current)==0&&
                    !compiler_puzzle_success_feedback(migrated,*model,current)&&
                    compiler_puzzle_completed(migrated,earned_lesson,credited)&&
                    compiler_puzzle_case_count(migrated,earned_lesson,credited)==1,
                "A new or returned recipe borrowed a real receipt from an earlier source epoch");
            completion(migrated,*model,false);
            validate_compiler_puzzle_state(migrated,*model,current);
            state::validate_lesson_state(migrated,*model);
        }
        for(const auto* domain:{"native_windows","cpp_lesson_drafts","qa"}) {
            const auto existing=before.preferences.find(domain);
            if(existing==before.preferences.end())continue;
            for(const auto& [key,value]:existing->second) {
                const auto stored=state::find(migrated,domain,key);
                require(stored&&*stored==value,
                    "A recipe publication overwrote a historical receipt, saved draft or unrelated value");
            }
        }
        const auto adopted=migrated;
        state::prepare_lesson_state(migrated,*version);
        require(migrated==adopted,"Replaying a recipe publication changed history or revived old credit");
    }
    state::StateStore storage(profile);
    (void)storage.load();storage.save(migrated);
    state::StateStore reopened_storage(profile);
    const auto reopened=reopened_storage.load();
    require(reopened==migrated&&compiler_puzzle_completed(reopened,earned_lesson,credited)&&
            compiler_puzzle_case_count(reopened,earned_lesson,credited)==1,
        "Physical reopening changed the real receipt or recipe publication history");
    completion(reopened,returned,false);
    completion(reopened,parse_lesson_progress_metadata(returned.raw_json,"es"),false);
    std::cout << "PASS real recipe receipt retained across a new publication and identical-byte return: "
                 "distinct epochs, no borrowed first case, unchanged drafts and physical reopening\n";
}

constexpr std::string_view builder_activity="block_0002_0003";
constexpr std::string_view builder_declaration=R"json({
  "activity":"block_0002_0003","kind":"cmake-adjustment-project-v1",
  "libraryPath":"src/ajuste.cpp","headerPath":"publico/control/ajuste.h",
  "header":"control/ajuste.h","function":"intentar_ajuste",
  "libraryTarget":"ajustes","clientPath":"app/consulta.cpp",
  "clientTarget":"consultar_ajustes","testPath":"pruebas/casos.cpp",
  "target":"comprobar_ajustes"
})json";
constexpr std::string_view builder_graph=R"json({
  "activity":"block_0002_0003","minimumCmakeVersion":"3.20",
  "minimumCmakeVersionAtLeast":true,
  "targets":[
    {"name":"ajustes","type":"STATIC_LIBRARY","sources":["src/ajuste.cpp"],
     "exactSources":true,"publicIncludeRoots":["publico"],"explicitCxx20":true,
     "explicitCxxExtensionsOff":true,"requireExplicitMsvcWarnings":true},
    {"name":"consultar_ajustes","type":"EXECUTABLE","sources":["app/consulta.cpp"],
     "exactSources":true,"linksTo":["ajustes"],"explicitCxx20":true,
     "explicitCxxExtensionsOff":true,"requireExplicitMsvcWarnings":true},
    {"name":"comprobar_ajustes","type":"EXECUTABLE","sources":["pruebas/casos.cpp"],
     "exactSources":true,"linksTo":["ajustes"],"explicitCxx20":true,
     "explicitCxxExtensionsOff":true,"requireExplicitMsvcWarnings":true}
  ]
})json";
constexpr std::string_view builder_library=R"cpp(#include "control/ajuste.h"
bool intentar_ajuste(int dato, int bajo, int alto, int& destino) {
    const bool dominio = -20 <= dato && dato <= 20 &&
        -20 <= bajo && bajo <= 20 && -20 <= alto && alto <= 20;
    if (!dominio || alto < bajo) return false;
    const int final = dato < bajo ? bajo : (alto < dato ? alto : dato);
    destino = final - dato;
    return true;
}
)cpp";
constexpr std::string_view builder_checker=R"cpp(#include "control/ajuste.h"
#include <iostream>
struct Caso {
    int valor, inferior, superior, inicial;
    bool acepta;
    int destino;
};
constexpr Caso casos[] = {
    {-20,-15,20,11,true,5}, {0,-20,20,-9,true,0},
    {20,-20,15,29,true,-5}, {-20,-20,10,4,true,0},
    {20,-10,20,-7,true,0}, {4,4,4,12,true,0},
    {-20,20,20,8,true,40}, {20,-20,-20,17,true,-40},
    {-21,-3,7,31,false,31}, {21,-3,7,-32,false,-32},
    {0,-21,7,33,false,33}, {0,21,7,-34,false,-34},
    {0,-3,-21,35,false,35}, {0,-3,21,-36,false,-36},
    {0,7,3,37,false,37}
};
int main() {
    int fallos = 0;
    int numero = 0;
    for (const auto& caso : casos) {
        int observado = caso.inicial;
        const bool acepta = intentar_ajuste(
            caso.valor, caso.inferior, caso.superior, observado);
        const bool coincide = acepta == caso.acepta && observado == caso.destino;
        std::cout << "Caso=" << ++numero << " Acepta=" << acepta
                  << " Ajuste=" << observado << " EsperaAcepta=" << caso.acepta
                  << " EsperaAjuste=" << caso.destino
                  << (coincide ? " OK\n" : " FALLO\n");
        if (!coincide) ++fallos;
    }
    std::cout << "Fallos=" << fallos << "\n";
    return fallos ? 1 : 0;
}
)cpp";
// This client really calls the shared component for every input. Only its
// published report is copied; the six prepared rows alone cannot expose it.
constexpr std::string_view copied_builder_client=R"cpp(#include "control/ajuste.h"
#include <iostream>
int main() {
    int valor=0, inferior=0, superior=0, ajuste=77;
    while (std::cin >> valor >> inferior >> superior)
        (void)intentar_ajuste(valor,inferior,superior,ajuste);
    std::cout << "Peticion=1 Ajuste=1\nPeticion=2 Ajuste=0\n"
                 "Peticion=3 Ajuste=-2\nPeticion=5 Ajuste=0\n"
                 "Aceptadas=4 Rechazadas=2 Suma=-1\n";
    std::cerr << "Rechazo: peticion=4 valor=0 inferior=7 superior=3."
                 " Se admiten valores de -20 a 20 con inferior <= superior."
                 " Revise los tres valores.\n"
                 "Rechazo: peticion=6 valor=21 inferior=3 superior=7."
                 " Se admiten valores de -20 a 20 con inferior <= superior."
                 " Revise los tres valores.\n";
    return 1;
}
)cpp";

void append_builder_declaration(JsonObject& document,const wchar_t* key,
        std::string_view declaration_text) {
    auto entries=document.HasKey(key)?document.GetNamedArray(key):JsonArray{};
    const auto desired=JsonObject::Parse(winrt::to_hstring(declaration_text));
    bool found=false;
    for(const auto& entry:entries)
        if(entry.GetObject().GetNamedString(L"activity")==winrt::to_hstring(builder_activity)) {
            require(!found&&same_json(entry,desired),
                "The authentic builder already has a different root contract");
            found=true;
        }
    if(!found)entries.Append(desired);
    document.Insert(key,entries);
}

bool same_builder_draft(const state::Draft& left,const state::Draft& right) {
    return left.source==right.source&&left.input==right.input&&
        left.indentation_json==right.indentation_json&&
        left.selection_start==right.selection_start&&left.selection_end==right.selection_end&&
        left.has_saved==right.has_saved;
}

void builder_completion(const state::StateData& data,const Lesson& lesson,bool expected) {
    const auto& block=owner(lesson,builder_activity);
    require(compiler_requires_completion(block)&&
            compiler_completion_snapshot(lesson,[&](const Block& candidate) {
                return compiler_puzzle_completed(data,lesson,candidate);
            }).at(block.stable_id)==expected,
        "The constructor did not retain its mandatory first-case gate");
}

void verify_adjustment_constructor(const std::filesystem::path& fixture_path,
        const std::string& live_text,const Lesson& original,
        const state::StateData& previous_data,const Lesson& previous_lesson) {
    const LaunchOptions options{ProfileId::Codex,
        L"ab-"+std::to_wstring(GetCurrentProcessId())+L"-"+
            std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"AB & (QA)";
    const auto& before=owner(original,builder_activity);
    const auto& reference=owner(original);
    require(before.project_files.size()==5&&before.cmake&&
            before.cmake->target=="consultar_ajustes"&&before.standard_input&&
            file(before.project_files,"CMakeLists.txt").empty(),
        "The authentic open constructor or its initial client target changed");
    // The constructor is a new root objective on the already reviewed
    // publication. Retain the earlier recipe journal and its earned receipt.
    auto document=JsonObject::Parse(winrt::to_hstring(previous_lesson.raw_json));
    append_builder_declaration(document,L"compilerCmakeGraphChecks",builder_graph);
    append_builder_declaration(document,L"compilerOpenBatteryChecks",builder_declaration);
    const auto reviewed=winrt::to_string(document.Stringify());
    const auto lesson=parse_lesson_json(reviewed,"es");
    const auto progress=parse_lesson_progress_metadata(reviewed,"es");
    const auto& block=owner(lesson,builder_activity);
    const auto& restored=owner(progress,builder_activity);
    for(const int changed:{0,1,2,3,4,5}) {
        auto invalid=JsonObject::Parse(winrt::to_hstring(reviewed));
        const auto entry=[&](const wchar_t* collection) {
            for(const auto& value:invalid.GetNamedArray(collection))
                if(value.GetObject().GetNamedString(L"activity")==winrt::to_hstring(builder_activity))
                    return value.GetObject();
            throw std::runtime_error("Missing constructor declaration in negative fixture");
        };
        if(changed==0)invalid.Remove(L"compilerCmakeGraphChecks");
        if(changed==1)entry(L"compilerCmakeGraphChecks").Insert(
            L"minimumCmakeVersionAtLeast",JsonValue::CreateBooleanValue(false));
        if(changed==2)entry(L"compilerCmakeGraphChecks").GetNamedArray(L"targets").GetObjectAt(0).Insert(
            L"requireExplicitMsvcWarnings",JsonValue::CreateBooleanValue(false));
        if(changed==3)entry(L"compilerOpenBatteryChecks").Insert(
            L"target",JsonValue::CreateStringValue(L"consultar_ajustes"));
        if(changed==4)entry(L"compilerOpenBatteryChecks").Insert(
            L"headerPath",JsonValue::CreateStringValue(L"otro/control/ajuste.h"));
        if(changed==5)entry(L"compilerOpenBatteryChecks").Insert(
            L"unverifiedShortcut",JsonValue::CreateBooleanValue(true));
        const auto invalid_text=winrt::to_string(invalid.Stringify());
        for(const bool progress_only:{false,true}) {
            bool rejected=false;
            try {
                if(progress_only)(void)parse_lesson_progress_metadata(invalid_text,"es");
                else (void)parse_lesson_json(invalid_text,"es");
            }catch(const std::exception&) {rejected=true;}
            require(rejected,"A malformed constructor or weaker graph reached a native loader");
        }
    }
    require(block.adjustment_project_probe&&restored.adjustment_project_probe&&
            block.adjustment_project_probe==restored.adjustment_project_probe&&
            block.cmake_graph_check&&block.cmake_graph_check==restored.cmake_graph_check&&
            block.output_check&&restored.output_check&&
            block.output_check->adjustment_project_check&&restored.output_check->adjustment_project_check&&
            block.output_check->cases.size()==1&&restored.output_check->cases.size()==1&&
            block.output_check->cases.front().standard_input.empty()&&
            restored.output_check->cases.front().standard_input.empty()&&
            block.output_check->cases.front().executable==
                std::optional<std::string>{"comprobar_ajustes"}&&
            restored.output_check->cases.front().executable==
                block.output_check->cases.front().executable&&
            block.adjustment_project_probe->client_input==*before.standard_input,
        "The builder did not load one empty-input checker objective and the original client batch");
    for(const auto* model:{&lesson,&progress}) {
        const auto& current=owner(*model,builder_activity);
        require(model->pages.size()==original.pages.size()&&
                model->state_layout.raw_json==previous_lesson.state_layout.raw_json&&
                model->state_layout.signatures==original.state_layout.signatures&&
                model->state_layout.lineage==original.state_layout.lineage&&
                current.project_files==before.project_files&&current.initial_code==before.initial_code&&
                current.standard_input==before.standard_input&&current.cmake==before.cmake&&
                current.stable_id==before.stable_id&&current.address.page==before.address.page&&
                current.address.block==before.address.block&&
                exercise_key(*model,current)==exercise_key(original,before)&&
                draft_key("programming",*model,current)==draft_key("programming",original,before),
            "The root constructor objective changed an original source, draft, input or mapped identity");
    }
    require(reference_objective_frame(lesson,block)==reference_objective_frame(progress,restored),
        "The constructor lost full/progress objective parity");

    auto data=previous_data;
    const auto draft_identity=draft_key("programming",lesson,block);
    state::prepare_lesson_state(data,previous_lesson);
    state::Draft draft;
    draft.source=before.initial_code;draft.input=*before.standard_input;
    draft.selection_start=5;draft.selection_end=12;draft.has_saved=true;
    state::put_draft(data,draft_identity,draft);
    JsonArray draft_files;
    for(const auto& [path,source]:before.project_files) {
        JsonObject item;
        item.Insert(L"path",JsonValue::CreateStringValue(winrt::to_hstring(path)));
        item.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(source)));
        draft_files.Append(item);
    }
    JsonObject project_draft;
    project_draft.Insert(L"selected",JsonValue::CreateStringValue(L"app/consulta.cpp"));
    project_draft.Insert(L"files",draft_files);
    project_draft.Insert(L"sources",JsonArray{});
    project_draft.Insert(L"indentationV2",JsonObject{});
    project_draft.Insert(L"cursors",JsonObject{});
    const auto saved_project=winrt::to_string(project_draft.Stringify());
    data.preferences["cpp_lesson_drafts"][draft_identity+"/project"]=saved_project;
    state::prepare_lesson_state(data,lesson);
    require(same_builder_draft(state::get_draft(data,draft_identity,before.initial_code),draft)&&
            state::string_value(data,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
            compiler_puzzle_completed(data,previous_lesson,owner(previous_lesson)),
        "Adding the builder objective changed historical drafts or the earlier earned extension");
    const auto seed=data;
    builder_completion(data,lesson,false);
    builder_completion(data,progress,false);
    std::cout << "PASS authentic constructor full/progress declarations, 12 invalid-loader checks, "
                 "original map and saved project/input retained\n";

    auto files=before.project_files;
    file(files,"CMakeLists.txt")=
        portable_warning_recipe(file(reference.project_files,"CMakeLists.txt"));
    file(files,"src/ajuste.cpp")=builder_library;
    file(files,"app/consulta.cpp")=file(reference.project_files,"app/consulta.cpp");
    file(files,test_path)=builder_checker;
    std::uint64_t sequence=20000;
    const auto request=[&](const CppProjectFiles& sources) {
        CppRunRequest result;
        result.request_id=++sequence;result.revision=30000+sequence;
        result.source_utf8=before.initial_code;
        result.projects_directory=projects;result.project_key=draft_identity;
        result.project_files=sources;result.cmake=before.cmake;
        result.cmake->target="comprobar_ajustes";
        result.capture_cmake_graph=true;result.cmake_graph_check=block.cmake_graph_check;
        result.adjustment_project_probe=block.adjustment_project_probe;
        result.compile_timeout=90s;result.execution_timeout=5s;
        return result;
    };
    auto initial=request(before.project_files);
    const auto initial_result=run_cpp(initial);
    const auto initial_capture=capture_cpp_execution(initial,initial_result);
    require(!initial_capture.adjustment_project_probe_correct&&
            !record_compiler_puzzle_case(data,lesson,block,initial,initial_capture,"pending initial")&&
            data==seed,
        "The empty CMake or pending initial programs earned constructor credit");

    auto positive=request(files);
    require(compiler_runtime_request_allowed(block,positive),
        "The open constructor rejected a genuine completed project");
    const auto positive_result=run_cpp(positive);
    public_success(positive,positive_result);
    const auto positive_capture=capture_cpp_execution(positive,positive_result);
    require(positive_result.adjustment_project_evidence&&
            positive_result.adjustment_project_request_fingerprint==
                cpp_adjustment_project_request_fingerprint(positive)&&
            positive_capture.adjustment_project_probe_correct&&positive_capture.cmake_graph_correct&&
            evaluate_cmake_graph(positive,positive_result,*block.cmake_graph_check).correct,
        "The real component/client/checker request lacked one of its independent proofs: "+
            positive_capture.adjustment_project_probe_feedback+run_diagnostic(positive,positive_result));
    const auto& evidence=*positive_result.adjustment_project_evidence;
    const auto& runs=evidence.runs;
    require(!runs.empty(),"The builder received empty execution evidence");
    using Part=decltype(runs.front().phase.part);
    const auto count=[&](Part part,OpenBatteryMutation mutation) {
        return std::ranges::count_if(runs,[&](const auto& run) {
            return run.phase.part==part&&run.phase.mutation==mutation;
        });
    };
    const auto clients=adjustment_project_client_inputs(*block.adjustment_project_probe);
    require(count(Part::component,OpenBatteryMutation::baseline)==1&&
            count(Part::client,OpenBatteryMutation::baseline)==
                static_cast<std::ptrdiff_t>(clients.size())&&
            count(Part::client,OpenBatteryMutation::invert_acceptance)>0&&
            count(Part::client,OpenBatteryMutation::corrupt_destination)>0&&
            count(Part::battery,OpenBatteryMutation::baseline)==2&&
            count(Part::battery,OpenBatteryMutation::invert_acceptance)==15&&
            count(Part::battery,OpenBatteryMutation::corrupt_destination)==15&&
            std::ranges::all_of(runs,[&](const auto& run) {
                return run.compiled&&run.executed&&
                    run.program_frame==evidence.original_program_fingerprint;
            }),
        "The constructor omitted actual component, client or independent checker phases");
    const auto battery=std::ranges::find_if(runs,[](const auto& run) {
        return run.phase.part==Part::battery&&
            run.phase.mutation==OpenBatteryMutation::baseline;
    });
    require(battery!=runs.end()&&battery->observations.size()==15&&
            battery->stdout_bytes==positive_result.execution.stdout_bytes&&
            battery->stderr_bytes==positive_result.execution.stderr_bytes,
        "The private checker did not reproduce the actual public program");
    const auto without_assertions=std::ranges::find_if(runs,[](const auto& run) {
        return run.phase.part==Part::battery&&
            run.phase.mutation==OpenBatteryMutation::baseline&&run.phase.assertions_disabled;
    });
    require(without_assertions!=runs.end()&&
            without_assertions->observations==battery->observations,
        "Disabling assertions omitted or changed mandatory checker calls");
    const auto mixed_client=std::ranges::find_if(runs,[&](const auto& run) {
        return run.phase.part==Part::client&&
            run.phase.mutation==OpenBatteryMutation::baseline&&
            run.standard_input==*before.standard_input;
    });
    require(mixed_client!=runs.end()&&mixed_client->exit_code==1&&
            mixed_client->observations.size()==6&&
            std::ranges::count_if(mixed_client->observations,
                [](const auto& observed){return observed.accepted;})==4&&
            mixed_client->stdout_bytes.find("Suma=-1")!=std::string::npos,
        "The legitimate mixed client batch lost its four results or normal failure code");
    preserved_public_project(positive,positive_result);
    require(record_compiler_puzzle_case(data,lesson,block,positive,positive_capture,"Proyecto comprobado")&&
            compiler_puzzle_case_count(data,lesson,block)==1&&
            compiler_puzzle_case_count(data,progress,restored)==1,
        "The real combined proof did not earn exactly the first checker case");
    builder_completion(data,lesson,true);
    builder_completion(data,progress,true);
    const auto earned=data;
    std::cout << "PASS authentic adjustment constructor: shared CMake component/client, "
              << clients.size() << " client inputs, 15 freely organized checker calls, "
              << runs.size() << " real private phases and one sealed first case\n";

    const auto reject_reuse=[&](const CppRunRequest& changed) {
        auto untouched=earned;
        require(!capture_cpp_execution(changed,positive_result).adjustment_project_probe_correct&&
                !record_compiler_puzzle_case(untouched,lesson,block,changed,positive_capture,"stale")&&
                untouched==earned,
            "The constructor promoted a capture from another request or source revision");
    };
    for(const auto& path:{"CMakeLists.txt","publico/control/ajuste.h","src/ajuste.cpp",
                         "app/consulta.cpp","pruebas/casos.cpp"}) {
        auto changed=positive;file(changed.project_files,path)+="\n// later revision\n";
        reject_reuse(changed);
    }
    auto changed=positive;++changed.request_id;reject_reuse(changed);
    changed=positive;++changed.revision;reject_reuse(changed);
    changed=positive;changed.cmake->target="consultar_ajustes";reject_reuse(changed);
    changed=positive;changed.cmake->build_type="Release";reject_reuse(changed);
    // Windows supports only native_msvc. Exercise rejection of an invalid
    // backend without referring to a removed platform enumerator.
    changed=positive;changed.backend=static_cast<CppRunRequest::Backend>(-1);reject_reuse(changed);
    changed=positive;changed.action=CppRunRequest::Action::configure;reject_reuse(changed);
    changed=positive;changed.open_battery_probe=owner(previous_lesson).open_battery_probe;
    require(changed.open_battery_probe.has_value(),"The mixed-proof negative lost its legacy probe");
    reject_reuse(changed);
    changed=positive;changed.stdin_utf8=*before.standard_input;reject_reuse(changed);
    changed=positive;changed.project_key+="/another-owner";reject_reuse(changed);
    changed=positive;changed.adjustment_project_probe->client_input="\n";reject_reuse(changed);
    changed=positive;
    changed.cmake_graph_check->targets.front().require_explicit_msvc_warnings=false;
    require(cpp_adjustment_project_request_fingerprint(changed)!=
            cpp_adjustment_project_request_fingerprint(positive),
        "The warning requirement was omitted from the captured graph contract");
    reject_reuse(changed);
    changed=positive;changed.cmake_graph_check->minimum_cmake_version_at_least=false;
    require(cpp_adjustment_project_request_fingerprint(changed)!=
            cpp_adjustment_project_request_fingerprint(positive),
        "The minimum-version comparison policy was omitted from the captured graph contract");
    reject_reuse(changed);
    for(const int part:{0,1,2,3,4,5}) {
        auto altered=positive_result;
        if(part==0)altered.adjustment_project_evidence.reset();
        if(part==1)altered.adjustment_project_evidence->runs.pop_back();
        if(part==2)altered.adjustment_project_evidence->runs.front().observations.clear();
        if(part==3)altered.execution.stdout_bytes+="forged report";
        if(part==4)altered.project_fingerprint="other project";
        if(part==5)altered.adjustment_project_request_fingerprint="other capture";
        const auto captured=capture_cpp_execution(positive,altered);
        auto untouched=seed;
        require(!captured.adjustment_project_probe_correct&&
                !record_compiler_puzzle_case(untouched,lesson,block,positive,captured,"forged")&&
                untouched==seed,
            "Incomplete or forged execution evidence earned the constructor");
    }
    auto graph_only=positive_result;
    graph_only.adjustment_project_evidence.reset();
    const auto graph_capture=capture_cpp_execution(positive,graph_only);
    require(evaluate_cmake_graph(positive,graph_only,*block.cmake_graph_check).correct&&
            !record_compiler_puzzle_case(data,lesson,block,positive,graph_capture,"only graph")&&
            data==earned,
        "An authentic CMake graph alone replaced the runtime construction proof");
    auto unbound=positive_capture;
    unbound.adjustment_project_request_fingerprint.clear();
    require(!record_compiler_puzzle_case(data,lesson,block,positive,unbound,"summary only")&&
            data==earned,
        "A correct summary without its execution binding earned progress");
    std::cout << "PASS constructor source/revision/target binding, forged phases and graph-only rejection\n";

    auto copied_files=files;
    file(copied_files,"app/consulta.cpp")=copied_builder_client;
    auto copied=request(copied_files);
    const auto copied_result=run_cpp(copied);
    public_success(copied,copied_result);
    const auto copied_capture=capture_cpp_execution(copied,copied_result);
    require(!copied_capture.adjustment_project_probe_correct&&
            !record_compiler_puzzle_case(data,lesson,block,copied,copied_capture,"copied client")&&
            data==earned,
        "A client that copied the prepared report passed its independent input/observation checks");
    preserved_public_project(copied,copied_result);

    auto fixed_files=files;
    replace_once(file(fixed_files,test_path),"return fallos ? 1 : 0;","return 0;");
    auto fixed=request(fixed_files);
    const auto fixed_result=run_cpp(fixed);
    public_success(fixed,fixed_result);
    const auto fixed_capture=capture_cpp_execution(fixed,fixed_result);
    require(!fixed_capture.adjustment_project_probe_correct&&
            !record_compiler_puzzle_case(data,lesson,block,fixed,fixed_capture,"fixed checker")&&
            data==earned,
        "An insensitive fixed-return checker earned construction credit");
    preserved_public_project(fixed,fixed_result);
    std::cout << "PASS real copied-client and fixed-return-checker rejection with earlier achievement preserved\n";

    // The core-generated probes test the equivalent loop/formatting pattern.
    // Compile and run it here with the same authentic graph without repeating
    // its entire private matrix; this ordinary run deliberately grants no credit.
    auto equivalent_files=files;
    file(equivalent_files,"src/ajuste.cpp")=file(reference.project_files,"src/ajuste.cpp");
    replace_once(file(equivalent_files,"CMakeLists.txt"),"VERSION 3.20","VERSION 3.21");
    auto& equivalent_cmake=file(equivalent_files,"CMakeLists.txt");
    for(auto at=equivalent_cmake.find("/W4");at!=equivalent_cmake.npos;
            at=equivalent_cmake.find("/W4",at+3))
        equivalent_cmake.replace(at,3,"/W3");
    replace_once(file(equivalent_files,test_path),"for (const auto& caso : casos) {",
        "for (unsigned i=sizeof(casos)/sizeof(casos[0]); i>0; --i) {\n"
        "        const auto& caso=casos[i-1];");
    replace_once(file(equivalent_files,test_path),
        "        std::cout << \"Caso=\" << ++numero",
        "        ++numero;\n        if (!coincide) std::cout << \"Caso=\" << numero");
    replace_once(file(equivalent_files,test_path),
        "    std::cout << \"Fallos=\" << fallos << \"\\n\";\n","");
    auto equivalent=request(equivalent_files);
    equivalent.adjustment_project_probe.reset();
    const auto equivalent_result=run_cpp(equivalent);
    public_success(equivalent,equivalent_result);
    require(equivalent_result.execution.stdout_bytes.empty()&&
            equivalent_result.execution.stderr_bytes.empty(),
        "The equivalent checker did not preserve its legitimate silent success");
    require(evaluate_cmake_graph(equivalent,equivalent_result,*block.cmake_graph_check).correct&&
            !record_compiler_puzzle_case(data,lesson,block,equivalent,
                capture_cpp_execution(equivalent,equivalent_result),"ordinary equivalent")&&data==earned,
        "A minimum above 3.20 failed the graph or an ordinary build forged private runtime credit");
    preserved_public_project(equivalent,equivalent_result);

    auto disabled_files=equivalent_files;
    file(disabled_files,"CMakeLists.txt")+="\ntarget_compile_options(ajustes PRIVATE /W4 /w)\n";
    auto disabled=request(disabled_files);
    disabled.adjustment_project_probe.reset();
    const auto disabled_result=run_cpp(disabled);
    public_success(disabled,disabled_result);
    require(!evaluate_cmake_graph(disabled,disabled_result,*block.cmake_graph_check).correct&&
            !record_compiler_puzzle_case(data,lesson,block,disabled,
                capture_cpp_execution(disabled,disabled_result),"disabled warnings")&&data==earned,
        "An authored /W4 overridden by /w satisfied the effective warning requirement");
    preserved_public_project(disabled,disabled_result);
    std::cout << "PASS actual /W4 overridden by /w is rejected without rerunning private probes\n";

    state::StateStore storage(environment.paths.root);
    (void)storage.load();storage.save(data);
    state::StateStore reopened_storage(environment.paths.root);
    const auto reopened=reopened_storage.load();
    require(reopened==earned&&
            same_builder_draft(state::get_draft(reopened,draft_identity,before.initial_code),draft)&&
            state::string_value(reopened,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
            compiler_puzzle_completed(reopened,previous_lesson,owner(previous_lesson)),
        "Physical reopening changed the builder draft, receipt or unrelated earned extension");
    builder_completion(reopened,progress,true);
    validate_compiler_puzzle_state(reopened,progress,restored);
    state::validate_lesson_state(reopened,progress);
    const auto key="compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
        exercise_key(lesson,block)+"/"+state::sha256(reference_objective_frame(lesson,block))+"/";
    auto tampered=reopened;
    tampered.preferences["native_windows"][key+"cases"]=std::vector<std::string>{"unverified"};
    builder_completion(tampered,progress,false);
    auto reset=reopened;
    reset_compiler_puzzle(reset,lesson,block);
    builder_completion(reset,progress,false);
    require(same_builder_draft(state::get_draft(reset,draft_identity,before.initial_code),draft)&&
            state::string_value(reset,"cpp_lesson_drafts",draft_identity+"/project")==saved_project&&
            compiler_puzzle_completed(reset,previous_lesson,owner(previous_lesson)),
        "Resetting the constructor removed a historical draft or another achievement");
    require(read(fixture_path)==live_text,
        "The constructor QA rewrote the active lesson or its source map");
    std::cout << "PASS equivalent silent checker, CMake 3.21 and /W3, exact physical receipt/draft reopening, "
                 "tamper rejection and isolated constructor reset; published source unchanged\n";
}

// Keep the real active publication distinct from the pinned pre-adoption
// fixture. This function never inserts or replaces a declaration in live.
bool verify_active_adoption(const Lesson& original,const Lesson& live,
        const std::filesystem::path& projects,bool execute) {
    const auto compact=parse_lesson_progress_metadata(live.raw_json,"es");
    require(live.id==original.id&&compact.id==live.id&&
            live.state_layout.lineage==original.state_layout.lineage&&
            compact.state_layout.lineage==live.state_layout.lineage&&
            live.state_layout.signatures==original.state_layout.signatures&&
            compact.state_layout.signatures==live.state_layout.signatures,
        "The active U3L10 lost its authentic original lineage");
    for(const auto identity:{activity,previous_activity,builder_activity}) {
        const auto& old=owner(original,identity);
        const auto& current=owner(live,identity);
        const auto& lean=owner(compact,identity);
        require(exercise_key(original,old)==exercise_key(live,current)&&
                exercise_key(live,current)==exercise_key(compact,lean)&&
                draft_key("programming",original,old)==draft_key("programming",live,current)&&
                draft_key("programming",live,current)==draft_key("programming",compact,lean),
            "The active U3L10 changed an authentic owner or draft address");
        require(current.open_battery_probe==lean.open_battery_probe&&
                current.adjustment_project_probe==lean.adjustment_project_probe&&
                current.cmake_graph_check==lean.cmake_graph_check,
            "The active full and compact U3L10 contracts disagree");
        if(current.open_battery_probe||current.adjustment_project_probe||current.cmake_graph_check)
            require(current.project_files==lean.project_files&&
                    reference_objective_frame(live,current)==reference_objective_frame(compact,lean),
                "The active full and compact objectives or sources disagree");
    }

    // The oracle is independent of the current JSON. Derive it from the
    // immutable predecessor and the reviewed root declarations.
    auto expected_document=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    require(!expected_document.HasKey(L"compilerOpenBatteryChecks")&&
            !expected_document.HasKey(L"compilerCmakeGraphChecks"),
        "The pinned source is not a genuine pre-adoption fixture");
    JsonArray goals;
    goals.Append(JsonObject::Parse(winrt::to_hstring(declaration)));
    goals.Append(JsonObject::Parse(winrt::to_hstring(builder_declaration)));
    expected_document.Insert(L"compilerOpenBatteryChecks",goals);
    append_builder_declaration(expected_document,L"compilerCmakeGraphChecks",builder_graph);
    const auto expected=parse_lesson_json(winrt::to_string(expected_document.Stringify()),"es");
    const auto& extension=owner(live);
    const auto& builder=owner(live,builder_activity);
    const auto& repair=owner(live,previous_activity);
    const auto& old_extension=owner(original);
    const auto& expected_extension=owner(expected);
    const auto& expected_builder=owner(expected,builder_activity);
    const bool adopted=extension.open_battery_probe.has_value()||
        builder.adjustment_project_probe.has_value()||repair.cmake_graph_check.has_value();
    if(extension.open_battery_probe) {
        auto spec=*extension.open_battery_probe;
        const auto& oracle=*expected_extension.open_battery_probe;
        require(spec.original_cases.size()==oracle.original_cases.size()&&
                spec.original_case_numbers.size()==spec.original_cases.size(),
            "The active extension lost an independent original case");
        for(std::size_t i=0;i<spec.original_cases.size();++i) {
            const auto found=std::ranges::find(oracle.original_cases,spec.original_cases[i]);
            require(found!=oracle.original_cases.end()&&spec.original_case_numbers[i]==
                    oracle.original_case_numbers[static_cast<std::size_t>(found-oracle.original_cases.begin())],
                "An active original case has another tuple or historical number");
        }
        spec.original_cases=oracle.original_cases;
        spec.original_case_numbers=oracle.original_case_numbers;
        require(spec==oracle&&extension.output_check&&
                extension.output_check->open_battery_check&&
                extension.output_check->input_comparison==TextComparison::exact&&
                extension.output_check->cases.size()==1&&
                extension.output_check->cases.front().standard_input.empty()&&
                extension.output_check->cases.front().executable==
                    std::optional<std::string>{oracle.test_target},
            "The active extension does not retain its reviewed first-case oracle");
        if(file(extension.project_files,"CMakeLists.txt")!=
                file(old_extension.project_files,"CMakeLists.txt")) {
            require(!extension.compiler_client_epoch.empty()&&
                std::ranges::any_of(extension.compiler_case_revisions,[&](const auto& revision) {
                    if(!revision.client_revision||!revision.client_revision->cmake_recipe||
                       !revision.preserves_root_output_check)return false;
                    const auto prior=compiler_case_revision_previous_block(extension,revision);
                    return file(prior.project_files,"CMakeLists.txt")==
                        file(old_extension.project_files,"CMakeLists.txt")&&
                        prior.open_battery_probe.has_value();
                }),"The active recipe lacks its authenticated pre-adoption source epoch");
        }
    }
    if(builder.adjustment_project_probe) {
        require(builder.adjustment_project_probe==expected_builder.adjustment_project_probe&&
                builder.cmake_graph_check&&
                adjustment_project_graph_valid(*builder.adjustment_project_probe,*builder.cmake_graph_check)&&
                builder.output_check&&builder.output_check->adjustment_project_check&&
                builder.output_check->cases.size()==1&&
                builder.output_check->cases.front().standard_input.empty(),
            "The adopted constructor lost its combined component/client/checker goal");
    }
    if(!execute||!adopted)return adopted;

    // Real requests use the active owners and actual authored recipes.
    // The complete historical negative/receipt suite still runs separately.
    state::StateData data;
    state::prepare_lesson_state(data,live);
    std::uint64_t sequence=40000;
    const auto request_for=[&](const Block& block) {
        require(block.cmake.has_value(),"The active owner lacks its CMake project");
        CppRunRequest request;
        request.request_id=++sequence;request.revision=sequence;
        request.projects_directory=projects;
        request.project_key=draft_key("programming",live,block);
        request.source_utf8=block.initial_code;
        request.project_files=block.project_files;request.source_files=block.source_files;
        request.cmake=block.cmake;
        request.cmake_graph_check=block.cmake_graph_check;
        request.capture_cmake_graph=block.cmake_graph_check.has_value();
        request.open_battery_probe=block.open_battery_probe;
        request.adjustment_project_probe=block.adjustment_project_probe;
        request.compile_timeout=90s;request.execution_timeout=5s;
        return request;
    };
    if(repair.cmake_graph_check) {
        require(repair.output_check&&!repair.output_check->cases.empty()&&
                repair.cmake_graph_check->expression_repair,
            "The active repair lost its original expression or first case");
        if(const auto& observation=repair.cmake_graph_check->initial_observation) {
            auto faulty=request_for(repair);
            faulty.cmake->target=observation->executable;
            faulty.stdin_utf8=observation->standard_input;
            require(compiler_runtime_request_allowed(repair,faulty),
                "The active initial-failure request is not admissible");
            const auto failed=run_cpp(faulty);
            require(record_compiler_cmake_initial_observation(data,live,repair,faulty,failed)&&
                    !compiler_puzzle_completed(data,live,repair),
                "The active original failure did not earn only its observation: "+run_diagnostic(faulty,failed));
        }
        auto fixed=request_for(repair);
        const auto& first=repair.output_check->cases.front();
        fixed.stdin_utf8=first.standard_input;
        if(first.executable)fixed.cmake->target=*first.executable;
        const auto& expression=*repair.cmake_graph_check->expression_repair;
        replace_once(file(fixed.project_files,expression.path),"valor - superior","superior - valor");
        if(repair.cmake_graph_check->initial_observation) {
            fixed.cmake_initial_observation_prerequisite=
                compiler_cmake_initial_observation_prerequisite(data,live,repair,fixed);
            require(!fixed.cmake_initial_observation_prerequisite.empty(),
                "The active repair did not inherit its freshly observed failure");
        }
        require(compiler_runtime_request_allowed(repair,fixed),
            "The active first-case repair request is not admissible");
        const auto result=run_cpp(fixed);
        const auto captured=capture_cpp_execution(fixed,result);
        require(record_compiler_puzzle_case(data,live,repair,fixed,captured,"QA active repair")&&
                compiler_puzzle_case_count(data,live,repair)==1&&
                compiler_puzzle_completed(data,compact,owner(compact,previous_activity)),
            "The actual integrated first repair case failed: "+run_diagnostic(fixed,result));
        std::cout<<"PASS active first repair case with its actual graph and prerequisites\n";
    }
    if(extension.open_battery_probe) {
        // Keep the actual learner request and its receipts tied to the
        // published expansion, which does not own the construction graph.
        auto unchanged=request_for(extension);
        require(compiler_runtime_request_allowed(extension,unchanged),
            "The active expansion rejected its exact published project");
        const auto initial=run_cpp(unchanged);
        public_success(unchanged,initial);
        const auto seed=data;
        require(!record_compiler_puzzle_case(data,live,extension,unchanged,
                    capture_cpp_execution(unchanged,initial),"QA unchanged expansion")&&data==seed,
            "The active eleven-case checker earned new extension credit");
        auto extended=request_for(extension);
        file(extended.project_files,test_path)=extended_driver(extension,
            "    if (!comprobar(12, 3, 3, 7, 59, true, 0)) { ++fallos; }\n");
        require(compiler_runtime_request_allowed(extension,extended),
            "The active expansion rejected a legitimate new independent comparison");
        const auto result=run_cpp(extended);
        public_success(extended,result);
        const auto capture=capture_cpp_execution(extended,result);
        require(result.open_battery_evidence&&
                result.open_battery_evidence->runs.size()==37&&capture.open_battery_probe_correct&&
                record_compiler_puzzle_case(data,live,extension,extended,capture,"QA active extension")&&
                compiler_puzzle_case_count(data,live,extension)==1&&
                compiler_puzzle_completed(data,compact,owner(compact)),
            "The active expansion lost its independently executed comparisons: "+
                run_diagnostic(extended,result));
        preserved_public_project(extended,result);

        // The File API alone cannot prove explicitly authored compile
        // features, options or include scopes. Request the authenticated
        // construction graph in a separate QA execution of the identical
        // submitted project. Do not attach a foreign grading requirement to
        // the expansion or use this result to record a learner receipt.
        require(expected_builder.cmake_graph_check.has_value(),
            "The pinned construction graph oracle is missing");
        auto graph_request=extended;
        graph_request.request_id=++sequence;graph_request.revision=sequence;
        graph_request.open_battery_probe.reset();
        graph_request.capture_cmake_graph=true;
        graph_request.cmake_graph_check=expected_builder.cmake_graph_check;
        const auto graph_result=run_cpp(graph_request);
        public_success(graph_request,graph_result);
        const auto graph=evaluate_cmake_graph(graph_request,graph_result,
            *expected_builder.cmake_graph_check);
        require(graph.correct,
            "The exact active expansion lost its shared authored CMake graph: "+
                graph.reason+run_diagnostic(graph_request,graph_result));
        preserved_public_project(graph_request,graph_result);
        std::cout<<"PASS actual adopted expansion: shared CMake graph, 37 phases and one first-case receipt\n";
    }
    if(builder.adjustment_project_probe) {
        auto built=request_for(builder);
        built.cmake->target=builder.adjustment_project_probe->test_target;
        // This is a solution submitted to the original blank CMake editor,
        // not a rewrite of a published recipe. Use the reviewed MSVC options.
        auto recipe=file(old_extension.project_files,"CMakeLists.txt");
        for(const auto target:{"ajustes","consultar_ajustes","comprobar_ajustes"})
            replace_once(recipe,"target_compile_options("+std::string(target)+
                " PRIVATE -Wall -Wextra -Wpedantic)",
                "target_compile_options("+std::string(target)+" PRIVATE /W4 /permissive-)");
        file(built.project_files,"CMakeLists.txt")=std::move(recipe);
        file(built.project_files,"src/ajuste.cpp")=builder_library;
        file(built.project_files,"app/consulta.cpp")=file(old_extension.project_files,"app/consulta.cpp");
        file(built.project_files,test_path)=builder_checker;
        require(compiler_runtime_request_allowed(builder,built),
            "The active constructor rejected its complete shared project");
        const auto result=run_cpp(built);
        public_success(built,result);
        const auto capture=capture_cpp_execution(built,result);
        require(capture.adjustment_project_probe_correct&&capture.cmake_graph_correct&&
                record_compiler_puzzle_case(data,live,builder,built,capture,"QA active constructor")&&
                compiler_puzzle_case_count(data,live,builder)==1&&
                compiler_puzzle_completed(data,compact,owner(compact,builder_activity)),
            "The actual integrated constructor did not earn its own first-case receipt: "+
                capture.adjustment_project_probe_feedback+run_diagnostic(built,result));
        preserved_public_project(built,result);
        std::cout<<"PASS actual adopted constructor: combined component, client and checker proof\n";
    }
    const auto reopened=state::decode(state::encode(data));
    require(reopened==data,"The adopted U3L10 receipts changed on reopening");
    if(extension.open_battery_probe)completion(reopened,compact,true);
    if(builder.adjustment_project_probe)builder_completion(reopened,compact,true);
    if(repair.cmake_graph_check)
        require(compiler_puzzle_completed(reopened,compact,owner(compact,previous_activity)),
            "Reopening lost the active repair receipt");
    return true;
}

} // namespace

int main(int argc,char** argv) {
    const bool active_only=argc==3&&std::string_view(argv[1])=="--active";
    if(argc!=2&&!active_only)return 2;
    try {
        const LaunchOptions options{ProfileId::Codex,
            L"ob-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        // Keep room for CMakeScratch/TryCompile while retaining the path's
        // spaces, ampersand, parentheses and the isolated PID/tick namespace.
        const auto projects=environment.paths.projects/L"OB & (QA)";
        const auto fixture_path=std::filesystem::path(argv[active_only?2:1]);
        const auto live_text=read(fixture_path);
        const auto live=parse_lesson_json(live_text,"es");
        const auto original_text=qa::published_adoption_source(
            std::filesystem::path(build::source_root),lesson_relative);
        const auto original=parse_lesson_json(original_text,"es");
        const bool adopted=verify_active_adoption(original,live,projects,active_only);
        require(read(fixture_path)==live_text,"Active U3L10 QA modified its fixture");
        if(active_only) {
            if(!adopted) {
                std::cout<<"SKIP active U3L10 runtime: no reviewed owner has been adopted yet\n";
                return 77;
            }
            return 0;
        }
        const auto& before=owner(original);
        require(before.standard_input&&!before.standard_input->empty()&&
                before.project_files.size()==5&&before.cmake&&
                before.cmake->target=="comprobar_ajustes",
            "The authentic initial project or historical client batch changed");
        const auto position=stable_block_position(original,activity);
        require(position.has_value(),"The authentic owner lost its stable map address");
        auto document=JsonObject::Parse(winrt::to_hstring(original_text));
        const auto original_document=JsonObject::Parse(winrt::to_hstring(original_text));
        auto revised_files=before.project_files;
        auto revised_recipe=portable_warning_recipe(file(before.project_files,"CMakeLists.txt"));
        if(revised_recipe==file(before.project_files,"CMakeLists.txt"))
            revised_recipe+="\n# QA: retain the already portable warning profile.\n";
        file(revised_files,"CMakeLists.txt")=revised_recipe;
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
        const auto published_lesson=parse_lesson_json(reviewed,"es");
        const auto published_progress=parse_lesson_progress_metadata(reviewed,"es");
        const auto& published_block=owner(published_lesson);
        const auto& published_restored=owner(published_progress);
        require(original.pages.size()==published_lesson.pages.size()&&
                published_lesson.pages.size()==published_progress.pages.size()&&
                original.state_layout.raw_json==published_lesson.state_layout.raw_json&&
                original.state_layout.raw_json==published_progress.state_layout.raw_json&&
                original.state_layout.signatures==published_lesson.state_layout.signatures&&
                original.state_layout.signatures==published_progress.state_layout.signatures&&
                original.state_layout.lineage==published_lesson.state_layout.lineage&&
                original.state_layout.lineage==published_progress.state_layout.lineage,
            "The QA root goal changed the original stable state layout");
        require(published_block.open_battery_probe&&published_restored.open_battery_probe&&
                published_block.open_battery_probe==published_restored.open_battery_probe&&
                published_block.open_battery_probe->original_cases.size()==11&&
                published_block.open_battery_probe->original_case_numbers==
                    std::vector<int>({1,2,3,4,5,6,7,8,9,10,11})&&
                published_block.output_check&&published_restored.output_check&&
                published_block.output_check->open_battery_check&&published_restored.output_check->open_battery_check&&
                published_block.output_check->cases.size()==1&&published_restored.output_check->cases.size()==1&&
                published_block.output_check->input_comparison==TextComparison::exact&&
                published_restored.output_check->input_comparison==TextComparison::exact&&
                published_block.output_check->cases.front().standard_input.empty()&&
                published_restored.output_check->cases.front().standard_input.empty()&&
                published_block.output_check->cases.front().executable==
                    std::optional<std::string>{published_block.open_battery_probe->test_target}&&
                published_restored.output_check->cases.front().executable==
                    published_block.output_check->cases.front().executable,
            "Full/progress loading did not require the original numbers and empty checker input");
        {
            // The declaration lists tuples; historical numbers belong to the
            // authenticated comparator calls, not their JSON array positions.
            auto reordered=JsonObject::Parse(winrt::to_hstring(reviewed));
            for(const auto& value:reordered.GetNamedArray(L"compilerOpenBatteryChecks")) {
                const auto entry=value.GetObject();
                if(entry.GetNamedString(L"activity")!=winrt::to_hstring(activity))continue;
                const auto cases=entry.GetNamedArray(L"originalCases");
                JsonArray reversed;
                for(auto i=cases.Size();i>0;--i)reversed.Append(cases.GetAt(i-1));
                entry.Insert(L"originalCases",reversed);
            }
            const auto source=winrt::to_string(reordered.Stringify());
            const auto full=parse_lesson_json(source,"es");
            const auto compact=parse_lesson_progress_metadata(source,"es");
            for(const auto* model:{&full,&compact}) {
                const auto& mapped=owner(*model);
                require(mapped.open_battery_probe&&
                        mapped.open_battery_probe->original_case_numbers==
                            std::vector<int>({11,10,9,8,7,6,5,4,3,2,1})&&
                        mapped.project_files==published_block.project_files,
                    "Reordering declared tuples detached their historical case numbers");
            }
        }
        require(published_block.project_files==before.project_files&&
                published_restored.project_files==before.project_files&&
                published_block.initial_code==before.initial_code&&published_restored.initial_code==before.initial_code&&
                published_block.standard_input==before.standard_input&&published_restored.standard_input==before.standard_input&&
                published_block.cmake==before.cmake&&published_restored.cmake==before.cmake&&
                published_block.stable_id==before.stable_id&&published_restored.stable_id==before.stable_id&&
                published_block.address.page==before.address.page&&published_block.address.block==before.address.block&&
                published_restored.address.page==before.address.page&&published_restored.address.block==before.address.block&&
                exercise_key(original,before)==exercise_key(published_lesson,published_block)&&
                draft_key("programming",original,before)==draft_key("programming",published_lesson,published_block)&&
                reference_objective_frame(published_lesson,published_block)==
                    reference_objective_frame(published_progress,published_restored),
            "The new goal changed source, initial input, target, identity or full/progress evidence");
        {
            // An edited curricular recipe is rejected until its exact source
            // transition and complete editorial history are supplied.
            auto incompatible=JsonObject::Parse(winrt::to_hstring(reviewed));
            const auto edited=incompatible.GetNamedArray(L"pages").
                GetObjectAt(static_cast<std::uint32_t>(position->page)).
                GetNamedArray(L"blocks").GetObjectAt(static_cast<std::uint32_t>(position->block));
            for(const auto& value:edited.GetNamedArray(L"projectFiles")) {
                const auto item=value.GetObject();
                if(item.GetNamedString(L"path")==L"CMakeLists.txt")
                    item.Insert(L"initialCode",JsonValue::CreateStringValue(
                        winrt::to_hstring(revised_recipe)));
            }
            rejected_recipe(incompatible,"An unauthenticated CMake edit passed a native loader");
        }
        const auto revised_text=recipe_revision_json(published_lesson,revised_recipe);
        const auto lesson=parse_lesson_json(revised_text,"es");
        const auto progress=parse_lesson_progress_metadata(revised_text,"es");
        for(const bool remove_history:{false,true}) {
            auto invalid=JsonObject::Parse(winrt::to_hstring(revised_text));
            const auto layout=invalid.GetNamedObject(L"stateLayout");
            const auto records=layout.GetNamedArray(L"compilerCaseRevisions");
            if(remove_history) {
                records.RemoveAtEnd();
                if(!records.Size())layout.Remove(L"compilerCaseRevisions");
            }else records.GetObjectAt(records.Size()-1).Insert(L"historySha256",
                JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
            rejected_recipe(invalid,"A missing or altered recipe history passed a native loader");
        }
        const auto& block=owner(lesson);
        const auto& restored=owner(progress);
        require(block.project_files==revised_files&&restored.project_files==revised_files&&
                block.raw_json!=published_block.raw_json&&
                !JsonObject::Parse(winrt::to_hstring(block.raw_json)).HasKey(L"outputCheck")&&
                block.initial_code==published_block.initial_code&&
                restored.initial_code==published_restored.initial_code&&
                block.standard_input==before.standard_input&&restored.standard_input==before.standard_input&&
                block.cmake==before.cmake&&restored.cmake==before.cmake&&
                block.open_battery_probe==published_block.open_battery_probe&&
                restored.open_battery_probe==published_restored.open_battery_probe&&
                block.output_check&&block.output_check->open_battery_check&&
                restored.output_check&&restored.output_check->open_battery_check&&
                block.output_check->cases.size()==1&&restored.output_check->cases.size()==1&&
                block.output_check->cases.front().standard_input.empty()&&
                restored.output_check->cases.front().standard_input.empty()&&
                reference_objective_frame(lesson,block)==reference_objective_frame(progress,restored)&&
                reference_objective_frame(lesson,block)!=reference_objective_frame(published_lesson,published_block),
            "The authenticated recipe lost its independent root goal, source boundary or objective epoch");
        for(const auto* model:{&lesson,&progress}) {
            const auto& current=owner(*model);
            require(model->state_layout.signatures==original.state_layout.signatures&&
                    model->state_layout.lineage==original.state_layout.lineage&&
                    current.stable_id==before.stable_id&&current.address.page==before.address.page&&
                    current.address.block==before.address.block&&
                    exercise_key(*model,current)==exercise_key(original,before)&&
                    draft_key("programming",*model,current)==draft_key("programming",original,before)&&
                    current.compiler_case_revisions.size()==published_block.compiler_case_revisions.size()+1,
                "A recipe transition changed a historical identity or lost its append-only journal");
            const auto& revision=current.compiler_case_revisions.back();
            require(revision.client_revision&&revision.client_revision->cmake_recipe&&
                    revision.preserves_root_output_check&&
                    revision.revised_client_epoch==revision.record_sha256&&
                    current.compiler_client_epoch==revision.revised_client_epoch&&
                    current.compiler_client_epoch!=published_block.compiler_client_epoch,
                "The recipe did not receive its own authenticated source epoch");
            const auto archived=compiler_case_revision_previous_block(current,revision);
            const auto after=compiler_case_revision_after_block(current,revision);
            require(archived.project_files==before.project_files&&
                    archived.output_check&&archived.output_check->open_battery_check&&
                    archived.output_check->cases.size()==1&&
                    reference_objective_frame(*model,archived)==
                        reference_objective_frame(published_lesson,published_block)&&
                    reference_objective_frame(*model,after)==reference_objective_frame(*model,current),
                "Recipe reconstruction changed the historical root objective or its real case count");
        }
        auto former_input_contract=block;
        former_input_contract.output_check->cases.front().standard_input=*before.standard_input;
        require(reference_objective_frame(lesson,former_input_contract)!=
                reference_objective_frame(lesson,block),
            "The corrected input reused the former objective's evidence identity");
        require(read(fixture_path)==live_text,
            "Preparing the historical QA fixture rewrote the active source");

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
        file(previous_files,"CMakeLists.txt")=revised_recipe;
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
        public_success(prerequisite,prerequisite_result);
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
        public_success(initial,initial_result);
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
            "Configuring claimed runtime credit or failed to generate the real project: "+
                run_diagnostic(first,configured));
        const auto built=phase(first,CppRunRequest::Action::build);
        require(built.error_utf8.empty()&&built.compilation.succeeded()&&
                built.execution.status==CppPhaseStatus::not_started&&
                !built.open_battery_evidence&&!compiler_runtime_request_allowed(block,first)&&
                !record_compiler_puzzle_case(data,lesson,block,first,
                    capture_cpp_execution(first,built),"build")&&data==seed&&
                inspect_cpp_project(first).built,
            "Building claimed runtime credit or failed to leave the executable ready: "+
                run_diagnostic(first,built));
        const auto first_result=phase(first,CppRunRequest::Action::execute);
        require(compiler_runtime_request_allowed(block,first),
            "Executing the configured and built target still failed its curricular gate");
        const auto first_capture=capture_cpp_execution(first,first_result);
        checked_battery(first,first_result,first_capture,block,first_added,true);
        require(record_compiler_puzzle_case(data,lesson,block,first,first_capture,"checked extension"),
            "The real configure/build/execute sequence did not earn a sealed receipt");
        completion(data,lesson,true);
        require(compiler_puzzle_case_count(data,lesson,block)==1&&
                compiler_puzzle_case_count(data,progress,restored)==1,
            "The recipe review required another visible case or lost its first-case count");
        const auto earned=data;
        require(!compiler_puzzle_completed(earned,published_lesson,published_block)&&
                !compiler_puzzle_completed(earned,published_progress,published_restored),
            "The recipe receipt credited the earlier published CMake objective");
        verify_recipe_return(lesson,earned,environment.paths.root/L"recipe-return");
        std::cout << "PASS actual configure build execute, 37 private phases and first-case completion\n";

        auto old_input=first;
        old_input.request_id=++sequence;
        old_input.stdin_utf8=*before.standard_input;
        require(!compiler_runtime_request_allowed(block,old_input),
            "The historical client input was accepted by the checker request gate");
        const auto old_input_result=run_cpp(old_input);
        public_success(old_input,old_input_result);
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
            "    for (int numero = 3 * 7 + 16; numero < 38; ++numero) {\n"
            "        if (!comprobar(numero, 0, -3, -4, -29, false, -29)) { ++fallos; }\n"
            "    }\n");
        auto second=request(second_source,CppRunRequest::Action::compile_run);
        require(compiler_runtime_request_allowed(block,second),
            "The existing compile_run API was rejected");
        const auto second_result=run_cpp(second);
        const auto second_capture=capture_cpp_execution(second,second_result);
        checked_battery(second,second_result,second_capture,block,second_added,true,37);
        auto second_data=seed;
        require(record_compiler_puzzle_case(second_data,lesson,block,second,second_capture,"second extension")&&
                compiler_program_frame(first)!=compiler_program_frame(second)&&second_data!=earned,
            "A different valid ternary case did not earn its own source-bound receipt");
        completion(second_data,progress,true);
        std::cout << "PASS independent rejected-input extension with computed case 37 in an equivalent loop\n";

        constexpr OpenBatteryCall nonzero_added{1,3,7,59};
        const auto exact_source=extended_driver(block,
            "    if (!comprobar(12, 1, 3, 7, 59, true, 2)) { ++fallos; }\n");
        auto exact=request(exact_source,CppRunRequest::Action::compile_run);
        require(compiler_runtime_request_allowed(block,exact),
            "The exact nonzero adjustment comparison was rejected before its runtime probe");
        const auto exact_result=run_cpp(exact);
        const auto exact_capture=capture_cpp_execution(exact,exact_result);
        checked_battery(exact,exact_result,exact_capture,block,nonzero_added,true);
        auto exact_data=seed;
        require(record_compiler_puzzle_case(exact_data,lesson,block,exact,exact_capture,
                    "exact nonzero destination")&&exact_data!=seed,
            "An exact comparison did not earn its own nonzero-destination receipt");
        completion(exact_data,progress,true);
        for(const int expected:{2,99}) {
            auto source=exact_source;
            replace_once(source,"obtenido == ajuste_esperado",
                "bool(obtenido) == bool(ajuste_esperado)");
            if(expected!=2)
                replace_once(source,"comprobar(12, 1, 3, 7, 59, true, 2)",
                    "comprobar(12, 1, 3, 7, 59, true, 99)");
            auto candidate=request(source,CppRunRequest::Action::compile_run);
            require(compiler_runtime_request_allowed(block,candidate),
                "The truthiness regression must reach the independent runtime probe");
            const auto result=run_cpp(candidate);
            const auto capture=capture_cpp_execution(candidate,result);
            checked_battery(candidate,result,capture,block,nonzero_added,false);
            const auto& runs=result.open_battery_evidence->runs;
            require(std::ranges::all_of(runs,[](const auto& run) {
                        return run.mutation==OpenBatteryMutation::corrupt_nonzero_destination||
                            run.exit_code==(run.mutation==OpenBatteryMutation::baseline?0:1);
                    })&&std::ranges::any_of(runs,[&](const auto& run) {
                        return run.mutation==OpenBatteryMutation::corrupt_nonzero_destination&&
                            run.target==nonzero_added&&run.exit_code==0;
                    }),
                "The regression did not isolate the former zero/nonzero coverage gap");
            require(!record_compiler_puzzle_case(data,lesson,block,candidate,capture,
                        "truthiness instead of exact destination")&&data==earned,
                "A truthiness comparison earned credit or erased the earlier exact receipt");
        }
        std::cout << "PASS exact nonzero adjustment earns credit while bool comparisons with expected 2 or 99 fail\n";

        const auto reject_number_change=[&](std::string source,std::string_view description) {
            auto candidate=request(source,CppRunRequest::Action::compile_run);
            require(compiler_runtime_request_allowed(block,candidate),
                "Case-number QA must reach the actual independent runtime probe");
            const auto result=run_cpp(candidate);
            public_success(candidate,result);
            const auto capture=capture_cpp_execution(candidate,result);
            require(!result.open_battery_evidence&&!capture.open_battery_probe_correct&&
                    result.execution.stdout_bytes.find("Fallos=0")!=std::string::npos&&
                    evaluate_cpp_output_check(*block.output_check,capture,candidate.revision).verdict==
                        OutputVerdict::incorrect&&
                    !record_compiler_puzzle_case(data,lesson,block,candidate,capture,description)&&
                    data==earned,
                "A reused or renamed case number earned credit or erased earlier success");
            preserved_public_project(candidate,result);
        };
        auto reused_number=first_source;
        replace_once(reused_number,"comprobar(12, 3, 3, 7, 59, true, 0)",
            "comprobar(11, 3, 3, 7, 59, true, 0)");
        reject_number_change(std::move(reused_number),"new case reusing historical number 11");
        auto renamed_history=first_source;
        replace_once(renamed_history,"comprobar(1, 2, 3, 7, 77, true, 1)",
            "comprobar(99, 2, 3, 7, 77, true, 1)");
        reject_number_change(std::move(renamed_history),"historical case renamed to 99");
        std::cout << "PASS real duplicate and renamed case numbers fail after the private baseline\n";

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
        require(!compiler_puzzle_completed(reopened,published_lesson,published_block)&&
                !compiler_puzzle_completed(reopened,published_progress,published_restored),
            "Reopening promoted the recipe receipt to the earlier published objective");
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
        require(after==live_text,
            "The historical runtime test rewrote the active curricular source");
        std::cout << "PASS physical reopening, tamper rejection, isolated reset and unchanged published source SHA\n";
        verify_adjustment_constructor(fixture_path,live_text,original,data,lesson);
        return 0;
    }catch(const std::exception& error) {
        std::cerr << "FAIL open battery action runtime: " << error.what() << '\n';
        return 1;
    }catch(const winrt::hresult_error& error) {
        std::cerr << "FAIL open battery action runtime: " << winrt::to_string(error.message()) << '\n';
        return 1;
    }
}


