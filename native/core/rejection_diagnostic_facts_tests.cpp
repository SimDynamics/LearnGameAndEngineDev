#include "core/rejection_diagnostic_facts.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::RejectionFacts;
using lgaed::curriculum::RejectionOperation;
using lgaed::curriculum::evaluate_rejection_diagnostics;

void require(bool result,std::string_view what) {
    if(!result)throw std::runtime_error(std::string(what));
}

std::string boxes(std::string_view interval,std::string_view action) {
    return "Cajas, petición 1, piezas=-1: rango permitido " +
        std::string(interval)+". "+std::string(action)+" la cantidad.\n"+
        "Cajas, petición 3, piezas=21: rango permitido "+
        std::string(interval)+". "+std::string(action)+" la cantidad.\n";
}
std::string balances(std::string_view interval,std::string_view action) {
    return "Saldo, petición 2, cobros=10, devoluciones=0: "
        "intervalo entero "+std::string(interval)+". "+
        std::string(action)+" los datos.\n"+
        "Saldo, petición 3, cobros=0, devoluciones=-1: "
        "intervalo entero "+std::string(interval)+". "+
        std::string(action)+" los datos.\n";
}

void accepted_semantics() {
    constexpr RejectionFacts box_facts[]{{1,-1},{3,21}};
    constexpr RejectionFacts balance_facts[]{{2,10,0},{3,0,-1}};
    // The same allowed interval can be expressed in ordinary prose,
    // spaced dashes or a mathematical closed interval. Preserve meaning.
    for(const auto interval:{"de 0 a 20","0-20","0 - 20","0..20",
                             "[0,20]","[0, 20]","[ 0 , 20 ]"}) {
        for(const auto action:{"Revise","Verifique","Compruebe"}) {
            require(evaluate_rejection_diagnostics(
                boxes(interval,action),RejectionOperation::boxes,box_facts).correct,
                "An equivalent boxes interval or corrective instruction was rejected");
        }
    }
    for(const auto interval:{"de 0 a 9","0-9","0 - 9","0..9",
                             "[0,9]","[0, 9]","[ 0 , 9 ]"}) {
        for(const auto action:{"Revise","Verifique","Compruebe"}) {
            require(evaluate_rejection_diagnostics(
                balances(interval,action),RejectionOperation::balance,balance_facts).correct,
                "An equivalent balances interval or corrective instruction was rejected");
        }
    }
    // An explicitly named lower and upper bound states the same integer
    // domain without requiring a single arbitrary range punctuation style.
    for(const auto interval:{"mínimo 0, máximo 20",
                             "limite inferior 0; limite superior 20",
                             "mínimo permitido de 0 y máximo permitido de 20"}) {
        for(const auto action:{"Revise","Verifique","Compruebe"}) {
            require(evaluate_rejection_diagnostics(
                boxes(interval,action),RejectionOperation::boxes,box_facts).correct,
                "A complete boxes min/max domain was rejected");
        }
    }
    for(const auto interval:{"mínimo 0, máximo 9",
                             "límite inferior 0, límite superior 9",
                             "mínimo permitido de 0; máximo permitido de 9"}) {
        for(const auto action:{"Revise","Verifique","Compruebe"}) {
            require(evaluate_rejection_diagnostics(
                balances(interval,action),RejectionOperation::balance,balance_facts).correct,
                "A complete balances min/max domain was rejected");
        }
    }
    // Distinct output streams are mandatory even with a different interval
    // spelling; no generic acknowledgement is accepted as two diagnoses.
    const auto separated=boxes("[0,20]","Revise");
    const auto cut=separated.find('\n')+1;
    const auto stream=separated.substr(0,cut)+'\x1e'+separated.substr(cut);
    require(lgaed::curriculum::evaluate_rejection_stream_probe(
        stream,"",RejectionOperation::boxes).correct,
        "Alternative ostream evidence rejected a valid closed interval");
}

void rejected_semantics() {
    constexpr RejectionFacts box_facts[]{{1,-1},{3,21}};
    constexpr RejectionFacts balance_facts[]{{2,10,0},{3,0,-1}};
    for(const auto invalid:{"0, 9","[0, 9)","(0, 9]","[0, 8]",
                            "0 - 8","0 - 10","[9,0]"}) {
        require(!evaluate_rejection_diagnostics(
            balances(invalid,"Revise"),RejectionOperation::balance,balance_facts).correct,
            "Wrong bounds or a missing range bracket were falsely accepted");
    }
    for(const auto invalid:{"0, 20","[0, 20)","[0, 19]","0 - 21"}) {
        require(!evaluate_rejection_diagnostics(
            boxes(invalid,"Revise"),RejectionOperation::boxes,box_facts).correct,
            "Wrong boxes bounds or an ambiguous comma were falsely accepted");
    }
    // The true data fields cannot substitute for both explicit bounds.
    // Repetition with a contradictory bound also must never grant credit.
    for(const auto interval:{"mínimo 0",
                             "máximo 9",
                             "mínimo 1, máximo 9",
                             "mínimo 0, máximo 8",
                             "mínimo 0, máximo 9, máximo 10",
                             "mínimo 0, mínimo 1, máximo 9",
                             "mínimo cobros 0, máximo 9",
                             "límite inferior 1, límite superior 9",
                             "límite inferior 0, límite superior 10"}) {
        require(!evaluate_rejection_diagnostics(
            balances(interval,"Revise"),RejectionOperation::balance,balance_facts).correct,
            "A missing, contradictory or unbound min/max statement was accepted");
    }
    for(const auto contradictory:{
        "rango 0..9; mínimo 1, máximo 9",
        "rango [0, 9]; mínimo 0 y máximo 10",
        "rango 0 - 9; mínimo 0, máximo 8",
        "rango de 0 a 9; límite inferior 1",
        "rango de 0 a 9; límite superior 10"
    }) {
        require(!evaluate_rejection_diagnostics(
            balances(contradictory,"Revise"),
            RejectionOperation::balance,balance_facts).correct,
            "An authentic interval masked contradictory explicit lower or upper bounds");
    }
    const auto unlabelled=std::string(
        "Cajas, petición 1, piezas=-1: 0 - 20, revise.\n"
        "Cajas, petición 3, piezas=21: 0 - 20, revise.\n");
    require(!evaluate_rejection_diagnostics(
        unlabelled,RejectionOperation::boxes,box_facts).correct,
        "Two otherwise unlabelled numeric values were mistaken for a range");
    auto switched=balances("[0, 9]","Verifique");
    const auto found=switched.find("cobros=10, devoluciones=0");
    require(found!=std::string::npos,"Expected the original balance-field fixture");
    switched.replace(found,std::string_view("cobros=10, devoluciones=0").size(),
                     "cobros=0, devoluciones=10");
    require(!evaluate_rejection_diagnostics(
        switched,RejectionOperation::balance,balance_facts).correct,
        "A correct range hid a swapped pair of rejected field values");
    require(!evaluate_rejection_diagnostics(
        balances("[0, 9]","No revise"),RejectionOperation::balance,
        balance_facts).correct,
        "A negated corrective instruction was accepted as an affirmative action");
}
} // namespace

int main() {
    try {
        accepted_semantics();
        rejected_semantics();
        std::cout<<"PASS U3L6 semantic diagnostics: closed intervals, spaced dashes, "
            "three valid review actions, field identities and negative cases\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL U3L6 diagnostic semantics: "<<error.what()<<'\n';
        return 1;
    }
}
