#include "core/open_battery_check.hpp"
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
int main()try{
    generated();parsing();specification();reporting();agreement();destination_coverage();judgments();
    std::cout<<"PASS open-battery portable core: original, new, mutation and provenance\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL open-battery core: "<<error.what()<<'\n';return 1;
}
