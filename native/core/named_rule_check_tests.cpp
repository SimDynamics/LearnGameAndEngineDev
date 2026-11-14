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
            int const plazas_por_caja = 4;
            return (piezas + plazas_por_caja - 1) / plazas_por_caja;
        })cpp",rules,true,"Equivalent east-const spelling");

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
            int const cupo{4};
            int const ajuste = cupo - 1;
            int const cajas = (piezas + ajuste) / cupo;
            return cajas;
        })cpp",rules,true,"East-const intermediates preserve two genuine dependencies");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int cupo{4};
            auto const ajuste = cupo - 1;
            auto const cajas = (piezas + ajuste) / cupo;
            return cajas;
        })cpp",rules,true,"Const-qualified auto intermediates are not discarded");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int cupo{4};
            int const observacion = cupo + cupo;
            return (piezas + cupo - 1) / 4;
        })cpp",rules,false,"Unused east-const intermediates cannot satisfy minUses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int observacion = capacidad + capacidad;
            return (piezas + capacidad - 1) / 4;
        })cpp",rules,false,
        "Unused auxiliary calculations cannot supply a second result use");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int observacion = capacidad + capacidad;
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "Unused local results cannot substitute for the named capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (false) return (piezas + capacidad - 1) / capacidad;
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "An unreachable unbraced return cannot earn named-rule credit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (0) {
                return (piezas + capacidad - 1) / capacidad;
            } else {
                return (piezas + 3) / 4;
            }
        })cpp",rules,false,
        "A false branch cannot substitute for the live else result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if constexpr (false) {
                return (piezas + capacidad - 1) / capacidad;
            }
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "An inactive constexpr branch cannot supply result references");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (false) return 0;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "Skipping dead code must preserve a valid later return");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "An unreachable return cannot repair an earlier hardcoded result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (true) return (piezas + 3) / 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "A constant-true early return makes the named branch unreachable");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (1) {
                return (piezas + 3) / 4;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "A braced always-true return cannot be bypassed");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (false) {
                return (piezas + capacidad - 1) / capacidad;
            } else {
                return (piezas + 3) / 4;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "A guaranteed else return cannot borrow uses from dead code");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas <= 0) return 0;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "A runtime validation guard must not suppress the normal named result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas <= 0) {
                return 0;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "A braced runtime guard must preserve an equivalent solution");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) {
                int calculo = (piezas + 3) / 4;
                return calculo;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "An unrecognized nested return cannot hide a hardcoded early path");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas < 0) {
                const int observacion = 0;
                (void)observacion;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "A nonreturning conditional branch does not invalidate a named result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + 3) / 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "A later correct return cannot excuse an earlier hardcoded calculation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) {
                return (piezas + 3) / 4;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "A braced hardcoded conditional branch cannot borrow later source evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + capacidad - 1) / 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "One named use in a conditional return is not the required two");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "Two equivalent named return paths must remain accepted");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas <= 0) {
                return 0;
            } else {
                return (piezas + capacidad - 1) / capacidad;
            }
        })cpp",rules,true,
        "An exhaustive zero guard and a named calculation remain compatible");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas < 0) return (piezas + capacidad - 1) / capacidad;
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "Named uses only in a conditional guard cannot certify the main result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas < 0) {
                return (piezas + capacidad - 1) / capacidad;
            }
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "A braced conditional return cannot hide a hardcoded ordinary result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
            if (piezas <= 10) return (piezas + 3) / 4;
        })cpp",rules,false,
        "Complementary guards cannot credit one named and one hardcoded path");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "One conditional return without a guaranteed result is insufficient");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
            else return (piezas + 3) / 4;
        })cpp",rules,false,
        "An if/else must not accept one hardcoded branch");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + 3) / 4;
            else return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "An if/else must not ignore a hardcoded first branch");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) return (piezas + capacidad - 1) / capacidad;
            else return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "Equivalent named returns on both sides of an if/else are accepted");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) {
                return (piezas + capacidad - 1) / capacidad;
            } else {
                return (piezas + capacidad - 1) / capacidad;
            }
        })cpp",rules,true,
        "Braced if/else named returns remain valid");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (piezas > 10) {
                return (piezas + capacidad - 1) / capacidad;
            } else {
                return (piezas + 3) / 4;
            }
        })cpp",rules,false,
        "A braced if/else must reject a hardcoded alternate branch");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int ajuste = capacidad - 1;
            const int resultado = (piezas + ajuste) / capacidad;
            return resultado;
        })cpp",rules,true,
        "Real dependencies across local intermediate values must be accepted");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            const int copia = resultado;
            resultado = (piezas + 3) / 4;
            return copia;
        })cpp",rules,true,
        "Reassigning a different local must not invalidate the returned copy");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int observacion = capacidad + capacidad;
            return (piezas + capacidad - 1) / 4;
        })cpp",rules,false,
        "minUses must apply to the returned computation, not the whole body");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int cajas = (piezas + capacidad - 1) / capacidad;
            cajas = (piezas + 3) / 4;
            return cajas;
        })cpp",rules,false,"An overwritten computed result is not a valid use");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int cajas = (piezas + 3) / 4;
            cajas = (piezas + capacidad - 1) / capacidad;
            return cajas;
        })cpp",rules,true,"A later valid assignment still establishes the result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int cajas = (piezas + capacidad - 1) / capacidad;
            cajas = cajas * 0;
            return cajas;
        })cpp",rules,false,"Multiplication by zero discards the computed result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int cajas = (piezas + capacidad - 1) / capacidad;
            cajas = cajas + 0;
            return cajas;
        })cpp",rules,true,"An assignment preserving the result remains valid");

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
            return (piezas + 3) / 4 + 0 * (capacidad + capacidad);
        })cpp",rules,false,"Zero times a parenthesized sum is not two uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (false ? capacidad * capacidad : 0);
        })cpp",rules,false,"An unreachable false ternary arm is not a named rule");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (true ? 0 : (capacidad + capacidad));
        })cpp",rules,false,"An unreachable true ternary alternative is not a rule");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (0 ? (capacidad + capacidad) : 0);
        })cpp",rules,false,"A numeric zero conditional cannot forge two uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (false ? capacidad * capacidad : 0);
        })cpp",rules,true,"An irrelevant unreachable arm must not erase valid rule uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (false && capacidad && capacidad);
        })cpp",rules,false,"A constant false && prefix cannot establish two uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (true || capacidad || capacidad) - 1;
        })cpp",rules,false,"A true || prefix cannot establish two uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (0 && (capacidad + capacidad));
        })cpp",rules,false,"Nested references behind 0 && are still dead");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (false && capacidad && capacidad);
        })cpp",rules,true,"A dead short-circuit branch must not erase real uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (false && capacidad || piezas > 5) * 0;
        })cpp",rules,true,"An independent live || branch does not erase real uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 ? 0 : 0) +
                   (capacidad < 10 ? 0 : 0);
        })cpp",rules,false,"Equal ternary arms cannot credit two decorative capacity checks");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 ? (2 - 2) : 0) +
                   (capacidad == 4 ? 0 : (4 / 4 - 1));
        })cpp",rules,false,"Equivalent arithmetic constant arms cannot establish uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad > 0 ? 0 : 0);
        })cpp",rules,true,"A decorative constant-result conditional must preserve real calculations");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (piezas > 10 ? capacidad - 4 : 0) * 0;
        })cpp",rules,true,"A real rule is still valid with an unrelated conditional");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad + capacidad) * (2 - 2);
        })cpp",rules,false,"A computed zero factor on the right cancels a whole sum");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (2 - 2) * (capacidad + capacidad);
        })cpp",rules,false,"A computed zero factor on the left cancels a whole sum");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   0 * ((capacidad + capacidad) / 2);
        })cpp",rules,false,"An outer zero factor cancels nested expressions too");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int ajuste = capacidad - 1;
            return (piezas + ajuste) / capacidad +
                   0 * (capacidad + capacidad);
        })cpp",rules,true,"An irrelevant zero product must not erase real uses");
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
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                (__LINE__ == 1 ? 0 : 0);
        })cpp",rules,false,"Line numbers can distinguish the private probe");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                (__FILE__[0] == 'x' ? 0 : 0);
        })cpp",rules,false,"File identities can distinguish the probe workspace");
    verify(R"cpp(
        #include <source_location>
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                (std::source_location::current().line() == 1 ? 0 : 0);
        })cpp",rules,false,"std::source_location can distinguish compilations");
    verify(R"cpp(
        // __LINE__, __FILE__ and source_location in prose are harmless.
        int cajas_para(int piezas) {
            const char* texto = "__LINE__: source_location";
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Comments and literals are not build-location evidence");
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
    if(!source||source->find("#include <cstdio>\n")==source->npos||
       source->find("#include \"envio/cajas.h\"")==source->npos||
       source->find("static_cast<int>(cajas_para(19)) != 4")==source->npos||
       source->find("static_cast<int>(llega_a_tiempo(5)) != 0")==source->npos||
       source->find("std::fputs(\""+std::string(named_rule_probe_completion_marker)+
           "\", stderr);")==source->npos||
       source->find("std::fputs(")<source->find(
           "static_cast<int>(llega_a_tiempo(5)) != 0"))
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
    // The source is deliberately supplied verbatim; the independent main
    // uses different inputs rather than trusting the student's own report.
    NamedRuleExecutionProbe single{"","",{
        {"precio_total",11,77},{"alcanza_minimo",4,0},
        {"alcanza_minimo",5,1}}};
    const std::string candidate=R"cpp(int precio_total(int n) {return n * 7;}
bool alcanza_minimo(int n) {return n >= 5;}
int main() {return 0;}
)cpp";
    const auto independent=named_rule_single_file_probe_source(single,candidate);
    if(!independent||
       independent->find("#include <cstdio>\n#define main lgaed_student_original_main\n")!=0||
       independent->find(candidate)==independent->npos||
       independent->find("#undef main\n\nint main()") == independent->npos||
       independent->find("static_cast<int>(precio_total(11)) != 77")==
           independent->npos||
       independent->find("std::fputs(\""+std::string(named_rule_probe_completion_marker)+
           "\", stderr);")==independent->npos||
       independent->find("std::fputs(")<independent->find(
           "static_cast<int>(alcanza_minimo(5)) != 1"))
        throw std::runtime_error("A trusted single-file main was not generated");
    auto malicious=single;malicious.header="envio/cajas.h";
    if(named_rule_single_file_probe_source(malicious,candidate))
        throw std::runtime_error("A single-file probe injected a foreign header");
    malicious=single;malicious.harness_path="pruebas/casos.cpp";
    if(named_rule_single_file_probe_source(malicious,candidate))
        throw std::runtime_error("A single-file probe accepted a project harness");
    malicious=single;malicious.cases.push_back(malicious.cases.front());
    if(named_rule_single_file_probe_source(malicious,candidate))
        throw std::runtime_error("A single-file probe repeated an expected input");
    malicious=single;malicious.cases.front().function="precio_total(0), abort()";
    if(named_rule_single_file_probe_source(malicious,candidate))
        throw std::runtime_error("A single-file probe accepted injected C++");
    if(named_rule_single_file_probe_source(single,""))
        throw std::runtime_error("A single-file probe accepted an empty source");
    // A process can fake the fixed witness and call exit(0) before the
    // remaining comparisons; the authored source gate must reject the
    // termination even when all named constants are otherwise correct.
    for(const auto term:{"std::exit(0)","exit /*comment*/ (0)",
            "auto premature = &std::exit; premature(0)",
            "std::_Exit(0)","std::quick_exit(0)",
            "ExitProcess(0)","TerminateProcess(0, 0)",
            "ExitThread(0)","std::_exit(0)"}) {
        const auto premature=candidate+
            "\nvoid early_finish() { "+std::string(term)+"; }\n";
        if(named_rule_single_file_probe_source(single,premature)||
           named_rule_detail::probe_source_no_early_exit(premature))
            throw std::runtime_error("An early process termination spoofed a probe");
    }
    const auto harmless=candidate+
        "\n// std::exit(0) and ExitProcess(0) are comments.\n"
        "const char* warning = \"exit(0) quick_exit(0)\";\n";
    if(!named_rule_single_file_probe_source(single,harmless)||
       !named_rule_detail::probe_source_no_early_exit(harmless))
        throw std::runtime_error("Harmless exit text blocked a legitimate probe");
    for(const auto marker:{"__LINE__","__FILE__","__COUNTER__",
            "std::source_location::current().line()"}) {
        const auto location_sensitive=candidate+
            "\nint lgaed_probe_sensitive_marker = "+std::string(marker)+";\n";
        if(named_rule_single_file_probe_source(single,location_sensitive))
            throw std::runtime_error("A probe accepted source-dependent compilation markers");
    }
    const auto prose_only=candidate+
        "\n// __LINE__ is a comment.\nconst char* note = \"__FILE__ source_location\";\n";
    if(!named_rule_single_file_probe_source(single,prose_only))
        throw std::runtime_error("A comment or string disabled the independent probe");
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
