#include "core/named_rule_check.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed::curriculum;
void verify(std::string_view source,std::span<const NamedRuleRequirement> requirements,
        bool expected,std::string_view context) {
    const auto result=analyze_named_rule_sources(source,requirements);
    if(result.source_evidence!=expected)
        throw std::runtime_error(std::string(context)+": "+result.reason);
}
void capacity() {
    const std::array rules{NamedRuleRequirement{"cajas_para",4,
        {"capacidad","plaza","cupo"},2}};
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Correct local capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int plazas_por_caja = 4;
            return (piezas + plazas_por_caja - 1) / plazas_por_caja;
        })cpp",rules,true,"Equivalent capacity name");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad = 2 + 4 / 2;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Integer arithmetic and operator precedence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{(2 * 3) - 2};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Parenthesized arithmetic in a brace initializer");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{-(-4)};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Unary arithmetic in a local constant");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{8 / 0};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"Division by zero does not establish a capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{2147483647 + 1};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"Integer overflow cannot establish a capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{010};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"An octal literal must not be read as decimal 10");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{2 + 3};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"A different computed value is still a wrong agreement");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int cupo{4};
            const int ajuste = cupo - 1;
            const int cajas = (piezas + ajuste) / cupo;
            return cajas;
        })cpp",rules,true,"A returned intermediate derived from the local capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int cupo{4};
            const int ajuste = cupo - 1;
            const int cajas = (piezas + 3) / 4;
            return cajas;
        })cpp",rules,false,"An unused intermediate must not establish the returned calculation");
    verify("int cajas_para(int piezas) {return (piezas + 3) / 4;}",
        rules,false,"Unmodified source");
    verify(R"cpp(
        const int capacidad{4};
        int cajas_para(int piezas) {return (piezas + capacidad - 1) / capacidad;}
        )cpp",rules,false,"Global constant is not local");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{5};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"Wrong capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4;
        })cpp",rules,false,"Unused capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            // const int capacidad{4}; return capacidad / capacidad;
            return (piezas + 3) / 4;
        })cpp",rules,false,"Comment injection");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + 0 * capacidad;
        })cpp",rules,false,"Zero multiplication as decoration");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad - capacidad);
        })cpp",rules,false,"A self-cancelling subtraction does not use the capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   capacidad * (2 - 2) + capacidad * (3 - 3);
        })cpp",rules,false,"Two products by computed zero are not useful references");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   capacidad / capacidad + capacidad % capacidad - 1;
        })cpp",rules,false,"Self division and remainder do not establish the agreement");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad - capacidad);
        })cpp",rules,true,"A neutral extra expression does not erase two real uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (2 - 2) * capacidad;
        })cpp",rules,true,"A zero factor alongside real uses is harmless");
    verify(R"cpp(
        #if 0
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        }
        #endif
        int main() {return 0;}
        )cpp",rules,false,"An inactive function cannot establish compiled source evidence");
    verify(R"cpp(
        #define capacidad capacidad_falsa
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        }
        )cpp",rules,false,"A macro cannot substitute the named local rule");
    verify(R"cpp(
        /* This comment becomes whitespace before the preprocessor runs. */#if 0
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        }
        #endif
        int main() {return 0;}
        )cpp",rules,false,"A comment-prefixed #if cannot hide inactive evidence");
    verify(R"cpp(
        /* A comment may also precede the %: digraph spelling. */%:if 0
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        }
        %:endif
        int main() {return 0;}
        )cpp",rules,false,"A comment-prefixed digraph directive cannot hide code");
    verify(R"cpp(
        #include_next <iostream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        }
        )cpp",rules,false,"Include-like preprocessing directives need an exact keyword");
    verify(R"cpp(
        // #if 0 is merely an explanation, not an active directive.
        # include <iostream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        }
        )cpp",rules,true,"Normal includes and comments remain allowed");
}
void separate_rules() {
    const std::array rules{
        NamedRuleRequirement{"cajas_para",6,{"capacidad","plaza","cupo"},2},
        NamedRuleRequirement{"llega_a_tiempo",4,{"plazo","dias","tiempo"},1}
    };
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad_por_caja{6};
            return (piezas + capacidad_por_caja - 1) / capacidad_por_caja;
        }
        bool llega_a_tiempo(int dias) {
            const int plazo_dias{4};
            return dias <= plazo_dias;
        })cpp",rules,true,"Independent capacity and deadline");
    verify(R"cpp(
        const int limite{4};
        int cajas_para(int piezas) {return (piezas + limite - 1) / limite;}
        bool llega_a_tiempo(int dias) {return dias <= limite;}
        )cpp",rules,false,"Original shared global agreement");
}
void price_and_minimum() {
    const std::array rules{
        NamedRuleRequirement{"precio_total",7,{"precio","costo","tarifa","credito"},1},
        NamedRuleRequirement{"alcanza_minimo",5,{"minimo","umbral","cantidad"},1}
    };
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {
            constexpr int minimo_articulos = 5;
            return articulos >= minimo_articulos;
        })cpp",rules,true,"Independent price and minimum");
    verify(R"cpp(
        const int acuerdo{7};
        int precio_total(int articulos) {return articulos * acuerdo;}
        bool alcanza_minimo(int articulos) {return articulos >= acuerdo;}
        )cpp",rules,false,"Original shared global value");
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {return articulos >= 5;}
        )cpp",rules,false,"Hardcoded minimum");
}
}
void comment_only_header_changes() {
    constexpr auto original=R"cpp(#ifndef ENVIO_CAJAS_H
#define ENVIO_CAJAS_H
// Minimo numero de cajas de 4 plazas.
int cajas_para(int piezas);
#endif
)cpp";
    constexpr auto documented=R"cpp(#ifndef ENVIO_CAJAS_H
#define ENVIO_CAJAS_H
// Minimo numero de cajas de 6 plazas.
int cajas_para(int piezas);
#endif
)cpp";
    constexpr auto changed_signature=R"cpp(#ifndef ENVIO_CAJAS_H
#define ENVIO_CAJAS_H
// Minimo numero de cajas de 6 plazas.
int cajas_para(long piezas);
#endif
)cpp";
    constexpr auto diverted=R"cpp(#ifndef ENVIO_CAJAS_H
#define ENVIO_CAJAS_H
// Minimo numero de cajas de 6 plazas.
#define cajas_para(piezas) ((piezas + 5) / 6)
int cajas_para(int piezas);
#endif
)cpp";
    if(!named_rule_comment_only_edit(original,documented)||
       named_rule_comment_only_edit(original,changed_signature)||
       named_rule_comment_only_edit(original,diverted))
        throw std::runtime_error("A comment-only rule changed executable C++ header semantics");
    // A C++ literal that resembles a comment is still a literal, not prose.
    if(named_rule_comment_only_edit("constexpr auto marker=\"// 4\";",
                                    "constexpr auto marker=\"// 6\";"))
        throw std::runtime_error("Changed literal bytes were treated as comments");
}

void independently_generated_probe() {
    const NamedRuleExecutionProbe valid{"pruebas/cajas_casos.cpp","envio/cajas.h",{
        {"cajas_para",0,0},{"cajas_para",6,1},{"cajas_para",7,2},
        {"cajas_para",19,4},{"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}
    }};
    const auto source=named_rule_probe_source(valid);
    if(!source||source->find("#include \"envio/cajas.h\"")==source->npos||
       source->find("static_cast<int>(cajas_para(19)) != 4")==source->npos||
       source->find("static_cast<int>(llega_a_tiempo(5)) != 0")==source->npos)
        throw std::runtime_error("The independent probe was not derived from authored facts");
    const auto invalid=[&](NamedRuleExecutionProbe value,const char* description) {
        if(named_rule_probe_source(value))
            throw std::runtime_error(description);
    };
    auto changed=valid;
    changed.header="../../outside.h";
    invalid(changed,"A parent traversal became an include");
    changed=valid;changed.header="envio/\"; evil.h";
    invalid(changed,"A C++ quote injection became an include");
    changed=valid;changed.cases.push_back(changed.cases.front());
    invalid(changed,"Duplicated probe facts were accepted");
    changed=valid;changed.cases.front().function="cajas_para(3), exit(0)";
    invalid(changed,"Raw C++ was injected through a function name");
    changed=valid;changed.cases.clear();
    invalid(changed,"A vacuous independent probe was accepted");
}
int main() {
    try {
        capacity();separate_rules();price_and_minimum();
        comment_only_header_changes();independently_generated_probe();
        std::cout<<"PASS local named rules: roles, values, uses and rejected shortcuts\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL named-rule source evidence: "<<e.what()<<'\n';return 1;
    }
}
