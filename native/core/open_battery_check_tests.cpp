#include "core/open_battery_check.hpp"
#include "core/adjustment_project_check.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::curriculum;
void verify(bool accepted,std::string_view message) {
    if(!accepted)throw std::runtime_error(std::string(message));
}
constexpr std::array originals{
    OpenBatteryCall{2,3,7,77},OpenBatteryCall{5,3,7,-8},
    OpenBatteryCall{9,3,7,19},OpenBatteryCall{4,4,4,-4},
    OpenBatteryCall{-20,20,20,8},OpenBatteryCall{20,-20,-20,17},
    OpenBatteryCall{0,7,3,77},OpenBatteryCall{21,3,7,-8},
    OpenBatteryCall{-21,3,7,19},OpenBatteryCall{0,-21,7,-4},
    OpenBatteryCall{0,3,21,8}
};
constexpr std::string_view library=R"cpp(
#include "control/ajuste.h"
bool intentar_ajuste(int valor, int inferior, int superior, int& ajuste) {
    if(valor<-20||valor>20||inferior<-20||inferior>20||
       superior<-20||superior>20||inferior>superior)return false;
    ajuste=valor<inferior?inferior-valor:valor>superior?superior-valor:0;
    return true;
}
)cpp";
std::vector<OpenBatteryRun> correct(OpenBatteryCall added) {
    std::vector<OpenBatteryRun> runs(2*originals.size()+3);
    runs[0].program_frame="project-fingerprint";
    runs[0].compiled=runs[0].executed=true;
    runs[0].exit_code=0;
    runs[0].stdout_bytes="Fallos=0\n";
    runs[0].calls.assign(originals.begin(),originals.end());
    runs[0].calls.push_back(added);
    for(std::size_t i=1;i<runs.size();++i) {
        runs[i]=runs[0];
        runs[i].exit_code=1;
        runs[i].stdout_bytes="Fallos=1\n";
        // Deliberately independent of the production plan implementation.
        runs[i].mutation=i%2==0?OpenBatteryMutation::corrupt_destination:
            OpenBatteryMutation::invert_acceptance;
        const auto target=(i-1)/2;
        runs[i].target=target<originals.size()?originals[target]:added;
    }
    return runs;
}
void generated() {
    const auto baseline=open_battery_mutated_library(
        library,"intentar_ajuste",OpenBatteryMutation::baseline);
    verify(baseline&&baseline->find("LGAED_OPEN_BATTERY_CALL:")!=std::string::npos,
           "Missing private call witness");
    const auto flip=open_battery_mutated_library(
        library,"intentar_ajuste",OpenBatteryMutation::invert_acceptance,
        OpenBatteryCall{3,3,7,59});
    verify(flip&&flip->find("return !aceptado")!=std::string::npos,
           "Missing private acceptance mutant");
    const auto destination=open_battery_mutated_library(
        library,"intentar_ajuste",OpenBatteryMutation::corrupt_destination,
        OpenBatteryCall{3,3,7,59});
    verify(destination&&destination->find("ajuste == 0 ? 1 : 0")!=std::string::npos,
           "Missing private destination mutant");
    verify(!open_battery_mutated_library(
        library,"intentar_ajuste",OpenBatteryMutation::baseline,
        OpenBatteryCall{3,3,7,59}),"Baseline accepted a mutant target");
    verify(!open_battery_mutated_library(
        library,"intentar_ajuste",OpenBatteryMutation::invert_acceptance),
        "Mutant accepted an absent target");
    verify(!open_battery_mutated_library(
        library,"invalid function(",OpenBatteryMutation::baseline),
        "An unsafe C++ function name entered the wrapper");
    verify(!open_battery_mutated_library(
        std::string(library)+std::string(library),
        "intentar_ajuste",OpenBatteryMutation::baseline),
        "Ambiguous original function definitions passed");
}
void parsing() {
    const auto witness=open_battery_parse_calls(
        "unrelated diagnostic\nLGAED_OPEN_BATTERY_CALL:3,3,7,59\r\n"
        "LGAED_OPEN_BATTERY_CALL:-20,20,20,-8\n");
    verify(witness&&witness->size()==2&&
           witness->at(0)==OpenBatteryCall{3,3,7,59}&&
           witness->at(1)==OpenBatteryCall{-20,20,20,-8},
           "CRLF or signed integer evidence failed");
    for(const auto malformed:{"LGAED_OPEN_BATTERY_CALL:3,3,7",
            "LGAED_OPEN_BATTERY_CALL:3,3,7,59,1",
            "LGAED_OPEN_BATTERY_CALL:3,3,7,N",
            "LGAED_OPEN_BATTERY_CALL:2147483648,3,7,59"})
        verify(!open_battery_parse_calls(malformed),
               "An incomplete private execution trace was accepted");
}
void specification() {
    const OpenBatteryProbeSpec correct_spec{
        "src/ajuste.cpp","pruebas/casos.cpp","intentar_ajuste",
        "comprobar","comprobar_ajustes",{originals.begin(),originals.end()}};
    verify(open_battery_probe_spec_valid(correct_spec),
           "An authentic original CMake owner was rejected");
    auto forged=correct_spec;forged.test_path="../escape.cpp";
    verify(!open_battery_probe_spec_valid(forged),
           "A private probe could escape the project");
    forged=correct_spec;forged.library_path=forged.test_path;
    verify(!open_battery_probe_spec_valid(forged),
           "The harness was treated as a shared library");
    forged=correct_spec;forged.original_cases.push_back(forged.original_cases.front());
    verify(!open_battery_probe_spec_valid(forged),
           "Duplicate original calls were accepted");
    forged=correct_spec;forged.test_target="test;escape";
    verify(!open_battery_probe_spec_valid(forged),
           "A test target accepted shell metacharacters");
}
void reporting() {
    const std::string stderr_text=
        "Revisión=OK\nLGAED_OPEN_BATTERY_CALL:3,3,7,59\r\n"
        "Se conservó el destino\n";
    verify(open_battery_student_stderr(stderr_text)==
           "Revisión=OK\nSe conservó el destino\n",
           "Private per-call witness leaked into observable student output");
    verify(open_battery_student_stderr("LGAED_OPEN_BATTERY_CALL:3,3,7,59\n").empty(),
           "A private trace was mistaken for student reporting");
    verify(open_battery_student_stderr("Fallo detectado\n")=="Fallo detectado\n",
           "Ordinary student diagnostics were changed");
}
void agreement() {
    verify(open_battery_expected({2,3,7,59})==std::pair{true,1},
           "Below interval expected +1");
    verify(open_battery_expected({5,3,7,59})==std::pair{true,0},
           "Within interval expected zero");
    verify(open_battery_expected({9,3,7,59})==std::pair{true,-2},
           "Above interval expected -2");
    verify(open_battery_expected({21,3,7,59})==std::pair{false,59},
           "Rejected request must preserve destination");
}
void destination_coverage() {
    const OpenBatteryCall added{3,3,7,59};
    const auto plan=open_battery_mutation_plan(originals,added);
    verify(plan&&plan->size()==25,"Eleven old cases require 25 native phases");
    const auto reference=correct(added);
    for(std::size_t i=0;i<plan->size();++i)
        verify((*plan)[i].mutation==reference[i].mutation&&
               (*plan)[i].target==reference[i].target,
               "Native plan skipped or reordered an observation");
    for(std::size_t i=0;i<originals.size();++i) {
        auto runs=reference;
        runs[2*i+2].exit_code=0;
        verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
               "An accepted or rejected historical call ignored its destination");
        runs=reference;
        runs[2*i+2]=runs[2*i+1];
        verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
               "Repeated acceptance evidence replaced a destination mutation");
    }
    std::vector<OpenBatteryRun> legacy{reference.front()};
    for(std::size_t i=0;i<originals.size();++i)legacy.push_back(reference[2*i+1]);
    legacy.push_back(reference[reference.size()-2]);
    legacy.push_back(reference.back());
    verify(legacy.size()==14&&
           !open_battery_verify(originals,legacy,"project-fingerprint").accepted,
           "A former fourteen-run witness silently acquired v2 credit");
    verify(!open_battery_mutation_plan({},added),"An empty plan was accepted");
    verify(open_battery_objective_version==
           "open-cmake-battery-independent-mutations-v2",
           "The stronger contract reused the previous receipt version");
}
void judgments() {
    for(const auto added:{OpenBatteryCall{3,3,7,59},
                          OpenBatteryCall{7,3,7,-13}}) {
        const auto runs=correct(added);
        const auto verdict=open_battery_verify(originals,runs,"project-fingerprint");
        verify(verdict.accepted&&verdict.added==added,
               "Equivalent new case rejected");
    }
    auto runs=correct({3,3,7,59});
    runs[1].exit_code=0;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Original comparison did not catch a mutation");
    runs=correct({3,3,7,59});
    runs.back().exit_code=0;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "New destination comparison was skipped");
    runs=correct({3,3,7,59});
    runs[2].calls.pop_back();
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Short circuit dropped a later case");
    runs=correct({3,3,7,59});
    runs[4].target=originals[0];
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "An unrelated old mutant replaced the required case");
    runs=correct({3,3,7,59});
    runs.back().mutation=OpenBatteryMutation::invert_acceptance;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "The new destination mutation was replaced with an acceptance test");
    runs=correct({3,3,7,59});
    runs[0].target=OpenBatteryCall{3,3,7,59};
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "A pre-mutated baseline counted as a clean original");
    runs=correct({3,3,7,59});
    runs[1].stdout_bytes=runs.front().stdout_bytes;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "A historical comparison silently returned 1 without reporting its failure");
    runs=correct({3,3,7,59});
    runs[2*originals.size()].stdout_bytes=runs.front().stdout_bytes;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "The final historical comparison silently passed an incorrect report");
    runs=correct({3,3,7,59});
    runs[1].stdout_bytes=runs.front().stdout_bytes;
    runs[1].stderr_bytes="Caso distinto: 2,3,7\n";
    verify(open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Equivalent error reporting on stderr was incorrectly rejected");
    runs=correct({3,3,7,59});
    runs.back().stdout_bytes=runs.front().stdout_bytes;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "A silent destination failure passed without its contrast");
    runs=correct({3,3,7,59});
    runs[2*originals.size()+1].stdout_bytes=runs.front().stdout_bytes;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "A fixed new-case label passed without reporting the acceptance error");
    runs=correct({3,3,7,59});
    runs.front().stdout_bytes.clear();
    runs.front().stderr_bytes.clear();
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "A silent baseline was accepted without a report");
    runs=correct({3,3,7,59});
    runs[1].program_frame="another-source";
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "A different submission borrowed progress");
    runs=correct({3,3,7,59});
    runs[1].executed=false;
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Unexecuted mutant borrowed progress");
    runs=correct({2,3,7,59});
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Repeated input triple counted as independent");
    runs=correct({3,3,7,59});
    runs[0].calls.pop_back();
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Published eleven cases alone gained a receipt");
    runs=correct({3,3,7,59});
    runs[0].calls.push_back({3,3,7,59});
    verify(!open_battery_verify(originals,runs,"project-fingerprint").accepted,
           "Duplicate extra call gained credit");
    std::vector<OpenBatteryRun> none;
    verify(!open_battery_verify(originals,none,"project-fingerprint").accepted,
           "Empty evidence was accepted");
}
}
namespace {
AdjustmentProjectSpec construction_spec() {
    return {"src/ajuste.cpp","publico/control/ajuste.h","control/ajuste.h","intentar_ajuste","ajustes",
        "app/consulta.cpp","consultar_ajustes","pruebas/casos.cpp","comprobar_ajustes",
        "2 3 7\n5 3 7\n9 3 7\n0 7 3\n4 4 4\n21 3 7\n"};
}
std::vector<AdjustmentProjectObservation> construction_calls() {
    // A different, complete construction. Neither its order nor its values
    // are the historical eleven-case extension contract.
    return {{{-20,-15,20,11},true,5},{{0,-20,20,-9},true,0},
        {{20,-20,15,29},true,-5},{{-20,-20,10,4},true,0},
        {{20,-10,20,-7},true,0},{{4,4,4,12},true,0},
        {{-20,20,20,8},true,40},{{20,-20,-20,17},true,-40},
        {{-21,-3,7,31},false,31},{{21,-3,7,-32},false,-32},
        {{0,-21,7,33},false,33},{{0,21,7,-34},false,-34},
        {{0,-3,-21,35},false,35},{{0,-3,21,-36},false,-36},
        {{0,7,3,37},false,37}};
}
void construction_client_text(AdjustmentProjectRun& run,bool equivalent=false) {
    run.stdout_bytes.clear();run.stderr_bytes.clear();
    int accepted=0,rejected=0,sum=0;
    for(std::size_t i=0;i<run.observations.size();++i) {
        const auto& observed=run.observations[i];
        const auto& call=observed.call;
        if(observed.accepted) {
            ++accepted;sum+=observed.destination;
            run.stdout_bytes+=(equivalent?"Solicitud número ":"Peticion=")+std::to_string(i+1)+
                (equivalent?"; cambio obtenido: ":" Ajuste=")+std::to_string(observed.destination)+"\n";
        }else {
            ++rejected;
            run.stderr_bytes+=(equivalent?"Consulta número ":"Rechazo: peticion=")+std::to_string(i+1)+
                (equivalent?", dato recibido ":" valor=")+std::to_string(call.value)+
                " inferior="+std::to_string(call.lower)+" superior="+std::to_string(call.upper)+
                (equivalent?". Intervalo admitido [-20,20]; el inferior no puede ser mayor que el superior. Verifique los datos.\n":
                    ". Se admiten valores de -20 a 20 con inferior <= superior. Revise los tres valores.\n");
        }
    }
    run.stdout_bytes+=(equivalent?"Admitidas: ":"Aceptadas=")+std::to_string(accepted)+
        (equivalent?"\nRechazos: ":" Rechazadas=")+std::to_string(rejected)+
        (equivalent?"\nAcumulado final: ":" Suma=")+std::to_string(sum)+"\n";
    run.exit_code=rejected?1:0;
}
std::vector<AdjustmentProjectRun> construction_runs(std::vector<AdjustmentProjectObservation> battery=construction_calls()) {
    const auto spec=construction_spec();
    const auto plan=adjustment_project_plan(spec,battery);
    verify(plan.has_value(),"A construction did not produce a private plan");
    const auto inputs=adjustment_project_client_inputs(spec);
    std::vector<AdjustmentProjectRun> result;
    for(const auto& phase:*plan) {
        AdjustmentProjectRun run;
        run.program_frame="construction-revision";run.compiled=run.executed=true;
        run.phase=phase;run.exit_code=0;
        if(phase.part==AdjustmentProjectPart::component) {
            for(const auto& call:adjustment_project_component_cases()) {
                const auto expected=open_battery_expected(call);
                run.observations.push_back({call,expected.first,expected.second});
            }
        }else if(phase.part==AdjustmentProjectPart::client) {
            run.standard_input=inputs[phase.sample_index];
            auto requests=adjustment_project_detail::input_calls(run.standard_input);
            verify(requests.has_value(),"The construction input is invalid");
            for(auto call:*requests) {
                call.initial=77;
                const auto expected=open_battery_expected(call);
                run.observations.push_back({call,expected.first,expected.second});
            }
        }else {
            run.observations=battery;
            run.stdout_bytes=phase.assertions_disabled?"Todas las comparaciones coincidieron sin assert.\n":"Comparaciones correctas.\n";
        }
        if(phase.call_index||phase.matching_input) {
            for(std::size_t i=0;i<run.observations.size();++i) {
                auto& selected=run.observations[i];
                if(!((phase.call_index&&*phase.call_index==i)||
                    (phase.matching_input&&adjustment_project_detail::same_request(selected.call,*phase.matching_input))))continue;
                if(phase.mutation==OpenBatteryMutation::invert_acceptance)selected.accepted=!selected.accepted;
                else selected.destination=selected.destination==0?1:0;
            }
            if(phase.part==AdjustmentProjectPart::battery) {
                run.exit_code=1;run.stdout_bytes="Discrepancia observada en caso "+
                    std::to_string(phase.call_index.value_or(0))+".\n";
            }
        }
        if(phase.part==AdjustmentProjectPart::client)construction_client_text(run);
        result.push_back(std::move(run));
    }
    return result;
}
void construction_specifications() {
    const auto spec=construction_spec();
    verify(adjustment_project_spec_valid(spec),"The construction original was rejected");
    verify(adjustment_project_header_valid("// contrato\n#ifndef H\n#define H\nbool intentar_ajuste(int valor,int inferior,int superior,int& ajuste);\n#endif\n",spec),
        "Whitespace and parameter identifiers restricted the public declaration");
    verify(adjustment_project_header_valid("bool intentar_ajuste(int,int,int,int &);",spec),"Unnamed public parameters failed");
    for(const auto header:{"bool intentar_ajuste(int,int,int,int&){return false;}",
            "bool intentar_ajuste(int,int,int,int);","int intentar_ajuste(int,int,int,int&);",
            "bool intentar_ajuste(int,int,int,int&); int copied;"})
        verify(!adjustment_project_header_valid(header,spec),"An incompatible or inline component header passed");
    for(const auto source:{"#include \"control/ajuste.h\"\nint main(){}",
            "  #  include /* public */ <control/ajuste.h> // interface\n",
            "/* before */ # include \"control/ajuste.h\"\n",
            "#inc\\\nlude \\\r\n \"control/ajuste.h\"\n",
            "%:include <control/ajuste.h>\n"})
        verify(adjustment_project_source_includes_header(source,spec),"A real, spaced or continued public include was rejected");
    for(const auto source:{"bool intentar_ajuste(int,int,int,int&); int main(){}",
            "// #include \"control/ajuste.h\"\n",
            "/* #include \"control/ajuste.h\" */\n",
            "const char* s=\"#include \\\"control/ajuste.h\\\"\";\n",
            "const char* s=R\"(#include \"control/ajuste.h\")\";\n",
            "// continued \\\n#include \"control/ajuste.h\"\n",
            "int value; #include \"control/ajuste.h\"\n",
            "#include \"another/ajuste.h\"\n"})
        verify(!adjustment_project_source_includes_header(source,spec),"A redeclaration, comment or literal impersonated a public include");
    auto invalid=spec;invalid.library_path="src/../ajuste.cpp";
    verify(!adjustment_project_spec_valid(invalid),"A private construction escaped its workspace");
    invalid=spec;invalid.test_path=invalid.client_path;
    verify(!adjustment_project_spec_valid(invalid),"The two consumers shared the same source role");
    invalid=spec;invalid.header="other/ajuste.h";
    verify(!adjustment_project_spec_valid(invalid),"An unrelated header obtained the public include root");
    invalid=spec;invalid.client_input="2147483648 0 1";
    verify(!adjustment_project_spec_valid(invalid),"An overflowing int at EOF passed the client domain");
    invalid=spec;invalid.client_input="0 1";
    verify(!adjustment_project_spec_valid(invalid),"An incomplete request passed the client domain");
    invalid=spec;invalid.client_input="+0 -1 +1\n";
    verify(adjustment_project_spec_valid(invalid),"Explicit positive signs were rejected");
    invalid.client_input="";
    verify(adjustment_project_spec_valid(invalid),"The empty, complete batch was excluded");
    verify(adjustment_project_spec_frame(spec)!=adjustment_project_spec_frame(invalid),"Prepared input escaped the authenticated frame");
    CmakeGraphRequirement graph;graph.minimum_cmake_version="3.20";graph.minimum_cmake_version_at_least=true;
    for(const auto& [name,type,path]:std::array{
        std::tuple{spec.library_target,std::string("STATIC_LIBRARY"),spec.library_path},
        std::tuple{spec.client_target,std::string("EXECUTABLE"),spec.client_path},
        std::tuple{spec.test_target,std::string("EXECUTABLE"),spec.test_path}}) {
        CmakeRequiredTarget target;target.name=name;target.type=type;target.sources={path};target.exact_sources=true;
        target.require_explicit_cxx20=target.require_explicit_extensions_off=target.require_explicit_msvc_warnings=true;
        if(name==spec.library_target)target.public_include_roots={"publico"};else target.links_to={spec.library_target};
        graph.targets.push_back(std::move(target));
    }
    verify(adjustment_project_graph_valid(spec,graph),"The actual three-role contract was rejected");
    auto altered=graph;altered.minimum_cmake_version_at_least=false;
    verify(!adjustment_project_graph_valid(spec,altered),"The construction minimum became an exact older contract");
    altered=graph;altered.minimum_cmake_version="3.19";
    verify(!adjustment_project_graph_valid(spec,altered),"CMake before 3.20 was accepted");
    altered=graph;altered.minimum_cmake_version="3.25.1";
    verify(adjustment_project_graph_valid(spec,altered),"A higher CMake minimum was rejected");
    altered=graph;altered.targets[1].sources.push_back(spec.library_path);
    verify(!adjustment_project_graph_valid(spec,altered),"A copied library source entered a consumer");
    altered=graph;altered.targets[2].links_to.clear();
    verify(!adjustment_project_graph_valid(spec,altered),"A checker without the shared library passed");
    altered=graph;altered.targets[0].authored_compile_options={"/W0"};
    verify(!adjustment_project_graph_valid(spec,altered),"Disabling warnings satisfied authored warnings");
    altered=graph;altered.targets[0].public_include_roots.clear();
    verify(!adjustment_project_graph_valid(spec,altered),"The public header propagation was absent");
}
void construction_generation() {
    const auto spec=construction_spec();
    const auto driver=adjustment_project_component_source(spec);
    verify(driver&&driver->find("bool(*)(int,int,int,int&)")!=driver->npos,
        "The component driver did not call the public ABI");
    const auto renamed=adjustment_project_instrumented_library(
        "#include \"control/ajuste.h\"\nauto intentar_ajuste(int x,int lo,int hi,int& out)->bool {out=x<lo?lo-x:x>hi?hi-x:0;return true;}\n",spec,
        {AdjustmentProjectPart::battery,OpenBatteryMutation::baseline,{},0});
    verify(renamed&&renamed->find("#define intentar_ajuste lgaed_adjustment_original_intentar_ajuste")!=renamed->npos&&
        renamed->find("auto intentar_ajuste(int x,int lo,int hi,int& out)->bool")!=renamed->npos,
        "Free implementations were parsed by one textual signature");
    const auto mutation=adjustment_project_instrumented_library(library,spec,
        {AdjustmentProjectPart::battery,OpenBatteryMutation::corrupt_destination,5,0,true});
    verify(mutation&&mutation->find("if(index==5)")!=mutation->npos,
        "A repeated input could not be targeted independently by call index");
    verify(!adjustment_project_instrumented_library(library,spec,
        {AdjustmentProjectPart::component,OpenBatteryMutation::baseline,{},0}),"The independent driver used a substituted component");
    const std::string traces="Antes LGAED_ADJUSTMENT_CALL:0,2,3,7,77,1,1\r\nDespués\nLGAED_ADJUSTMENT_CALL:1,0,7,3,-8,0,-8\n";
    const auto observations=adjustment_project_parse_observations(traces);
    verify(observations&&observations->size()==2&&(*observations)[1]==AdjustmentProjectObservation{{0,7,3,-8},false,-8},
        "The private record lost its return or destination");
    verify(adjustment_project_student_stderr(traces)=="Antes Después\n","Private records changed the student's visible report");
    for(const auto malformed:{"LGAED_ADJUSTMENT_CALL:0,2,3,7,77,1,1", // truncated record
            "LGAED_ADJUSTMENT_CALL:1,2,3,7,77,1,1\n", // wrong sequence
            "LGAED_ADJUSTMENT_CALL:0,2,3,7,77,2,1\n", // not bool
            "LGAED_ADJUSTMENT_CALL:0,2,3,7,77,1,2147483648\n"})
        verify(!adjustment_project_parse_observations(malformed),"Malformed or forged call evidence passed");
}
void construction_judgments() {
    const auto spec=construction_spec();
    const auto accepted=[&](const auto& runs){return adjustment_project_verify(spec,runs,"construction-revision").accepted;};
    const auto reference=construction_runs();
    verify(reference.size()==45&&accepted(reference),"An independent complete construction was rejected");
    verify(reference[1].stdout_bytes=="Peticion=1 Ajuste=1\nPeticion=2 Ajuste=0\nPeticion=3 Ajuste=-2\nPeticion=5 Ajuste=0\nAceptadas=4 Rechazadas=2 Suma=-1\n",
        "The prepared client's independent expected observation changed");
    auto runs=reference;
    for(auto& run:runs)if(run.phase.part==AdjustmentProjectPart::client)construction_client_text(run,true);
    verify(accepted(runs),"Equivalent labels, multiline summary, interval syntax or order wording was rejected");
    runs=reference;
    for(auto& run:runs)if(run.phase.part==AdjustmentProjectPart::client) {
        for(auto at=run.stderr_bytes.find("<=");at!=std::string::npos;at=run.stderr_bytes.find("<="))
            run.stderr_bytes.replace(at,2,"\xE2\x89\xA4");
        for(auto at=run.stderr_bytes.find("peticion");at!=std::string::npos;at=run.stderr_bytes.find("peticion"))
            run.stderr_bytes.replace(at,8,"petición");
    }
    verify(accepted(runs),"UTF-8 accents or the inclusive mathematical comparator changed diagnostic boundaries");
    auto duplicated=construction_calls();duplicated.push_back(duplicated.front());
    const auto duplicate_runs=construction_runs(duplicated);
    verify(accepted(duplicate_runs),"An additional independently checked duplicate was forbidden");
    runs=duplicate_runs;runs[runs.size()-4].exit_code=0;
    verify(!accepted(runs),"An ignored duplicate comparison escaped its own mutation");
    runs=duplicate_runs;runs[runs.size()-2].exit_code=0;
    verify(!accepted(runs),"Calling the evaluated function again supplied supposedly independent acceptance expectations");
    runs=duplicate_runs;runs.back().exit_code=0;
    verify(!accepted(runs),"Calling the evaluated function again supplied supposedly independent destination expectations");
    auto reordered=construction_calls();std::ranges::reverse(reordered);
    verify(accepted(construction_runs(reordered)),"A differently ordered complete table was rejected");
    for(std::size_t missing:{1u,3u,4u,6u,7u,8u,9u,10u,11u,12u,13u,14u}) {
        auto calls=construction_calls();calls.erase(calls.begin()+static_cast<std::ptrdiff_t>(missing));
        // Boundary calls 6/7 are also the only positive lower/negative upper
        // domain endpoints; these selected rows establish distinct duties.
        verify(!adjustment_project_battery_coverage(calls).accepted,"A missing battery obligation was hidden by a fixed row count");
    }
    runs=reference;runs.front().observations[0].destination=999;
    verify(!accepted(runs),"A wrong original component was certified by its own checker");
    runs=reference;runs.front().stdout_bytes="Mensaje de biblioteca\n";
    verify(!accepted(runs),"The component printed its own client output");
    runs=reference;runs.front().observations.pop_back();
    verify(!accepted(runs),"An early component exit supplied incomplete ABI evidence");
    runs=reference;runs[3].stdout_bytes=runs[1].stdout_bytes;
    verify(!accepted(runs),"A client's prepared hardcoded report passed a different batch");
    runs=reference;runs[1].stdout_bytes.replace(runs[1].stdout_bytes.find("Suma=-1"),7,"Suma=-3");
    verify(!accepted(runs),"A preserved rejection destination was counted as new");
    runs=reference;runs[1].observations.pop_back();
    verify(!accepted(runs),"The client stopped after a rejected request");
    runs=reference;runs[1].stderr_bytes.clear();
    verify(!accepted(runs),"A client omitted rejection diagnostics");
    runs=reference;runs[1].stderr_bytes.replace(runs[1].stderr_bytes.find("valor=21"),8,"valor=20");
    verify(!accepted(runs),"Rejection data were attributed to another request");
    runs=reference;
    for(auto at=runs[1].stderr_bytes.find("-20 a 20");at!=std::string::npos;at=runs[1].stderr_bytes.find("-20 a 20"))
        runs[1].stderr_bytes.replace(at,8,"-19 a 20");
    verify(!accepted(runs),"A different allowed domain passed by merely mentioning numbers");
    runs=reference;
    for(auto at=runs[1].stderr_bytes.find("<=");at!=std::string::npos;at=runs[1].stderr_bytes.find("<="))
        runs[1].stderr_bytes.replace(at,2,"<");
    verify(!accepted(runs),"A strict interval condition excluded a legal degenerate interval");
    runs=reference;
    const auto condition=runs[1].stderr_bytes.find("con inferior <= superior");
    runs[1].stderr_bytes.replace(condition,24,"pero no se exige inferior <= superior");
    verify(!accepted(runs),"A negated ordering requirement passed by merely spelling its comparison");
    runs=reference;
    for(auto at=runs[1].stderr_bytes.find("Revise");at!=std::string::npos;at=runs[1].stderr_bytes.find("Revise",at+9))
        runs[1].stderr_bytes.replace(at,6,"No revise");
    verify(!accepted(runs),"Negated review instructions passed the diagnostic facts");
    runs=reference;runs[6].observations.clear();
    verify(!accepted(runs),"Calls inside assert disappeared under NDEBUG but earned credit");
    runs=reference;runs[6].exit_code=1;
    verify(!accepted(runs),"A failing NDEBUG baseline passed");
    runs=reference;runs[5].stdout_bytes.clear();runs[6].stdout_bytes.clear();
    verify(accepted(runs),"A checker was forced to print a success message absent from the contract");
    runs=reference;
    const auto second= runs[1].stderr_bytes.find("Rechazo: peticion=6");
    runs[1].stderr_bytes= runs[1].stderr_bytes.substr(0,second)+"Rechazo: peticion=6 valor=21 inferior=3 superior=7.\n";
    verify(!accepted(runs),"A complete first rejection masked missing advice on the next rejected request");
    runs=reference;runs[7].stdout_bytes=runs[1].stdout_bytes;runs[7].stderr_bytes=runs[1].stderr_bytes;runs[7].exit_code=runs[1].exit_code;
    verify(!accepted(runs),"A client called but ignored the library's actual acceptance");
    runs=reference;runs[8].stdout_bytes=runs[1].stdout_bytes;runs[8].stderr_bytes=runs[1].stderr_bytes;runs[8].exit_code=runs[1].exit_code;
    verify(!accepted(runs),"A client recomputed the adjustment after discarding the real destination");
    runs=reference;runs[7].observations.resize(1);runs[7].stdout_bytes="";runs[7].stderr_bytes="Componente incoherente\n";runs[7].exit_code=2;
    verify(accepted(runs),"A defensive client was required to continue after a deliberately broken component");
    runs=reference;runs[17].observations.pop_back();
    verify(!accepted(runs),"A battery short circuit left later comparisons unobserved");
    runs=reference;runs.back().stdout_bytes=runs[6].stdout_bytes;runs.back().stderr_bytes=runs[6].stderr_bytes;
    verify(!accepted(runs),"A changed exit without a changed battery report passed");
    runs=reference;runs.back().phase=runs[runs.size()-2].phase;
    verify(!accepted(runs),"An acceptance mutant impersonated a destination mutant");
    runs=reference;runs.back().phase.assertions_disabled=false;
    verify(!accepted(runs),"A Debug-only mutation impersonated the required NDEBUG execution");
    runs=reference;runs[3].standard_input=spec.client_input;
    verify(!accepted(runs),"A copied client report borrowed another input's execution");
    runs=reference;runs[3].program_frame="previous-client-source";
    verify(!accepted(runs),"Different component/client revisions were combined");
    runs=reference;runs[4].compiled=false;
    verify(!accepted(runs),"An uncompiled private variant earned construction credit");
}
}
int main()try{
    generated();parsing();specification();reporting();agreement();destination_coverage();judgments();
    construction_specifications();construction_generation();construction_judgments();
    std::cout<<"PASS open-battery portable core: original, new, mutation and provenance\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL open-battery core: "<<error.what()<<'\n';return 1;
}

