#include "core/boolean_battery_check.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed::curriculum;
void require(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(std::string(message));
}
BooleanBatterySpec specification(bool broken) {
    BooleanBatterySpec result;
    result.function_name="debe_reponer";
    result.protected_definition="bool debe_reponer(int disponibles, int minimo) {\n    return disponibles "+
        std::string(broken?"<":"<=")+" minimo;\n}";
    result.cases={{4,5,true},{5,5,true},{6,5,false}};
    if(!broken)result.cases.insert(result.cases.end(),{{7,8,true},{8,8,true},{9,8,false}});
    result.published_result=broken?BooleanBatteryPublishedResult::failure:BooleanBatteryPublishedResult::success;
    return result;
}
std::vector<BooleanBatteryRun> evidence(const BooleanBatterySpec& spec) {
    // Construct the expected phase order independently of the production plan.
    std::vector<BooleanBatteryRun> runs(spec.cases.size()+2);
    for(std::size_t i=0;i<runs.size();++i) {
        auto& run=runs[i];
        run.program_frame="student-source-and-profile";
        run.compiled=run.executed=run.completed_normally=true;
        for(const auto& c:spec.cases)run.calls.push_back({c.first,c.second});
        if(i==0)continue; // An authentic correct control may be silent.
        if(i+1==runs.size()) {
            run.mutation=BooleanBatteryMutation::published;
            if(spec.published_result==BooleanBatteryPublishedResult::failure) {
                run.exit_code=1;
                run.stdout_bytes="La comparación de igualdad discrepa.\n";
            }
        }else {
            run.mutation=BooleanBatteryMutation::invert_result;
            run.target=BooleanBatteryInput{spec.cases[i-1].first,spec.cases[i-1].second};
            run.exit_code=1;
            run.stdout_bytes="Discrepancia en caso "+std::to_string(i)+".\n";
        }
    }
    return runs;
}
bool accepted(const BooleanBatterySpec& spec,const std::vector<BooleanBatteryRun>& runs) {
    return boolean_battery_verify(spec,runs,"student-source-and-profile").accepted;
}
void grammar() {
    const auto spec=specification(true);
    const auto canonical="#include <iostream>\n"+spec.protected_definition+
        "\nint main() { return debe_reponer(4,5) ? 0 : 1; }\n";
    const auto extracted=boolean_battery_extract_definition(canonical,spec.function_name);
    require(extracted&&*extracted==spec.protected_definition,"Published definition was not extracted exactly");
    require(boolean_battery_preserves_function(canonical,spec),"An ordinary return call was mistaken for a declaration");
    const std::string spaced=R"cpp(
#include <iostream>
// bool debe_reponer(int disponibles,int minimo) { return false; }
const char* quoted = R"text(bool debe_reponer(int a,int b) { } )text";
bool debe_reponer(int disponibles,int minimo);
int main() { return debe_reponer(4,5) ? 0 : 1; }
bool /* name */ debe_reponer ( int disponibles , int minimo ) {
    // A brace in a comment: }
    return disponibles /* preserve comparison */ < minimo;
}
)cpp";
    require(boolean_battery_preserves_function(spaced,spec),"Whitespace, comments or an exact prototype changed the protected contract");
    for(const auto& bad:std::vector<std::string>{
        "bool debe_reponer(int disponibles,int minimo) { return disponibles <= minimo; }",
        "bool debe_reponer(int minimo,int disponibles) { return disponibles < minimo; }",
        "static "+spec.protected_definition,
        "inline "+spec.protected_definition,
        "namespace hidden { "+spec.protected_definition+" }",
        "struct Hidden { "+spec.protected_definition+" };",
        spec.protected_definition+"\n"+spec.protected_definition,
        spec.protected_definition+"\nbool debe_reponer(int disponibles,int minimo,int otro) { return false; }",
        spec.protected_definition+"\nbool& debe_reponer(int a,int b,int c) { static bool value; return value; }",
        spec.protected_definition+"\nbool& debe_reponer(int a,int b,int c);",
        "bool debe_reponer(int a,int b);\n"+spec.protected_definition,
        "#define debe_reponer replacement\n"+spec.protected_definition,
        "/* before directive */ #define X 1\n"+spec.protected_definition,
        "// fake " + spec.protected_definition,
        "debe_reponer(int disponibles,int minimo) { return true; }",
        "bool debe_reponer(int disponibles,int minimo) { return disponibles < minimo;",
        "bool debe_reponer(int disponibles,int minimo) {\n#include <string>\nreturn true; }",
    })require(!boolean_battery_preserves_function(bad,spec),"Altered, ambiguous or inactive protected function was accepted");
    auto invalid=spec;
    invalid.protected_definition+="\nint another;";
    require(!boolean_battery_spec_valid(invalid),"Protected definition accepted unrelated declarations");
    require(!boolean_battery_preserves_function(std::string(1,'\0')+canonical,spec),
        "A binary source byte entered the protected program");
}
void contracts() {
    for(bool broken:{false,true}) {
        const auto spec=specification(broken);
        require(boolean_battery_spec_valid(spec),"A real U1 case contract was rejected");
        const auto plan=boolean_battery_mutation_plan(spec);
        const auto runs=evidence(spec);
        require(plan&&plan->size()==spec.cases.size()+2,"Missing independent or published phase");
        for(std::size_t i=0;i<runs.size();++i)
            require((*plan)[i].mutation==runs[i].mutation&&(*plan)[i].target==runs[i].target,
                "Mutation plan substituted or reordered a declared case");
    }
    auto spec=specification(false);
    spec.cases.clear();
    require(!boolean_battery_spec_valid(spec),"Empty table accepted");
    spec=specification(false);spec.cases.push_back(spec.cases.front());
    require(!boolean_battery_spec_valid(spec),"Repeated pair accepted");
    spec.cases.back().expected=!spec.cases.back().expected;
    require(!boolean_battery_spec_valid(spec),"Conflicting duplicate pair accepted");
    spec=specification(false);spec.cases.clear();
    for(int i=0;i<33;++i)spec.cases.push_back({i,1,true});
    require(!boolean_battery_spec_valid(spec),"Unbounded mutation table accepted");
    spec.cases.resize(32);
    require(boolean_battery_spec_valid(spec),"Thirty-two distinct cases rejected");
    spec=specification(false);spec.published_result=static_cast<BooleanBatteryPublishedResult>(99);
    require(!boolean_battery_spec_valid(spec),"Unknown published outcome accepted");
    require(boolean_battery_objective_version=="open-bool-pair-battery-independent-mutations-v1",
        "Boolean probes reused CMake receipts");
}
void generation() {
    const auto spec=specification(true);
    const std::string suffix="\nint main() { std::cout << __FILE__ << ':' << __LINE__; return 0; }\n";
    const auto source=spec.protected_definition+suffix;
    const auto plan=*boolean_battery_mutation_plan(spec);
    std::vector<std::string> variants;
    for(const auto& phase:plan) {
        const auto program=boolean_battery_instrument(source,spec,phase);
        require(program&&program->ends_with(suffix),"Instrumentation rewrote the student's driver");
        require(program->find("#line 1\n")!=program->npos&&
            program->find("#line 3\n")!=program->npos,"Instrumentation changed the driver line coordinates");
        require(program->find("LGAED_U1_MUTANT")==program->npos&&
            program->find("getenv")==program->npos,"A phase selector was exposed to the driver");
        variants.push_back(*program);
    }
    for(std::size_t i=0;i<variants.size();++i)
        for(std::size_t j=0;j<i;++j)require(variants[i]!=variants[j],"Independent phases compiled the same source");
    require(!boolean_battery_instrument(source,spec,{BooleanBatteryMutation::invert_result,{}}),
        "Mutant without target accepted");
    require(!boolean_battery_instrument(source,spec,{BooleanBatteryMutation::published,BooleanBatteryInput{4,5}}),
        "Published phase carried a mutation target");
    require(!boolean_battery_instrument(source,spec,{BooleanBatteryMutation::invert_result,BooleanBatteryInput{7,8}}),
        "A mutation not in the authored table was generated");
    require(!boolean_battery_instrument(specification(false).protected_definition,spec,plan.front()),
        "A repaired published defect was instrumented");
}
void traces() {
    const auto trace=boolean_battery_parse_trace(
        "prefijo=LGAED_BOOLEAN_BATTERY_CALL:-2147483648,2147483647\r\n"
        "mensaje\nLGAED_BOOLEAN_BATTERY_END\r\nfinal\n");
    require(trace&&trace->completed_normally&&trace->calls==std::vector<BooleanBatteryInput>{{
        std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}},
        "Signed int32 or CRLF witness was lost");
    require(trace->visible=="prefijo=mensaje\nfinal\n","Private evidence leaked or changed visible diagnostics");
    const auto missing=boolean_battery_parse_trace("LGAED_BOOLEAN_BATTERY_CALL:4,5\n");
    require(missing&&!missing->completed_normally,"Missing normal completion became success");
    for(const auto bad:{
        "LGAED_BOOLEAN_BATTERY_CALL:4\n",
        "LGAED_BOOLEAN_BATTERY_CALL:4,5,6\n",
        "LGAED_BOOLEAN_BATTERY_CALL:2147483648,5\n",
        "LGAED_BOOLEAN_BATTERY_CALL:4,x\n",
        "LGAED_BOOLEAN_BATTERY_CALL:4,5",
        "LGAED_BOOLEAN_BATTERY_END",
        "LGAED_BOOLEAN_BATTERY_ENDx\n",
        "LGAED_BOOLEAN_BATTERY_END\nLGAED_BOOLEAN_BATTERY_END\n",
        "LGAED_BOOLEAN_BATTERY_END\nLGAED_BOOLEAN_BATTERY_CALL:4,5\n",
    })require(!boolean_battery_parse_trace(bad),"Malformed or duplicated private proof accepted");
    std::string excessive;
    for(int i=0;i<33;++i)excessive+="LGAED_BOOLEAN_BATTERY_CALL:4,5\n";
    require(!boolean_battery_parse_trace(excessive),"Unbounded witness accepted");
}
void judgments() {
    for(bool broken:{false,true}) {
        const auto spec=specification(broken);
        const auto good=evidence(spec);
        require(accepted(spec,good),"A valid U1 battery was rejected");
        for(const auto exit:{std::uint32_t{257},std::uint32_t{0xffffffffu}}) {
            auto runs=good;
            for(auto& run:runs)if(run.exit_code)run.exit_code=exit;
            require(accepted(spec,runs),"A normal nonzero Windows exit was restricted to eight bits");
        }
        for(std::size_t index=0;index<good.size();++index) {
            auto runs=good;runs[index].completed_normally=false;
            require(!accepted(spec,runs),"Crash or abort counted as a detected discrepancy");
            runs=good;runs[index].executed=false;
            require(!accepted(spec,runs),"Unexecuted phase counted as evidence");
            runs=good;runs[index].compiled=false;
            require(!accepted(spec,runs),"Failed build counted as evidence");
            runs=good;runs[index].program_frame="another source";
            require(!accepted(spec,runs),"A different source borrowed the receipt");
            runs=good;runs[index].calls.pop_back();
            require(!accepted(spec,runs),"Short circuit skipped a required comparison");
            runs=good;runs[index].calls.push_back(runs[index].calls.front());
            require(!accepted(spec,runs),"A repeated or unrelated call was accepted");
            runs=good;runs[index].exit_code=runs[index].exit_code?0:1;
            require(!accepted(spec,runs),"A fixed or wrong return status was accepted");
        }
        auto runs=good;runs.pop_back();
        require(!accepted(spec,runs),"The published function was never executed");
        runs=good;runs[2]=runs[1];
        require(!accepted(spec,runs),"One passing mutant replaced another case");
        runs=good;runs[1].stdout_bytes.clear();
        require(!accepted(spec,runs),"A discrepancy was never reported");
        runs=good;
        for(std::size_t i=1;i+1<runs.size();++i)runs[i].stdout_bytes="Fallo\n";
        require(!accepted(spec,runs),"Generic identical diagnostics did not identify failed cases");
        runs=good;
        for(std::size_t i=1;i+1<runs.size();++i) {
            runs[i].stderr_bytes=runs[i].stdout_bytes;runs[i].stdout_bytes.clear();
        }
        require(accepted(spec,runs),"Distinct equivalent diagnostics on stderr were rejected");
        runs=good;runs.front().stdout_bytes="Todas correctas\n";
        if(!broken)runs.back().stdout_bytes="Todas correctas\n";
        require(accepted(spec,runs),"Informative success output was incorrectly forbidden");
    }
}
}
int main()try {
    grammar();contracts();generation();traces();judgments();
    std::cout<<"PASS boolean battery: protected source, independent mutations, normal completion and diagnostics\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL boolean battery: "<<error.what()<<'\n';return 1;
}
