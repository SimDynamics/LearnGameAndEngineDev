#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/named_rule_check.hpp"
#include "core/named_rule_constant_variation_check.hpp"

#include <algorithm>
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
std::string require_named_rule_rejection(const Block& owner,
        const CppRunRequest& request,std::string_view function,std::string_view why) {
    require(owner.named_rule_source_check.has_value(),
        "The diagnostic fixture has no named-rule contract");
    const auto& named=*owner.named_rule_source_check;
    std::string_view source=request.source_utf8;
    if(!named.path.empty()) {
        const auto file=std::find_if(request.project_files.begin(),request.project_files.end(),
            [&](const auto& entry){return entry.first==named.path;});
        require(file!=request.project_files.end(),
            "The diagnostic fixture lost its declared project source");
        source=file->second;
    }
    const auto analyzed=analyze_named_rule_sources(source,named.requirements);
    require(!analyzed.source_evidence&&!analyzed.reason.empty()&&
        analyzed.reason.find(function)!=std::string::npos,
        "The diagnostic fixture did not isolate the intended function: "+std::string(why));
    std::string reason="previous rejection";
    require(!compiler_runtime_request_allowed(owner,request,&reason),why);
    const auto prefix=named.path.empty()?std::string{}:"Archivo "+named.path+": ";
    require(reason==prefix+analyzed.reason,
        "Windows lost the specific named-rule source reason: "+std::string(why));
    return reason;
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
                    {"function":"llega_a_tiempo","input":5,"expected":0}],
           "batteryCheck":{"cases":[{"function":"cajas_para","input":0,"expected":0},
                       {"function":"cajas_para","input":4,"expected":1},
                       {"function":"cajas_para","input":8,"expected":2},
                       {"function":"cajas_para","input":20,"expected":4},
                       {"function":"cajas_para","input":1,"expected":1},
                       {"function":"cajas_para","input":5,"expected":1},
                       {"function":"cajas_para","input":17,"expected":3},
                       {"function":"cajas_para","input":6,"expected":1},
                       {"function":"cajas_para","input":7,"expected":2},
                       {"function":"llega_a_tiempo","input":4,"expected":1},
                       {"function":"llega_a_tiempo","input":5,"expected":0}],
             "failureExitCode":1}},
         "rules":[{"function":"cajas_para","value":6,
                   "purposeWords":["capacidad","plaza","cupo","caja"],"minUses":2},
                  {"function":"llega_a_tiempo","value":4,
                   "purposeWords":["plazo","dias","tiempo"],"minUses":1}]},
        {"activity":"block_0007_0004","path":"",
         "executionProbe":{"harness":"","header":"",
           "cases":[{"function":"precio_total","input":11,"expected":77},
                    {"function":"precio_total","input":5,"expected":35},
                    {"function":"alcanza_minimo","input":4,"expected":0},
                    {"function":"alcanza_minimo","input":5,"expected":1}],
           "batteryCheck":{"cases":[{"function":"precio_total","input":0,"expected":0},
                       {"function":"precio_total","input":4,"expected":28},
                       {"function":"precio_total","input":5,"expected":35},
                       {"function":"precio_total","input":6,"expected":42},
                       {"function":"precio_total","input":10,"expected":70},
                       {"function":"alcanza_minimo","input":0,"expected":0},
                       {"function":"alcanza_minimo","input":4,"expected":0},
                       {"function":"alcanza_minimo","input":5,"expected":1},
                       {"function":"alcanza_minimo","input":6,"expected":1},
                       {"function":"alcanza_minimo","input":10,"expected":1}],
             "failureExitCode":1}},
         "rules":[{"function":"precio_total","value":7,
                   "purposeWords":["precio","costo","tarifa","credito"],"minUses":1},
                  {"function":"alcanza_minimo","value":5,
                   "purposeWords":["minimo","umbral","cantidad"],"minUses":1}]},
        {"activity":"block_0008_0007","path":"",
         "executionProbe":{"harness":"","header":"",
           "cases":[{"function":"precio_total","input":11,"expected":77},
                    {"function":"precio_total","input":5,"expected":35},
                    {"function":"alcanza_minimo","input":4,"expected":0},
                    {"function":"alcanza_minimo","input":5,"expected":1}],
           "batteryCheck":{"cases":[{"function":"precio_total","input":0,"expected":0},
                       {"function":"precio_total","input":4,"expected":28},
                       {"function":"precio_total","input":5,"expected":35},
                       {"function":"precio_total","input":6,"expected":42},
                       {"function":"precio_total","input":10,"expected":70},
                       {"function":"alcanza_minimo","input":0,"expected":0},
                       {"function":"alcanza_minimo","input":4,"expected":0},
                       {"function":"alcanza_minimo","input":5,"expected":1},
                       {"function":"alcanza_minimo","input":6,"expected":1},
                       {"function":"alcanza_minimo","input":10,"expected":1}],
             "failureExitCode":1}},
         "rules":[{"function":"precio_total","value":7,
                   "purposeWords":["precio","costo","tarifa","credito"],"minUses":1},
                  {"function":"alcanza_minimo","value":5,
                   "purposeWords":["minimo","umbral","cantidad"],"minUses":1}]}
    ])json";
    // load_lesson has already authenticated any published root declaration.
    // Derive the historical comparison only in memory, so later editorial
    // activation neither overwrites real content nor makes this QA obsolete.
    auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    if(root.HasKey(L"compilerNamedRuleChecks"))root.Remove(L"compilerNamedRuleChecks");
    const auto historical=parse_lesson_json(winrt::to_string(root.Stringify()),"es");
    auto contracts=JsonArray::Parse(winrt::to_hstring(declarations));
    const auto variations=JsonArray::Parse(LR"json([
        [{"function":"cajas_para","value":6,"cases":[
            {"function":"cajas_para","input":0,"expected":0},
            {"function":"cajas_para","input":7,"expected":2},
            {"function":"cajas_para","input":19,"expected":4}]}],
        [{"function":"cajas_para","value":4,"cases":[
            {"function":"cajas_para","input":0,"expected":0},
            {"function":"cajas_para","input":6,"expected":2},
            {"function":"cajas_para","input":19,"expected":5},
            {"function":"llega_a_tiempo","input":4,"expected":1},
            {"function":"llega_a_tiempo","input":5,"expected":0}]}],
        [{"function":"precio_total","value":9,"cases":[
            {"function":"precio_total","input":11,"expected":99},
            {"function":"precio_total","input":5,"expected":45},
            {"function":"alcanza_minimo","input":4,"expected":0},
            {"function":"alcanza_minimo","input":5,"expected":1}]}],
        [{"function":"precio_total","value":9,"cases":[
            {"function":"precio_total","input":11,"expected":99},
            {"function":"precio_total","input":5,"expected":45},
            {"function":"alcanza_minimo","input":4,"expected":0},
            {"function":"alcanza_minimo","input":5,"expected":1}]},
         {"function":"alcanza_minimo","value":6,"cases":[
            {"function":"precio_total","input":11,"expected":77},
            {"function":"precio_total","input":5,"expected":35},
            {"function":"alcanza_minimo","input":4,"expected":0},
            {"function":"alcanza_minimo","input":5,"expected":0}]}]
    ])json");
    for(std::uint32_t i=0;i<contracts.Size();++i)
        contracts.GetObjectAt(i).GetNamedObject(L"executionProbe")
            .Insert(L"constantVariations",variations.GetArrayAt(i));
    root.Insert(L"compilerNamedRuleChecks",contracts);
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
        const auto& old=historical.pages.at(page).blocks.at(index);
        const auto& current=after.pages.at(page).blocks.at(index);
        const auto& read_only=metadata.pages.at(page).blocks.at(index);
        require(!old.named_rule_source_check&&current.named_rule_source_check&&
            read_only.named_rule_source_check&&
            current.named_rule_source_check->requirements.size()==
                read_only.named_rule_source_check->requirements.size()&&
            current.named_rule_source_check==read_only.named_rule_source_check&&
            current.named_rule_source_check->execution_probe&&
            current.named_rule_source_check->execution_probe->constant_variations&&
            current.named_rule_source_check->execution_probe->constant_variations->source_path==
                current.named_rule_source_check->path&&
            current.named_rule_source_check->execution_probe->constant_variations->requirements==
                current.named_rule_source_check->requirements&&
            current.stable_id==old.stable_id&&
            exercise_key(historical,old)==exercise_key(after,current)&&
            draft_key("programming",historical,old)==
                draft_key("programming",after,current)&&
            reference_objective_frame(historical,old)!=
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
    auto unchanged=JsonObject::Parse(winrt::to_hstring(reviewed));
    unchanged.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(0)
        .GetNamedObject(L"executionProbe").GetNamedArray(L"constantVariations")
        .GetObjectAt(0).Insert(L"value",JsonValue::CreateNumberValue(4));
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(unchanged.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader accepted a constant variation with no value change");
    auto coupled=JsonObject::Parse(winrt::to_hstring(reviewed));
    coupled.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(2)
        .GetNamedObject(L"executionProbe").GetNamedArray(L"constantVariations")
        .GetObjectAt(0).GetNamedArray(L"cases").GetObjectAt(3)
        .Insert(L"expected",JsonValue::CreateNumberValue(0));
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(coupled.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader accepted a price variation that also changes the minimum");
    const auto accepts_value=[&](std::wstring_view json) {
        auto candidate=JsonObject::Parse(winrt::to_hstring(reviewed));
        candidate.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(0)
            .GetNamedObject(L"executionProbe").GetNamedArray(L"constantVariations")
            .GetObjectAt(0).Insert(L"value",JsonValue::Parse(winrt::hstring(json)));
        try {(void)parse_lesson_json(winrt::to_string(candidate.Stringify()),"es");return true;}
        catch(const std::exception&) {return false;}
    };
    require(accepts_value(L"6.0"),
        "The Windows loader rejected an integral JSON number for a constant variation");
    for(const auto invalid_value:{L"true",L"6.5",L"2147483648",L"-2147483649"})
        require(!accepts_value(invalid_value),
            "The Windows loader accepted a noninteger or out-of-int32 constant variation");
    auto extra=JsonObject::Parse(winrt::to_hstring(reviewed));
    extra.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(0)
        .GetNamedObject(L"executionProbe").GetNamedArray(L"constantVariations")
        .GetObjectAt(0).Insert(L"unused",JsonValue::CreateBooleanValue(true));
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(extra.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader ignored an unknown constant-variation field");
    auto empty=JsonObject::Parse(winrt::to_hstring(reviewed));
    empty.GetNamedArray(L"compilerNamedRuleChecks").GetObjectAt(0)
        .GetNamedObject(L"executionProbe").Insert(L"constantVariations",JsonArray{});
    rejected=false;
    try {(void)parse_lesson_json(winrt::to_string(empty.Stringify()),"es");}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"The Windows loader accepted an explicitly empty constant-variation list");
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
    std::string rejection=require_named_rule_rejection(owner,submitted,"cajas_para",
        "The original four-place expression won credit without a named rule");
    change_project(submitted,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"

int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + capacidad - 1) / capacidad;
}
)cpp");
    require(compiler_runtime_request_allowed(owner,submitted,&rejection)&&rejection.empty(),
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
    rejection="previous source rejection";
    require(compiler_runtime_request_allowed(owner,const_intermediates,&rejection)&&rejection.empty(),
        "Windows rejected east-const intermediate values with the same contract");
    // C++ accepts both positions of constexpr and requires only actual
    // dependencies in the result. The Windows request gate must agree with
    // the portable source checker for this equivalent authored solution.
    auto constexpr_intermediates=submitted;
    change_project(constexpr_intermediates,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    int constexpr capacidad{4};
    auto constexpr ajuste = capacidad - 1;
    int const cajas = (piezas + ajuste) / capacidad;
    return cajas;
}
)cpp");
    rejection="previous source rejection";
    require(compiler_runtime_request_allowed(owner,constexpr_intermediates,&rejection)&&rejection.empty(),
        "Windows rejected valid trailing constexpr declarations and derived result");
    auto unused_constexpr=constexpr_intermediates;
    change_project(unused_constexpr,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    int constexpr capacidad{4};
    auto constexpr observacion = capacidad + capacidad;
    return (piezas + capacidad - 1) / 4;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,unused_constexpr),
        "An unused constexpr observation forged a second meaningful capacity use");
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

    auto binary_hex_zero=submitted;
    change_project(binary_hex_zero,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + 3) / 4 + capacidad * 0x0 + capacidad * 0b0;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,binary_hex_zero),
        "Windows credited decorative uses multiplied by hexadecimal and binary zero");
    auto short_circuit_zero=submitted;
    change_project(short_circuit_zero,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + 3) / 4 +
           (capacidad > 0 && capacidad < 8 && 0x0);
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,short_circuit_zero),
        "Windows credited boolean uses absorbed by the hexadecimal false suffix");
    auto hexadecimal_false_branch=submitted;
    change_project(hexadecimal_false_branch,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    if (0x0) return (piezas + capacidad - 1) / capacidad;
    return (piezas + 3) / 4;
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,hexadecimal_false_branch),
        "An unreachable hexadecimal-false branch supplied a named-rule proof");

    auto equal_ternaries=submitted;
    change_project(equal_ternaries,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + 3) / 4 +
           (capacidad > 0 ? 0 : 0) +
           (capacidad < 10 ? 0 : 0);
}
)cpp");
    require(!compiler_runtime_request_allowed(owner,equal_ternaries),
        "Windows credited constants used only to select identical results");
    auto valid_with_equal_ternary=submitted;
    change_project(valid_with_equal_ternary,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    const int capacidad{4};
    return (piezas + capacidad - 1) / capacidad +
           (capacidad > 0 ? 0 : 0);
}
)cpp");
    require(compiler_runtime_request_allowed(owner,valid_with_equal_ternary),
        "Windows rejected a real capacity computation with harmless conditional prose");
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
    require_named_rule_rejection(owner,wrong,"cajas_para",
        "Declaring but not using the capacity satisfied the runtime gate");
    wrong=submitted;
    wrong.project_files.emplace_back("extra.cpp","int injected=0;");
    require(!compiler_runtime_request_allowed(owner,wrong,&rejection)&&
        rejection.find("archivos")!=std::string::npos,
        "An extra project file earned the source evidence");
    wrong=submitted;
    change_project(wrong,"pruebas/cajas_casos.cpp","int main(){return 0;}\n");
    require(!compiler_runtime_request_allowed(owner,wrong,&rejection)&&
        rejection.find("pruebas/cajas_casos.cpp")!=std::string::npos,
        "An unapproved replacement for the original test battery earned credit");
    wrong=submitted;
    wrong.cmake->target="consultar_cajas";
    require(!compiler_runtime_request_allowed(owner,wrong,&rejection)&&
        rejection.find("CMake")!=std::string::npos,
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
    require(compiler_runtime_request_allowed(owner,revised,&rejection)&&rejection.empty(),
        "An expressly permitted new test battery failed the source edit gate");
    change_project(revised,"CMakeLists.txt","project(Injected)\n");
    require(!compiler_runtime_request_allowed(owner,revised,&rejection)&&
        rejection.find("CMakeLists.txt")!=std::string::npos,
        "Changing unauthorized CMake instructions borrowed source credit");
    auto header_edit=submitted;
    const auto header=std::find_if(header_edit.project_files.begin(),header_edit.project_files.end(),
        [](const auto& file){return file.first=="componente/publico/envio/cajas.h";});
    require(header!=header_edit.project_files.end(),
        "The comment-only named-rule header disappeared");
    header->second+="\n// Capacity and deadline remain independent agreements.\n";
    require(compiler_runtime_request_allowed(owner,header_edit,&rejection)&&rejection.empty(),
        "An authorized header comment retained a stale rejection");
    header->second+="\nint injected_rule;\n";
    require(!compiler_runtime_request_allowed(owner,header_edit,&rejection)&&
        rejection.find("componente/publico/envio/cajas.h")!=std::string::npos&&
        rejection.find("comentario")!=std::string::npos,
        "A comment-only violation lost its path or edit restriction");
}
std::string replace_unique(std::string source,std::string_view before,
        std::string_view after) {
    const auto at=source.find(before);
    require(at!=source.npos&&source.find(before,at+before.size())==source.npos,
        "The original named-rule editor source no longer matches its reviewed fixture");
    source.replace(at,before.size(),after);
    return source;
}
void add_constant_variations(Block& owner,std::vector<NamedRuleConstantVariation> variations) {
    require(owner.named_rule_source_check&&owner.named_rule_source_check->execution_probe,
        "Constant variations require the original reviewed source and independent probe");
    auto& source=*owner.named_rule_source_check;
    source.execution_probe->constant_variations=NamedRuleConstantVariationSpec{
        source.path,source.requirements,std::move(variations)};
    require(named_rule_constant_variation_spec_valid(
        *source.execution_probe->constant_variations,source.execution_probe->cases),
        "The native constant-variation fixture does not discriminate its declared rule");
}
void check_real_constant_variation_evidence(const Lesson& lesson,const Block& owner,
        const CppRunRequest& request,const CppRunResult& real) {
    require(request.named_rule_probe&&request.named_rule_probe->constant_variations,
        "The native variation request lost its authored contract");
    const auto& spec=*request.named_rule_probe->constant_variations;
    const auto fresh=capture_cpp_execution(request,real);
    require(real.compilation.succeeded()&&real.execution.succeeded()&&
        real.named_rule_probe&&fresh.named_rule_probe_correct&&
        real.named_rule_constant_variation_probes.size()==spec.variations.size()&&
        !fresh.named_rule_constant_variation_request_fingerprint.empty()&&
        fresh.named_rule_constant_variation_request_fingerprint==
            cpp_named_rule_constant_variation_request_fingerprint(request)&&
        fresh.named_rule_constant_variation_feedback.empty()&&
        evaluate_cpp_output_check(*owner.output_check,fresh,request.revision).verdict==
            OutputVerdict::correct,
        "The real public program and all changed-constant phases did not complete");
    for(std::size_t i=0;i<spec.variations.size();++i) {
        const auto& phase=real.named_rule_constant_variation_probes[i];
        require(phase.index==i&&phase.proof.compilation.succeeded()&&
            phase.proof.execution.succeeded()&&phase.proof.execution.stdout_bytes.empty()&&
            phase.proof.execution.stderr_bytes==named_rule_probe_completion_marker,
            "A claimed constant variation lacks a separately compiled final witness");
    }
    auto forged=real;
    forged.named_rule_constant_variation_probes.pop_back();
    const auto missing=capture_cpp_execution(request,forged);
    require(!missing.named_rule_probe_correct&&
        missing.named_rule_constant_variation_feedback.find(spec.variations.back().function)!=
            std::string::npos&&
        missing.named_rule_constant_variation_feedback.find("No se obtuvo")!=std::string::npos,
        "A missing constant variation was credited or attributed to the wrong function");
    forged=real;
    forged.named_rule_constant_variation_probes.front().proof.compilation.status=
        CppPhaseStatus::not_started;
    const auto uncompiled=capture_cpp_execution(request,forged);
    require(!uncompiled.named_rule_probe_correct&&
        uncompiled.named_rule_constant_variation_feedback.find("No se obtuvo")!=std::string::npos&&
        uncompiled.named_rule_constant_variation_feedback.find("Deriva") == std::string::npos,
        "An uncompiled private phase was mistaken for a failed calculation");
    forged=real;
    forged.named_rule_constant_variation_probes.front().index=spec.variations.size();
    require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
        "A constant variation in another position was accepted");
    forged=real;
    forged.named_rule_constant_variation_probes.front().proof=*real.named_rule_probe;
    require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
        "The unchanged baseline was substituted for a changed-constant compilation");
    forged=real;
    forged.named_rule_constant_variation_probes.front().proof.project_sha256="0";
    require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
        "A constant variation from another program supplied evidence");
    forged=real;
    forged.named_rule_constant_variation_probes.front().proof.execution.stderr_bytes.clear();
    require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
        "A constant variation without its final witness was accepted");
    forged=real;
    forged.named_rule_constant_variation_request_fingerprint.clear();
    require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
        "A result without a constant-variation request binding was accepted");
    if(spec.variations.size()>1) {
        forged=real;
        std::swap(forged.named_rule_constant_variation_probes[0],
            forged.named_rule_constant_variation_probes[1]);
        require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
            "Reordered private constant phases earned credit");
        forged=real;
        forged.named_rule_constant_variation_probes[1]=
            forged.named_rule_constant_variation_probes[0];
        require(!capture_cpp_execution(request,forged).named_rule_probe_correct,
            "A duplicated private constant phase replaced a different variation");
    }
    auto changed=request;
    ++changed.named_rule_probe->constant_variations->variations.front().value;
    require(!capture_cpp_execution(changed,real).named_rule_probe_correct,
        "Evidence was reused after changing the authored replacement value");
    changed=request;
    ++changed.revision;
    forged=real;
    forged.revision=changed.revision;
    require(!capture_cpp_execution(changed,forged).named_rule_probe_correct,
        "Relabeling an old result authenticated a new source revision");
    changed=request;
    changed.named_rule_probe->constant_variations->requirements.front().purpose_words.push_back("forged");
    require(!compiler_runtime_request_allowed(owner,changed)&&
        !capture_cpp_execution(changed,real).named_rule_probe_correct,
        "A private variant changed the authenticated source requirement");

    // Reuse only the actual unchanged baseline under its historical contract.
    // The same public program must not promote that receipt into this goal.
    auto legacy_owner=owner;
    legacy_owner.named_rule_source_check->execution_probe->constant_variations.reset();
    auto legacy_request=request;
    legacy_request.named_rule_probe->constant_variations.reset();
    const auto legacy=capture_cpp_execution(legacy_request,real);
    require(legacy.named_rule_probe_correct&&
        legacy.named_rule_constant_variation_request_fingerprint.empty()&&
        legacy.named_rule_constant_variation_feedback.empty()&&
        compiler_runtime_request_allowed(legacy_owner,legacy_request)&&
        reference_objective_frame(lesson,legacy_owner)!=reference_objective_frame(lesson,owner)&&
        exercise_key(lesson,legacy_owner)==exercise_key(lesson,owner)&&
        draft_key("programming",lesson,legacy_owner)==draft_key("programming",lesson,owner),
        "The optional variation altered the baseline contract or student identity");
    state::StateData saved;
    state::prepare_lesson_state(saved,lesson);
    state::Draft draft;
    draft.source=request.cmake?request.project_files.front().second:request.source_utf8;
    draft.source+="\n// preserve my later unpublished edit\n";
    draft.input="saved optional input";
    const auto draft_address=draft_key("programming",lesson,owner);
    state::put_draft(saved,draft_address,draft);
    require(record_compiler_puzzle_case(saved,lesson,legacy_owner,legacy_request,legacy,
            "actual historical function probe")&&
        compiler_puzzle_completed(saved,lesson,legacy_owner)&&
        !compiler_puzzle_completed(saved,lesson,owner),
        "A historical function receipt disappeared or satisfied constant variation");
    const auto before=state::encode(saved);
    require(!record_compiler_puzzle_case(saved,lesson,owner,request,legacy,
            "old capture without variation")&&state::encode(saved)==before,
        "An old successful capture without the new fingerprint earned credit");
    require(record_compiler_puzzle_case(saved,lesson,owner,request,fresh,
            "actual varied constant evidence")&&
        compiler_puzzle_completed(saved,lesson,owner)&&
        compiler_puzzle_case_count(saved,lesson,owner)==1,
        "The first public case with genuine constant variations did not earn its receipt");
    const auto completed=state::encode(saved);
    require(!record_compiler_puzzle_case(saved,lesson,owner,request,legacy,
            "later old capture")&&state::encode(saved)==completed,
        "A later rejected variation erased the achievement or its draft");
    auto optional_owner=owner;
    auto optional_case=optional_owner.output_check->cases.front();
    optional_case.standard_input="optional later input\n";
    optional_case.outputs={"optional output not executed\n"};
    optional_owner.output_check->cases.push_back(std::move(optional_case));
    state::StateData first_only;
    state::prepare_lesson_state(first_only,lesson);
    require(record_compiler_puzzle_case(first_only,lesson,optional_owner,request,fresh,
            "actual varied constant evidence")&&
        compiler_puzzle_completed(first_only,lesson,optional_owner)&&
        compiler_puzzle_case_count(first_only,lesson,optional_owner)==1,
        "Private constant variations made an optional second editor case mandatory");
    const auto reopened=state::decode(state::encode(saved));
    const auto restored=state::get_draft(reopened,draft_address,"","");
    require(compiler_puzzle_completed(reopened,lesson,legacy_owner)&&
        compiler_puzzle_completed(reopened,lesson,owner)&&
        compiler_puzzle_case_count(reopened,lesson,owner)==1&&
        restored.has_saved&&restored.source==draft.source&&restored.input==draft.input,
        "Reopening lost the variation receipt, historical receipt or saved draft");
    std::cout<<"PASS authentic constant variation "<<owner.stable_id
        <<": private compilations, exact provenance, first case and history retained\n";
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
    add_constant_variations(owner,{
        {"precio_total",9,{{"precio_total",11,99},{"precio_total",5,45},
            {"alcanza_minimo",4,0},{"alcanza_minimo",5,1}}},
        {"alcanza_minimo",6,{{"precio_total",11,77},{"precio_total",5,35},
            {"alcanza_minimo",4,0},{"alcanza_minimo",5,0}}}});
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
    check_real_constant_variation_evidence(lesson,owner,passing,real);
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
    require_named_rule_rejection(owner,shortcut,"alcanza_minimo",
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
        OutputVerdict::correct&&!forged_capture.named_rule_probe_correct&&
        forged_capture.named_rule_constant_variation_feedback.empty(),
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
    std::string rejection="previous source rejection";
    require(!compiler_runtime_request_allowed(owner,early_exit,&rejection)&&
        rejection.find("termin")!=std::string::npos&&
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
    require(compiler_runtime_request_allowed(owner,harmless_exit_comment,&rejection)&&rejection.empty(),
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
    const auto before_rejection=state::encode(saved);
    require(!record_compiler_puzzle_case(saved,lesson,owner,shortcut,bad_capture,"wrong")&&
        !compiler_puzzle_completed(saved,lesson,owner)&&
        compiler_puzzle_case_count(saved,lesson,owner)==0&&
        state::encode(saved)==before_rejection,
        "Identical MSVC output incorrectly earned a source-aware completion receipt");
    require(record_compiler_puzzle_case(saved,lesson,owner,passing,captured,"correct")&&
        compiler_puzzle_completed(saved,lesson,owner),
        "The first compiled valid case failed to unlock the authentic exercise");
    const auto completed_state=state::encode(saved);
    require(!record_compiler_puzzle_case(saved,lesson,owner,shortcut,bad_capture,
            "wrong after completion")&&
        compiler_puzzle_completed(saved,lesson,owner)&&
        compiler_puzzle_case_count(saved,lesson,owner)==1&&
        state::encode(saved)==completed_state,
        "A rejected source changed the previously earned receipt or student state");
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
    add_constant_variations(owner,{{"cajas_para",4,{
        {"cajas_para",0,0},{"cajas_para",6,2},{"cajas_para",7,2},{"cajas_para",19,5},
        {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}}}});
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
    check_real_constant_variation_evidence(lesson,owner,correct,real);

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

void actual_constant_variation_controls(const Lesson& lesson,
        const std::filesystem::path& projects) {
    auto boxes=lesson.pages.at(2).blocks.at(4);
    require(boxes.stable_id=="block_0002_0004"&&boxes.cmake&&boxes.output_check&&
        boxes.output_check->cases.size()==1,
        "The original four-place named-rule editor is unavailable");
    boxes.named_rule_source_check=NamedRuleSourceCheck{
        "componente/src/cajas.cpp",{{"cajas_para",4,{"capacidad","plaza","cupo"},2}}};
    boxes.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
        "pruebas/cajas_casos.cpp","envio/cajas.h",{
            {"cajas_para",0,0},{"cajas_para",1,1},{"cajas_para",4,1},
            {"cajas_para",7,2},{"cajas_para",19,5},{"cajas_para",20,5}}};
    // Seven pieces is executed first in the private driver: both apparently
    // dependent formulas below fail there (3 and 1), against the authored 2.
    // Nineteen supplies the shared baseline witness whose expected value changes.
    add_constant_variations(boxes,{{"cajas_para",6,{
        {"cajas_para",7,2},{"cajas_para",19,4},{"cajas_para",0,0}}}});
    auto correct=request_of(boxes);
    correct.request_id=1801;correct.revision=1001;
    correct.projects_directory=projects;
    correct.project_key=draft_key("programming",lesson,boxes)+"/constant-control";
    correct.compile_timeout=std::chrono::seconds(100);
    correct.execution_timeout=std::chrono::seconds(8);
    change_project(correct,"componente/src/cajas.cpp",R"cpp(#include "envio/cajas.h"
int cajas_para(int piezas) {
    int const capacidad{4};
    int const ajuste{capacidad - 1};
    return (piezas + ajuste) / capacidad;
}
)cpp");
    require(compiler_runtime_request_allowed(boxes,correct),
        "A valid east-const capacity with a derived intermediate was rejected");
    const auto real=run_cpp(correct);
    require(real.error_utf8.empty(),
        "The actual four-place constant control failed: "+real.error_utf8+
        real.configuration.stderr_bytes+real.compilation.stderr_bytes);
    check_real_constant_variation_evidence(lesson,boxes,correct,real);

    std::size_t ordinal=0;
    for(const auto expression:{
            "(piezas + capacidad - (capacidad / 4)) / 4",
            "(piezas + 3) / (capacidad / 2 + capacidad / 2)"}) {
        auto fixed=correct;
        fixed.request_id+=++ordinal;fixed.revision+=ordinal;
        fixed.project_key+="/fixed-"+std::to_string(ordinal);
        change_project(fixed,"componente/src/cajas.cpp",
            std::string("#include \"envio/cajas.h\"\nint cajas_para(int piezas) {\n")+
            "    const int capacidad{4};\n    return "+expression+";\n}\n");
        require(compiler_runtime_request_allowed(boxes,fixed),
            "The false-dependency control did not preserve the original minUses contract");
        const auto observed=run_cpp(fixed);
        const auto captured=capture_cpp_execution(fixed,observed);
        require(observed.error_utf8.empty()&&observed.compilation.succeeded()&&
            observed.execution.succeeded()&&observed.named_rule_probe&&
            observed.named_rule_probe->compilation.succeeded()&&
            observed.named_rule_probe->execution.succeeded()&&
            observed.named_rule_probe->execution.stderr_bytes==named_rule_probe_completion_marker&&
            observed.named_rule_constant_variation_probes.size()==1&&
            observed.named_rule_constant_variation_probes.front().proof.compilation.succeeded()&&
            observed.named_rule_constant_variation_probes.front().proof.execution.status==
                CppPhaseStatus::completed&&
            observed.named_rule_constant_variation_probes.front().proof.execution.exit_code==1&&
            !captured.named_rule_probe_correct&&
            captured.named_rule_constant_variation_feedback.find("cajas_para")!=std::string::npos&&
            captured.named_rule_constant_variation_feedback.find("relación pedida")!=std::string::npos&&
            evaluate_cpp_output_check(*boxes.output_check,captured,fixed.revision).verdict==
                OutputVerdict::correct,
            "A fixed divisor/adjustment was not isolated by its real changed-constant execution");
        auto legacy_owner=boxes;
        legacy_owner.named_rule_source_check->execution_probe->constant_variations.reset();
        auto legacy_request=fixed;
        legacy_request.named_rule_probe->constant_variations.reset();
        const auto legacy=capture_cpp_execution(legacy_request,observed);
        require(legacy.named_rule_probe_correct&&
            legacy.named_rule_constant_variation_feedback.empty()&&
            compiler_runtime_request_allowed(legacy_owner,legacy_request),
            "The counterexample did not actually pass the unchanged historical contract");
        state::StateData saved;
        state::prepare_lesson_state(saved,lesson);
        require(record_compiler_puzzle_case(saved,lesson,legacy_owner,legacy_request,legacy,
                "actual old-contract counterexample"),
            "The historical counterexample no longer reproduces its former accreditation");
        const auto before=state::encode(saved);
        require(!record_compiler_puzzle_case(saved,lesson,boxes,fixed,captured,
                "fixed numeric dependency")&&state::encode(saved)==before&&
            !compiler_puzzle_completed(saved,lesson,boxes),
            "A false constant dependency earned a new receipt or removed valid history");
        const auto reopened=state::decode(state::encode(saved));
        require(compiler_puzzle_completed(reopened,lesson,legacy_owner)&&
            !compiler_puzzle_completed(reopened,lesson,boxes),
            "Reopening promoted the old counterexample receipt to constant-dependence credit");
        std::cout<<"PASS authentic constant counterexample "<<ordinal
            <<": public/base correct, changed capacity rejected, no new receipt\n";
    }

    auto price=lesson.pages.at(7).blocks.at(4);
    require(price.stable_id=="block_0007_0004"&&!price.cmake&&price.output_check&&
        price.output_check->cases.size()==1,
        "The original independent price/minimum editor is unavailable");
    price.named_rule_source_check=NamedRuleSourceCheck{"",{
        {"precio_total",7,{"precio","costo","tarifa","credito"},1},
        {"alcanza_minimo",5,{"minimo","cantidad","umbral"},1}}};
    price.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
        "","",{{"precio_total",11,77},{"precio_total",5,35},
            {"alcanza_minimo",4,0},{"alcanza_minimo",5,1}}};
    add_constant_variations(price,{{"precio_total",9,{
        {"precio_total",11,99},{"precio_total",5,45},
        {"alcanza_minimo",4,0},{"alcanza_minimo",5,1}}}});
    auto priced=request_of(price);
    priced.request_id=1810;priced.revision=1010;
    priced.projects_directory=projects;
    priced.compile_timeout=std::chrono::seconds(60);
    priced.execution_timeout=std::chrono::seconds(8);
    const auto& reference=lesson.pages.at(8).blocks.at(1);
    require(reference.type==BlockType::code&&!reference.code.empty(),
        "The published complete price/minimum reference is unavailable");
    priced.source_utf8=replace_unique(reference.code,
        "return articulos * creditos_por_articulo;",
        "int const total{creditos_por_articulo * articulos};\n    return total;");
    require(compiler_runtime_request_allowed(price,priced),
        "An equivalent result intermediate did not preserve separate price and minimum rules");
    const auto priced_run=run_cpp(priced);
    require(priced_run.error_utf8.empty(),
        "The actual independent price variation failed: "+priced_run.error_utf8+
        priced_run.compilation.stderr_bytes);
    check_real_constant_variation_evidence(lesson,price,priced,priced_run);
}


Block named_battery_owner(const Lesson& lesson,std::size_t page,std::size_t index) {
    auto owner=lesson.pages.at(page).blocks.at(index);
    require(owner.output_check&&owner.output_check->cases.size()==1,
        "The authentic named-rule battery no longer has one authored case");
    if(owner.cmake) {
        require(owner.stable_id=="block_0005_0006",
            "The named CMake battery owner changed");
        owner.named_rule_source_check=NamedRuleSourceCheck{
            "componente/src/cajas.cpp",{
                {"cajas_para",6,{"capacidad","plaza","cupo","caja"},2},
                {"llega_a_tiempo",4,{"plazo","tiempo","dias"},1}}};
        owner.named_rule_source_check->editable_paths={
            "componente/publico/envio/cajas.h","pruebas/cajas_casos.cpp"};
        owner.named_rule_source_check->comment_only_paths={
            "componente/publico/envio/cajas.h"};
        owner.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
            "pruebas/cajas_casos.cpp","envio/cajas.h",{
                {"cajas_para",0,0},{"cajas_para",6,1},{"cajas_para",7,2},
                {"cajas_para",19,4},{"cajas_para",20,4},
                {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}}};
        owner.named_rule_source_check->execution_probe->battery_check=
            NamedRuleBatterySpec{"componente/src/cajas.cpp",{
                {"cajas_para",0,0},{"cajas_para",4,1},{"cajas_para",8,2},
                {"cajas_para",20,4},{"cajas_para",1,1},{"cajas_para",5,1},
                {"cajas_para",17,3},{"cajas_para",6,1},{"cajas_para",7,2},
                {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}},1};
    }else{
        require((owner.stable_id=="block_0007_0004"||
                 owner.stable_id=="block_0008_0007")&&owner.project_files.empty(),
            "The authentic single-file battery owner changed");
        owner.named_rule_source_check=NamedRuleSourceCheck{"",{
            {"precio_total",7,{"precio","costo","tarifa","credito"},1},
            {"alcanza_minimo",5,{"minimo","cantidad","umbral"},1}}};
        owner.named_rule_source_check->execution_probe=NamedRuleExecutionProbe{
            "","",{{"precio_total",11,77},{"precio_total",5,35},
                   {"alcanza_minimo",4,0},{"alcanza_minimo",5,1}}};
        owner.named_rule_source_check->execution_probe->battery_check=
            NamedRuleBatterySpec{"",{
                {"precio_total",0,0},{"precio_total",4,28},
                {"precio_total",5,35},{"precio_total",6,42},{"precio_total",10,70},
                {"alcanza_minimo",0,0},{"alcanza_minimo",4,0},
                {"alcanza_minimo",5,1},{"alcanza_minimo",6,1},
                {"alcanza_minimo",10,1}},1};
    }
    return owner;
}
CppRunRequest named_battery_reference(const Lesson& lesson,const Block& owner,
        const std::filesystem::path& projects,std::size_t ordinal) {
    auto request=request_of(owner);
    request.request_id=1400+ordinal*10;
    request.revision=700+ordinal*10;
    request.projects_directory=projects;
    request.project_key=draft_key("programming",lesson,owner)+"/battery";
    request.compile_timeout=std::chrono::seconds(120);
    request.execution_timeout=std::chrono::seconds(8);
    if(owner.cmake) {
        // These are the actual published reference functions and complete
        // eleven-comparison checker, not reconstructed passing transcripts.
        const auto& functions=lesson.pages.at(6).blocks.at(1);
        const auto& battery=lesson.pages.at(6).blocks.at(3);
        require(functions.type==BlockType::code&&battery.type==BlockType::code&&
            !functions.code.empty()&&!battery.code.empty(),
            "The original CMake reference functions or battery are missing");
        change_project(request,"componente/src/cajas.cpp",functions.code);
        change_project(request,"pruebas/cajas_casos.cpp",battery.code);
        for(auto& file:request.project_files)
            if(file.first=="componente/publico/envio/cajas.h")
                file.second=replace_unique(std::move(file.second),
                    "cajas de 4 plazas","cajas de 6 plazas");
    }else{
        const auto& reference=lesson.pages.at(8).blocks.at(1);
        require(reference.type==BlockType::code&&!reference.code.empty(),
            "The complete published price/minimum reference is missing");
        request.source_utf8=reference.code;
    }
    require(compiler_runtime_request_allowed(owner,request),
        "The complete original reference was rejected by the authored battery requirement");
    return request;
}
CppRunResult execute_named_battery(const Block& owner,const CppRunRequest& request) {
    const auto result=run_cpp(request);
    require(result.error_utf8.empty()&&result.compilation.succeeded()&&
        result.execution.succeeded(),
        "The named-rule battery control did not compile and run: "+result.error_utf8+
        result.compilation.stderr_bytes+result.execution.stderr_bytes);
    const auto captured=capture_cpp_execution(request,result);
    require(captured.named_rule_probe_correct&&
        evaluate_cpp_output_check(*owner.output_check,captured,request.revision).verdict==
            OutputVerdict::correct,
        "The battery control did not isolate correct functions and identical public output");
    return result;
}
void actual_named_batteries(const Lesson& lesson,const std::filesystem::path& projects) {
    std::optional<CppRunResult> previous_single;
    std::size_t ordinal=0;
    for(const auto [page,index]:{
            std::pair<std::size_t,std::size_t>{5,6},{7,4},{8,7}}) {
        const auto owner=named_battery_owner(lesson,page,index);
        const auto passing=named_battery_reference(lesson,owner,projects,ordinal++);
        const auto real=execute_named_battery(owner,passing);
        const auto fresh=capture_cpp_execution(passing,real);
        require(fresh.named_rule_battery_correct&&real.named_rule_battery_evidence&&
            real.named_rule_battery_evidence->runs.size()==
                passing.named_rule_probe->battery_check->cases.size()+1&&
            fresh.named_rule_battery_request_fingerprint==
                cpp_named_rule_battery_request_fingerprint(passing),
            "The genuine complete reference did not prove its preserved battery: "+
                fresh.named_rule_battery_feedback);

        // The previous independent driver proves only the functions. Correct
        // functions plus a fixed transcript must still fail this new objective.
        auto fixed=passing;
        ++fixed.request_id;++fixed.revision;
        fixed.project_key+="/fixed";
        const auto transcript=owner.output_check->cases.front().outputs.front();
        const auto fixed_main="int main(){std::cout << R\"LGAED("+transcript+
            ")LGAED\";return 0;}\n";
        if(fixed.cmake) {
            change_project(fixed,"pruebas/cajas_casos.cpp",
                "#include <iostream>\n"+fixed_main);
        }else{
            const auto main=fixed.source_utf8.find("int main()");
            require(main!=fixed.source_utf8.npos,
                "The published reference main is unavailable");
            fixed.source_utf8.resize(main);
            fixed.source_utf8+=fixed_main;
        }
        require(compiler_runtime_request_allowed(owner,fixed),
            "The fixed-output regression did not reach execution of the preserved functions");
        const auto fake=execute_named_battery(owner,fixed);
        const auto fake_capture=capture_cpp_execution(fixed,fake);
        require(!fake_capture.named_rule_battery_correct&&
            !fake_capture.named_rule_battery_feedback.empty(),
            "Correct functions plus a fixed battery transcript earned battery evidence");

        // Omission/duplicate-call/phase matrices are covered by the portable
        // verifier. These two controls additionally exercise instrumentation,
        // the real compiler and the Windows completion gate in both formats.
        if(page==5||page==7) {
            auto defective=passing;
            defective.request_id+=2;defective.revision+=2;
            defective.project_key+="/defective";
            if(defective.cmake) {
                for(auto& file:defective.project_files)
                    if(file.first=="pruebas/cajas_casos.cpp")
                        file.second=replace_unique(std::move(file.second),
                            "comprobar(5, 1)","comprobar(5, cajas_para(5))");
            }else{
                for(const auto& call:{"comprobar(0, 0, false)",
                        "comprobar(4, 28, false)","comprobar(5, 35, true)",
                        "comprobar(6, 42, true)","comprobar(10, 70, true)"}) {
                    const auto old="    if (!"+std::string(call)+") { correcto = false; }";
                    const auto replacement="    correcto = correcto && "+std::string(call)+";";
                    defective.source_utf8=replace_unique(
                        std::move(defective.source_utf8),old,replacement);
                }
            }
            require(compiler_runtime_request_allowed(owner,defective),
                "The defective battery control was rejected before actual observation");
            const auto observed=execute_named_battery(owner,defective);
            const auto rejected=capture_cpp_execution(defective,observed);
            require(!rejected.named_rule_battery_correct,
                defective.cmake?
                    "A self-referential expected value proved an independent comparison":
                    "A short-circuiting checker proved all comparisons after a failure");
            state::StateData empty;
            state::prepare_lesson_state(empty,lesson);
            require(!record_compiler_puzzle_case(empty,lesson,owner,defective,rejected,
                    "defective battery")&&!compiler_puzzle_completed(empty,lesson,owner),
                "An observed defective battery earned a native completion receipt");
        }

        // Binding controls reuse real observations; they do not recompile an
        // unchanged reference merely to alter a fingerprint, request or channel.
        auto other=passing;
        if(other.cmake) {
            for(auto& file:other.project_files)
                if(file.first=="pruebas/cajas_casos.cpp")file.second+="\n// other source\n";
        }else other.source_utf8+="\n// other source\n";
        require(!capture_cpp_execution(other,real).named_rule_battery_correct,
            "Battery evidence from different submitted source was accepted");
        other=passing;++other.revision;
        require(!capture_cpp_execution(other,real).named_rule_battery_correct,
            "Battery evidence from another source revision was accepted");
        other=passing;++other.request_id;
        require(!capture_cpp_execution(other,real).named_rule_battery_correct,
            "Battery evidence from another request was accepted");
        other=passing;
        ++other.named_rule_probe->battery_check->cases.front().expected;
        require(!capture_cpp_execution(other,real).named_rule_battery_correct,
            "Battery evidence satisfied a different authored expected value");
        auto mixed=real;
        mixed.named_rule_battery_evidence->original_program_fingerprint="0";
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "Battery evidence carried a foreign program fingerprint");
        mixed=real;
        mixed.named_rule_battery_request_fingerprint="0";
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "The worker accepted a foreign battery request fingerprint");
        mixed=real;
        mixed.named_rule_battery_evidence->runs.front().stdout_bytes+="extra\n";
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "The instrumented baseline no longer matched public stdout");
        mixed=real;
        mixed.named_rule_battery_evidence->runs.front().stderr_bytes+="extra\n";
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "The instrumented baseline no longer matched public stderr");
        mixed=real;
        mixed.named_rule_battery_evidence->runs.back().program_frame="0";
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "A mutant observation from another program supplied battery credit");
        mixed=real;
        mixed.named_rule_battery_evidence->runs.back().calls.clear();
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "Missing calls in a real mutant observation were accepted");
        mixed=real;
        mixed.named_rule_battery_evidence->runs.pop_back();
        require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
            "An incomplete set of observed mutant phases earned battery credit");
        if(page==8) {
            require(previous_single&&previous_single->named_rule_battery_evidence,
                "The earlier genuine single-file battery was not observed");
            mixed=real;
            mixed.named_rule_battery_evidence=previous_single->named_rule_battery_evidence;
            require(!capture_cpp_execution(passing,mixed).named_rule_battery_correct,
                "The same reference from another owner supplied its battery evidence");
        }
        if(!passing.cmake)previous_single=real;

        // Additional evidence does not change legacy objectives which never
        // requested it. Reuse the actual public execution and function probe
        // under that less restrictive contract; no synthetic success is made.
        auto legacy_owner=owner;
        legacy_owner.named_rule_source_check->execution_probe->battery_check.reset();
        auto legacy_request=fixed;
        legacy_request.named_rule_probe->battery_check.reset();
        const auto legacy_capture=capture_cpp_execution(legacy_request,fake);
        require(legacy_capture.named_rule_probe_correct&&
            compiler_runtime_request_allowed(legacy_owner,legacy_request)&&
            reference_objective_frame(lesson,legacy_owner)!=
                reference_objective_frame(lesson,owner)&&
            exercise_key(lesson,legacy_owner)==exercise_key(lesson,owner)&&
            draft_key("programming",lesson,legacy_owner)==
                draft_key("programming",lesson,owner),
            "The optional battery requirement changed legacy behavior or draft identities");
        state::StateData saved;
        state::prepare_lesson_state(saved,lesson);
        state::Draft draft;
        draft.source=passing.cmake?
            lesson.pages.at(6).blocks.at(3).code:passing.source_utf8;
        draft.source+="\n// keep this unpublished draft\n";
        draft.input="draft input";
        const auto key=draft_key("programming",lesson,owner);
        state::put_draft(saved,key,draft);
        require(record_compiler_puzzle_case(saved,lesson,legacy_owner,
                legacy_request,legacy_capture,"legacy function evidence")&&
            compiler_puzzle_completed(saved,lesson,legacy_owner)&&
            !compiler_puzzle_completed(saved,lesson,owner),
            "A legacy receipt disappeared or promoted itself to the new battery objective");
        const auto preserved=state::encode(saved);
        require(!record_compiler_puzzle_case(saved,lesson,owner,fixed,fake_capture,
                "fixed battery output")&&state::encode(saved)==preserved&&
            compiler_puzzle_completed(saved,lesson,legacy_owner)&&
            !compiler_puzzle_completed(saved,lesson,owner),
            "A rejected new battery erased history/drafts or earned new progress");
        require(record_compiler_puzzle_case(saved,lesson,owner,passing,fresh,
                "preserved real battery")&&compiler_puzzle_completed(saved,lesson,owner),
            "The first real case with a complete battery did not grant progress");
        const auto completed=state::encode(saved);
        require(!record_compiler_puzzle_case(saved,lesson,owner,fixed,fake_capture,
                "later fixed battery")&&state::encode(saved)==completed&&
            compiler_puzzle_completed(saved,lesson,owner),
            "A later battery failure erased the previously earned achievement");
        const auto reopened=state::decode(state::encode(saved));
        const auto restored=state::get_draft(reopened,key,"","");
        require(compiler_puzzle_completed(reopened,lesson,owner)&&
            compiler_puzzle_completed(reopened,lesson,legacy_owner)&&
            compiler_puzzle_case_count(reopened,lesson,owner)==1&&
            restored.has_saved&&restored.source==draft.source&&restored.input==draft.input,
            "Reopening lost either battery receipt, historical receipt or draft");

        // A prospective second output case is optional. It neither represents
        // another battery phase nor becomes a second mandatory editor attempt.
        auto optional_owner=owner;
        auto optional_case=optional_owner.output_check->cases.front();
        optional_case.standard_input="optional input\n";
        optional_case.outputs={"optional result not executed\n"};
        optional_owner.output_check->cases.push_back(std::move(optional_case));
        state::StateData first_only;
        state::prepare_lesson_state(first_only,lesson);
        require(record_compiler_puzzle_case(first_only,lesson,optional_owner,
                passing,fresh,"first case only")&&
            compiler_puzzle_completed(first_only,lesson,optional_owner)&&
            compiler_puzzle_case_count(first_only,lesson,optional_owner)==1,
            "Private battery phases made another output case mandatory");
        std::cout<<"PASS authentic named battery "<<owner.stable_id
            <<": preserved comparisons, fixed output rejected, history retained\n";
    }
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
        std::string rejection=require_named_rule_rejection(owner,submitted,"precio_total",
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
        require(compiler_runtime_request_allowed(owner,submitted,&rejection)&&rejection.empty(),
            "Purpose-bearing independent price and minimum failed source validation");
        auto forged=submitted;
        forged.source_utf8+="\n// declared another rule in a comment\n";
        // Comments do not change the verdict, because source evidence comes
        // from analyzed function definitions rather than textual matches.
        require(compiler_runtime_request_allowed(owner,forged),
            "Harmless prose comments altered a real source prerequisite");
        forged=submitted;
        forged.source_utf8=replace_unique(std::move(forged.source_utf8),
            "bool alcanza_minimo(","bool otro_minimo(");
        forged.source_utf8=replace_unique(std::move(forged.source_utf8),
            "<< alcanza_minimo(5)","<< otro_minimo(5)");
        require_named_rule_rejection(owner,forged,"alcanza_minimo",
            "A missing required function lost its specific diagnostic");
        forged=submitted;
        forged.source_utf8=replace_unique(std::move(forged.source_utf8),
            "const int minimo_articulos{5};","int minimo_articulos{5};");
        require_named_rule_rejection(owner,forged,"alcanza_minimo",
            "A missing purpose constant lost its specific diagnostic");
        forged=submitted;
        forged.source_utf8=replace_unique(std::move(forged.source_utf8),
            "const int minimo_articulos{5};","const int minimo_articulos{6};");
        require_named_rule_rejection(owner,forged,"alcanza_minimo",
            "A wrong rule value lost its specific diagnostic");
        forged=submitted;
        forged.source_utf8=replace_unique(std::move(forged.source_utf8),
            "const int minimo_articulos{5};",
            "const int minimo_articulos{5};\n    const int umbral_articulos{5};");
        require_named_rule_rejection(owner,forged,"alcanza_minimo",
            "Two competing purpose constants lost their ambiguity diagnostic");
        forged=submitted;
        forged.source_utf8.replace(
            forged.source_utf8.find("return articulos >= minimo_articulos;"),
            std::string("return articulos >= minimo_articulos;").size(),
            "return articulos >= 5;");
        rejection=require_named_rule_rejection(owner,forged,"alcanza_minimo",
            "The unused minimum falsely satisfied the new compiler rubric");
        auto unrelated=submitted;
        unrelated.local_role_check.emplace();
        require(!compiler_runtime_request_allowed(owner,unrelated,&rejection)&&rejection.empty(),
            "An earlier unrelated contract retained a stale named-rule diagnostic");
        forged=submitted;
        forged.project_files.emplace_back("main.cpp",forged.source_utf8);
        require(!compiler_runtime_request_allowed(owner,forged,&rejection)&&
            rejection.find("archivo")!=std::string::npos,
            "A foreign project was admitted to the single-file source goal");
    }
    std::cout<<"PASS named-rule request diagnostics: specific core reasons and cleared stale feedback\n";
}
}
int main(int argc,char** argv) {
    try {
        const bool focused=argc==3&&std::string_view(argv[1])=="--single-file";
        require(argc==2||focused,"Expected the published U3 lesson path or --single-file");
        const auto lesson=load_lesson(std::filesystem::path(argv[focused?2:1]),"es");
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
        if(focused) {
            // The existing full QA entrypoint remains unchanged. This hosted
            // focus checks actual source, private probes, variations and sealed
            // first-case progress without the unrelated CMake battery.
            std::cout<<"PASS real U3 named-rule single-file MSVC and native sealed receipts\n";
            return 0;
        }
        real_independent_cmake_probe(lesson,
            // Keep CMakeScratch/TryCompile and MSVC object paths below the
            // verified Windows CMAKE_OBJECT_PATH_MAX limit on hosted runners.
            environment.paths.projects/L"u3-cmake");
        actual_constant_variation_controls(lesson,environment.paths.projects/L"u3-var");
        actual_named_batteries(lesson,environment.paths.projects/L"u3-battery");
        std::cout<<"PASS original named-rule editors: source constraints, historic receipt and draft isolation\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL original named-rule source gate: "<<error.what()<<'\n';
        return 1;
    }
}
