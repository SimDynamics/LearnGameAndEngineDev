#pragma once

#include <array>
#include <cstdint>
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

// Windows CRT may translate every LF to CRLF in redirected console streams.
// This is only used by the three reviewed U3L9 assertion exercises.
inline bool assertion_native_lines(std::string_view expected,std::string_view actual) {
    if(expected==actual)return true;
    std::string windows;
    windows.reserve(expected.size()*2);
    for(const char value:expected) {
        if(value=='\n')windows+='\r';
        windows+=value;
    }
    return windows==actual;
}

// The exact native counterpart of the original failed assertion: the public
// rejection for -1, followed by the CRT message from line 7 in main.cpp.
// An arbitrary nonzero exit, stdout-only imitation or unrelated crash never
// constitutes the private assertion proof.
inline bool assertion_native_abort(std::string_view stdout_bytes,
        std::string_view stderr_bytes,std::uint32_t raw_exit_code) {
    // MSVC CRT can abort through fast fail (0xC0000409), fatal app exit
    // (0x40000015) or a classic CRT exit(3), depending on the runtime.
    if(raw_exit_code!=0xC0000409u&&raw_exit_code!=0x40000015u&&
       raw_exit_code!=3u)return false;
    if(!stdout_bytes.empty()||stderr_bytes.size()>4096)return false;
    std::string normalized;
    normalized.reserve(stderr_bytes.size());
    for(std::size_t i=0;i<stderr_bytes.size();++i) {
        if(stderr_bytes[i]=='\r'&&i+1<stderr_bytes.size()&&
           stderr_bytes[i+1]=='\n')continue;
        normalized+=stderr_bytes[i];
    }
    constexpr std::string_view prefix=
        "Cajas: peticion 1, piezas=-1. Se admite un entero de 0 a 20. "
        "Revise la cantidad.\n"
        "Assertion failed: cajas * piezas_por_caja >= piezas, file ";
    constexpr std::string_view suffix=", line 7\n";
    std::string_view evidence=normalized;
    if(!evidence.starts_with(prefix)||!evidence.ends_with(suffix)||
       evidence.size()<=prefix.size()+suffix.size())return false;
    const auto file=evidence.substr(prefix.size(),
        evidence.size()-prefix.size()-suffix.size());
    if(!file.ends_with("main.cpp")||
       file.find_first_of("\r\n\t")!=file.npos)return false;
    if(file.size()>8&&file[file.size()-9]!='/'&&file[file.size()-9]!='\\')
        return false;
    return true;
}

inline bool assertion_repaired_matches(std::string_view input,std::string_view stdout_bytes,
        std::string_view stderr_bytes,int exit_code) {
    const auto expected=expected_repaired_assertion(input);
    return expected&&expected->exit_code==exit_code&&
        assertion_native_lines(expected->stdout_bytes,stdout_bytes)&&
        assertion_native_lines(expected->stderr_bytes,stderr_bytes);
}

} // namespace lgaed::curriculum
