#include "core/named_rule_check.hpp"
#include "core/named_rule_battery_check.hpp"
#include "core/named_rule_constant_variation_check.hpp"
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
    for(const auto declaration:{
            "const int capacidad = {4};",
            "constexpr int capacidad = {((2 + 2)),};",
            "int const capacidad = {0x4};",
            "int constexpr capacidad{(2 * 3) - 2,};"})
        verify("int cajas_para(int piezas) { "+std::string(declaration)+
                "\nreturn (piezas + capacidad - 1) / capacidad; }",
            rules,true,"Equivalent scalar list initialization retains source evidence");
    for(const auto declaration:{
            "const int capacidad = {};",
            "const int capacidad{};",
            "const int capacidad = {4, 4};",
            "const int capacidad{4, 4,};",
            "const int capacidad = {4,,};",
            "const int capacidad = {4.0};",
            "const int capacidad = {5,};",
            "const int capacidad(4,);",
            "const auto capacidad = {4};",
            "const int capacidad{4, \"texto\"};",
            "const int capacidad = {4, 'x',};"})
        verify("int cajas_para(int piezas) { "+std::string(declaration)+
                "\nreturn (piezas + capacidad - 1) / capacidad; }",
            rules,false,"Only one explicit integer expression can establish the four-place rule");
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
    // Extra parentheses do not turn a neutral expression into an actual
    // dependency on the four-place agreement. Each case has two lexical
    // occurrences that can otherwise satisfy the min_uses=2 source gate.
    for(const auto* neutral:{
            "(capacidad - (capacidad))",
            "((capacidad) - ((capacidad)))",
            "(capacidad / (capacidad))",
            "((capacidad) % capacidad)"}) {
        verify("int cajas_para(int piezas) { const int capacidad{4}; "
               "return (piezas + 3) / 4 + "+std::string(neutral)+"; }",
            rules,false,"Parenthesized self-cancellation is not a productive constant use");
    }
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return capacidad - (capacidad);
        })cpp",rules,false,
        "A complete unparenthesized return must not credit two cancelled uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int ajuste = capacidad - (capacidad);
            return (piezas + 3) / 4 + ajuste + ajuste;
        })cpp",rules,false,
        "An alias of a neutral subtraction cannot launder two fake uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad - (capacidad));
        })cpp",rules,true,
        "A neutral subexpression must not suppress two genuine independent uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad - (capacidad * 2));
        })cpp",rules,true,
        "A non-neutral subtraction keeps its two real uses for the independent runtime probe");
    // C++ evaluates multiplication, division and remainder before subtraction.
    // An immediate "capacity - capacity" token pair is not a cancellation
    // when its right-hand operand is still part of one of those operations.
    for(const auto* arithmetic:{
            "capacidad - capacidad * 2",
            "capacidad - capacidad / 2",
            "capacidad - capacidad % 3"}) {
        verify("int cajas_para(int piezas) { const int capacidad{4}; "
               "return (piezas + 3) / 4 + ("+std::string(arithmetic)+"); }",
            rules,true,"Higher-precedence right operand must retain both productive rule uses");
    }
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int diferencia = capacidad - capacidad * 2;
            return (piezas + 3) / 4 + diferencia;
        })cpp",rules,true,
        "An alias must carry both genuinely dependent uses of the constant");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad - capacidad * 0);
        })cpp",rules,false,
        "A zero product leaves only one productive use, below minUses two");

    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + ((capacidad + 1) / capacidad);
        })cpp",rules,true,
        "A quotient of different expressions must not be mistaken for a neutral pair");
    // Unary negation also cancels the same local value in an additive
    // expression. The two tokens do not establish two meaningful uses.
    for(const auto* neutral:{
            "(capacidad + (-capacidad))",
            "((-capacidad) + capacidad)",
            "(capacidad + (-((capacidad))))"}) {
        verify("int cajas_para(int piezas) { const int capacidad{4}; "
               "return (piezas + 3) / 4 + "+std::string(neutral)+"; }",
            rules,false,"Additive inverse operands must not manufacture named-rule evidence");
    }
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int ajuste = capacidad + (-capacidad);
            return (piezas + 3) / 4 + ajuste + ajuste;
        })cpp",rules,false,
        "An additive inverse cannot launder fake constant uses through an alias");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad +
                   (capacidad + (-capacidad));
        })cpp",rules,true,
        "An additive inverse must not erase genuine independent uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad - (-capacidad));
        })cpp",rules,true,
        "Subtracting a negation is not a cancellation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + 3) / 4 + (capacidad + capacidad);
        })cpp",rules,true,
        "Adding two positive references is not a cancellation");
    // The left operand of a built-in comma expression is discarded. Two
    // appearances cannot satisfy minUses=2 when only one reaches the
    // returned value. This must not confuse a function-call argument list.
    for(const auto* discarded:{
            "(capacidad, capacidad)",
            "(capacidad + capacidad, 0)",
            "(capacidad, capacidad + 0)",
            "(capacidad, capacidad, 0)",
            "(capacidad, (capacidad))"}) {
        verify("int cajas_para(int piezas) { const int capacidad{4}; "
               "return (piezas + 3) / 4 + "+std::string(discarded)+"; }",
            rules,false,"Discarded pure comma operand manufactured rule uses");
    }
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return capacidad, capacidad;
        })cpp",rules,false,
        "A direct return comma expression must count only the final named value");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return capacidad + capacidad, 0;
        })cpp",rules,false,
        "Both named references before an unparenthesized return comma are discarded");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            const int ajuste = (capacidad + capacidad, 0);
            return (piezas + 3) / 4 + ajuste;
        })cpp",rules,false,
        "An alias of a discarded expression must not create two rule uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (capacidad, (piezas + capacidad - 1) / capacidad);
        })cpp",rules,true,
        "An unused comma operand must not erase two productive references");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            return (piezas + capacidad - 1) / capacidad + (capacidad, 0);
        })cpp",rules,true,
        "A harmless discarded comma operand must not erase a correct computation");
    verify(R"cpp(
        int elegir(int a, int b) { return a + b; }
        int cajas_para(int piezas) {
            const int capacidad{4};
            return elegir(capacidad, capacidad);
        })cpp",rules,true,
        "Function argument separators cannot be interpreted as the comma operator");
    verify(R"cpp(
        #include <array>
        int cajas_para(int piezas) {
            const int capacidad{4};
            return std::array{capacidad, capacidad}[0];
        })cpp",rules,true,
        "Braced initializer separators cannot be interpreted as the comma operator");



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
void conditional_assignment_branches() {
    const std::array rules{NamedRuleRequirement{"cajas_para",4,
        {"capacidad","plaza","cupo"},2}};
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas == 0) {
                resultado = 0;
            } else {
                resultado = (piezas + capacidad - 1) / capacidad;
            }
            return resultado;
        })cpp",rules,true,
        "A zero-domain guard and an assigned named result are equivalent");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int plazas_por_caja{4};
            int resultado{0};
            if (piezas > 0)
                resultado = (piezas + plazas_por_caja - 1) / plazas_por_caja;
            else
                resultado = 0;
            return resultado;
        })cpp",rules,true,
        "Unbraced if/else assignment retains the named rule");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas % 2 == 0) {
                resultado = (piezas + capacidad - 1) / capacidad;
            } else {
                resultado = (piezas - 1 + capacidad) / capacidad;
            }
            return resultado;
        })cpp",rules,true,
        "Distinct yet equivalent branches may both depend on capacity");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas > 10) {
                resultado = (piezas + 3) / 4;
            } else {
                resultado = (piezas + capacidad - 1) / capacidad;
            }
            return resultado;
        })cpp",rules,false,
        "A hardcoded branch cannot borrow named uses from the other branch");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas > 10)
                resultado = (piezas + capacidad - 1) / capacidad;
            else
                resultado = 1;
            return resultado;
        })cpp",rules,false,
        "Only a zero guard may omit named calculation in a branch");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas < 0) resultado = 0;
            else resultado = 0;
            return resultado;
        })cpp",rules,false,
        "Two literal guards do not establish a named result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas > 10)
                resultado = (piezas + capacidad - 1) / capacidad;
            else
                resultado = (piezas + 3) / 4;
            resultado = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "A later hardcoded overwrite removes branch-assignment evidence");

    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + 3) / 4;
            if (piezas < 0)
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,false,
        "A conditional named assignment cannot overwrite a hardcoded default on every path");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 10) {
                resultado = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "An optional hardcoded overwrite must not inherit initial named-rule evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{2};
            if (piezas > 0)
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,false,
        "A nonzero hardcoded fallback is not a valid zero-domain guard");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas > 0)
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "Value-initialized zero plus a positive-input branch is a legitimate calculation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = {0};
            if (piezas > 0) {
                resultado = (piezas + capacidad - 1) / capacidad;
            }
            return resultado;
        })cpp",rules,true,
        "Copy-list initialized zero preserves a calculated conditional result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas == 0) resultado = 0;
            return resultado;
        })cpp",rules,true,
        "A conditional zero guard does not erase the real calculation on other paths");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (true)
                resultado = (piezas + capacidad - 1) / capacidad;
            else
                resultado = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,true,
        "A constant-true assignment ignores a dead hardcoded else");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (true)
                resultado = (piezas + 3) / 4;
            else
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,false,
        "An unreachable named else cannot rescue a true hardcoded branch");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (false) resultado = (piezas + 3) / 4;
            else resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "A constant-false condition gives the live else the named-rule calculation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas > 0) {
                resultado = (piezas + capacidad - 1) / capacidad;
                (void)resultado;
            }
            return resultado;
        })cpp",rules,false,
        "An unrecognized multi-statement branch cannot appear to assign unconditionally");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            if (piezas > 10) {
                resultado = (piezas + capacidad - 1) / capacidad;
                resultado = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "A conditional branch cannot hide a hardcoded second assignment");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas < 0)
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "Two authentic results survive a conditional reassignment without an else");

    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado;
            if (piezas == 0)
                resultado = 0;
            else
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "An uninitialized local assigned on both paths can return a valid result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado;
            if (piezas % 2 == 0) {
                resultado = (piezas + capacidad - 1) / capacidad;
            } else {
                resultado = (piezas - 1 + capacidad) / capacidad;
            }
            return resultado;
        })cpp",rules,true,
        "A local without initializer may be assigned two independently named results");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado;
            if (piezas > 0)
                resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,false,
        "An uninitialized local with a missing branch cannot earn source credit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado;
            if (piezas > 10)
                resultado = (piezas + capacidad - 1) / capacidad;
            else
                resultado = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "A declared result may not borrow the agreement from only one branch");
    // A branch-local result named like the outer result does not modify
    // the calculated value. Both declarations and assignments stay scoped.
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int resultado = 3;
                resultado = 4;
                (void)resultado;
            }
            return resultado;
        })cpp",rules,true,
        "A branch-local same-name result must not erase a valid outer calculation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                const int resultado = 123;
                (void)resultado;
            } else {
                auto resultado = 5;
                (void)resultado;
            }
            return resultado;
        })cpp",rules,true,
        "Two harmless branches may shadow the outer name without changing it");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int otro = 1;
                resultado = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "An actual outer assignment inside a multi-statement branch must be rejected");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + 3) / 4;
            if (piezas > 0) {
                int resultado = (piezas + capacidad - 1) / capacidad;
            } else {
                int resultado = (piezas + capacidad - 1) / capacidad;
            }
            return resultado;
        })cpp",rules,false,
        "Named calculations in shadowed locals do not repair an outer hardcoded result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int otro = 1;
                return (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "A hidden early return cannot borrow the named calculation from fallthrough");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (true) {
                int resultado = 8;
                (void)resultado;
            } else {
                resultado = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,true,
        "An always-true harmless shadow ignores the dead outer overwrite");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int resultado = 8;
                (void)resultado;
            } else {
                resultado = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "A live else branch must not be treated as unconditional harmless code");
    verify(R"cpp(
        #include <utility>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int sustituto = (piezas + 3) / 4;
            if (piezas > 0) {
                std::swap(resultado, sustituto);
            }
            return resultado;
        })cpp",rules,false,
        "A call can replace the result without a visible assignment operator");
    verify(R"cpp(
        void reemplazar(int& destino, int valor) { destino = valor; }
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0)
                reemplazar(resultado, (piezas + 3) / 4);
            return resultado;
        })cpp",rules,false,
        "A user-defined function can mutate a returned alias by reference");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int& referencia = resultado;
                referencia = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "A new reference name can overwrite the real result invisibly");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int* puntero = &resultado;
                *puntero = (piezas + 3) / 4;
            }
            return resultado;
        })cpp",rules,false,
        "A pointer to the result is not an inert use");
    verify(R"cpp(
        #include <iostream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                std::cin >> resultado;
            }
            return resultado;
        })cpp",rules,false,
        "Input extraction can modify the result without a C++ assignment");
    verify(R"cpp(
        #include <iostream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                std::cout << resultado;
            }
            return resultado;
        })cpp",rules,true,
        "Output insertion that only observes the result stays harmless to source evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                const int vista = (resultado);
                (void)vista;
            }
            return resultado;
        })cpp",rules,true,
        "Parenthesized observation of a named result is not a mutable call");
    verify(R"cpp(
        #include <utility>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int resultado = (piezas + 3) / 4;
                std::swap(resultado, piezas);
            }
            return resultado;
        })cpp",rules,true,
        "A mutable call affecting a shadow must not erase its outer counterpart");
    verify(R"cpp(
        #include <utility>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int sustituto = (piezas + 3) / 4;
            std::swap(resultado, sustituto);
            return resultado;
        })cpp",rules,false,
        "An unconditional swap must not launder a hardcoded replacement");
    verify(R"cpp(
        void sustituir(int& destino, int value) { destino = value; }
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            sustituir(resultado, (piezas + 3) / 4);
            return resultado;
        })cpp",rules,false,
        "A direct custom call can mutate the named calculation's result");
    verify(R"cpp(
        #include <utility>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int sustituto = (piezas + 3) / 4;
            {
                std::swap(resultado, sustituto);
            }
            return resultado;
        })cpp",rules,false,
        "A nested block cannot conceal an unconditional swap of the outer alias");
    verify(R"cpp(
        #include <utility>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            {
                int resultado = (piezas + 3) / 4;
                int sustituto = 4;
                std::swap(resultado, sustituto);
            }
            return resultado;
        })cpp",rules,true,
        "Swapping an independent block-local shadow preserves the outer result");
    verify(R"cpp(
        #include <iostream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            std::cout << resultado;
            return resultado;
        })cpp",rules,true,
        "Observing a result through stream insertion does not replace it");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int& alias = resultado;
            alias = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "An escaped non-const reference can replace a named result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int* puntero = &resultado;
            *puntero = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "Taking the result's address can bypass direct assignment tracking");
    verify(R"cpp(
        #include <functional>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            auto variable = std::ref(resultado);
            variable.get() = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "An initializer that forwards the result by reference is not inert");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            return int(resultado);
        })cpp",rules,true,
        "A built-in functional cast reads, but cannot mutate, the result");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int copia(resultado);
                (void)copia;
            }
            return resultado;
        })cpp",rules,true,
        "Direct-initializing an independent int inside an if is harmless");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                int const copia(resultado);
                (void)copia;
            }
            return resultado;
        })cpp",rules,true,
        "East-const direct initialization is not a mutating function call");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            if (piezas > 0) {
                (void)int(resultado);
            }
            return resultado;
        })cpp",rules,true,
        "An ignored built-in value cast leaves the named computation intact");
}

void compound_assignment_evidence() {
    const std::array rules{NamedRuleRequirement{"cajas_para",4,
        {"capacidad","plaza","cupo"},2}};
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            resultado ^= resultado;
            resultado += (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "XOR self-cancellation followed by a literal answer is not named evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            resultado -= resultado;
            resultado += (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "Subtracting the named result from itself cannot leave earlier credit");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            resultado *= 0;
            resultado += (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "Multiplying a dependent result by zero removes its authenticated uses");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado{0};
            resultado += (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "A zero initialized accumulator may genuinely use the named rule");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = {0};
            resultado += (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "Copy-list zero initialization supports an equivalent accumulation");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            resultado += 0;
            resultado -= 0;
            resultado ^= 0;
            resultado |= 0;
            return resultado;
        })cpp",rules,true,
        "Provably neutral compound updates preserve useful source evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int observacion = (piezas + capacidad - 1) / capacidad;
            observacion ^= observacion;
            return (piezas + capacidad - 1) / capacidad;
        })cpp",rules,true,
        "An unrelated canceled temporary cannot reject a correct direct return");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            resultado ^= resultado;
            resultado = (piezas + capacidad - 1) / capacidad;
            return resultado;
        })cpp",rules,true,
        "A later correct plain assignment can restore proven dependency");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            resultado++;
            resultado--;
            resultado += (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "Increment and decrement cannot lend a later literal answer old evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            --resultado;
            resultado = (piezas + 3) / 4;
            return resultado;
        })cpp",rules,false,
        "Prefix updates invalidate earlier evidence before a hardcoded return");
}

void stream_extraction_and_shift_evidence() {
    const std::array rules{NamedRuleRequirement{"cajas_para",4,
        {"capacidad","plaza","cupo"},2}};
    verify(R"cpp(
        #include <sstream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            std::istringstream entrada{"1"};
            entrada >> resultado;
            return resultado;
        })cpp",rules,false,
        "A stream extraction overwrites the named result without '='");
    verify(R"cpp(
        #include <sstream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            std::istringstream entrada{"1"};
            entrada >> (resultado);
            return resultado;
        })cpp",rules,false,
        "Parenthesized extraction is still a write to the result");
    verify(R"cpp(
        #include <sstream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            std::istringstream entrada{"1"};
            return (entrada >> resultado, resultado);
        })cpp",rules,false,
        "A comma expression cannot hide input extraction inside return");
    verify(R"cpp(
        #include <sstream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            std::ostringstream salida;
            salida << resultado;
            return resultado;
        })cpp",rules,true,
        "Output insertion only reads the named result");
    verify(R"cpp(
        #include <sstream>
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int otro = 0;
            std::istringstream entrada{"1"};
            entrada >> otro;
            return resultado;
        })cpp",rules,true,
        "Extraction into an unrelated local does not erase genuine evidence");
    verify(R"cpp(
        int cajas_para(int piezas) {
            const int capacidad{4};
            int resultado = (piezas + capacidad - 1) / capacidad;
            int desplazar = 32 >> resultado;
            (void)desplazar;
            return resultado;
        })cpp",rules,true,
        "A literal integer bit shift cannot mutate its shift count");
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

NamedRuleExecutionProbe constant_variation_example() {
    NamedRuleExecutionProbe probe{"","",{
        {"cajas_para",0,0},{"cajas_para",4,1},{"cajas_para",5,2},{"cajas_para",20,5},
        {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}}};
    probe.constant_variations = NamedRuleConstantVariationSpec{"",{
        {"cajas_para",4,{"capacidad","plaza","cupo"},2},
        {"llega_a_tiempo",4,{"plazo"},1}}, {
        {"cajas_para",3,{
            {"cajas_para",0,0},{"cajas_para",4,2},{"cajas_para",5,2},{"cajas_para",20,7},
            {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}}},
        {"cajas_para",5,{
            {"cajas_para",0,0},{"cajas_para",4,1},{"cajas_para",5,1},{"cajas_para",20,4},
            {"llega_a_tiempo",4,1},{"llega_a_tiempo",5,0}}}}};
    return probe;
}
std::string constant_variation_candidate(std::string_view declaration,
        std::string_view body="return (piezas + capacidad - 1) / capacidad;",
        bool with_main=true) {
    // Repeated numeric text, an unrelated agreement and a nested namesake
    // must retain their exact bytes when the authentic initializer changes.
    std::string source=R"cpp(// Información: capacidad 4; const int capacidad{4};
const char* literal="const int capacidad{4}; 4";
const char* raw=R"nota(const int capacidad{4}; /* 4 */)nota";
int cajas_para(int piezas) {
    )cpp";
    source+=declaration;
    source+=R"cpp(
    { const int capacidad{4}; (void)capacidad; }
    )cpp";
    source+=body;
    source+=R"cpp(
}
bool llega_a_tiempo(int dias) {
    const int plazo{4};
    return dias <= plazo;
}
)cpp";
    if(with_main)source+="int main() { return 0; }\n";
    return source;
}
void constant_variation_contract() {
    const auto require=named_battery_require;
    const auto probe=constant_variation_example();
    const auto& spec=*probe.constant_variations;
    require(named_rule_constant_variation_spec_valid(spec,probe.cases),
        "Discriminating variations cover both independent functions");
    const auto invalid=[&](const NamedRuleConstantVariationSpec& value,
            std::string_view reason) {
        require(!named_rule_constant_variation_spec_valid(value,probe.cases),reason);
    };
    auto changed=spec; changed.variations.clear();
    invalid(changed,"Present variations cannot be empty");
    changed=spec;
    for(int i=0;i<7;++i) {
        auto variation=changed.variations.front();variation.value=10+i;
        changed.variations.push_back(std::move(variation));
    }
    invalid(changed,"Private recompilations are bounded to eight variations");
    changed=spec;changed.variations.front().value=4;
    invalid(changed,"Replacing an agreement with its original value proves nothing");
    changed=spec;changed.variations.push_back(changed.variations.front());
    invalid(changed,"A function/value pair cannot be repeated");
    changed=spec;changed.variations.front().function="another_function";
    invalid(changed,"A variation cannot select an unauthenticated function");
    changed=spec;changed.source_path="../outside.cpp";
    invalid(changed,"The authenticated source path cannot traverse the project");
    changed=spec;changed.requirements.front().min_uses=0;
    invalid(changed,"Derived source prerequisites must remain present");
    changed=spec;changed.requirements.front().min_uses=9;
    invalid(changed,"Derived minimum use count retains its authored bound");
    changed=spec;changed.requirements.front().purpose_words={"capacidad; injected"};
    invalid(changed,"Purpose vocabulary must remain an authored bounded identifier fragment");
    changed=spec;changed.requirements.push_back(changed.requirements.front());
    invalid(changed,"A derived function requirement must remain unique");
    changed=spec;changed.variations.front().cases.clear();
    invalid(changed,"A variation cannot omit its independent answers");
    changed=spec;changed.variations.front().cases.resize(33,{"cajas_para",40,10});
    invalid(changed,"Each private driver is bounded to 32 cases");
    changed=spec;changed.variations.front().cases.push_back(changed.variations.front().cases.front());
    invalid(changed,"A private function/input pair cannot be repeated");
    changed=spec;changed.variations.front().cases.front().function="unknown";
    invalid(changed,"Private cases cannot invoke an undeclared function");
    changed=spec;changed.variations.front().cases.resize(4);
    invalid(changed,"A variation must observe the functions that were not changed too");
    changed=spec;
    for(auto& item:changed.variations.front().cases)
        if(item.function=="cajas_para") {
            const auto original=std::ranges::find_if(probe.cases,[&](const auto& value) {
                return value.function==item.function&&value.input==item.input;
            });
            item.expected=original->expected;
        }
    invalid(changed,"A shared target case must change its independently authored answer");
    changed=spec;
    for(auto& item:changed.variations.front().cases)
        if(item.function=="cajas_para")item.input+=100;
    invalid(changed,"New inputs alone do not witness changed behavior on a shared input");
    changed=spec;
    for(auto& item:changed.variations.front().cases)
        if(item.function=="llega_a_tiempo")item.input+=100;
    invalid(changed,"Every other function needs a shared independence witness");
    changed=spec;changed.variations.front().cases.back().expected=1;
    invalid(changed,"One unchanged observation cannot excuse another contradictory shared answer");
    auto base=probe.cases;base.pop_back();
    require(named_rule_constant_variation_spec_valid(spec,base),
        "An additional authored case is allowed beside a shared independence witness");
    base.pop_back();
    require(!named_rule_constant_variation_spec_valid(spec,base),
        "The base probe must cover each derived source function");
    base=probe.cases;base.push_back(base.front());
    require(!named_rule_constant_variation_spec_valid(spec,base),
        "The base probe cannot supply duplicated witnesses");

    const auto frame=named_rule_constant_variation_spec_frame(spec);
    changed=spec;changed.source_path="src/reglas.cpp";
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "The precise source path belongs to variation provenance");
    changed=spec;changed.requirements.front().purpose_words.push_back("espacio");
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "The authentic declaration selection vocabulary belongs to variation provenance");
    changed=spec;changed.requirements.front().min_uses=1;
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "The historic source prerequisite belongs to variation provenance");
    changed=spec;changed.requirements.front().value=6;
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "The original agreement belongs to variation provenance");
    changed=spec;changed.variations.front().value=6;
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "The counterfactual agreement belongs to variation provenance");
    changed=spec;changed.variations.front().cases.front().expected=9;
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "The independently authored answer belongs to variation provenance");
    changed=spec;std::ranges::reverse(changed.variations);
    require(named_rule_constant_variation_spec_frame(changed)!=frame,
        "Phase order belongs to variation provenance");

    const auto legacy_checks=named_rule_probe_checks(probe);
    auto legacy=probe;legacy.constant_variations.reset();
    require(named_rule_probe_checks(legacy)==legacy_checks,
        "The optional extension must not change the original independent driver");
    require(!named_rule_constant_variation_probe(legacy,0)&&
        !named_rule_constant_variation_source(constant_variation_candidate(
            "const int capacidad{4};"),legacy,0),
        "Absent variation authoring must not invent private recompilations");
    auto with_battery=probe;
    with_battery.battery_check=NamedRuleBatterySpec{"",probe.cases,1};
    const auto private_probe=named_rule_constant_variation_probe(with_battery,0);
    require(private_probe&&!private_probe->constant_variations&&!private_probe->battery_check&&
        private_probe->cases==spec.variations.front().cases&&
        private_probe->header==probe.header&&private_probe->harness_path==probe.harness_path&&
        with_battery.constant_variations==probe.constant_variations&&with_battery.battery_check,
        "Each phase has its own facts and cannot recursively mutate the student battery");
    require(!named_rule_constant_variation_probe(probe,2),
        "An undeclared phase cannot be constructed");
}

void constant_variation_source_identity() {
    const auto require=named_battery_require;
    const auto probe=constant_variation_example();
    struct Spelling {std::string_view before,after;};
    constexpr Spelling spellings[]{
        {"const int capacidad{4};","const int capacidad{(3)};"},
        {"const int capacidad = {4};","const int capacidad = {(3)};"},
        {"constexpr int capacidad = {((2 + 2)),};","constexpr int capacidad = {(3),};"},
        {"int const capacidad = {0x4};","int const capacidad = {(3)};"},
        {"int constexpr capacidad{(2 * 3) - 2,};","int constexpr capacidad{(3),};"},
        {"const int capacidad = { /* antes */ (2 /* cálculo */ + 2) /* después */, /* borde */ };",
         "const int capacidad = { /* antes */ (3) /* después */, /* borde */ };"},
        {"int const capacidad = (2 /* cálculo */ + 2);","int const capacidad = (3);"},
        {"constexpr int capacidad(0b100);","constexpr int capacidad((3));"},
        {"int constexpr capacidad{4'0 / 10};","int constexpr capacidad{(3)};"},
        {"static const int capacidad = -(-4);","static const int capacidad = (3);"},
        {"const int capacidad{ /* antes 4 */ 0x4 /* después 4 */ };",
         "const int capacidad{ /* antes 4 */ (3) /* después 4 */ };"},
        {"const int capacidad = 04;","const int capacidad = (3);"}};
    for(const auto& spelling:spellings) {
        const auto original=constant_variation_candidate(spelling.before);
        const auto varied=named_rule_constant_variation_source(original,probe,0);
        require(varied&&*varied==constant_variation_candidate(spelling.after),
            "Only the genuine initializer changes across equivalent declarations, comments and literals");
    }
    const auto crlf=[](std::string text) {
        std::string result;
        for(const auto c:text){if(c=='\n')result+='\r';result+=c;}
        return result;
    };
    const auto windows_source=crlf(constant_variation_candidate("const int capacidad = 2 +\n        2;"));
    require(named_rule_constant_variation_source(windows_source,probe,0)==
            crlf(constant_variation_candidate("const int capacidad = (3);")),
        "Byte offsets retain CRLF and UTF-8 outside a multiline initializer");
    const auto windows_list_source=crlf(constant_variation_candidate(
        "const int capacidad = {\n        /* comienzo */ (2 +\n        2), /* final */\n    };"));
    require(named_rule_constant_variation_source(windows_list_source,probe,0)==
            crlf(constant_variation_candidate(
                "const int capacidad = {\n        /* comienzo */ (3), /* final */\n    };")),
        "A copy-list variation preserves its braces, trailing comma and surrounding CRLF bytes");
    for(const auto body:{
            "return piezas / capacidad + (piezas % capacidad != 0);",
            "const auto ajuste = capacidad - 1; return (piezas + ajuste) / capacidad;",
            "if (piezas == 0) return 0; return (piezas + capacidad - 1) / capacidad;"}) {
        const auto original=constant_variation_candidate("const int capacidad{4};",body);
        require(named_rule_constant_variation_source(original,probe,0)==
                constant_variation_candidate("const int capacidad{(3)};",body),
            "Equivalent calculation, alias and normal domain guard retain the original minimum-use contract");
    }
    const auto renamed=constant_variation_candidate("const int plazas_por_caja{4};",
        "return (piezas + plazas_por_caja - 1) / plazas_por_caja;");
    require(named_rule_constant_variation_source(renamed,probe,0)==
            constant_variation_candidate("const int plazas_por_caja{(3)};",
                "return (piezas + plazas_por_caja - 1) / plazas_por_caja;"),
        "Purpose vocabulary selects the student's equivalent local name");
    for(const auto expression:{
            "return (piezas + capacidad - (capacidad / 4)) / 4;",
            "return (piezas + 3) / (capacidad / 2 + capacidad / 2);"}) {
        const auto original=constant_variation_candidate("const int capacidad{4};",expression);
        require(analyze_named_rule_sources(original,probe.constant_variations->requirements).source_evidence,
            "Historic minUses remains a source prerequisite, not a universal dependence proof");
        require(named_rule_constant_variation_source(original,probe,0)==
                constant_variation_candidate("const int capacidad{(3)};",expression),
            "A private change must not repair the student's hard-coded divisor or adjustment");
    }
    const auto original=constant_variation_candidate("const int capacidad{4};");
    const auto first=named_rule_constant_variation_source(original,probe,0);
    const auto second=named_rule_constant_variation_source(original,probe,1);
    require(first&&second&&*second==constant_variation_candidate("const int capacidad{(5)};")&&
        !named_rule_constant_variation_source(*first,probe,1),
        "Each phase must originate in the authenticated original agreement");
    for(const auto bound:{std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}) {
        auto bounded=probe;bounded.constant_variations->variations.front().value=bound;
        const auto varied=named_rule_constant_variation_source(original,bounded,0);
        require(varied&&*varied==constant_variation_candidate(
                "const int capacidad{("+std::to_string(bound)+")};"),
            "Signed int limits are emitted as complete canonical expressions without overflow");
    }
    auto project=probe;
    project.harness_path="pruebas/casos.cpp";project.header="envio/reglas.h";
    project.constant_variations->source_path="src/reglas.cpp";
    const auto library=constant_variation_candidate("const int capacidad{4};",
        "return (piezas + capacidad - 1) / capacidad;",false);
    require(named_rule_constant_variation_source(library,project,0)==
            constant_variation_candidate("const int capacidad{(3)};",
                "return (piezas + capacidad - 1) / capacidad;",false),
        "The same bounded initializer transformation applies to an authenticated CMake source");
    const auto project_probe=named_rule_constant_variation_probe(project,0);
    require(project_probe&&named_rule_probe_source(*project_probe).has_value(),
        "The CMake phase reuses the independently generated driver");
    const auto single_probe=named_rule_constant_variation_probe(probe,0);
    require(single_probe&&first&&named_rule_single_file_probe_source(*single_probe,*first).has_value(),
        "The single-file phase reuses the independently generated driver");

    for(const auto declaration:{
            "const int capacidad = {};",
            "const int capacidad{};",
            "const int capacidad = {4, 4};",
            "const int capacidad{4, 4,};",
            "const int capacidad = {4,,};",
            "const int capacidad = {4.0};",
            "const int capacidad = {5,};",
            "const int capacidad(4,);",
            "const auto capacidad = {4};",
            "const int capacidad{4, \"texto\"};",
            "const int capacidad = {4, 'x',};"})
        require(!named_rule_constant_variation_source(
                constant_variation_candidate(declaration),probe,0),
            "A private variation must not turn an unsupported scalar declaration into a rule");
    for(const auto& bad:{
            constant_variation_candidate("const int capacidad{6};"),
            constant_variation_candidate("const int capacidad{4}; const int cupo{4};"),
            constant_variation_candidate("const int capacidad{4};","return (piezas + 3) / 4;"),
            "const int capacidad{4};\n"+constant_variation_candidate(""),
            "#define capacidad 4\n"+original,
            "// continued\\\n"+original,
            original+"\nconst int observed_line = __LINE__;\n",
            original+"\nvoid finish() { auto end = &std::exit; end(0); }\n",
            original+"\n/* unterminated",
            original+std::string("\0hidden",7)})
        require(!named_rule_constant_variation_source(bad,probe,0),
            "Wrong, ambiguous, unproductive, global, spliced or unauthenticated source cannot yield a private variation");
}

int main() {
    try {
        capacity();conditional_assignment_branches();compound_assignment_evidence();stream_extraction_and_shift_evidence();separate_rules();price_and_minimum();
        comment_only_header_changes();independently_generated_probe();
        real_battery_contract_and_sources();real_battery_traces_and_verdicts();
        constant_variation_contract();constant_variation_source_identity();
        std::cout<<"PASS local named rules: roles, values, uses and rejected shortcuts\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL named-rule source evidence: "<<e.what()<<'\n';return 1;
    }
}
