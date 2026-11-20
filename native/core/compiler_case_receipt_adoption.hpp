#pragma once

#include "core/lesson.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Reconcile a complete, authenticated historical compiler-case receipt with
// one current edition. Caller validates both receipt vectors and snapshots;
// this function only decides whether their *execution requirements* agree.
// A program selected by an earlier source cannot be mixed with another one.
// The historical case list can be an aggregate: it will not match any
// independent case unless input, outputs, target and channels match exactly.
template<class MakeProof>
[[nodiscard]] bool adopt_compatible_compiler_case_proofs(
        const OutputCheck& current, std::string_view current_client_epoch,
        std::string& current_program, std::vector<std::string>& current_proofs,
        const OutputCheck& historical, std::string_view historical_client_epoch,
        std::string_view historical_program,
        std::span<const std::string> historical_proofs, MakeProof&& make_proof) {
    if(current_proofs.size()!=current.cases.size() ||
       historical.input_comparison!=current.input_comparison ||
       historical_client_epoch!=current_client_epoch ||
       historical_program.empty() ||
       (!current_program.empty() && current_program!=historical_program))
        return false;

    bool added=false;
    for(std::size_t index=0;index<current.cases.size();++index) {
        if(!current_proofs[index].empty()) continue;
        for(std::size_t prior=0;prior<std::min(historical.cases.size(),historical_proofs.size());++prior) {
            if(historical_proofs[prior].empty() ||
               !same_output_case_requirements(historical.cases[prior],current.cases[index]))
                continue;
            const std::string program=current_program.empty()
                ?std::string(historical_program):current_program;
            auto proof=make_proof(program,index,current.cases[index]);
            if(proof.empty())
                throw std::invalid_argument("A compatible compiler case cannot create an empty proof.");
            if(current_program.empty()) current_program=program;
            current_proofs[index]=std::move(proof);
            added=true;
            break;
        }
    }
    return added;
}

} // namespace lgaed::curriculum
