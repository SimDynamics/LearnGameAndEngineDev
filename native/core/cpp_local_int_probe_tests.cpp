#include "core/cpp_local_int_probe.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::CppLocalIntProbe;
using lgaed::curriculum::cpp_local_int_override;

void require(bool condition, std::string_view explanation) {
    if (!condition) throw std::runtime_error(std::string(explanation));
}

void equivalent_initializers_do_not_change_original_source() {
    const std::string declarations[]{
        "int llaves = 7;",
        "int llaves{7};",
        "int llaves(7);",
        "int llaves = {7};",
        "int llaves = 3 + 4;",
        "int llaves = static_cast<int>(7);",
        "int llaves = [] { return 7; }();"
    };
    for (const auto& declaration : declarations) {
        const std::string source = "#include <iostream>\nint main() {\n    " +
            declaration + "\n    std::cout << \"Llaves: \" << llaves;\n    return 0;\n}\n";
        const auto original = source;
        const auto rewritten = cpp_local_int_override(source, {"llaves", 13});
        require(rewritten.has_value(), "A valid int initializer could not be probed");
        require(source == original &&
            rewritten->find(declaration + "\n    llaves = 13;\n") != std::string::npos &&
            rewritten->find("std::cout << \"Llaves: \" << llaves;") != std::string::npos,
            "The original source or its independent output expression was changed");
    }
}

void independently_probe_two_variables() {
    const std::string source = R"(#include <iostream>
int main() {
    int cajas{4};
    int sobres = 9;
    std::cout << "Cajas: " << cajas << "\n";
    std::cout << "Sobres: " << sobres;
    return 0;
})";
    const auto cajas = cpp_local_int_override(source, {"cajas", 17});
    const auto sobres = cpp_local_int_override(source, {"sobres", 23});
    require(cajas && sobres &&
        cajas->find("int cajas{4};\n    cajas = 17;\n") != std::string::npos &&
        cajas->find("int sobres = 9;\n") != std::string::npos &&
        sobres->find("int cajas{4};\n") != std::string::npos &&
        sobres->find("int sobres = 9;\n    sobres = 23;\n") != std::string::npos &&
        cajas->find("sobres = 23") == std::string::npos &&
        sobres->find("cajas = 17") == std::string::npos,
        "Probing one integer rewrote the independent companion variable");
}

void separate_assignment_and_copy_before_update() {
    const auto separate = cpp_local_int_override(
        "int main() { int llaves; llaves = 7; std::cout << llaves; }",
        {"llaves", 19});
    require(separate &&
        separate->find("llaves = 7;\n    llaves = 19;\n") != std::string::npos,
        "A complete assignment after declaration was rejected");
    const std::string source = R"(#include <iostream>
int main() {
    int aforo = 5;
    int anterior = aforo;
    aforo += 3;
    std::cout << "Antes: " << anterior << "\nAhora: " << aforo;
})";
    const auto aforo = cpp_local_int_override(source, {"aforo", 10});
    const auto anterior = cpp_local_int_override(source, {"anterior", 20});
    require(aforo && anterior &&
        aforo->find("int aforo = 5;\n    aforo = 10;\n") != std::string::npos &&
        aforo->find("int anterior = aforo;\n") != std::string::npos &&
        anterior->find("int anterior = aforo;\n    anterior = 20;\n") != std::string::npos &&
        anterior->find("aforo += 3;") != std::string::npos,
        "The copy and later state cannot be probed independently");
}

void comments_literals_and_similar_names_are_inert() {
    const std::string source =
        "#include <iostream>\n"
        "// int llaves{99};\n"
        "int llaves_ajenas{50};\n"
        "int main() {\n"
        "  const char* quote = R\"(int llaves{123};)\";\n"
        "  int llaves{7}; // int llaves{42};\n"
        "  std::cout << \"Llaves: \" << llaves;\n"
        "}\n";
    const auto changed = cpp_local_int_override(source, {"llaves", 99});
    require(changed &&
        changed->find("int llaves{7};\n    llaves = 99;\n") != std::string::npos &&
        changed->find("int llaves_ajenas{50};") != std::string::npos &&
        changed->find("R\"(int llaves{123};)\"") != std::string::npos,
        "An unrelated identifier, raw string or comment was changed");
}

void unsafe_ambiguous_and_other_scope_are_rejected() {
    const std::string prefix = "#include <iostream>\nint main() { ";
    const std::string suffix = " std::cout << llaves; }";
    const char* invalid[]{
        "const int llaves=7;",
        "unsigned int llaves=7;",
        "long int llaves=7;",
        "auto llaves=7;",
        "int *llaves=nullptr;",
        "int llaves[1]{7};",
        "int llaves{7}, extra{8};",
        "int llaves=7; int llaves=8;",
        "if(true) { int llaves=7; }",
        "for(int llaves=0; llaves<1; ++llaves) {}",
        "int llaves; std::cout << llaves; llaves=7;",
        "int llaves; if(true) llaves=7;",
        "int llaves;",
        "#define llaves 7\nint otra{1};"
    };
    for (const auto* statement : invalid)
        require(!cpp_local_int_override(prefix + statement + suffix, {"llaves", 15}),
            "Unsafe syntax or a differently typed/non-local int was altered");
    require(!cpp_local_int_override(prefix + "int llaves{7};" + suffix,
        {"llaves; std::cout << \"forged\"", 15}),
        "A variable name escaped its identifier boundary");
    require(!cpp_local_int_override(prefix + "int llaves{7};" + suffix,
        {"int", 15}), "A keyword masqueraded as a variable");
    require(!cpp_local_int_override(
        "int llaves{7}; int main() { std::cout << llaves; }",
        {"llaves", 15}), "A global int was mistaken for a main-local int");
    require(!cpp_local_int_override(
        "int main() { int llaves=7; } int main() { int llaves=8; }",
        {"llaves", 15}), "Ambiguous main functions were accepted");
}

void int32_limits_and_old_text_are_preserved() {
    const std::string source = "int main() { int llaves{}; std::cout << llaves; }";
    const auto low = cpp_local_int_override(source, {"llaves", INT32_MIN});
    const auto high = cpp_local_int_override(source, {"llaves", INT32_MAX});
    require(low && high &&
        low->find("llaves = -2147483648;") != std::string::npos &&
        high->find("llaves = 2147483647;") != std::string::npos &&
        source == "int main() { int llaves{}; std::cout << llaves; }",
        "An integer limit was mangled or an original learner text was edited");
}

void expected_outputs_are_independent_and_reversible() {
    using lgaed::curriculum::CppLocalIntCheck;
    using lgaed::curriculum::cpp_local_int_check_valid;
    using lgaed::curriculum::cpp_local_int_expected_output;
    CppLocalIntCheck check{{{"llaves", 13, {"Llaves: 13", "Llaves: 13\n"}}}};
    require(cpp_local_int_check_valid(check) &&
        cpp_local_int_expected_output(check.probes[0], "Llaves: 13") &&
        cpp_local_int_expected_output(check.probes[0], "Llaves: 13\r\n") &&
        !cpp_local_int_expected_output(check.probes[0], "Llaves: 7") &&
        !cpp_local_int_expected_output(check.probes[0], "Llaves: 13 extra"),
        "Expected output alternatives are not checked exactly");
    check.probes.push_back({"llaves", 13, {"Llaves: 13"}});
    require(!cpp_local_int_check_valid(check), "A duplicate mutation was admitted");
    check.probes.pop_back();
    check.probes[0].expected_stdout = {"Llaves: 13", "Llaves: 13"};
    require(!cpp_local_int_check_valid(check), "Duplicate output spelling was admitted");
    check.probes[0].expected_stdout = {};
    require(!cpp_local_int_check_valid(check), "Missing independent output was admitted");
}

} // namespace

int main() {
    try {
        equivalent_initializers_do_not_change_original_source();
        independently_probe_two_variables();
        separate_assignment_and_copy_before_update();
        comments_literals_and_similar_names_are_inert();
        unsafe_ambiguous_and_other_scope_are_rejected();
        int32_limits_and_old_text_are_preserved();
        expected_outputs_are_independent_and_reversible();
        std::cout << "PASS local int probes: equivalent initialization, independent "
                     "variables, copy state, safe token positions.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL local int probes: " << error.what() << '\n';
        return 1;
    }
}
