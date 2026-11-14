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
    // A prose connector containing the letter 'n' is not an LF. The
    // range noun remains in the same sentence as the actual limits.
    for(const auto interval:{"rango con números de 0 a 20",
                             "dominio con valores 0..20",
                             "intervalo con extremos 0 - 20"}) {
        require(evaluate_rejection_diagnostics(
            boxes(interval,"Revise"),RejectionOperation::boxes,box_facts).correct,
            "An ordinary Spanish connector was mistaken for a sentence boundary");
    }
    for(const auto interval:{"rango con números de 0 a 9",
                             "dominio con valores 0..9",
                             "intervalo con extremos 0 - 9"}) {
        require(evaluate_rejection_diagnostics(
            balances(interval,"Verifique"),RejectionOperation::balance,
            balance_facts).correct,
            "The letter n inside a connector hid valid balance interval facts");
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
    // An unrelated test example after a sentence boundary is not another
    // claim about the admitted interval.
    for(const auto message:{
        "rango permitido de 0 a 9; ejemplo independiente: 2 a 2",
        "intervalo 0..9. Otra observación: 2 a 2",
        "intervalo de 0 a 9; también se observa 10 y 0"
    }) {
        require(evaluate_rejection_diagnostics(
            balances(message,"Compruebe"),RejectionOperation::balance,
            balance_facts).correct,
            "An independent example was misread as a contradictory domain");
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
    for(const auto contradiction:{
        "rango permitido de 0 a 9; rango permitido de 0 a 8",
        "rango de 1 a 9; intervalo admitido de 0 a 9",
        "dominio válido [0, 9]; intervalo permitido [0, 10]",
        "rango 0-9; dominio 0-8",
        "intervalo 0 - 9. Rango admitido de 0 a 10"
    }) {
        require(!evaluate_rejection_diagnostics(
            balances(contradiction,"Revise"),RejectionOperation::balance,
            balance_facts).correct,
            "A second contradictory permitted range borrowed credit from the first");
    }
    // A mathematically correct pair of bounds is not a statement of what
    // is ALLOWED if the message explicitly calls that range forbidden.
    for(const auto invalid:{
        "rango no permitido de 0 a 9",
        "intervalo prohibido 0..9",
        "dominio inválido [0, 9]",
        "rango no es válido de 0 a 9",
        "no se admiten enteros entre 0 y 9",
        "no se permite el rango 0 - 9",
        "no se aceptan valores entre 0 y 9"
    }) {
        require(!evaluate_rejection_diagnostics(
            balances(invalid,"Revise"),RejectionOperation::balance,
            balance_facts).correct,
            "A negated or prohibited range was mistaken for an allowed domain");
    }
    for(const auto invalid:{
        "rango 0..9 prohibido",
        "rango [0, 9] no válido",
        "intervalo 0 - 9 no es permitido",
        "dominio de 0 a 9 no está permitido",
        "rango 0..9 inválido"
    }) {
        require(!evaluate_rejection_diagnostics(
            balances(invalid,"Revise"),RejectionOperation::balance,
            balance_facts).correct,
            "A forbidden suffix after the real bounds gained semantic credit");
    }
    const auto valid_with_independent_negation=std::string(
        "Saldo, petición 2, cobros=10, devoluciones=0: "
        "no se admite 21; rango permitido de 0 a 9. Revise los datos.\n"
        "Saldo, petición 3, cobros=0, devoluciones=-1: "
        "no se admite 21; rango permitido de 0 a 9. Revise los datos.\n");
    require(evaluate_rejection_diagnostics(
        valid_with_independent_negation,RejectionOperation::balance,
        balance_facts).correct,
        "Rejecting a different observed value suppressed a separate valid allowed range");
    const auto valid_after_range=std::string(
        "Saldo, petición 2, cobros=10, devoluciones=0: "
        "rango permitido de 0 a 9; no se permite 21. Revise los datos.\n"
        "Saldo, petición 3, cobros=0, devoluciones=-1: "
        "rango permitido de 0 a 9; no se permite 21. Revise los datos.\n");
    require(evaluate_rejection_diagnostics(
        valid_after_range,RejectionOperation::balance,
        balance_facts).correct,
        "An unrelated forbidden value after a valid domain blocked a genuine review");
    for(const auto phrase:{
        "valores no permitidos: 21; rango permitido de 0 a 9",
        "límite no permitido 21; rango permitido de 0 a 9",
        "no se admiten valores -1; rango permitido de 0 a 9"
    }) {
        const auto message=[&](int request,int charges,int returns) {
            return "Saldo, petición "+std::to_string(request)+
                ", cobros="+std::to_string(charges)+
                ", devoluciones="+std::to_string(returns)+
                ": "+std::string(phrase)+". Revise los datos.\n";
        };
        const auto valid=message(2,10,0)+message(3,0,-1);
        require(evaluate_rejection_diagnostics(
            valid,RejectionOperation::balance,balance_facts).correct,
            "Rejecting a specific outside value was misread as banning the allowed domain");
    }
    // An actual sentence or statement boundary is still not a valid
    // link between a range label and otherwise unrelated numeric data.
    for(const auto interval:{"rango con números; 0 a 9",
                             "dominio con valores. 0 a 9",
                             "intervalo con extremos; 0 - 9"}) {
        require(!evaluate_rejection_diagnostics(
            balances(interval,"Revise"),RejectionOperation::balance,
            balance_facts).correct,
            "A separated numeric example was credited as the stated permitted domain");
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
