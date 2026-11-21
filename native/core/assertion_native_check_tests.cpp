#include "core/assertion_runtime_check.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
using namespace lgaed::curriculum;
void require(bool pass,std::string_view message) {
    if(!pass)throw std::runtime_error(std::string(message));
}
int main() {
    try {
        const std::string actual=
            "Cajas: peticion 1, piezas=-1. Se admite un entero de 0 a 20. "
            "Revise la cantidad.\n"
            "Assertion failed: cajas * piezas_por_caja >= piezas, file "
            "C:\\QA\\assertions\\main.cpp, line 7\n";
        require(assertion_native_abort("",actual,0xC0000409u),
            "A real native MSVC assertion was rejected");
        auto crlf=actual;
        for(std::size_t i=0;i<crlf.size();++i)
            if(crlf[i]=='\n') {crlf.insert(i,"\r");++i;}
        require(assertion_native_abort("",crlf,0xC0000409u),
            "MSVC native CRLF streams were rejected");
        require(assertion_native_lines("one\n","one\r\n"),
            "CRLF matching failed for the reviewed native output");
        for(const auto code:{0u,1u,2u,4u,0xC0000005u})
            require(!assertion_native_abort("",actual,code),
                "An unrelated return/crash was credited as an assertion");
        for(const auto& [from,to]:{
                std::pair<std::string_view,std::string_view>{"line 7","line 8"},
                {"main.cpp","forged.cpp"},
                {"cajas * piezas_por_caja >= piezas","1 != 1"},
                {"piezas=-1","piezas=21"},
                {"Assertion failed:","Other runtime error:"}}) {
            auto forged=actual;
            const auto at=forged.find(from);
            require(at!=forged.npos,"Malformed native assertion test fixture");
            forged.replace(at,from.size(),to);
            require(!assertion_native_abort("",forged,0xC0000409u),
                "An unrelated or forged runtime diagnostic was accepted");
        }
        require(!assertion_native_abort("forged",actual,0xC0000409u),
            "An unexpected stdout line was accepted");
        require(!assertion_native_abort("",actual.substr(0,30),0xC0000409u),
            "An incomplete stderr prefix was accepted");
        std::cout<<"PASS exact MSVC assertion versus unrelated crash\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL native assert proof: "<<e.what()<<'\n';
        return 1;
    }
}
