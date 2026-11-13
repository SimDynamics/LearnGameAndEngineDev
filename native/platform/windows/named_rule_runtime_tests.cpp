#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"

#include <filesystem>
#include <windows.h>
#include <chrono>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;

void require(bool value,std::string_view why) {
    if(!value)throw std::runtime_error(std::string(why));
}
CppRunRequest request_of(const Block& owner) {
    CppRunRequest request;
    request.source_utf8=owner.initial_code;
    request.stdin_utf8=owner.standard_input.value_or("");
    request.project_files=owner.project_files;
    request.source_files=owner.source_files;
    request.cmake=owner.cmake;
    if(owner.named_rule_source_check)
        request.named_rule_probe=owner.named_rule_source_check->execution_probe;
    request.action=CppRunRequest::Action::compile_run;
    return request;
}
void change_project(CppRunRequest& request,std::string_view path,std::string text) {
    for(auto& file:request.project_files)
        if(file.first==path){file.second=std::move(text);return;}
    throw std::runtime_error("The original named-rule project source is missing");
}
void authored_root_roundtrip(const Lesson& original) {
    using namespace winrt::Windows::Data::Json;
    // A prospective Work declaration is injected in memory; none of the
    // published curriculum files or stored student progress are rewritten.
    constexpr auto declarations=R"json([
        {"activity":"block_0002_0004","path":"componente/src/cajas.cpp",
         "executionProbe":{"harness":"pruebas/cajas_casos.cpp","header":"envio/cajas.h",
           "cases":[{"function":"cajas_para","input":0,"expected":0},
                    {"function":"cajas_para","input":1,"expected":1},
                    {"function":"cajas_para","input":4,"expected":1},
                    {"function":"cajas_para","input":19,"expected":5},
                    {"function":"cajas_para","input":20,"expected":5}]},
         "rules":[{"function":"cajas_para","value":4,
                   "purposeWords":["capacidad","plaza","cupo"],"minUses":2}]},
        {"activity":"block_0005_0006","path":"componente/src/cajas.cpp",
         "editablePaths":["componente/publico/envio/cajas.h","pruebas/cajas_casos.cpp"],
         "executionProbe":{"harness":"pruebas/cajas_casos.cpp","header":"envio/cajas.h",
           "cases":[{"function":"cajas_para","input":0,"expected":0},
                    {"function":"cajas_para","input":6,"expected":1},
                    {"function":"cajas_para","input":7,"expected":2},
                    {"function":"cajas_para","input":19,"expected":4},
                    {"function":"cajas_para","input":20,"expected":4},
                    {"function":"llega_a_tiempo","input":4,"expected":1},
                    {"function":"llega_a_tiempo","input":5,"expected":0}]},
         "rules":[{"function":"cajas_para","value":6,
                   "purposeWords":["capacidad","plaza","cupo"],"minUses":2},
                  {"function":"llega_a_tiempo","value":4,
                   "purposeWords":["plazo","dias","tiempo"],"minUses":1}]},
        {"activity":"block_0007_0004","path":"",
         "rules":[{"function":"precio_total","value":7,
                   "purposeWords":["precio","costo","tarifa"],"minUses":1},
                  {"function":"alcanza_minimo","value":5,
                   "purposeWords":["minimo","umbral","cantidad"],"minUses":1}]},
        {"activity":"block_0008_0007","path":"",
         "rules":[{"function":"precio_total","value":7,
                   "purposeWords":["precio","costo","tarifa"],"minUses":1},
                  {"function":"alcanza_minimo","value":5,
                   "purposeWords":["minimo","umbral","cantidad"],"minUses":1}]}
    ])json";
    auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    root.Insert(L"compilerNamedRuleChecks",
        JsonArray::Parse(winrt::to_hstring(declarations)));
    const auto reviewed=winrt::to_string(root.Stringify());
    const auto after=parse_lesson_json(reviewed,"es");
    const auto metadata=parse_lesson_progress_metadata(reviewed,"es");
    require(original.state_layout.lineage==after.state_layout.lineage&&
        metadata.state_layout.lineage==after.state_layout.lineage&&
        original.state_layout.signatures==after.state_layout.signatures&&
        metadata.state_layout.signatures==after.state_layout.signatures,
        "A root-only source role changed historical state or layout");
    for(const auto [page,index]:{
            std::pair<std::size_t,std::size_t>{2,4},{5,6},{7,4},{8,7}}) {
        const auto& old=original.pages.at(page).blocks.at(index);
        const auto& current=after.pages.at(page).blocks.at(index);
        const auto& read_only=metadata.pages.at(page).blocks.at(index);
        require(!old.named_rule_source_check&&current.named_rule_source_check&&
            read_only.named_rule_source_check&&
            current.named_rule_source_check->requirements.size()==
                read_only.named_rule_source_check->requirements.size()&&
            current.named_rule_source_check==read_only.named_rule_source_check&&
            current.stable_id==old.stable_id&&
            exercise_key(original,old)==exercise_key(after,current)&&
            draft_key("programming",original,old)==
                draft_key("programming",after,current)&&
            reference_objective_frame(original,old)!=
                reference_objective_frame(after,current)&&
            !compiler_runtime_request_allowed(current,request_of(current)),
            "A root-authored named-rule objective was lost or credited the original");
    }
    // The native loader must reject an unknown or duplicated authored owner.
    auto invalid=root.GetNamedArray(L"compilerNamedRuleChecks");
    invalid.Append(invalid.GetObjectAt(0));
    bool rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(root.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader accepted duplicate named-rule owners");
}

void project_examples(const Lesson& lesson) {
    const auto& original=lesson.pages.at(2).blocks.at(4);
    require(original.stable_id=="block_0002_0004"&&original.output_check&&original.cmake,
        "The real U3 named-rule four-place editor changed");
    auto owner=original;
    owner.named_rule_source_check=NamedRuleSourceCheck{
        "componente/src/cajas.cpp",
        {NamedRuleRequirement{"cajas_para",4,{"capacidad","plaza","cupo"},2}}};
    require(reference_objective_frame(lesson,owner)!=reference_objective_frame(lesson,original)&&
        exercise_key(lesson,owner)==exercise_key(lesson,original)&&
        draft_key("programming",lesson,owner)==draft_key("programming",lesson,original),
        "Named-rule source proof reused old stdout receipts or changed historical draft coordinates");
    auto submitted=request_of(owner);
    require(!compiler_runtime_request_allowed(owner,submitted),
        "The original four-place expression won credit without a named rule");
    change_project(submitted,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"

int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(compiler_runtime_request_allowed(owner,submitted),
        "An equivalent local capacity cannot be graded by the editor");
    auto wrong=submitted;
    change_project(wrong,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + 3) / 4;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,wrong),
        "Declaring but not using the capacity satisfied the runtime gate");
    wrong=submitted;
    wrong.project_files.emplace_back("extra.cpp","int injected=0;");
    require(!compiler_runtime_request_allowed(owner,wrong),
        "An extra project file earned the source evidence");
    wrong=submitted;
    change_project(wrong,"pruebas/cajas_casos.cpp","int main(){return 0;}\n");
    require(!compiler_runtime_request_allowed(owner,wrong),
        "An unapproved replacement for the original test battery earned credit");
    wrong=submitted;
    wrong.cmake->target="consultar_cajas";
    require(!compiler_runtime_request_allowed(owner,wrong),
        "A different executable borrowed the named-rule receipt");
    const auto& dual=lesson.pages.at(5).blocks.at(6);
    require(dual.stable_id=="block_0005_0006"&&dual.output_check&&dual.cmake,
        "The real U3 capacity-and-deadline owner moved");
    owner=dual;
    owner.named_rule_source_check=NamedRuleSourceCheck{
        "componente/src/cajas.cpp",{
            NamedRuleRequirement{"cajas_para",6,{"capacidad","plaza","cupo"},2},
            NamedRuleRequirement{"llega_a_tiempo",4,{"plazo","tiempo","dias"},1}}};
    submitted=request_of(owner);
    require(!compiler_runtime_request_allowed(owner,submitted),
        "The original shared limit satisfied independent authored agreements");
    change_project(submitted,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{6};
    return (piezas + capacidad - 1) / capacidad;
}
bool llega_a_tiempo(int dias) {
    const int plazo{4};
    return dias <= plazo;
}
)cpp");
    require(compiler_runtime_request_allowed(owner,submitted),
        "Independent six-place capacity and four-day deadline were rejected");
    owner.named_rule_source_check->editable_paths={
        "componente/publico/envio/cajas.h","pruebas/cajas_casos.cpp"};
    auto revised=submitted;
    change_project(revised,"pruebas/cajas_casos.cpp",
        "int main(){return 0;}\n");
    require(compiler_runtime_request_allowed(owner,revised),
        "An expressly permitted new test battery failed the source edit gate");
    change_project(revised,"CMakeLists.txt","project(Injected)\n");
    require(!compiler_runtime_request_allowed(owner,revised),
        "Changing unauthorized CMake instructions borrowed source credit");
}
std::string replace_unique(std::string source,std::string_view before,
        std::string_view after) {
    const auto at=source.find(before);
    require(at!=source.npos&&source.find(before,at+before.size())==source.npos,
        "The original named-rule editor source no longer matches its reviewed fixture");
    source.replace(at,before.size(),after);
    return source;
}
void actual_named_rule_msvc(const Lesson& lesson,const std::filesystem::path& projects) {
    // The original student's complete five-case program is a stronger
    // end-to-end check than a mocked stdout transcript.
    const auto& original=lesson.pages.at(8).blocks.at(7);
    require(original.stable_id=="block_0008_0007"&&original.output_check&&
        original.output_check->cases.size()==1,
        "The original U3 named-rule five-case objective changed");
    auto owner=original;
    owner.named_rule_source_check=NamedRuleSourceCheck{"",{
        NamedRuleRequirement{"precio_total",7,{"precio","costo","tarifa"},1},
        NamedRuleRequirement{"alcanza_minimo",5,{"minimo","cantidad","umbral"},1}}};
    auto passing=request_of(owner);
    passing.request_id=901;passing.revision=301;
    passing.projects_directory=projects;
    passing.compile_timeout=std::chrono::seconds(60);
    passing.execution_timeout=std::chrono::seconds(8);
    passing.source_utf8=replace_unique(std::move(passing.source_utf8),
        "const int acuerdo{7};\n\n","");
    passing.source_utf8=replace_unique(std::move(passing.source_utf8),
        "return articulos * acuerdo;",
        "const int precio_unitario{7};\n    return articulos * precio_unitario;");
    passing.source_utf8=replace_unique(std::move(passing.source_utf8),
        "return articulos >= acuerdo;",
        "const int minimo_articulos{5};\n    return articulos >= minimo_articulos;");
    require(compiler_runtime_request_allowed(owner,passing),
        "A repaired, purpose-named student source failed the rubric");
    const auto real=run_cpp(passing);
    require(real.error_utf8.empty()&&real.compilation.succeeded()&&
        real.execution.succeeded(),
        "MSVC did not execute the real repaired U3 student program: "+
        real.error_utf8+real.compilation.stderr_bytes+real.execution.stderr_bytes);
    const auto captured=capture_cpp_execution(passing,real);
    require(evaluate_cpp_output_check(*owner.output_check,captured,passing.revision).verdict==
        OutputVerdict::correct,
        "The repaired program did not preserve the actual five-case authored output");
    state::StateData saved;
    state::prepare_lesson_state(saved,lesson);
    require(!compiler_puzzle_completed(saved,lesson,owner),
        "A new source-aware objective inherited completion from a blank profile");
    // Compile a genuinely wrong source: its stdout still agrees with the
    // five-case target, but the declared minimum has no useful source use.
    auto shortcut=passing;
    shortcut.request_id=902;shortcut.revision=302;
    shortcut.source_utf8=replace_unique(std::move(shortcut.source_utf8),
        "return articulos >= minimo_articulos;","return articulos >= 5;");
    require(!compiler_runtime_request_allowed(owner,shortcut),
        "An unused named constant was awarded source credit");
    const auto bad=run_cpp(shortcut);
    require(bad.error_utf8.empty()&&bad.compilation.succeeded()&&
        bad.execution.succeeded(),
        "The shortcut control failed to execute: "+bad.error_utf8+
        bad.compilation.stderr_bytes);
    const auto bad_capture=capture_cpp_execution(shortcut,bad);
    require(evaluate_cpp_output_check(*owner.output_check,bad_capture,shortcut.revision).verdict==
        OutputVerdict::correct,
        "The control shortcut does not actually produce identical visible results");
    require(!record_compiler_puzzle_case(saved,lesson,owner,shortcut,bad_capture,"wrong")&&
        !compiler_puzzle_completed(saved,lesson,owner),
        "Identical MSVC output incorrectly earned a source-aware completion receipt");
    require(record_compiler_puzzle_case(saved,lesson,owner,passing,captured,"correct")&&
        compiler_puzzle_completed(saved,lesson,owner),
        "The first compiled valid case failed to unlock the authentic exercise");
    const auto reopened=state::decode(state::encode(saved));
    require(compiler_puzzle_completed(reopened,lesson,owner)&&
        compiler_puzzle_case_count(reopened,lesson,owner)==1,
        "Source-aware completion was not preserved after the state roundtrip");
    std::cout<<"PASS real U3 named-rule MSVC: identical output rejected without code evidence\n";
}

void single_file_examples(const Lesson& lesson) {
    const NamedRuleSourceCheck rules{"",{
        NamedRuleRequirement{"precio_total",7,{"precio","costo","tarifa","credito"},1},
        NamedRuleRequirement{"alcanza_minimo",5,{"minimo","umbral","cantidad"},1}}};
    for(const auto [page,index]:{
        std::pair<std::size_t,std::size_t>{7,4},{8,7}}) {
        const auto& original=lesson.pages.at(page).blocks.at(index);
        require(original.type==BlockType::cpp_compiler&&original.output_check&&
            original.project_files.empty()&&original.cmake==std::nullopt,
            "The U3 single-file rule editor changed");
        auto owner=original;owner.named_rule_source_check=rules;
        auto submitted=request_of(owner);
        require(!compiler_runtime_request_allowed(owner,submitted),
            "A source with a shared limit or magic numbers won the named-rule objective");
        submitted.source_utf8=R"cpp(#include <iostream>

int precio_total(int articulos) {
    const int precio_creditos{7};
    return articulos * precio_creditos;
}
bool alcanza_minimo(int articulos) {
    const int minimo_articulos{5};
    return articulos >= minimo_articulos;
}
int main() {
    std::cout << precio_total(5) << " " << alcanza_minimo(5);
    return 0;
}
)cpp";
        require(compiler_runtime_request_allowed(owner,submitted),
            "Purpose-bearing independent price and minimum failed source validation");
        auto forged=submitted;
        forged.source_utf8+="\n// declared another rule in a comment\n";
        // Comments do not change the verdict, because source evidence comes
        // from analyzed function definitions rather than textual matches.
        require(compiler_runtime_request_allowed(owner,forged),
            "Harmless prose comments altered a real source prerequisite");
        forged=submitted;
        forged.source_utf8.replace(
            forged.source_utf8.find("return articulos >= minimo_articulos;"),
            std::string("return articulos >= minimo_articulos;").size(),
            "return articulos >= 5;");
        require(!compiler_runtime_request_allowed(owner,forged),
            "The unused minimum falsely satisfied the new compiler rubric");
        forged=submitted;
        forged.project_files.emplace_back("main.cpp",forged.source_utf8);
        require(!compiler_runtime_request_allowed(owner,forged),
            "A foreign project was admitted to the single-file source goal");
    }
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected the existing published U3 lesson path");
        const auto lesson=load_lesson(std::filesystem::path(argv[1]),"es");
        require(lesson.id=="programming_practice_tools__comprobaciones_repetibles__nombrar_reglas_y_separar_acuerdos",
            "The named-rules source test loaded another lesson");
        authored_root_roundtrip(lesson);
        project_examples(lesson);
        single_file_examples(lesson);
        const LaunchOptions options{ProfileId::Codex,
            L"named-rule-"+std::to_wstring(GetCurrentProcessId())+
            L"-"+std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        actual_named_rule_msvc(lesson,
            environment.paths.projects/L"U3 named rules real MSVC");
        std::cout<<"PASS original named-rule editors: source constraints, historic receipt and draft isolation\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL original named-rule source gate: "<<error.what()<<'\n';
        return 1;
    }
}
