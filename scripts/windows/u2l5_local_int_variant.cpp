// Isolated Windows QA adapter. Reuse the production source mutation logic;
// never edit a student's draft or claim that this is the application's runner.
#include "core/cpp_local_int_probe.hpp"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: variant source.cpp variable replacement after_update(0|1) output.cpp\n";
        return 2;
    }
    const char* begin = argv[3];
    const char* end = begin + std::strlen(begin);
    std::int32_t replacement = 0;
    const auto parsed = std::from_chars(begin, end, replacement, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        (std::string_view(argv[4]) != "0" && std::string_view(argv[4]) != "1")) {
        std::cerr << "Invalid independent local-int probe argument\n";
        return 2;
    }
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
    if (!input) {
        std::cerr << "Could not open authored source\n";
        return 2;
    }
    const std::string source(std::istreambuf_iterator<char>{input}, {});
    lgaed::curriculum::CppLocalIntProbe probe;
    probe.variable = argv[2];
    probe.replacement = replacement;
    probe.after_update = std::string_view(argv[4]) == "1";
    const auto rewritten = lgaed::curriculum::cpp_local_int_override(source, probe);
    if (!rewritten) {
        std::cerr << "UNSAFE_PROBE_SOURCE\n";
        return 3;
    }
    std::ofstream output(std::filesystem::path(argv[5]), std::ios::binary | std::ios::trunc);
    if (!output || !output.write(rewritten->data(), static_cast<std::streamsize>(rewritten->size()))) {
        std::cerr << "Could not write isolated variant\n";
        return 2;
    }
    return 0;
}
