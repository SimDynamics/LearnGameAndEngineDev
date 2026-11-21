#include "core/rejection_diagnostic_facts.hpp"

#include <array>
#include <iostream>
#include <limits>
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
                             "0–20","0 — 20","0−20",
                             "[0,20]","[0, 20]","[ 0 , 20 ]"}) {
        for(const auto action:{"Revise","Verifique","Compruebe"}) {
            require(evaluate_rejection_diagnostics(
                boxes(interval,action),RejectionOperation::boxes,box_facts).correct,
                "An equivalent boxes interval or corrective instruction was rejected");
        }
    }
    for(const auto interval:{"de 0 a 9","0-9","0 - 9","0..9",
                             "0–9","0 — 9","0−9",
                             "[0,9]","[0, 9]","[ 0 , 9 ]"}) {
        for(const auto action:{"Revise","Verifique","Compruebe"}) {
            require(evaluate_rejection_diagnostics(
                balances(interval,action),RejectionOperation::balance,balance_facts).correct,
                "An equivalent balances interval or corrective instruction was rejected");
        }
    }
    // A mathematical minus (U+2212) is an actual negative datum, not a
    // lost punctuation mark or a positive integer disguised by Unicode.
    const std::string typographic_boxes=
        "Cajas, petición 1, piezas=−1: rango permitido 0–20. Revise la cantidad.\n"
        "Cajas, petición 3, piezas=21: rango permitido 0–20. Verifique la cantidad.\n";
    require(evaluate_rejection_diagnostics(typographic_boxes,
        RejectionOperation::boxes,box_facts).correct,
        "A mathematical negative sign or Unicode range dropped a boxes fact");
    const std::string typographic_balances=
        "Saldo, petición 2, cobros=10, devoluciones=0: intervalo entero 0–9. Revise los datos.\n"
        "Saldo, petición 3, cobros=0, devoluciones=−1: intervalo entero 0–9. Compruebe los datos.\n";
    require(evaluate_rejection_diagnostics(typographic_balances,
        RejectionOperation::balance,balance_facts).correct,
        "A mathematical negative sign or Unicode range dropped a balance fact");
    require(!evaluate_rejection_diagnostics(boxes("0–21","Revise"),
        RejectionOperation::boxes,box_facts).correct,
        "A wrong Unicode upper bound was mistaken for the permitted domain");
    require(!evaluate_rejection_diagnostics(balances("0–10","Verifique"),
        RejectionOperation::balance,balance_facts).correct,
        "A wrong Unicode balance bound was mistaken for the permitted domain");
    const std::string forged_boxes=
        "Cajas, petición 1, piezas=−2: rango permitido 0–20. Revise la cantidad.\n"
        "Cajas, petición 3, piezas=21: rango permitido 0–20. Revise la cantidad.\n";
    require(!evaluate_rejection_diagnostics(forged_boxes,
        RejectionOperation::boxes,box_facts).correct,
        "A mismatched negative value was accepted after Unicode folding");

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

void validity_claims_keep_their_field_and_clause() {
    const auto message=[](const RejectionFacts& fact,std::string_view claim) {
        return "Saldo, petición "+std::to_string(fact.request)+
            ", cobros="+std::to_string(fact.first)+
            ", devoluciones="+std::to_string(fact.second)+": "+
            std::string(claim)+". Rango permitido de 0 a 9. Revise los datos.\n";
    };
    constexpr RejectionFacts charges{2,10,0},refunds{3,0,-1},both{4,10,-1};
    const auto check_claim=[&](const RejectionFacts& fact,std::string_view claim,bool accepted) {
        const auto review=evaluate_rejection_diagnostics(
            message(fact,claim),RejectionOperation::balance,std::span(&fact,1));
        require(review.correct==accepted,"A validity claim was assigned to the wrong field or clause: "+
            std::string(claim));
    };
    for(const auto claim:{
        "los cobros son inválidos. Ambos valores deben ser enteros de 0 a 9",
        "Ambos valores deben ser enteros de 0 a 9; los cobros son inválidos",
        "los cobros son inválidos, las devoluciones son válidas",
        "solo los cobros son incorrectos",
        "las devoluciones no son inválidas",
        "los cobros no son válidos",
        "son inválidos los cobros",
        "son inválidos los cobros y son válidas las devoluciones",
        "son inválidos los cobros y las devoluciones son válidas",
        "son inválidos los cobros y las devoluciones no son inválidas",
        "son inválidos los cobros, son válidas las devoluciones",
        "son inválidos los cobros y no son inválidas las devoluciones",
        "cobros: inválidos; devoluciones: válidas",
        "no ambos valores son inválidos",
        "ambos valores deben ser válidos"
    })check_claim(charges,claim,true);
    for(const auto claim:{
        "las devoluciones son inválidas. Ambos valores deben ser enteros de 0 a 9",
        "Ambos valores deben ser enteros de 0 a 9; las devoluciones son inválidas",
        "las devoluciones son inválidas, los cobros son válidos",
        "solo las devoluciones son incorrectas",
        "los cobros no son inválidos",
        "las devoluciones no son válidas",
        "son inválidas las devoluciones",
        "son inválidas las devoluciones y son válidos los cobros",
        "son inválidas las devoluciones y los cobros son válidos",
        "son inválidas las devoluciones y los cobros no son inválidos",
        "son inválidas las devoluciones, son válidos los cobros",
        "son inválidas las devoluciones y no son inválidos los cobros",
        "devoluciones: inválidas; cobros: válidos"
    })check_claim(refunds,claim,true);
    for(const auto claim:{
        "solo las devoluciones son inválidas",
        "las devoluciones son incorrectas",
        "los cobros son válidos",
        "los cobros no son inválidos",
        "son inválidas las devoluciones",
        "son inválidos los cobros y son inválidas las devoluciones",
        "son inválidos los cobros y las devoluciones son inválidas",
        "son inválidos los cobros y las devoluciones",
        "son inválidos los cobros, no son válidas las devoluciones",
        "devoluciones: inválidas",
        "cobros: válidos",
        "ambos valores son inválidos",
        "cobros y devoluciones son incorrectos",
        "ambos valores son válidos",
        "ambos valores no son inválidos"
    })check_claim(charges,claim,false);
    for(const auto claim:{
        "solo los cobros son inválidos",
        "los cobros son incorrectos",
        "las devoluciones son válidas",
        "las devoluciones no son inválidas",
        "son inválidos los cobros",
        "son inválidas las devoluciones y son inválidos los cobros",
        "son inválidas las devoluciones y los cobros son inválidos",
        "son inválidas las devoluciones y los cobros",
        "son inválidas las devoluciones, no son válidos los cobros",
        "cobros: inválidos",
        "devoluciones: válidas",
        "ambos valores son inválidos",
        "cobros y devoluciones son incorrectos"
    })check_claim(refunds,claim,false);
    check_claim(both,"ambos valores son inválidos",true);
    check_claim(both,"cobros y devoluciones son incorrectos",true);
    check_claim(both,"son inválidos los cobros y las devoluciones",true);
    check_claim(both,"ambos valores no son válidos",true);
    check_claim(both,"no ambos valores son inválidos",false);
    check_claim(both,"solo los cobros son inválidos",false);
    check_claim(both,"solo las devoluciones son inválidas",false);

    const auto first=message(charges,
        "los cobros son inválidos. Ambos valores deben ser enteros de 0 a 9");
    const auto second=message(refunds,
        "las devoluciones son inválidas. Ambos valores deben ser enteros de 0 a 9");
    const auto stream=first+'\x1e'+second;
    require(lgaed::curriculum::evaluate_rejection_stream_probe(
        stream,"",RejectionOperation::balance).correct,
        "Correct field-specific diagnoses failed the real alternative-stream verifier");
    require(!lgaed::curriculum::evaluate_rejection_stream_probe(
        stream,"salida fuera del flujo",RejectionOperation::balance).correct,
        "Field-specific diagnoses bypassed the supplied ostream requirement");
    require(!lgaed::curriculum::evaluate_rejection_stream_probe(
        message(charges,"solo las devoluciones son inválidas")+'\x1e'+second,
        "",RejectionOperation::balance).correct,
        "The alternative stream accepted a false accusation of the valid operand");
}

void unsupported_claims_keep_their_predicate_and_polarity() {
    constexpr RejectionFacts boxes[]{{1,-1},{3,21}};
    constexpr RejectionFacts balances[]{{2,10,0},{3,0,-1}};
    const auto message=[](RejectionOperation operation,const RejectionFacts& fact,
            std::string_view statement,bool request_review) {
        auto text=std::string(operation==RejectionOperation::boxes?"Cajas":"Saldo")+
            ", petición "+std::to_string(fact.request);
        if(operation==RejectionOperation::boxes)
            text+=", piezas="+std::to_string(fact.first)+": intervalo entero permitido 0..20. ";
        else text+=", cobros="+std::to_string(fact.first)+", devoluciones="+
            std::to_string(fact.second)+": intervalo entero permitido 0..9. ";
        text+=statement;
        if(request_review)text+=" Revise los datos.";
        return text+'\n';
    };
    for(const auto operation:{RejectionOperation::boxes,RejectionOperation::balance}) {
        const auto facts=operation==RejectionOperation::boxes?
            std::span<const RejectionFacts>(boxes):std::span<const RejectionFacts>(balances);
        const auto check_statement=[&](std::string_view statement,bool accepted,
                bool request_review=true) {
            const auto first=message(operation,facts[0],statement,request_review);
            const auto second=message(operation,facts[1],statement,request_review);
            const auto result=evaluate_rejection_diagnostics(first+second,operation,facts);
            require(result.correct==accepted,
                "A diagnostic changed the meaning or scope of a claim: "+std::string(statement)+
                " / "+result.reason);
            const auto streamed=lgaed::curriculum::evaluate_rejection_stream_probe(
                first+'\x1e'+second,"",operation);
            require(streamed.correct==accepted,
                "The alternative ostream disagreed about the same diagnostic claim: "+
                std::string(statement));
        };
        for(const auto statement:{
            "El dato se ha corregido automáticamente.",
            "Se aplicó una corrección automática.",
            "La cantidad fue corregida automáticamente.",
            "La corrección automática se realizó.",
            "El dato se corrige automáticamente.",
            "El programa corrige automáticamente la cantidad.",
            "El sistema lo corrige automáticamente.",
            "El programa ya corrige automáticamente el dato.",
            "El programa no solamente lo corrige automáticamente.",
            "No sólo se ha corregido automáticamente el dato.",
            "No solamente se aplicó una corrección automática.",
            "No se corrigió solamente el dato, sino también el informe.",
            "El dato no era válido; se ha corregido automáticamente.",
            "No se ha corregido automáticamente el dato; se aplicó una corrección automática.",
            "No se ha corregido automáticamente el dato, pero se aplicó una corrección automática.",
            "No se ha corregido el dato y se aplicó una corrección automática.",
            "Se rechazó por falta de existencias.",
            "El rechazo se atribuye a falta de existencias.",
            "La causa es falta de existencias.",
            "El rechazo se debe a falta de existencias.",
            "El rechazo es por falta de existencias.",
            "Es falta de existencias.",
            "Sin existencias.",
            "No hay existencias.",
            "No se atribuye el rechazo a falta de existencias; no hay existencias.",
            "No se atribuye el rechazo a falta de existencias, pero faltan existencias."
        })check_statement(statement,false);
        for(const auto statement:{
            "No se ha corregido automáticamente el dato.",
            "No se aplicó ninguna corrección automática.",
            "La cantidad no fue corregida automáticamente.",
            "Ninguna corrección automática se realizó.",
            "El dato no se corrige automáticamente.",
            "El programa no corrige automáticamente la cantidad.",
            "El sistema no lo corrige automáticamente.",
            "El programa ya no corrige automáticamente el dato.",
            "El programa no solamente no lo corrige automáticamente.",
            "La cantidad debe ser corregida.",
            "La corrección automática podría ser aplicada.",
            "Se solicita que el dato sea corregido.",
            "Se solicita que la corrección sea aplicada.",
            "No hay ninguna corrección automática aplicada.",
            "No hay una corrección automática realizada.",
            "No se ha corregido ni ajustado el dato.",
            "No sólo no se ha corregido el dato, sino que no se ha ajustado.",
            "No se corrigió el dato; solamente se rechazó.",
            "No se atribuye el rechazo a falta de existencias.",
            "La causa no es falta de existencias.",
            "El rechazo no se debe a falta de existencias.",
            "El rechazo no es por falta de existencias.",
            "No es falta de existencias.",
            "No se rechazó por falta de existencias.",
            "La comprobación no consulta las existencias.",
            "No se ha corregido automáticamente el dato; no se atribuye el rechazo a falta de existencias."
        })check_statement(statement,true);
        check_statement("Corrija la cantidad.",true,false);
        check_statement("Es necesario corregir los datos.",true,false);
        check_statement("No se ha corregido automáticamente el dato.",false,false);
        check_statement("No se atribuye el rechazo a falta de existencias.",false,false);
    }
}

void source_edits_stay_inside_assigned_diagnostic() {
    using lgaed::curriculum::rejection_function_only_edit;
    const std::string original=
        "#include <ostream>\n"
        "void informar_rechazo_cajas(std::ostream& destino,int peticion,int piezas) {\n"
        "    destino << \"Error\\n\";\n"
        "}\n\nint main() { return 0; }\n";
    const auto modify=[&](std::string_view body) {
        const auto open=original.find('{');
        const auto end=original.find("\n}\n\nint main()",open);
        require(open!=std::string::npos&&end!=std::string::npos,
            "Original U3L6 diagnostic source isolation fixture is malformed");
        return original.substr(0,open+1)+std::string(body)+
            original.substr(end+1);
    };
    require(rejection_function_only_edit(
        original,original,RejectionOperation::boxes),
        "The unchanged original diagnostic was rejected");
    require(rejection_function_only_edit(original,modify(
        "\n    if (peticion>0) { destino << piezas; }\n"),
        RejectionOperation::boxes),
        "A legitimate branch inside the original rejection function was rejected");
    require(rejection_function_only_edit(original,modify(
        "\n    destino << R\"guard(} <% %> { #include)guard\"; // }\n"),
        RejectionOperation::boxes),
        "String or comment braces falsely terminated the assigned diagnostic");
    require(rejection_function_only_edit(original,modify(
        "\n    if (peticion>0) <% destino << piezas; %>\n"),
        RejectionOperation::boxes),
        "Balanced digraph braces inside the rejection function were rejected");
    require(rejection_function_only_edit(original,modify(
        "\n    destino << R\"probe(std::exit(0) std::cout printf stdout)probe\";\n"
        "    // ExitProcess(0), fputs and freopen are only explanation text.\n"),
        RejectionOperation::boxes),
        "Comments and quoted examples of unsafe APIs cannot block a valid stream edit");
    for(const auto dangerous:{
            "std::exit(0);","std::_Exit(0);","::quick_exit(0);",
            "auto finish=&std::exit; finish(0);",
            "std::cout << \"forged\";",
            "std::cout.setstate(std::ios::failbit);",
            "std::cerr << \"wrong stream\";",
            "std::printf(\"forged\");",
            "std::fputs(\"forged\",stdout);",
            "std::freopen(\"hidden.txt\",\"w\",stdout);",
            "std::system(\"echo forged\");",
            "::SetStdHandle(0,nullptr);",
            "std::terminate();"})
        require(!rejection_function_only_edit(original,
            modify("\n    "+std::string(dangerous)+"\n"),
            RejectionOperation::boxes),
            "An early exit or direct-output spoof bypassed the assigned stream contract");
    const auto driver=rejection_stream_probe_source(RejectionOperation::boxes,original);
    require(driver.find("std::fwrite(")!=driver.npos &&
            driver.find("std::fputc(")!=driver.npos &&
            driver.find("std::fflush(")!=driver.npos &&
            driver.find("std::cout << primero.str()")==driver.npos,
            "The trusted stream probe still trusts a student's mutable std::cout state");

    require(!rejection_function_only_edit(original,modify(
        "\n    destino << piezas;\n}\n"
        "namespace forged {\n"
        "    int hidden() { return 8; }\n"),
        RejectionOperation::boxes),
        "An early function close and injected namespace obtained source credit");
    require(!rejection_function_only_edit(original,modify(
        "\n    destino << piezas;\n%>\n"
        "namespace forged <% int hidden() <% return 8; %>\n"),
        RejectionOperation::boxes),
        "A digraph closure introduced code outside the assigned rejection function");
    require(!rejection_function_only_edit(original,modify(
        "\n    if (peticion>0) {\n        destino << piezas;\n"),
        RejectionOperation::boxes),
        "A missing nested closing brace was accepted");
    require(!rejection_function_only_edit(original,modify(
        "\n    #define FORGED 8\n    destino << FORGED;\n"),
        RejectionOperation::boxes),
        "A preprocessor macro slipped into an assigned rejection function");

    const auto changed_main=original.substr(0,original.size()-1)+" ";
    require(!rejection_function_only_edit(original,changed_main,
        RejectionOperation::boxes),
        "The code after the assigned diagnostic was changed");
}

void numeric_domain_boundaries() {
    constexpr RejectionFacts box_facts[]{{1,-1},{3,21}};
    constexpr RejectionFacts balance_facts[]{{2,10,0},{3,0,-1}};
    // A hyphenated upper bound may have the magnitude of INT_MIN. It is
    // still a wrong domain, both alone and after another, correct interval.
    for(const auto interval:{"0-2147483648","0-2147483647","0-2147483646"}) {
        require(!evaluate_rejection_diagnostics(
            boxes(interval,"Revise"),RejectionOperation::boxes,box_facts).correct,
            "An extreme upper bound was accepted as the boxes domain");
        require(!evaluate_rejection_diagnostics(
            balances(interval,"Revise"),RejectionOperation::balance,balance_facts).correct,
            "An extreme upper bound was accepted as the balance domain");
        require(!evaluate_rejection_diagnostics(
            boxes("0-20; rango permitido "+std::string(interval),"Revise"),
            RejectionOperation::boxes,box_facts).correct,
            "The correct boxes interval hid a contradictory extreme bound");
        require(!evaluate_rejection_diagnostics(
            balances("0-9; rango permitido "+std::string(interval),"Revise"),
            RejectionOperation::balance,balance_facts).correct,
            "The correct balance interval hid a contradictory extreme bound");
    }

    // Extreme received integers remain legitimate diagnostic facts. Only the
    // stated allowed domain is wrong above; do not reject these data outright.
    constexpr auto low=std::numeric_limits<int>::min();
    constexpr auto high=std::numeric_limits<int>::max();
    constexpr RejectionFacts extreme_boxes[]{{1,low},{3,high}};
    std::string box_messages;
    for(const auto& fact:extreme_boxes)
        box_messages+="Cajas, petición "+std::to_string(fact.request)+
            ", piezas="+std::to_string(fact.first)+
            ": rango permitido 0-20. Revise los datos.\n";
    require(evaluate_rejection_diagnostics(
        box_messages,RejectionOperation::boxes,extreme_boxes).correct,
        "A correctly reported extreme piece count was rejected");
    constexpr RejectionFacts extreme_balances[]{{2,low,high},{3,high,low}};
    std::string balance_messages;
    for(const auto& fact:extreme_balances)
        balance_messages+="Saldo, petición "+std::to_string(fact.request)+
            ", cobros="+std::to_string(fact.first)+
            ", devoluciones="+std::to_string(fact.second)+
            ": rango permitido 0-9. Revise los datos.\n";
    require(evaluate_rejection_diagnostics(
        balance_messages,RejectionOperation::balance,extreme_balances).correct,
        "Correctly reported extreme charges and refunds were rejected");
}

void diagnostic_numbers_must_be_complete_integers() {
    constexpr RejectionFacts boxes_expected[]{{1,-1},{3,21}};
    constexpr RejectionFacts balance_expected[]{{2,10,0},{3,0,-1}};
    const auto box=[&](std::string_view request,std::string_view pieces) {
        return "Cajas, petición "+std::string(request)+", piezas="+
            std::string(pieces)+": rango permitido 0..20. Revise la cantidad.\n"+
            "Cajas, petición 3, piezas=21: rango permitido 0..20. Revise la cantidad.\n";
    };
    const auto balance=[&](std::string_view request,std::string_view charges,
                           std::string_view returns) {
        return "Saldo, petición "+std::string(request)+", cobros="+
            std::string(charges)+", devoluciones="+std::string(returns)+
            ": intervalo permitido 0..9. Revise los datos.\n"+
            "Saldo, petición 3, cobros=0, devoluciones=-1: "
            "intervalo permitido 0..9. Revise los datos.\n";
    };
    require(evaluate_rejection_diagnostics(box("1","-1"),
        RejectionOperation::boxes,boxes_expected).correct&&
        evaluate_rejection_diagnostics(balance("2","10","0"),
        RejectionOperation::balance,balance_expected).correct,
        "Valid complete integer evidence no longer passes");
    for(const auto malformed:{"1x","1e0","1.5","1_0","1-0","1,5","1..2",
                               "1/2","1 / 2","1/-2","1*2","1 % 2","1^2"}) {
        require(!evaluate_rejection_diagnostics(box(malformed,"-1"),
            RejectionOperation::boxes,boxes_expected).correct,
            "A numeric prefix falsely named the observed request");
        require(!evaluate_rejection_diagnostics(balance(malformed,"10","0"),
            RejectionOperation::balance,balance_expected).correct,
            "A malformed balance-request number was accepted");
    }
    for(const auto malformed:{"-1x","-1kg","-1e0","-1.0","-1_0",
                              "-1-0","-1,5","-1..2","-1/2","-1 / -2",
                              "-1*2","-1 % 2","-1^2"}) {
        require(!evaluate_rejection_diagnostics(box("1",malformed),
            RejectionOperation::boxes,boxes_expected).correct,
            "A malformed pieces value was treated as the expected integer");
    }
    for(const auto malformed:{"10x","10kg","10e0","10.0","10_0",
                              "10-0","10,5","10..11","10/2","10 * 2"}) {
        require(!evaluate_rejection_diagnostics(balance("2",malformed,"0"),
            RejectionOperation::balance,balance_expected).correct,
            "A malformed charges value was treated as a valid field fact");
    }
    for(const auto malformed:{"0x","0e0","0.0","0_0","0-0","0,5","0..1",
                               "0/2","0 % 2"}) {
        require(!evaluate_rejection_diagnostics(balance("2","10",malformed),
            RejectionOperation::balance,balance_expected).correct,
            "A malformed refunds value was treated as zero");
    }
    // A slash between field statements is not fractional arithmetic:
    // preserve fully identified numbers while rejecting "petition 1/2".
    require(evaluate_rejection_diagnostics(
        "Cajas, petición 1 / registro, piezas=-1: rango permitido 0..20. "
        "Revise la cantidad.\n"
        "Cajas, petición 3, piezas=21: rango permitido 0..20. "
        "Revise la cantidad.\n",
        RejectionOperation::boxes,boxes_expected).correct,
        "A separator between full facts was mistaken for arithmetic");

    // Normal separators and closed intervals still communicate whole facts.
    require(evaluate_rejection_diagnostics(
        "Cajas, petición 1, piezas=-1: rango permitido [0,20]. "
        "Verifique la cantidad.\n"
        "Cajas, petición 3, piezas=21: rango permitido 0-20. "
        "Revise la cantidad.\n",
        RejectionOperation::boxes,boxes_expected).correct,
        "A full integer or a legitimate interval separator was lost");
}

void corrective_actions_require_real_verbs_not_shared_stems() {
    constexpr RejectionFacts box_facts[]{{1,-1},{3,21}};
    constexpr RejectionFacts balance_facts[]{{2,10,0},{3,0,-1}};
    const auto message=[](RejectionOperation operation,int request,int first,
            int second,std::string_view wording) {
        std::string line=operation==RejectionOperation::boxes?"Cajas":"Saldo";
        line+=", petición "+std::to_string(request);
        if(operation==RejectionOperation::boxes)
            line+=", piezas="+std::to_string(first)+
                ": rango permitido 0..20. ";
        else line+=", cobros="+std::to_string(first)+
                ", devoluciones="+std::to_string(second)+
                ": intervalo permitido 0..9. ";
        return line+std::string(wording)+"\n";
    };
    for(const auto operation:{RejectionOperation::boxes,RejectionOperation::balance}) {
        const auto facts=operation==RejectionOperation::boxes?
            std::span<const RejectionFacts>(box_facts):
            std::span<const RejectionFacts>(balance_facts);
        const auto verify=[&](std::string_view wording,bool expected) {
            const auto first=message(operation,facts[0].request,
                facts[0].first,facts[0].second,wording);
            const auto second=message(operation,facts[1].request,
                facts[1].first,facts[1].second,wording);
            const auto result=evaluate_rejection_diagnostics(first+second,operation,facts);
            require(result.correct==expected,
                "An unrelated shared-stem word was credited as corrective advice: "+
                std::string(wording)+" / "+result.reason);
        };
        for(const auto wording:{
            "El revisionismo no es una acción.",
            "El revisor figura en el historial.",
            "El verificador está instalado.",
            "La verificabilidad del cálculo es buena.",
            "El comprobador terminó.",
            "El informe es comprobatorio.",
            "El corrector es otro programa.",
            "El rectificador ya existe.",
            "El modificador es una opción.",
            "El ajustador está disponible.",
            "El dato es revisable."
        })verify(wording,false);
        for(const auto wording:{
            "Revise los datos.",
            "Verifique la cantidad.",
            "Compruebe la petición.",
            "Es necesario verificar el dato.",
            "Se requiere revisar el valor.",
            "Revíselo.",
            "Compruébelos.",
            "Corrija la petición.",
            "Rectifique la cantidad.",
            "Modifique los datos.",
            "Ajuste la entrada.",
            "La revisión es necesaria.",
            "La verificación resulta obligatoria."
        })verify(wording,true);
        for(const auto wording:{
            "La revisión no es necesaria.",
            "No es necesaria la revisión.",
            "La verificación ya fue realizada."
        })verify(wording,false);
    }
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
    // A valid inclusive range cannot exclude one of its own integers.
    // Outside-range rejections remain valid facts and are not contradictions.
    for(const auto phrase:{
        "0..9; no se permite 9",
        "0..9; no se admite el valor 0",
        "0..9; no se aceptan los valores 9",
        "0..9; el valor 9 está prohibido",
        "0..9; 9 no está permitido",
        "0..9; 0 no es válido"
    }) {
        require(!evaluate_rejection_diagnostics(
            balances(phrase,"Revise"),RejectionOperation::balance,
            balance_facts).correct,
            "A balance range excluded a permitted integer but was accepted");
    }
    for(const auto phrase:{
        "0..20; no se admite 20",
        "0..20; 0 está prohibido",
        "0..20; no se permite el valor 4"
    }) {
        require(!evaluate_rejection_diagnostics(
            boxes(phrase,"Revise"),RejectionOperation::boxes,
            box_facts).correct,
            "A boxes range excluded an included integer but was accepted");
    }
    for(const auto valid:{
        "0..9; no se permite 21",
        "0..9; 21 no está permitido",
        "0..9; no se admiten valores inferiores a 0",
        "0..9; no se admiten valores superiores a 9"
    }) {
        require(evaluate_rejection_diagnostics(
            balances(valid,"Compruebe"),RejectionOperation::balance,
            balance_facts).correct,
            "A valid exclusion of out-of-domain values was wrongly rejected");
    }
    // The first number belongs to the range, not to a subsequent refusal
    // with a separately identified object in the same clause.
    require(evaluate_rejection_diagnostics(
        balances("0..9, no se permite 21","Revise"),
        RejectionOperation::balance,balance_facts).correct,
        "The next explicitly denied 21 was attributed to the range endpoint 9");

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
        validity_claims_keep_their_field_and_clause();
        unsupported_claims_keep_their_predicate_and_polarity();
        source_edits_stay_inside_assigned_diagnostic();
        numeric_domain_boundaries();
        diagnostic_numbers_must_be_complete_integers();
        corrective_actions_require_real_verbs_not_shared_stems();
        rejected_semantics();
        std::cout<<"PASS U3L6 semantic diagnostics: closed intervals, spaced dashes, "
            "three valid review actions, field identities, scoped validity and action claims, negative cases\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL U3L6 diagnostic semantics: "<<error.what()<<'\n';
        return 1;
    }
}
