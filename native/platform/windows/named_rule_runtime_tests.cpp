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
         "commentOnlyPaths":["componente/publico/envio/cajas.h"],
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
         "executionProbe":{"harness":"","header":"",
           "cases":[{"function":"precio_total","input":11,"expected":77},
                    {"function":"precio_total","input":5,"expected":35},
                    {"function":"alcanza_minimo","input":4,"expected":0},
                    {"function":"alcanza_minimo","input":5,"expected":1}]},
         "rules":[{"function":"precio_total","value":7,
                   "purposeWords":["precio","costo","tarifa"],"minUses":1},
                  {"function":"alcanza_minimo","value":5,
                   "purposeWords":["minimo","umbral","cantidad"],"minUses":1}]},
        {"activity":"block_0008_0007","path":"",
         "executionProbe":{"harness":"","header":"",
           "cases":[{"function":"precio_total","input":11,"expected":77},
                    {"function":"precio_total","input":5,"expected":35},
                    {"function":"alcanza_minimo","input":4,"expected":0},
                    {"function":"alcanza_minimo","input":5,"expected":1}]},
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
    auto no_probe=JsonObject::Parse(winrt::to_hstring(reviewed));
    no_probe.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(1)
        .Remove(L"executionProbe");
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(no_probe.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The loader accepted an editable CMake harness without a probe");
    auto wrong_single_header=JsonObject::Parse(winrt::to_hstring(reviewed));
    wrong_single_header.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(3)
        .GetNamedObject(L"executionProbe")
        .Insert(L"header",JsonValue::CreateStringValue(L"envio/forgery.h"));
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(wrong_single_header.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader accepted an injected single-file header");
    auto duplicate_single=JsonObject::Parse(winrt::to_hstring(reviewed));
    auto repeated=duplicate_single.GetNamedArray(L"compilerNamedRuleChecks")
        .GetObjectAt(3).GetNamedObject(L"executionProbe")
        .GetNamedArray(L"cases");
    repeated.Append(repeated.GetObjectAt(0));
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(duplicate_single.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader accepted duplicate single-file probe arguments");
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
    // The same valid solution may spell the local and its intermediates
    // with east-const. This must be accepted by the actual Windows gate.
    auto const_intermediates=submitted;
    change_project(const_intermediates,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    int const capacidad{4};
    int const ajuste = capacidad - 1;
    int const cajas = (piezas + ajuste) / capacidad;
    return cajas;
}
)cpp");
    require(compiler_runtime_request_allowed(owner,const_intermediates),
        "Windows rejected east-const intermediate values with the same contract");
    auto ignored_intermediate=const_intermediates;
    change_project(ignored_intermediate,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    int const capacidad{4};
    int const observacion = capacidad + capacidad;
    return (piezas + capacidad - 1) / 4;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,ignored_intermediate),
        "An unused east-const variable supplied a fake second result use");
    auto zero_group=submitted;
    change_project(zero_group,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + 3) / 4 + 0 * (capacidad + capacidad);
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,zero_group),
        "Two zeroed references in a parenthesized expression earned credit");
    auto valid_with_zero_group=submitted;
    change_project(valid_with_zero_group,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + capacidad - 1) / capacidad +
           0 * ((capacidad + capacidad) / 2);
}
)cpp");
    require(compiler_runtime_request_allowed(owner,valid_with_zero_group),
        "Harmless zeroed extra expressions must not erase real capacity uses");
    auto unreachable_result=submitted;
    change_project(unreachable_result,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + 3) / 4;
    return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,unreachable_result),
        "Windows credited a named rule from unreachable code after return");
    auto guarded_result=submitted;
    change_project(guarded_result,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (piezas <= 0) return 0;
    return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(compiler_runtime_request_allowed(owner,guarded_result),
        "Windows rejected a valid early guard before the named calculation");
    auto conditional_shortcut=submitted;
    change_project(conditional_shortcut,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (piezas > 10) return (piezas + 3) / 4;
    return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,conditional_shortcut),
        "A hardcoded conditional return borrowed credit from the named fallthrough");
    auto braced_shortcut=submitted;
    change_project(braced_shortcut,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (piezas > 10) {
        return (piezas + 3) / 4;
    }
    return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,braced_shortcut),
        "A braced hardcoded return borrowed evidence from a later named return");
    auto split_guards=submitted;
    change_project(split_guards,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
    if (piezas <= 10) return (piezas + 3) / 4;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,split_guards),
        "Complementary guards concealed a hardcoded calculation in Windows");
    auto split_branches=submitted;
    change_project(split_branches,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
    else return (piezas + 3) / 4;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,split_branches),
        "Windows accepted an if/else with one hardcoded branch");
    auto legitimate_branches=submitted;
    change_project(legitimate_branches,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
    else return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(compiler_runtime_request_allowed(owner,legitimate_branches),
        "Windows rejected the same named computation in both if/else branches");
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
    owner.named_rule_source_check->comment_only_paths={
        "componente/publico/envio/cajas.h"};
    require(!compiler_runtime_request_allowed(owner,submitted),
        "An editable C++ test harness earned credit without independent probes");
    owner.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
        "pruebas/cajas_casos.cpp","envio/cajas.h",{
            {"cajas_para",0,0},{"cajas_para",6,1},{"cajas_para",19,4},
            {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}}};
    submitted.named_rule_probe=owner.named_rule_source_check->execution_probe;
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
    owner.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
        "","",{{"precio_total",11,77},{"precio_total",5,35},
               {"alcanza_minimo",4,0},{"alcanza_minimo",5,1}}};
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
    auto location_sensitive=passing;
    location_sensitive.source_utf8=replace_unique(
        std::move(location_sensitive.source_utf8),
        "return articulos * precio_unitario;",
        "return articulos * precio_unitario + "
        "(__LINE__ == 5 ? (articulos == 11 ? 1 : 0) : 0);");
    require(!compiler_runtime_request_allowed(owner,location_sensitive)&&
        !named_rule_single_file_probe_source(
            *owner.named_rule_source_check->execution_probe,
            location_sensitive.source_utf8),
        "The Windows source gate accepted code that detects independent compilation");
    auto harmless_prose=passing;
    harmless_prose.source_utf8+="\n// __LINE__ and source_location are documentation.\n";
    require(compiler_runtime_request_allowed(owner,harmless_prose),
        "A harmless comment was mistaken for compilation-environment behavior");
    const auto real=run_cpp(passing);
    require(real.error_utf8.empty()&&real.compilation.succeeded()&&
        real.execution.succeeded(),
        "MSVC did not execute the real repaired U3 student program: "+
        real.error_utf8+real.compilation.stderr_bytes+real.execution.stderr_bytes);
    const auto captured=capture_cpp_execution(passing,real);
    require(real.named_rule_probe&&captured.named_rule_probe_correct&&
        real.named_rule_probe->execution.stderr_bytes==
            named_rule_probe_completion_marker,
        "A valid MSVC single-file program failed the completed independent probe");
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
    // A student may forge a five-case report by omitting the eleventh-item
    // defect. The separate test driver must call the functions directly.
    auto forged=passing;
    forged.request_id=903;forged.revision=303;
    forged.source_utf8=replace_unique(std::move(forged.source_utf8),
        "return articulos * precio_unitario;",
        "return articulos * precio_unitario + (articulos == 11 ? 1 : 0);");
    require(compiler_runtime_request_allowed(owner,forged),
        "Independent probes must not substitute for source-based checks");
    const auto forged_run=run_cpp(forged);
    require(forged_run.error_utf8.empty()&&forged_run.compilation.succeeded()&&
        forged_run.execution.succeeded(),
        "The forged five-case output control did not compile or run");
    const auto forged_capture=capture_cpp_execution(forged,forged_run);
    require(evaluate_cpp_output_check(*owner.output_check,forged_capture,forged.revision).verdict==
        OutputVerdict::correct&&!forged_capture.named_rule_probe_correct,
        "A function with correct visible output passed an independent 11-item check");
    require(!record_compiler_puzzle_case(saved,lesson,owner,forged,forged_capture,
        "forged visible output")&&!compiler_puzzle_completed(saved,lesson,owner),
        "An incorrect function earned credit from a forged printed report");
    // A buggy function can call std::exit(0) on an independent test input.
    // That used to look like successful execution without checking later
    // cases. The generated driver's final witness must now be present.
    auto early_exit=passing;
    early_exit.request_id=904;early_exit.revision=304;
    early_exit.source_utf8="#include <cstdlib>\n"+early_exit.source_utf8;
    early_exit.source_utf8=replace_unique(std::move(early_exit.source_utf8),
        "return articulos * precio_unitario;",
        "if (articulos == 11) std::exit(0);\n"
        "    return articulos * precio_unitario;");
    require(!compiler_runtime_request_allowed(owner,early_exit)&&
        !named_rule_single_file_probe_source(
            *owner.named_rule_source_check->execution_probe,
            early_exit.source_utf8),
        "An early exit must be rejected before generating an independent probe");
    auto aliased_exit=passing;
    aliased_exit.source_utf8="#include <cstdlib>\n"+aliased_exit.source_utf8;
    aliased_exit.source_utf8=replace_unique(std::move(aliased_exit.source_utf8),
        "return articulos * precio_unitario;",
        "if (articulos == 11) {\n"
        "        auto finish = &std::exit; finish(0);\n"
        "    }\n"
        "    return articulos * precio_unitario;");
    require(!compiler_runtime_request_allowed(owner,aliased_exit)&&
        !named_rule_single_file_probe_source(
            *owner.named_rule_source_check->execution_probe,
            aliased_exit.source_utf8),
        "A function-pointer alias bypassed the early-exit source protection");
    auto harmless_exit_comment=passing;
    harmless_exit_comment.source_utf8+="\n// std::exit(0) is not executable code.\n";
    require(compiler_runtime_request_allowed(owner,harmless_exit_comment),
        "A harmless early-exit comment rejected the valid student source");
    const auto premature=run_cpp(early_exit);
    require(premature.error_utf8.empty()&&premature.compilation.succeeded()&&
        premature.execution.succeeded()&&!premature.named_rule_probe,
        "An early-exit source unexpectedly received independent probe evidence");
    const auto premature_capture=capture_cpp_execution(early_exit,premature);
    require(evaluate_cpp_output_check(*owner.output_check,premature_capture,
            early_exit.revision).verdict==OutputVerdict::correct&&
        !premature_capture.named_rule_probe_correct,
        "A zero exit from the student function bypassed remaining probe cases");
    require(!record_compiler_puzzle_case(saved,lesson,owner,early_exit,
            premature_capture,"early exit")&&!compiler_puzzle_completed(saved,lesson,owner),
        "An early exit awarded completion without the full independent probe");
    auto tampered=real;
    require(tampered.named_rule_probe.has_value(),
        "The independent single-file probe did not record its source digest");
    tampered.named_rule_probe->project_sha256="0";
    require(!capture_cpp_execution(passing,tampered).named_rule_probe_correct,
        "A result from other submitted source bytes was credited");
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

void real_independent_cmake_probe(const Lesson& lesson,
        const std::filesystem::path& projects) {
    const auto& original=lesson.pages.at(5).blocks.at(6);
    require(original.stable_id=="block_0005_0006"&&original.cmake&&
        original.output_check&&original.output_check->cases.size()==1,
        "The original U3 two-agreement CMake objective is unavailable");
    auto owner=original;
    owner.named_rule_source_check=NamedRuleSourceCheck{
        "componente/src/cajas.cpp",{
            NamedRuleRequirement{"cajas_para",6,{"capacidad","plaza","cupo"},2},
            NamedRuleRequirement{"llega_a_tiempo",4,{"plazo","tiempo","dias"},1}
        }};
    owner.named_rule_source_check->editable_paths={
        "componente/publico/envio/cajas.h","pruebas/cajas_casos.cpp"};
    owner.named_rule_source_check->comment_only_paths={
        "componente/publico/envio/cajas.h"};
    owner.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
        "pruebas/cajas_casos.cpp","envio/cajas.h",{
            {"cajas_para",0,0},{"cajas_para",6,1},
            {"cajas_para",7,2},{"cajas_para",19,4},
            {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}
        }};
    auto correct=request_of(owner);
    correct.request_id=1101;correct.revision=410;
    correct.project_key=draft_key("programming",lesson,original);
    correct.projects_directory=projects;
    correct.compile_timeout=std::chrono::seconds(100);
    correct.execution_timeout=std::chrono::seconds(8);
    change_project(correct,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{6};
    return (piezas + capacidad - 1) / capacidad;
}
bool llega_a_tiempo(int dias) {
    const int plazo{4};
    return dias <= plazo;
}
)cpp");
    for(auto& file:correct.project_files) {
        if(file.first=="componente/publico/envio/cajas.h") {
            file.second=replace_unique(std::move(file.second),
                "cajas de 4 plazas","cajas de 6 plazas");
        }else if(file.first=="pruebas/cajas_casos.cpp") {
            file.second=replace_unique(std::move(file.second),
                "comprobar(20, 5)","comprobar(20, 4)");
            file.second=replace_unique(std::move(file.second),
                "comprobar(5, 2)","comprobar(5, 1)");
            file.second=replace_unique(std::move(file.second),
                "comprobar(17, 5)","comprobar(17, 3)");
            file.second=replace_unique(std::move(file.second),
                "    if (!comprobar(17, 3)) { correcto = false; }\n",
                "    if (!comprobar(17, 3)) { correcto = false; }\n"
                "    if (!comprobar(6, 1)) { correcto = false; }\n"
                "    if (!comprobar(7, 2)) { correcto = false; }\n");
        }
    }
    require(compiler_runtime_request_allowed(owner,correct),
        "A student's valid source/battery edits did not pass authored file permissions");
    auto altered_header=correct;
    for(auto& file:altered_header.project_files)
        if(file.first=="componente/publico/envio/cajas.h") {
            file.second=replace_unique(std::move(file.second),
                "int cajas_para(int piezas);","int cajas_para(long piezas);");
        }
    require(!compiler_runtime_request_allowed(owner,altered_header),
        "A signature change hidden in an editable header earned source credit");
    altered_header=correct;
    for(auto& file:altered_header.project_files)
        if(file.first=="componente/publico/envio/cajas.h") {
            file.second=replace_unique(std::move(file.second),
                "int cajas_para(int piezas);",
                "#define cajas_para(n) ((n + 5) / 6)\nint cajas_para(int piezas);");
        }
    require(!compiler_runtime_request_allowed(owner,altered_header),
        "An edited header macro diverted the independent function probe");
    const auto real=run_cpp(correct);
    require(real.error_utf8.empty()&&real.compilation.succeeded()&&
        real.execution.succeeded(),
        "Real MSVC CMake could not build or run the amended U3 battery: "+
        real.error_utf8+
        "; configuration="+std::to_string(static_cast<int>(
            real.configuration.status))+
        "; compilation="+std::to_string(static_cast<int>(
            real.compilation.status))+
        "; execution="+std::to_string(static_cast<int>(
            real.execution.status))+
        "; compile_exit="+std::to_string(
            real.compilation.exit_code.value_or(999))+
        "; run_exit="+std::to_string(
            real.execution.exit_code.value_or(999))+
        "; configure_stderr="+real.configuration.stderr_bytes+
        "; compile_stderr="+real.compilation.stderr_bytes+
        "; run_stderr="+real.execution.stderr_bytes);
    const auto fresh=capture_cpp_execution(correct,real);
    require(fresh.named_rule_probe_correct&&real.named_rule_probe&&
        real.named_rule_probe->execution.succeeded()&&
        real.named_rule_probe->execution.stderr_bytes==
            named_rule_probe_completion_marker,
        "The correct student functions did not finish the independent CMake harness");
    require(evaluate_cpp_output_check(*owner.output_check,fresh,correct.revision).verdict
        ==OutputVerdict::correct,
        "The valid amended CMake battery did not produce all published comparisons");

    // The editable harness can print the expected text directly. It is not
    // evidence of the functions: the independent executable must catch the
    // deliberately defective but correctly named 19-piece case.
    auto forged=correct;
    forged.request_id=1102;forged.revision=411;
    std::string source;
    for(const auto& file:forged.project_files)
        if(file.first=="componente/src/cajas.cpp")source=file.second;
    require(!source.empty(),"The named-rule CMake file disappeared");
    // The negative function keeps the two source-level capacity uses in
    // each meaningful return path, but deliberately computes a wrong result
    // for 19 pieces. Source inspection must allow it through, and only the
    // independent CMake probe may reject its runtime behavior.
    source=replace_unique(std::move(source),
        "    return (piezas + capacidad - 1) / capacidad;",
        "    if (piezas == 19) return (piezas + capacidad - 1) / capacidad - 3;\n"
        "    return (piezas + capacidad - 1) / capacidad;");
    change_project(forged,"componente/src/cajas.cpp",std::move(source));
    const auto expected=owner.output_check->cases.front().outputs.front();
    const auto forged_test="#include <iostream>\nint main(){std::cout << R\"LGAED("+
        expected+")LGAED\";return 0;}\n";
    change_project(forged,"pruebas/cajas_casos.cpp",forged_test);
    require(compiler_runtime_request_allowed(owner,forged),
        "The independent probe test must isolate runtime behavior, not reject source");
    const auto fake=run_cpp(forged);
    require(fake.error_utf8.empty()&&fake.compilation.succeeded()&&
        fake.execution.succeeded(),
        "The negative forged-harness control failed to compile or execute");
    const auto fake_capture=capture_cpp_execution(forged,fake);
    require(!fake_capture.named_rule_probe_correct,
        "An edited harness hid an incorrect 19-piece computation");
    // Even a legitimate result cannot be reused with an altered probe hash
    // or a different project source. The worker's evidence is source-bound.
    auto mixed=real;
    require(mixed.named_rule_probe.has_value(),
        "An independent CMake probe did not capture its own executable");
    mixed.named_rule_probe->source_sha256="0";
    require(!capture_cpp_execution(correct,mixed).named_rule_probe_correct,
        "The capture accepted a probe belonging to other source bytes");
    mixed=real;
    mixed.named_rule_probe->project_sha256="0";
    require(!capture_cpp_execution(correct,mixed).named_rule_probe_correct,
        "The capture accepted a probe from another CMake project");
    require(evaluate_cpp_output_check(*owner.output_check,fake_capture,forged.revision).verdict
        ==OutputVerdict::correct,
        "The negative control did not actually reproduce the visible expected stdout");
    require(!fake_capture.named_rule_probe_correct,
        "An altered, faulty CMake function passed the independent test driver");
    state::StateData saved;
    state::prepare_lesson_state(saved,lesson);
    require(!record_compiler_puzzle_case(saved,lesson,owner,forged,fake_capture,
             "fake matches visible stdout")&&
        !compiler_puzzle_completed(saved,lesson,owner),
        "A fake, student-editable harness earned a curriculum receipt");
    // The student library can exit(0) when a hidden probe calls it,
    // even though the editable test executable prints the expected report.
    // Its zero exit status must not be accepted without the final witness.
    auto exit_forged=forged;
    // Isolate this adversarial build from the preceding incorrect project.
    // Otherwise an incremental object can exercise the old 19-piece return
    // instead of the requested std::exit(0) path and mask this assertion.
    exit_forged.project_key+="/qa-early-exit";
    exit_forged.request_id=1103;exit_forged.revision=412;
    for(auto& file:exit_forged.project_files)
        if(file.first=="componente/src/cajas.cpp") {
            file.second="#include <cstdlib>\n"+
                replace_unique(std::move(file.second),
                    "    if (piezas == 19) return (piezas + capacidad - 1) / capacidad - 3;",
                    "    if (piezas == 19) std::exit(0);");
        }
    require(!compiler_runtime_request_allowed(owner,exit_forged),
        "A CMake function with premature exit(0) passed the source gate");
    const auto early_run=run_cpp(exit_forged);
    const auto probe_status=early_run.named_rule_probe?
        std::to_string(static_cast<int>(early_run.named_rule_probe->execution.status))+
        "/"+std::to_string(early_run.named_rule_probe->execution.exit_code.value_or(999))+
        "/stderr="+early_run.named_rule_probe->execution.stderr_bytes+
        "/compile="+std::to_string(early_run.named_rule_probe->compilation.exit_code.value_or(999))
        :std::string("absent");
    require(early_run.error_utf8.empty()&&early_run.compilation.succeeded()&&
        early_run.execution.succeeded()&&early_run.named_rule_probe&&
        early_run.named_rule_probe->execution.succeeded()&&
        early_run.named_rule_probe->execution.stderr_bytes.empty(),
        "The controlled CMake early exit did not bypass the probe's final line"
        "; runner-error="+early_run.error_utf8+
        "; compilation="+std::to_string(early_run.compilation.exit_code.value_or(999))+
        "; execution="+std::to_string(early_run.execution.exit_code.value_or(999))+
        "; probe="+probe_status);
    const auto early_capture=capture_cpp_execution(exit_forged,early_run);
    require(evaluate_cpp_output_check(*owner.output_check,early_capture,
            exit_forged.revision).verdict==OutputVerdict::correct&&
        !early_capture.named_rule_probe_correct,
        "The CMake witness check accepted an early exit(0)");
    require(!record_compiler_puzzle_case(saved,lesson,owner,exit_forged,
            early_capture,"premature CMake exit")&&
        !compiler_puzzle_completed(saved,lesson,owner),
        "A zero exit before all CMake probes improperly unlocked the editor");
    // The completion witness is intentionally public. A deliberately
    // hostile library can imitate it before exiting, so the source gate
    // must refuse to grade calls to process-exit functions.
    auto spoofed_witness=exit_forged;
    // The forged-witness control must also execute its own freshly built
    // functions, not a leftover binary from the early-exit control.
    spoofed_witness.project_key+="/qa-forged-witness";
    spoofed_witness.request_id=1104;spoofed_witness.revision=413;
    for(auto& file:spoofed_witness.project_files)
        if(file.first=="componente/src/cajas.cpp") {
            file.second="#include <cstdio>\n"+
                replace_unique(std::move(file.second),
                    "    if (piezas == 19) std::exit(0);",
                    "    if (piezas == 19) {\n"
                    "        std::fputs(\"LGAED_NAMED_RULE_PROBE_COMPLETE_V1\", stderr);\n"
                    "        std::exit(0);\n"
                    "    }");
        }
    require(!compiler_runtime_request_allowed(owner,spoofed_witness),
        "A project function could fake the known final witness and exit");
    const auto spoof_run=run_cpp(spoofed_witness);
    require(spoof_run.error_utf8.empty()&&spoof_run.compilation.succeeded()&&
        spoof_run.execution.succeeded()&&spoof_run.named_rule_probe&&
        spoof_run.named_rule_probe->execution.succeeded()&&
        spoof_run.named_rule_probe->execution.stderr_bytes==
            named_rule_probe_completion_marker,
        "The deliberate fixed-witness spoof control did not reproduce");
    const auto spoof_capture=capture_cpp_execution(spoofed_witness,spoof_run);
    require(!record_compiler_puzzle_case(saved,lesson,owner,spoofed_witness,
            spoof_capture,"spoofed fixed witness")&&
        !compiler_puzzle_completed(saved,lesson,owner),
        "A forged completion marker bypassed the CMake source gate");
    require(record_compiler_puzzle_case(saved,lesson,owner,correct,fresh,"correct")&&
        compiler_puzzle_completed(saved,lesson,owner),
        "The first real case with verified independent probes did not grant progress");
    const auto reopened=state::decode(state::encode(saved));
    require(compiler_puzzle_completed(reopened,lesson,owner)&&
        compiler_puzzle_case_count(reopened,lesson,owner)==1,
        "Independent CMake progress was not kept across reopening");
    std::cout<<"PASS original U3 CMake: forged matching stdout rejected by independent function tests\n";
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
        real_independent_cmake_probe(lesson,
            // Keep CMakeScratch/TryCompile and MSVC object paths below the
            // verified Windows CMAKE_OBJECT_PATH_MAX limit on hosted runners.
            environment.paths.projects/L"u3-cmake");
        std::cout<<"PASS original named-rule editors: source constraints, historic receipt and draft isolation\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL original named-rule source gate: "<<error.what()<<'\n';
        return 1;
    }
}
