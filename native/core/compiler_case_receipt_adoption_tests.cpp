// Portable checks for exactly the receipt reconciliation used by the Windows
// curricular compiler. No learner state, CMake project or curriculum is edited.
#include "core/compiler_case_receipt_adoption.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::curriculum;

void require(bool success,std::string_view reason) {
    if(!success)throw std::runtime_error(std::string(reason));
}

OutputCase make_case(std::string input,std::string output,
                     std::string target={}) {
    OutputCase result;
    result.standard_input=std::move(input);
    result.outputs={std::move(output)};
    if(!target.empty())result.executable=std::move(target);
    return result;
}

std::string authentic(std::string_view program,std::size_t position,
                      const OutputCase& item) {
    return std::string(program)+"/"+std::to_string(position)+"/"+
        item.standard_input+"/"+item.outputs.front();
}

void completed_first_can_collect_a_later_optional_proof() {
    OutputCheck historical;
    historical.cases={make_case("0\n","0:0:91\n"),
                      make_case("1\n","1:1:1\n"),
                      make_case("2\n","2:1:2\n")};
    OutputCheck revised;
    revised.cases={historical.cases[1],historical.cases[0],historical.cases[2]};
    std::string program="one-student-program";
    std::vector<std::string> current{
        authentic(program,0,revised.cases[0]),"",""};
    const auto first=current.front();
    const std::string completion_seal="previously-earned-first-case-seal";
    std::vector<std::string> earlier{"authentic-old-case-0","",""};
    require(adopt_compatible_compiler_case_proofs(revised,"same-epoch",
        program,current,historical,"same-epoch","one-student-program",earlier,authentic),
        "An earned first case prevented importing a genuinely executed optional case");
    require(current[0]==first && current[1]==authentic(program,1,revised.cases[1])
        && current[2].empty() && program=="one-student-program",
        "Optional adoption changed the original first proof or invented other cases");
    require(completion_seal=="previously-earned-first-case-seal",
        "Optional evidence touched the independently stored completion seal");
    const auto saved=current;
    require(!adopt_compatible_compiler_case_proofs(revised,"same-epoch",
        program,current,historical,"same-epoch","one-student-program",earlier,authentic)
        && current==saved,"Repeating an adoption rewrote an existing case proof");
}

void historical_aggregate_does_not_prove_independent_inputs() {
    OutputCheck historical;
    historical.cases={make_case("1\n0\n","1:1:1\n0:0:91\n")};
    OutputCheck revised;
    revised.cases={make_case("1\n","1:1:1\n"),
                   make_case("0\n","0:0:91\n")};
    std::string program;
    std::vector<std::string> evidence(2);
    std::vector<std::string> previous{"entire-historical-run"};
    require(!adopt_compatible_compiler_case_proofs(revised,"same-epoch",
        program,evidence,historical,"same-epoch","one-student-program",previous,authentic)
        && program.empty() && evidence==std::vector<std::string>(2),
        "The old aggregate falsely credited either newly isolated run");
}

void never_merge_another_program_or_client_epoch() {
    OutputCheck historical;
    historical.cases={make_case("1\n","1:1:1\n"),
                      make_case("0\n","0:0:91\n")};
    OutputCheck current=historical;
    auto first=authentic("program-B",0,current.cases[0]);
    std::string program="program-B";
    std::vector<std::string> proofs{first,""};
    const std::vector<std::string> historical_proofs{"old-0","old-1"};
    require(!adopt_compatible_compiler_case_proofs(current,"epoch-2",
        program,proofs,historical,"epoch-2","program-A",historical_proofs,authentic)
        && proofs==std::vector<std::string>({first,""}),
        "Two student programs shared an earned optional receipt");
    require(!adopt_compatible_compiler_case_proofs(current,"epoch-2",
        program,proofs,historical,"epoch-1","program-B",historical_proofs,authentic)
        && proofs==std::vector<std::string>({first,""}),
        "A prior source epoch granted a receipt to the new source");

    // With no selected program, the first genuinely matching source chooses
    // one program. Any later, different source must be ignored.
    program.clear(); proofs={"",""};
    require(adopt_compatible_compiler_case_proofs(current,"epoch-2",
        program,proofs,historical,"epoch-2","program-A",historical_proofs,authentic)
        && program=="program-A" && !proofs[0].empty() && !proofs[1].empty(),
        "Independent same-program historical evidence could not be adopted");
    const auto adopted=proofs;
    require(!adopt_compatible_compiler_case_proofs(current,"epoch-2",
        program,proofs,historical,"epoch-2","program-B",historical_proofs,authentic)
        && proofs==adopted,"A second program rewrote a selected program's proofs");
}

void same_input_with_different_requirements_is_not_sufficient() {
    OutputCheck old;
    old.cases={make_case("7\n","7:1\n","consultar")};
    OutputCheck now;
    now.cases={old.cases[0],old.cases[0],old.cases[0],old.cases[0]};
    now.cases[0].outputs={"7:0\n"};
    now.cases[1].executable="comprobar";
    now.cases[2].exit_code=1;
    now.cases[3].standard_error_exact="";
    const std::vector<std::string> earlier{"verified-original"};
    std::string program;
    std::vector<std::string> proofs(4);
    require(!adopt_compatible_compiler_case_proofs(now,"epoch",
        program,proofs,old,"epoch","program-A",earlier,authentic)
        && program.empty() && proofs==std::vector<std::string>(4),
        "A changed output/target/exit/stderr borrowed the original receipt");

    now.cases={old.cases[0]};
    now.cases[0].explanation="New authorial feedback; the runtime goal is identical.";
    std::vector<std::string> matching(1);
    require(adopt_compatible_compiler_case_proofs(now,"epoch",
        program,matching,old,"epoch","program-A",earlier,authentic)
        && program=="program-A" && !matching[0].empty(),
        "A prose-only change unnecessarily discarded execution evidence");
}

} // namespace

int main() {
    try {
        completed_first_can_collect_a_later_optional_proof();
        historical_aggregate_does_not_prove_independent_inputs();
        never_merge_another_program_or_client_epoch();
        same_input_with_different_requirements_is_not_sufficient();
        std::cout<<"PASS case-receipt adoption: optional history, unchanged first, "
                    "no cross-program or cross-epoch proof, protected requirements.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL case-receipt adoption: "<<error.what()<<'\n';
        return 1;
    }
}
