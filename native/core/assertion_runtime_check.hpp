#pragma once

#include <array>
#include <sstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

enum class AssertionMode { enabled_abort, disabled, repaired };

inline bool assertion_source_allowed(AssertionMode mode,
        std::string_view original,std::string_view candidate) {
    // The two observation editors run their immutable original sources.
    if(mode!=AssertionMode::repaired)return candidate==original;
    constexpr std::string_view assignment="const int cajas = ";
    const auto index=original.find(assignment);
    if(index==original.npos||original.find(assignment,index+1)!=original.npos)return false;
    const auto begin=index+assignment.size();
    const auto end=original.find(';',begin);
    if(end==original.npos)return false;
    const auto prefix=original.substr(0,begin),suffix=original.substr(end);
    if(!candidate.starts_with(prefix)||!candidate.ends_with(suffix)||
        candidate.size()<=prefix.size()+suffix.size())return false;
    const auto expression=candidate.substr(prefix.size(),candidate.size()-prefix.size()-suffix.size());
    if(expression.size()>512)return false;
    for(const char c:expression)if(c==';'||c=='{'||c=='}'||c=='#'||c=='\n'||c=='\r')return false;
    return true;
}

struct AssertionExpected {
    std::string stdout_bytes,stderr_bytes;
    int exit_code=0;
};

// Domain of the reviewed program: up to eight signed inputs; the public
// rejection is a valid handled return of one, never an assertion abortion.
inline std::optional<AssertionExpected> expected_repaired_assertion(std::string_view input) {
    std::istringstream source{std::string(input)};
    AssertionExpected expected;
    int pieces=0,request=0;
    while(source>>pieces) {
        if(++request>8)return {};
        if(pieces<0||pieces>20) {
            expected.stderr_bytes+="Cajas: peticion "+std::to_string(request)+
                ", piezas="+std::to_string(pieces)+
                ". Se admite un entero de 0 a 20. Revise la cantidad.\n";
            expected.exit_code=1;
        }else expected.stdout_bytes+="Cajas="+std::to_string((pieces+3)/4)+"\n";
    }
    if(!source.eof())return {};
    return expected;
}

inline constexpr std::array<std::string_view,2> repaired_assertion_inputs{{
    "0 1 4 5 20",
    "-2 21 8 9"
}};

inline bool assertion_runtime_trap(std::string_view stdout_bytes,
        std::string_view stderr_bytes) {
    if(!stdout_bytes.empty())return false;
    constexpr std::string_view public_diagnostic=
        "Cajas: peticion 1, piezas=-1. Se admite un entero de 0 a 20. Revise la cantidad.\n";
    // The exact crash code is not portable. Validate diagnostic, expression,
    // original function and matching Wasmtime trap rather than 'exit != 0'.
    return stderr_bytes.starts_with(public_diagnostic)&&
        stderr_bytes.find("Assertion failed: cajas * piezas_por_caja >= piezas (")!=stderr_bytes.npos&&
        stderr_bytes.find("main.cpp: cajas_para: 7)")!=stderr_bytes.npos&&
        stderr_bytes.find("program.wasm")!=stderr_bytes.npos&&
        stderr_bytes.find("program.wasm!__assert_fail")!=stderr_bytes.npos&&
        stderr_bytes.find("program.wasm!cajas_para")!=stderr_bytes.npos&&
        stderr_bytes.find("wasm trap: wasm `unreachable` instruction executed")!=stderr_bytes.npos;
}

inline bool assertion_repaired_matches(std::string_view input,std::string_view stdout_bytes,
        std::string_view stderr_bytes,int exit_code) {
    const auto expected=expected_repaired_assertion(input);
    return expected&&expected->exit_code==exit_code&&
        expected->stdout_bytes==stdout_bytes&&expected->stderr_bytes==stderr_bytes;
}

} // namespace lgaed::curriculum
