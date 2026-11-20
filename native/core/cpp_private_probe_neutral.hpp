#pragma once

#include "core/code_format_compat.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

// Common lexical prerequisite for privately compiled ownership witnesses.
// The caller independently validates the authorized source region and later
// compiles/executes the semantic driver: this cannot grant credit by itself.
// Inert comments, character literals and raw strings are never executable
// tokens. A learner's C++ method must not terminate or impersonate the driver.
inline bool cpp_private_probe_body_neutral(
        const std::vector<std::string>& tokens,
        std::string_view private_namespace) {
    if(private_namespace.empty() ||
       code_format::cpp_position_sensitive_tokens(tokens) ||
       code_format::cpp_process_termination_tokens(tokens))return false;

    constexpr std::string_view forbidden[]{
        "Watch", "__FILE__", "__FILEW__", "__BASE_FILE__", "__COUNTER__",
        "__INCLUDE_LEVEL__", "__DATE__", "__TIME__", "__TIMESTAMP__",
        "__PRETTY_FUNCTION__", "__FUNCSIG__", "__FUNCDNAME__",
        "__builtin_FILE", "__builtin_FUNCTION",
        "__builtin_source_location", "source_location",
        "longjmp", "_longjmp", "siglongjmp", "setjmp", "sigsetjmp",
        "asm", "__asm", "__asm__", "__pragma", "_Pragma",
        "malloc", "calloc", "realloc", "free",
        "_aligned_malloc", "_aligned_free",
        "fputs", "puts", "printf", "fprintf", "fwrite", "putchar",
        "freopen", "freopen_s", "fclose", "stdout", "stderr",
        "cout", "cerr", "clog", "wcout", "wcerr", "wclog",
        "dup2", "_dup2", "SetStdHandle", "WriteFile", "WriteConsoleA",
        "WriteConsoleW", "CreateProcessA", "CreateProcessW", "WinExec",
        "system", "popen", "_popen",
    };
    for(const auto& token:tokens) {
        if(token.starts_with("directive:")||token=="op:#"||
           token=="op:##"||token=="op:%:"||token=="op:%:%:")
            return false;
        if(!token.starts_with("id:"))continue;
        const auto id=std::string_view(token).substr(3);
        if(id==private_namespace ||
           std::ranges::any_of(forbidden,
               [&](std::string_view word){return word==id;}))
            return false;
    }
    return true;
}
} // namespace lgaed::curriculum
