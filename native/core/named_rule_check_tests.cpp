#include "core/named_rule_check.hpp"
#include "core/named_rule_battery_check.hpp"
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
            int constexpr capacidad{4};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "Trailing constexpr names the same local integer agreement");
    verify(R"cpp(
        int cajas_para(int piezas) {
            constexpr int capacidad = 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "Leading constexpr remains a valid local rule");
    verify(R"cpp(
        int cajas_para(int piezas) {
            int constexpr capacidad{4};
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "An unused trailing constexpr declaration must not earn source credit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0x4};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Hexadecimal capacity represents four");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0b100};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"Binary capacity represents four");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{04};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"An octal four is an equivalent integer");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0X4u};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"A valid unsigned suffix preserves a small signed value");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0B100LL};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"A long long binary literal can initialize the same int");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4'0 / 10};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,"C++ digit separators are parsed before division");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0x5};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"Hexadecimal five is not the four-place contract");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{08};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"Invalid octal digits cannot earn credit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0b102};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"Invalid binary digits cannot earn credit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{0x4unit};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"A user-defined integer suffix has no guaranteed numeric meaning");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4.0};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,"A decimal floating point token is not an integer literal");
    const std::array octal_rules{NamedRuleRequirement{"cajas_para",8,
        {"capacidad","plaza","cupo"},2}};
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{010};
            return (piezas + capacidad - 1) / capacidad;
        })cpp",octal_rules,true,"The octal 010 is eight, not decimal ten");
    const std::array minimum_rules{NamedRuleRequirement{"limite_real",
        std::numeric_limits<int>::min(),{"umbral"},1}};
    verify(R"cpp(
        int limite_real(int numero) {
            const int umbral{-2147483648};
            return numero < umbral ? umbral : umbral;
        })cpp",minimum_rules,true,"Signed decimal INT_MIN remains valid");
    verify(R"cpp(
        int limite_real(int numero) {
            const int umbral{-0x80000000};
            return numero < umbral ? umbral : umbral;
        })cpp",minimum_rules,false,"Nondecimal unsigned negation is not the same int");


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
            const int capacidad{4};
            {
                const int capacidad{8};
                (void)capacidad;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "An unused inner constant shadow must not erase a valid outer capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            {
                const int capacidad{8};
                {
                    const int capacidad{2};
                    (void)capacidad;
                }
                (void)capacidad;
            }
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "Nested shadowing scopes must restore the original constant on exit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int total{0};
            {
                const int capacidad{4};
                total = (piezas + capacidad - 1) / capacidad;
            }
            return total;
        })cpp",rules,false,
        "An inner shadow cannot manufacture uses of an unused outer capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int total = (piezas + capacidad - 1) / capacidad;
            {
                const int capacidad{8};
                (void)capacidad;
            }
            return total;
        })cpp",rules,true,
        "A valid outer alias survives an unrelated same-named inner constant");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int total{0};
            {
                const int total = capacidad + capacidad;
                (void)total;
            }
            return (piezas + 3) / 4 + total;
        })cpp",rules,false,
        "An inner shadow cannot lend its capacity uses to the outer total");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int total{0};
            {
                int total{0};
                total = capacidad + capacidad;
            }
            return (piezas + 3) / 4 + total;
        })cpp",rules,false,
        "An assignment to a shadowed local must not credit its outer namesake");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int total = (piezas + capacidad - 1) / capacidad;
            {
                int total{0};
                total = piezas / 4;
            }
            return total;
        })cpp",rules,true,
        "A nested shadow must not erase a correct outer result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int total{0};
            {
                total = (piezas + capacidad - 1) / capacidad;
            }
            return total;
        })cpp",rules,true,
        "An inner block can legitimately assign a calculation to the outer alias");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4
                + (sizeof(capacidad) - sizeof(int))
                + (sizeof(capacidad) - sizeof(int));
        })cpp",rules,false,
        "sizeof never evaluates the capacity, despite two lexical references");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4
                + (sizeof(decltype(capacidad)) - sizeof(int))
                + (sizeof(capacidad) - sizeof(int));
        })cpp",rules,false,
        "decltype and sizeof must not create value-dependency evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4
                + (sizeof +capacidad - sizeof(int))
                + (sizeof(capacidad) - sizeof(int));
        })cpp",rules,false,
        "Unary sizeof cannot create a productive named-rule use");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            auto const tipo = sizeof(capacidad);
            return (piezas + 3) / 4 + tipo + tipo - 2 * sizeof(int);
        })cpp",rules,false,
        "An alias computed from sizeof must not launder source evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad
                + (sizeof(capacidad) - sizeof(int));
        })cpp",rules,true,
        "Real capacity uses stay credited alongside harmless sizeof metadata");
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
            int constexpr cupo{4};
            int constexpr ajuste = cupo - 1;
            int const cajas = (piezas + ajuste) / cupo;
            return cajas;
        })cpp",rules,true,
        "Trailing constexpr intermediates preserve the actual result dependencies");
    verify(R"cpp(
        int cajas_para(int piezas) {
            constexpr int cupo{4};
            auto constexpr ajuste = cupo - 1;
            auto const cajas = (piezas + ajuste) / cupo;
            return cajas;
        })cpp",rules,true,
        "Auto constexpr intermediates can carry an authenticated local rule");
    verify(R"cpp(
        int cajas_para(int piezas) {
            int constexpr cupo{4};
            auto constexpr observacion = cupo + cupo;
            return (piezas + cupo - 1) / 4;
        })cpp",rules,false,
        "An unused auto constexpr calculation cannot manufacture a second use");
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
            if (0x0) return (piezas + capacidad - 1) / capacidad;
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "A zero hex condition cannot make an unreachable result count");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if constexpr (0b0) {
                return (piezas + capacidad - 1) / capacidad;
            }
            return (piezas + 3) / 4;
        })cpp",rules,false,
        "An inactive binary-zero constexpr arm cannot justify hardcoded output");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if (0X1u) return (piezas + 3) / 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,false,
        "A numeric-true early return cannot be rescued by dead named code");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            if ((0b0)) return (piezas + 3) / 4;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "A parenthesized binary-false guard cannot suppress valid named code");

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
            return (piezas + 3) / 4 + capacidad * 0x0 + capacidad * 0b0;
        })cpp",rules,false,
        "Hexadecimal and binary zero factors cannot manufacture two uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + 00u * (capacidad + capacidad);
        })cpp",rules,false,
        "A suffixed octal zero annihilates an entire parenthesized expression");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad + capacidad) * 0X0;
        })cpp",rules,false,
        "An uppercase hexadecimal zero after grouped uses is still neutral");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   0x0 * capacidad;
        })cpp",rules,true,
        "A dead hexadecimal factor cannot erase an otherwise correct named result");

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
                   (capacidad > 0 && capacidad < 8 && false);
        })cpp",rules,false,
        "An && false suffix cannot make earlier references productive");

    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 && capacidad < 8 && 0x0);
        })cpp",rules,false,
        "An && hexadecimal-zero suffix discards both local-rule references");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 || capacidad < 8 || 0b1) - 1;
        })cpp",rules,false,
        "A binary-true OR suffix cannot establish a named-rule dependency");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (0x1 || capacidad || capacidad) - 1;
        })cpp",rules,false,
        "A true hex prefix skips both named OR operands");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (0b0 && capacidad && capacidad);
        })cpp",rules,false,
        "A false binary prefix skips both named AND operands");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad > 0 || 02) - 1;
        })cpp",rules,true,
        "An irrelevant octal-true suffix must preserve earlier real uses");

    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 && capacidad < 8 && (false));
        })cpp",rules,false,
        "Parenthesized false at the end still makes the whole conjunction false");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 || capacidad < 8 || true) - 1;
        })cpp",rules,false,
        "An || true suffix cannot credit earlier named references");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 +
                   (capacidad > 0 || capacidad < 8 || (1)) - 1;
        })cpp",rules,false,
        "An integral true at the end forces an OR-only result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad > 0 && false);
        })cpp",rules,true,
        "A constant-false suffix must not invalidate a separate real calculation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad > 0 || true) - 1;
        })cpp",rules,true,
        "A constant-true suffix must not invalidate a separate real calculation");
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
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                (__builtin_COLUMN() > 1 ? 0 : 0);
        })cpp",rules,false,"Column positions can distinguish the submitted source and its private probe");
    verify(R"cpp(
        // __LINE__, __FILE__ and __builtin_COLUMN in prose are harmless.
        int cajas_para(int piezas) {
            const char* texto = "__LINE__: source_location __builtin_COLUMN";
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
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {
            const int minimo_articulos{5};
            return articulos >= 5 || minimo_articulos < 10 && false;
        })cpp",rules,false,
        "A trailing false cannot make an unrelated local threshold productive");
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {
            const int minimo_articulos{5};
            return minimo_articulos >= 1 && false || articulos >= 5;
        })cpp",rules,false,
        "A false left conjunction cannot satisfy an unrelated right disjunction");
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {
            const int minimo_articulos{5};
            return articulos >= minimo_articulos || articulos < 0 && false;
        })cpp",rules,true,
        "A valid named result remains valid alongside an irrelevant false clause");
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {
            const int minimo_articulos{5};
            return articulos >= minimo_articulos || articulos > 5 && true;
        })cpp",rules,true,
        "A nonconstant second branch must not invalidate the actual named threshold");
    verify(R"cpp(
        int precio_total(int articulos) {
            const int precio_unitario{7};
            return articulos * precio_unitario;
        }
        bool alcanza_minimo(int articulos) {
            const int minimo_articulos{5};
            return minimo_articulos > 0 || true;
        })cpp",rules,false,
        "A stand-alone true disjunction renders the threshold irrelevant");
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
    if(!named_rule_comment_only_edit(
            "/* originally documented */ int value = 4;",
            "/* revised documentation */ int value = 4;"))
        throw std::runtime_error("Harmless comment-only prose was rejected");
    if(!named_rule_comment_only_edit(
            R"cpp(// __LINE__ and __builtin_COLUMN are just words here.
const char* label = "__LINE__: __builtin_COLUMN";
const char* raw = R"tag(source_location __builtin_LINE)tag";
)cpp",
            R"cpp(// __LINE__ and __builtin_COLUMN are still documentation.

const char* label = "__LINE__: __builtin_COLUMN";
const char* raw = R"tag(source_location __builtin_LINE)tag";
)cpp"))
        throw std::runtime_error("A quoted positional name blocked a safe header comment revision");
    if(named_rule_comment_only_edit(
            "#define ORIGINAL_LINE __LINE__\n// old guidance\nint x=ORIGINAL_LINE;",
            "#define ORIGINAL_LINE __LINE__\n// revised guidance\nint x=ORIGINAL_LINE;"))
        throw std::runtime_error("An authored macro with line-dependent expansion escaped comment review protection");
    if(named_rule_comment_only_edit(
            "/*x*/ int column = __builtin_COLUMN();",
            "/*expanded explanatory comment*/ int column = __builtin_COLUMN();"))
        throw std::runtime_error("A source-column-changing comment edit reused an old header contract");
    if(named_rule_comment_only_edit(
            "// label\nint line = __builtin_LINE();",
            "// label\n\nint line = __builtin_LINE();"))
        throw std::runtime_error("A source-line-changing comment edit reused an old header contract");
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
            "__builtin_LINE()","__builtin_COLUMN()",
            "std::source_location::current().line()"}) {
        const auto location_sensitive=candidate+
            "\nint lgaed_probe_sensitive_marker = "+std::string(marker)+";\n";
        if(named_rule_single_file_probe_source(single,location_sensitive))
            throw std::runtime_error("A probe accepted source-dependent compilation markers");
    }
    const auto prose_only=candidate+
        "\n// __LINE__ and __builtin_COLUMN() are comments.\n"
        "const char* note = \"__FILE__ source_location __builtin_COLUMN()\";\n";
    if(!named_rule_single_file_probe_source(single,prose_only))
        throw std::runtime_error("A comment or string disabled the independent probe");
}

void named_battery_require(bool condition, std::string_view message) {
    if(!condition) throw std::runtime_error(std::string(message));
}
lgaed::curriculum::NamedRuleBatterySpec named_battery_example() {
    return {"", {{"precio_total",0,0},{"precio_total",5,35},
        {"alcanza_minimo",0,0},{"alcanza_minimo",5,1}}, 1};
}
std::string named_battery_source() {
    return R"cpp(#include <iostream>
int precio_total(int articulos) {
    const int precio_creditos{7};
    return articulos * precio_creditos;
}
bool alcanza_minimo(int articulos) {
    const int minimo_articulos{5};
    return articulos >= minimo_articulos;
}
int main() {
    bool correcto=true;
    for(const int articulos : {0,5}) {
        const int total=precio_total(articulos);
        const bool minimo=alcanza_minimo(articulos);
        const bool coincide=total==(articulos==0?0:35) && minimo==(articulos==5);
        std::cout<<articulos<<" "<<total<<" "<<minimo<<" "<<coincide<<"\n";
        if(!coincide)correcto=false;
    }
    return correcto?0:1;
}
)cpp";
}
void real_battery_contract_and_sources() {
    using namespace lgaed::curriculum;
    const auto spec=named_battery_example();
    const auto source=named_battery_source();
    const auto require=named_battery_require;
    require(named_rule_battery_spec_valid(spec),"Valid mixed int/bool battery contract");
    require(named_rule_battery_source_valid(source,spec),"Valid single-file battery source");
    require(named_rule_battery_driver_valid(source),"The real main remains a valid driver");
    const auto plan=named_rule_battery_plan(spec);
    require(plan && plan->size()==5 && !plan->front().target &&
        plan->back().target==3,"Baseline plus one independent mutation per required call");
    for(std::size_t i=0;i<spec.cases.size();++i)
        require((*plan)[i+1].mutation==NamedRuleBatteryMutation::mutate &&
            (*plan)[i+1].target==i,"Each mutation names exactly its authored case");

    auto invalid=spec;
    invalid.cases.push_back(spec.cases.front());
    require(!named_rule_battery_spec_valid(invalid),"Duplicate function/input contract rejected");
    invalid=spec;invalid.cases.clear();
    require(!named_rule_battery_spec_valid(invalid),"Empty contract rejected");
    invalid=spec;invalid.failure_exit_code=0;
    require(!named_rule_battery_spec_valid(invalid),"Success cannot be the failure policy");
    invalid=spec;invalid.source_path="../library.cpp";
    require(!named_rule_battery_spec_valid(invalid),"Traversal path rejected");
    invalid=spec;invalid.cases[0].function="x); forged(";
    require(!named_rule_battery_spec_valid(invalid),"Injected identifier rejected");
    invalid=spec;invalid.cases[0].function="main";
    require(!named_rule_battery_spec_valid(invalid),"main is never the instrumented function");
    invalid=spec;invalid.cases[2].expected=2;
    require(named_rule_battery_spec_valid(invalid) &&
        !named_rule_battery_source_valid(source,invalid),"Boolean result requires zero or one");
    invalid=spec;
    for(int i=0;i<33;++i)invalid.cases.push_back({"precio_total",100+i,700+7*i});
    require(!named_rule_battery_spec_valid(invalid),"Mutation work is bounded to 32 cases");

    const auto frame=named_rule_battery_spec_frame(spec);
    invalid=spec;invalid.cases[0].expected=1;
    require(named_rule_battery_spec_frame(invalid)!=frame,"Expected result belongs to objective identity");
    invalid=spec;invalid.failure_exit_code=2;
    require(named_rule_battery_spec_frame(invalid)!=frame,"Failure policy belongs to objective identity");
    invalid=spec;invalid.source_path="library.cpp";
    require(named_rule_battery_spec_frame(invalid)!=frame,"Reviewed source path belongs to objective identity");
    require(!named_rule_battery_source_valid(source,invalid),"CMake library cannot contain main");
    const auto main=source.substr(source.find("int main()"));
    const auto library=source.substr(0,source.find("int main()"));
    require(named_rule_battery_source_valid(library,invalid),"CMake source validates without the driver");
    require(!named_rule_battery_source_valid(library,spec),"Single-file source needs its own main");
    require(named_rule_battery_instrument_driver(main,invalid).has_value(),
        "CMake instrumentation preserves the submitted driver");
    require(!named_rule_battery_instrument_driver(source,spec),
        "Raw single-file source cannot skip the source instrumentation step");

    const auto replace=[&](std::string from,std::string to) {
        auto changed=source;const auto at=changed.find(from);
        require(at!=std::string::npos,"Source fixture fragment missing");
        changed.replace(at,from.size(),to);return changed;
    };
    require(named_rule_battery_source_valid(
        "int precio_total(int cantidad);\nbool alcanza_minimo(int numero);\n"+source,spec),
        "Forward declarations can name the same int parameter differently");
    for(const auto& bad : std::vector<std::string>{
        replace("int precio_total(int articulos)","long precio_total(int articulos)"),
        replace("int precio_total(int articulos)","int precio_total(int& articulos)"),
        replace("int precio_total(int articulos)","static int precio_total(int articulos)"),
        replace("int precio_total(int articulos)","constexpr int precio_total(int articulos)"),
        replace("int precio_total(int articulos)","int precio_total(int articulos) noexcept"),
        replace("int main() {","int main() noexcept {"),
        source+"\nint precio_total(long articulos) { return 0; }\n",
        source+"\nint main(int argc,char** argv) { return 0; }\n",
        replace("int main() {","int main() {\n#include <cstdio>\n"),
        "#define activo 1\n"+source,
        "/* comentario */ #if 0\n"+source+"\n#endif\n",
        source+"\nint entorno=__LINE__;\n",
        source+"\nvoid final() { auto cierre=&std::exit; cierre(0); }\n",
        source+"\nint lgaed_named_rule_battery_count=0;\n",
        source+"\nint lgaed_boolean_battery_count=0;\n",
        source+"\nint lgaed_integer_battery_count=0;\n",
        source+"\nconst char* testigo=\"LGAED_NAMED_RULE_BATTERY_END\";\n",
        replace("return articulos * precio_creditos;","(void)__func__; return articulos * precio_creditos;")})
        require(!named_rule_battery_source_valid(bad,spec),"Unsupported or forged source rejected");
    require(!named_rule_battery_driver_valid("int main() {\n#include <cstdio>\nreturn 0; }\n"),
        "Nested include also rejected in a raw CMake driver");
    require(!named_rule_battery_driver_valid("int main() noexcept { return 0; }\n"),
        "Driver main grammar matches the source validator");
    require(named_rule_battery_source_valid(source+
        "\n// __LINE__, exit(0), lgaed_named_rule_battery_ are only prose.\n"
        "const char* comentario=\"__func__ __FUNCTION__ exit(0)\";\n",spec),
        "Ordinary prose is not mistaken for executable environment queries");

    for(const auto& phase:*plan) {
        const auto instrumented=named_rule_battery_instrument_source(source,spec,phase);
        require(instrumented.has_value(),"Mixed int/bool source instrumentation failed");
        require(instrumented->find(main)!=std::string::npos,
            "Instrumentation must retain the complete real main");
        require(instrumented->find("const int precio_creditos{7};")!=std::string::npos &&
            instrumented->find("return articulos >= minimo_articulos;")!=std::string::npos,
            "Instrumentation must retain the original function bodies");
        require(named_rule_battery_instrument_driver(*instrumented,spec).has_value(),
            "Single-file driver witness composes after source instrumentation");
    }
    require(!named_rule_battery_instrument_source(source,spec,
        {NamedRuleBatteryMutation::mutate,99}),"An undeclared mutation is never generated");
    require(!named_rule_battery_instrument_source(source,spec,
        {NamedRuleBatteryMutation::baseline,0}),"Baseline cannot select a mutant target");

    // Functional probes can intentionally include an input outside the battery.
    NamedRuleExecutionProbe probe{"","",{{"precio_total",11,77}}};
    probe.battery_check=spec;
    require(named_rule_probe_checks(probe).has_value() &&
        probe.battery_check->cases.size()==4 && probe.cases.size()==1,
        "Private function probes and the student's required calls remain separate");
}
std::vector<lgaed::curriculum::NamedRuleBatteryRun> named_battery_evidence() {
    using namespace lgaed::curriculum;
    const auto spec=named_battery_example();
    const auto plan=*named_rule_battery_plan(spec);
    const std::array changed{1,0,1,0};
    std::vector<NamedRuleBatteryRun> runs;
    for(std::size_t r=0;r<plan.size();++r) {
        NamedRuleBatteryRun run;
        run.program_frame="original-project-and-objective";
        run.compiled=run.executed=run.completed_normally=true;
        run.exit_code=r==0?0:1;run.phase=plan[r];
        for(std::size_t c=0;c<spec.cases.size();++c) {
            const auto& item=spec.cases[c];
            const int result=r==c+1?changed[c]:item.expected;
            run.calls.push_back({item.function,item.input,result});
            run.stdout_bytes+=item.function+" "+std::to_string(item.input)+" "+
                std::to_string(item.expected)+" "+std::to_string(result)+"\n";
        }
        run.stdout_bytes+=r==0?"Bateria: OK\n":"Bateria: FALLO\n";
        runs.push_back(std::move(run));
    }
    return runs;
}
void real_battery_traces_and_verdicts() {
    using namespace lgaed::curriculum;
    const auto require=named_battery_require;
    const std::string prefix="LGAED_NAMED_RULE_BATTERY_";
    const auto parsed=named_rule_battery_parse_trace(
        "aviso="+prefix+"CALL:0,precio_total,5,35\r\n"
        "continua\n"+prefix+"CALL:1,alcanza_minimo,5,1\n"+prefix+"END\r\n");
    require(parsed && parsed->completed_normally && parsed->calls.size()==2 &&
        parsed->visible=="aviso=continua\n" &&
        parsed->calls[0]==NamedRuleBatteryObservation{"precio_total",5,35},
        "Trace extraction preserves partial student stderr and both line endings");
    for(const auto& malformed:std::vector<std::string>{
        prefix+"CALL:1,precio_total,0,0\n",
        prefix+"CALL:0,precio_total,0,0,extra\n",
        prefix+"CALL:0,precio_total,2147483648,0\n",
        prefix+"CALL:0,precio_total,0,0",
        prefix+"FORGED\n",
        prefix+"END\n"+prefix+"END\n",
        prefix+"END\n"+prefix+"CALL:0,precio_total,0,0\n"})
        require(!named_rule_battery_parse_trace(malformed),"Malformed or duplicate private trace rejected");
    const auto interrupted=named_rule_battery_parse_trace(prefix+"CALL:0,precio_total,0,0\n");
    require(interrupted && !interrupted->completed_normally,
        "A partial trace cannot fabricate normal completion");
    require(!named_rule_battery_parse_trace(std::string("visible\0hidden",14)),
        "NUL in a trace is not silently ignored");
    require(!named_rule_battery_parse_trace(std::string(4*1024*1024+1,'x')),
        "Trace input has a finite byte limit");

    const auto spec=named_battery_example();
    const auto good=named_battery_evidence();
    const auto verify=[&](const auto& runs) {
        return named_rule_battery_verify(spec,runs,"original-project-and-objective").accepted;
    };
    require(verify(good),"A complete real battery detects every independent mutant");
    auto reverse=good;
    for(auto& run:reverse)std::ranges::reverse(run.calls);
    require(verify(reverse),"Call coverage is independent of case declaration order");

    auto broken=good;broken.pop_back();
    require(!verify(broken),"Every declared mutation requires its own run");
    broken=good;broken[1].program_frame="previous-revision";
    require(!verify(broken),"Evidence from a different source revision is rejected");
    broken=good;broken[1].phase=broken[2].phase;
    require(!verify(broken),"A result cannot be credited to a different mutant");
    broken=good;broken[1].compiled=false;
    require(!verify(broken),"Compilation failure is not a detected comparison failure");
    broken=good;broken[1].executed=false;
    require(!verify(broken),"Missing execution is not a comparison result");
    broken=good;broken[1].completed_normally=false;
    require(!verify(broken),"Timeout or abnormal completion cannot kill a mutant");
    broken=good;broken[0].calls[0].result=12;
    require(!verify(broken),"The public baseline must calculate each authored value");
    broken=good;broken[1].calls[0].result=0;
    require(!verify(broken),"A mutant trace must contain its actual altered value");
    broken=good;broken[1].calls.pop_back();
    require(!verify(broken),"Stopping after the first failure omits a required call");
    broken=good;broken[1].calls[1]=broken[1].calls[0];
    require(!verify(broken),"A repeated function call cannot replace another case");
    broken=good;broken[1].calls.push_back(broken[1].calls[0]);
    require(!verify(broken),"Computing an expected value by another call is detected");
    broken=good;broken[1].calls[0].input=19;
    require(!verify(broken),"A private probe input cannot replace a battery case");
    broken=good;broken[1].exit_code=0;
    require(!verify(broken),"Self-comparison or an unconditional success must fail a mutant");
    broken=good;broken[1].exit_code=2;
    require(!verify(broken),"Failure exit must follow its authored policy");
    broken=good;broken[1].stdout_bytes=broken[0].stdout_bytes;
    require(!verify(broken),"Fixed stdout cannot report the actual discrepancy");
    broken=good;broken[2].stdout_bytes=broken[1].stdout_bytes;
    require(!verify(broken),"A shared fixed failure report does not identify different cases");
    broken=good;broken[1].stdout_bytes.clear();
    require(!verify(broken),"A failure needs a visible diagnostic");
    require(named_rule_battery_detail::changed(std::numeric_limits<int>::min())==0 &&
        named_rule_battery_detail::changed(std::numeric_limits<int>::max())==0 &&
        named_rule_battery_detail::changed(0)==1,
        "Mutations never overflow and remain representable as bool or int");
}

int main() {
    try {
        capacity();separate_rules();price_and_minimum();
        comment_only_header_changes();independently_generated_probe();
        real_battery_contract_and_sources();real_battery_traces_and_verdicts();
        std::cout<<"PASS local named rules: roles, values, uses and rejected shortcuts\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL named-rule source evidence: "<<e.what()<<'\n';return 1;
    }
}
