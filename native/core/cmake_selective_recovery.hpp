#pragma once

#include "core/lesson.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lgaed::curriculum::selective_recovery {

// Both forms belong to the same reviewed History task. The individual form
// preserves the seven independent arithmetic examples, but only input 1
// drives the required failure/recovery sequence. Provenance of the revised
// checker is authenticated separately by the lesson loader.
struct Goal {
    bool individual = false;
    std::string standard_input;
    std::string broken_output;
    std::string restored_output;
};

inline constexpr std::array<std::pair<int,int>,7> reviewed_cases{{
    {0,0},{4,1},{8,2},{20,5},{1,1},{5,2},{17,5}
}};

inline std::string comparison_output(int pieces,int expected,bool broken) {
    const int actual=broken?pieces/4:(pieces+3)/4;
    return "piezas="+std::to_string(pieces)+" esperado="+std::to_string(expected)
        +" obtenido="+std::to_string(actual)+(actual==expected?" OK\n":" FALLO\n");
}

inline std::string individual_output(int pieces,int expected,bool broken) {
    const int actual=broken?pieces/4:(pieces+3)/4;
    return comparison_output(pieces,expected,broken)
        +(actual==expected?"Bateria: OK\n":"Bateria: FALLO\n");
}

inline std::string aggregate_output(bool broken) {
    std::string result;
    bool correct=true;
    for(const auto [pieces,expected]:reviewed_cases) {
        const int actual=broken?pieces/4:(pieces+3)/4;
        correct=correct&&actual==expected;
        result+=comparison_output(pieces,expected,broken);
    }
    return result+(correct?"Bateria: OK\n":"Bateria: FALLO\n");
}

inline bool compatible_case(const OutputCase& item,std::string_view expected) {
    return item.outputs.size()==1&&item.outputs.front()==expected&&
        item.exit_code==0&&
        (!item.executable||*item.executable=="comprobar_cajas")&&
        item.standard_error==StandardErrorExpectation::ignored&&
        (!item.standard_error_exact||item.standard_error_exact->empty())&&
        !item.balance_three_pairs;
}

inline std::optional<Goal> goal(const OutputCheck& check) {
    if(check.input_comparison!=TextComparison::exact||
       check.rejection_operation||check.observation_report||check.assertion_check||
       check.open_battery_check||check.boolean_battery_result||check.reduction_check||
       check.integer_battery_check||check.adjustment_project_check)
        return {};
    if(check.cases.size()==1) {
        const auto& item=check.cases.front();
        const auto restored=aggregate_output(false);
        if(!item.standard_input.empty()||!compatible_case(item,restored))return {};
        return Goal{false,{},aggregate_output(true),restored};
    }
    if(check.cases.size()!=reviewed_cases.size()||
       check.cases.front().standard_input!="1\n")
        return {};
    std::array<bool,reviewed_cases.size()> seen{};
    for(const auto& item:check.cases) {
        bool found=false;
        for(std::size_t i=0;i<reviewed_cases.size();++i) {
            const auto [pieces,expected]=reviewed_cases[i];
            if(item.standard_input!=std::to_string(pieces)+"\n")continue;
            if(seen[i]||!compatible_case(item,individual_output(pieces,expected,false)))
                return {};
            seen[i]=true;
            found=true;
            break;
        }
        if(!found)return {};
    }
    return Goal{true,"1\n",individual_output(1,1,true),individual_output(1,1,false)};
}

} // namespace lgaed::curriculum::selective_recovery
