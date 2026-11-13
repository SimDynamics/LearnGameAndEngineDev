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
int main() {
    try {
        capacity();separate_rules();price_and_minimum();
        std::cout<<"PASS local named rules: roles, values, uses and rejected shortcuts\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL named-rule source evidence: "<<e.what()<<'\n';return 1;
    }
}
