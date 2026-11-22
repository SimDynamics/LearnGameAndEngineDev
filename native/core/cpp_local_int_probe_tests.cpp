#include "core/cpp_local_int_probe.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::CppLocalIntProbe;
using lgaed::curriculum::CppLocalValueType;
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
        "int llaves = [] { return 7; }();",
        "signed int llaves = 7;",
        "int signed llaves{7};",
        "signed llaves(7);"
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

void noexcept_main_preserves_local_dependency_checks() {
    // All of these are valid C++23 main declarations. A semantic probe must
    // not mistake their noexcept clauses for an absent function body.
    const char* suffixes[]{
        "noexcept",
        "noexcept(true)",
        "noexcept(false)",
        "noexcept(noexcept(7))"
    };
    for (const auto* suffix : suffixes) {
        const auto source = std::string("#include <iostream>\nint main() ") +
            suffix + " {\n    int llaves{7};\n"
            "    std::cout << \"Llaves: \" << llaves;\n}\n";
        const auto variation = cpp_local_int_override(source, {"llaves", 13});
        require(variation &&
            variation->find("int llaves{7};\n    llaves = 13;\n") != std::string::npos &&
            variation->find(std::string("int main() ") + suffix + " {") != std::string::npos &&
            variation->find("std::cout << \"Llaves: \" << llaves;") != std::string::npos &&
            source.find("llaves = 13;") == std::string::npos,
            "A valid noexcept main lost its local-int dependency probe");
    }
    const auto updated = cpp_local_int_override(
        "int main() noexcept(noexcept(7)) {int aforo{5}; aforo+=3;"
        "int anterior=aforo; std::cout<<anterior<<aforo;}",
        {"aforo", 4, {}, true});
    require(updated &&
        updated->find("aforo+=3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n") != std::string::npos &&
        updated->find("int anterior=aforo;") > updated->find("aforo += 4;"),
        "A noexcept main lost the private probe after its first state update");
    require(!cpp_local_int_override(
        "int helper() noexcept {int llaves{7}; return llaves;}"
        "int main() noexcept {return helper();}",
        {"llaves", 13}),
        "The noexcept clause made a local from another function mutable");
    require(!cpp_local_int_override(
        "int main() noexcept {int llaves{7};}"
        "int main() {int llaves{8};}",
        {"llaves", 13}),
        "An ambiguous duplicate main passed with a noexcept clause");
    require(!cpp_local_int_override(
        "int main() noexcept( {int llaves{7}; std::cout<<llaves;}",
        {"llaves", 13}),
        "An incomplete noexcept clause was mistaken for a main function body");
}

void trailing_return_main_supports_equivalent_int_entrypoints() {
    const char* suffixes[]{"", " noexcept", " noexcept(true)",
                           " noexcept(noexcept(7))"};
    for(const auto* suffix:suffixes) {
        const auto source=std::string("#include <iostream>\n"
            "auto main()")+suffix+" -> int {\n"
            "int llaves{7};std::cout<<\"Llaves: \"<<llaves;\n}\n";
        const auto varied=cpp_local_int_override(source,{"llaves",13});
        require(varied&&
            varied->find("int llaves{7};\n    llaves = 13;\n")!=std::string::npos&&
            varied->find("std::cout<<\"Llaves: \"<<llaves;")!=std::string::npos,
            "Valid auto main()->int was not recognized as the unique owner");
    }
    const auto update=cpp_local_int_override(
        "auto main() noexcept -> int {int aforo{5};aforo+=3;"
        "std::cout<<aforo;}",{"aforo",4,{},true});
    require(update&&
        update->find("aforo+=3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=std::string::npos,
        "Trailing return main lost the independent after-update probe");
    for(const auto* invalid:{
        "auto main()->long {int llaves{7};std::cout<<llaves;}",
        "auto main()->void {int llaves{7};std::cout<<llaves;}",
        "int main()->int {int llaves{7};std::cout<<llaves;}",
        "auto helper()->int {int llaves{7};std::cout<<llaves;}",
        "auto main()->int {int llaves{7};}"
        "int main() {int llaves{8};}"}) {
        require(!cpp_local_int_override(invalid,{"llaves",13}),
            "An invalid, helper-only or ambiguous trailing return main passed");
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

void grouped_integer_declarations_keep_independent_data() {
    const std::string declarations[]{
        "int cajas{4}, sobres{9};",
        "int cajas=4, sobres=9;",
        "int cajas(4), sobres(9);",
        "int cajas=std::max(2,4), sobres=(9);",
        "signed cajas{4}, sobres=9;",
        "signed int cajas{4}, sobres{9};",
        "int signed cajas{4}, sobres{9};",
        "int cajas=4, sobres=9, ultimo=0;"
    };
    for(const auto& declaration:declarations) {
        const auto source="#include <iostream>\n#include <algorithm>\n"
            "int main(){\n    "+declaration+
            "\n    std::cout<<\"Cajas: \"<<cajas<<\"\\nSobres: \"<<sobres;\n}\n";
        const auto boxes=cpp_local_int_override(source,{"cajas",13});
        const auto envelopes=cpp_local_int_override(source,{"sobres",17});
        require(boxes&&envelopes&&
            boxes->find(declaration+"\n    cajas = 13;\n")!=std::string::npos&&
            envelopes->find(declaration+"\n    sobres = 17;\n")!=std::string::npos&&
            boxes->find("sobres = 17;")==std::string::npos&&
            envelopes->find("cajas = 13;")==std::string::npos&&
            source.find("cajas = 13;")==std::string::npos,
            "A grouped int was mutated before the last declarator or changed its companion");
    }
    const auto delayed=std::string(
        "int main(){int cajas, sobres; cajas=4; sobres=9; "
        "std::cout<<cajas<<sobres;}");
    const auto boxes=cpp_local_int_override(delayed,{"cajas",13});
    const auto envelopes=cpp_local_int_override(delayed,{"sobres",17});
    require(boxes&&envelopes&&
        boxes->find("cajas=4;\n    cajas = 13;\n")!=std::string::npos&&
        envelopes->find("sobres=9;\n    sobres = 17;\n")!=std::string::npos,
        "Two default-initialized grouped ints lost their separate assignment locations");
    for(const auto& invalid:{
        "int cajas=4, sobres=cajas+5;",
        "int cajas=4, sobres=9, derivado=cajas+1;",
        "int cajas=4, *ptr=nullptr;",
        "int cajas=4, &referencia=cajas;",
        "int cajas=4, otros[2]{1,2};",
        "int cajas=4, cajas=5;"
    }) {
        const auto src=std::string("int main(){")+invalid+
            " std::cout<<cajas;}";
        require(!cpp_local_int_override(src,{"cajas",13}),
            "A dependent, pointer, array or duplicate sibling was credited as independent");
    }
    const std::string dependent=
        "int main(){int cajas=4, sobres=cajas+5; std::cout<<sobres;}";
    const auto changed_envelopes=cpp_local_int_override(dependent,{"sobres",17});
    require(!cpp_local_int_override(dependent,{"cajas",13})&&
            changed_envelopes&&
            changed_envelopes->find(
                "int cajas=4, sobres=cajas+5;\n    sobres = 17;\n")!=std::string::npos&&
            changed_envelopes->find("std::cout<<sobres;")!=std::string::npos,
        "A later dependent int must be independently variable even when its source is not");
}

void separate_assignment_and_copy_before_update() {
    const auto separate = cpp_local_int_override(
        "int main() { int llaves; llaves = 7; std::cout << llaves; }",
        {"llaves", 19});
    require(separate &&
        separate->find("llaves = 7;\n    llaves = 19;\n") != std::string::npos,
        "A complete assignment after declaration was rejected");
    const auto signed_separate = cpp_local_int_override(
        "int main() { signed llaves; llaves = 7; std::cout << llaves; }",
        {"llaves", 19});
    require(signed_separate &&
        signed_separate->find("llaves = 7;\n    llaves = 19;\n") != std::string::npos,
        "A signed int initialized by separate assignment was rejected");
    const auto computed=cpp_local_int_override(
        "int main(){int llaves; llaves = 3 + 4; std::cout<<llaves;}",
        {"llaves",19});
    require(computed&&
        computed->find("llaves = 3 + 4;\n    llaves = 19;\n")!=std::string::npos,
        "A first assignment computed only from initialized values was rejected");
    const std::string homonyms=R"CPP(#include <iostream>
namespace ajustes { inline int llaves = 7; }
struct Datos { int llaves{7}; };
int main() {
    Datos datos;
    Datos* puntero = &datos;
    int llaves;
    llaves = datos.llaves;
    std::cout << llaves;
}
)CPP";
    const auto field=cpp_local_int_override(homonyms,{"llaves",19});
    require(field&&
        field->find("llaves = datos.llaves;\n    llaves = 19;\n")!=std::string::npos&&
        field->find("struct Datos { int llaves{7}; };")!=std::string::npos,
        "A field with the same name was treated as a self-read of the uninitialized local");
    const auto pointer=cpp_local_int_override(
        "#include <iostream>\nstruct Datos{int llaves{7};};\n"
        "int main(){Datos d;Datos* p=&d;int llaves;llaves=p->llaves;std::cout<<llaves;}",
        {"llaves",19});
    require(pointer&&
        pointer->find("llaves=p->llaves;\n    llaves = 19;\n")!=std::string::npos,
        "An arrow member access to another initialized object was rejected");
    const auto qualified=cpp_local_int_override(
        "#include <iostream>\nnamespace ajustes {inline int llaves=7;}\n"
        "int main(){int llaves;llaves=ajustes::llaves;std::cout<<llaves;}",
        {"llaves",19});
    require(qualified&&
        qualified->find("llaves=ajustes::llaves;\n    llaves = 19;\n")!=std::string::npos,
        "A namespace-qualified value was confused with the local int");
    const auto earlier_member=cpp_local_int_override(
        "#include <iostream>\nstruct Datos{int llaves{7};};\n"
        "int main(){Datos datos;int llaves;int anterior=datos.llaves;"
        "llaves=anterior;std::cout<<llaves;}",
        {"llaves",19});
    require(earlier_member&&
        earlier_member->find("llaves=anterior;\n    llaves = 19;\n")!=std::string::npos,
        "Reading a separate member before first assignment falsely rejected the local");
    const auto earlier_grouped_member=cpp_local_int_override(
        "#include <iostream>\nstruct Datos{int llaves{7};};\n"
        "int main(){Datos datos;int copia=datos.llaves, llaves=7;std::cout<<llaves;}",
        {"llaves",19});
    require(earlier_grouped_member&&
        earlier_grouped_member->find(
            "int copia=datos.llaves, llaves=7;\n    llaves = 19;\n")!=std::string::npos,
        "A grouped member name was confused with the later local declaration");
    require(!cpp_local_int_override(
        "#include <iostream>\nstruct Datos{int llaves{7};};\n"
        "int main(){Datos datos;int llaves;llaves=datos.llaves+llaves;"
        "std::cout<<llaves;}",
        {"llaves",19}),
        "A member reference hid a real uninitialized self-read");
    for(const auto* unsafe:{
        "llaves = llaves + 7;",
        "llaves = (7 + llaves);",
        "llaves = std::max(llaves, 7);"
    }) {
        const std::string invalid=
            "#include <iostream>\n#include <algorithm>\n"
            "int main(){int llaves; "+std::string(unsafe)+
            " std::cout<<llaves;}";
        require(!cpp_local_int_override(invalid,{"llaves",19}),
            "An uninitialized int read in its own first assignment received a probe");
    }
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
        "unsigned llaves=7;",
        "long int llaves=7;",
        "long signed int llaves=7;",
        "signed int *llaves=nullptr;",
        "signed llaves{7}, *extra=nullptr;",
        "auto llaves=7;",
        "int *llaves=nullptr;",
        "int llaves[1]{7};",
        "int llaves{7}, extra[2]{8};",
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

void local_probe_names_follow_real_cpp_token_boundaries() {
    const std::string source=
        "int main(){int llaves{7};std::cout<<llaves;}";
    for(const auto* bad:{
            "9llaves", "llaves-extra", "llaves.codigo",
            "llaves codigo", "true", "false", "nullptr", "__oculta",
            "llaves\notra"}) {
        lgaed::curriculum::CppLocalIntCheck spec{
            {{bad,13,{"Llaves: 13"}}}};
        require(!lgaed::curriculum::cpp_local_int_check_valid(spec)&&
                !cpp_local_int_override(source,{bad,13}),
            "An invalid C++ identifier was accepted as a local-int owner");
    }
    const std::string unicode=
        "int main(){int número{7};std::cout<<número;}";
    const auto variation=cpp_local_int_override(unicode,{"número",13});
    lgaed::curriculum::CppLocalIntCheck valid{
        {{"número",13,{"Llaves: 13"}}}};
    require(variation&&
            variation->find("número = 13;")!=std::string::npos&&
            lgaed::curriculum::cpp_local_int_check_valid(valid),
        "The source tokenizer's non-ASCII identifiers were accidentally disabled");
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

void changed_increase_is_observed_at_the_actual_update() {
    const char* equivalents[]{
        "aforo += 3;",
        "aforo = aforo + 3;",
        "aforo = 3 + aforo;",
        "aforo -= -3;",
        "++aforo; ++aforo; ++aforo;",
        "aforo++; aforo++; aforo++;"
    };
    for(const auto* increase:equivalents) {
        const std::string after="#include <iostream>\nint main(){\n"
            "int aforo{5};\n"+std::string(increase)+
            "\nint anterior=aforo;\nstd::cout<<anterior<<aforo;\n}";
        const auto changed=cpp_local_int_override(after,
            {"aforo",4,{},true});
        require(changed&&
            changed->find("aforo += 4;\n")!=std::string::npos&&
            changed->find("aforo += 4;\n")<changed->find("int anterior=aforo;")&&
            after.find("aforo += 4;\n")==std::string::npos,
            "After-update probe did not preserve the original increase or copy order");
        const std::string before="int main(){int aforo(5); int anterior=aforo; "
            +std::string(increase)+" std::cout<<anterior<<aforo;}";
        const auto other=cpp_local_int_override(before,
            {"aforo",4,{},true});
        require(other&&
            other->find("int anterior=aforo;")<
                other->find("aforo += 4;\n")&&
            other->find("aforo += 4;\n")<
                other->find("std::cout<<anterior<<aforo;"),
            "After-update mutation incorrectly changed the already captured copy");
    }
    // A fixed reassignment can reproduce 8 and become 12 after a hidden
    // delta, even though it ignored the original value of aforo. Likewise,
    // a different object's field with the same name is not a read of aforo.
    for(const auto* replacement:{
            "aforo = 8;",
            "aforo = aforo;",
            "aforo = (aforo);",
            "aforo = datos.aforo;",
            "aforo = ajustes::aforo;",
            "aforo = ::aforo;"}) {
        const auto sample=std::string(
            "#include <iostream>\n"
            "int aforo=8; namespace ajustes{int aforo=8;}\n"
            "struct Datos{int aforo{8};};\n"
            "int main(){Datos datos;int aforo=5; ")
            +replacement+" std::cout<<aforo;}";
        require(!cpp_local_int_override(sample,{"aforo",4,{},true}),
            "A constant, qualified value or identity assignment was credited as an increase");
    }
    const auto dependent_member=cpp_local_int_override(
        "#include <iostream>\nstruct Datos{int aforo{3};};\n"
        "int main(){Datos datos;int aforo{5};aforo=aforo+datos.aforo;"
        "std::cout<<aforo;}",
        {"aforo",4,{},true});
    require(dependent_member&&dependent_member->find(
            "aforo=aforo+datos.aforo;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=std::string::npos,
        "A genuine update using the local and a differently owned field was rejected");
    for(const auto& original:{
            "int main(){int aforo{5}; std::cout<<aforo;}",
            "int main(){int aforo=5; if(true){aforo+=3;} std::cout<<aforo;}",
            "int main(){int aforo=5; int copia=++aforo; std::cout<<aforo;}",
            "int main(){int aforo=5; aforo+=3, ++aforo; std::cout<<aforo;}"
        })
        require(!cpp_local_int_override(original,{"aforo",4,{},true}),
            "Absent, nested or ambiguous first increase was instrumented");
    lgaed::curriculum::CppLocalIntCheck distinct{
        {{"aforo",4,{"Ahora: 9"},false},
         {"aforo",4,{"Ahora: 13"},true}}};
    require(lgaed::curriculum::cpp_local_int_check_valid(distinct),
        "Changing the initial int and its later increase must remain independent");
    distinct.probes.push_back({"aforo",4,{"Ahora: 13"},true});
    require(!lgaed::curriculum::cpp_local_int_check_valid(distinct),
        "A repeated after-update mutation was treated as a distinct probe");
}

void neutral_updates_cannot_fake_u2l3_increase() {
    const std::string prefix=
        "#include <iostream>\nint main(){int aforo{5};"
        "int anterior=aforo;";
    const std::string output=
        "std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo+3;}";
    for(const auto* unchanged:{
        "aforo=aforo+0;",
        "aforo=(aforo+0);",
        "aforo=aforo+(0);",
        "aforo=0+aforo;",
        "aforo=aforo-0;",
        "aforo=aforo*1;",
        "aforo=1*aforo;",
        "aforo=aforo/1;",
        "aforo+=0;",
        "aforo-=(0x0u);",
        "aforo=aforo+00;",
        "aforo=(aforo);"
    }) {
        const auto program=prefix+unchanged+output;
        require(!cpp_local_int_override(program,{"aforo",4,{},true}),
            "A neutral state operation was credited as the original increase");
    }
    // A harmless no-op does not disqualify a subsequent genuine update.
    const auto later=cpp_local_int_override(
        prefix+"aforo=aforo+0;aforo+=3;std::cout<<aforo;}",
        {"aforo",4,{},true});
    require(later&&
        later->find("aforo+=3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=std::string::npos&&
        later->find("aforo=aforo+0;")<
            later->find("aforo += 4;"),
        "A harmless earlier neutral expression obscured the later real increase");
    const auto combined=cpp_local_int_override(
        prefix+"aforo=aforo+0+3;std::cout<<aforo;}",
        {"aforo",4,{},true});
    require(combined&&
        combined->find("aforo=aforo+0+3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=std::string::npos,
        "A genuine update with a redundant zero was rejected");
}

void composed_neutral_updates_cannot_fake_u2l3_increase() {
    // The unchanged learner program can print the expected "Ahora: 8"
    // using aforo+3, without ever increasing the stored aforo. In that
    // situation a hidden +4 after a compound no-op would also produce the
    // expected variant and falsely certify an actual state update.
    const std::string start=
        "#include <iostream>\nint main(){int aforo{5};"
        "int anterior=aforo;";
    const std::string output=
        "std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo+3;}";
    for(const auto* neutral:{
        "aforo=aforo+0+0;",
        "aforo=(aforo+0)+0;",
        "aforo=0+(aforo+0);",
        "aforo=(aforo*1)/1;",
        "aforo=aforo+(0+0);",
        "aforo=aforo+(0*1);",
        "aforo=0*aforo+aforo;",
        "aforo=aforo*(1+0);",
        "aforo+=(0+0);",
        "aforo-=(1-1);",
        "aforo+=(+0);",
        "aforo=aforo+(2-2);",
        "aforo+=(2-2);",
        "aforo=aforo+((17-17));",
        "aforo+=(2147483647-2147483647);",
        "aforo=aforo+2-2;",
        "aforo=aforo-2+2;",
        "aforo=aforo+3-2-1;",
        "aforo=(aforo+7)-7;",
        "aforo+=(2+3-5);",
        "aforo-=(4-3-1);",
        "aforo=aforo+(+6)-6;",
        "aforo=aforo*(2-1);",
        "aforo=aforo/(9/3-2);",
        "aforo+=(4/2-2);",
        "aforo-=(6%3);",
        "aforo=aforo+((3*3)-9);"
    }) {
        require(!cpp_local_int_override(start+neutral+output,
            {"aforo",4,{},true}),
            "A composed neutral operation falsely counted as increasing aforo");
    }
    // Skipping the harmless first statement must still find a genuine
    // later increment, regardless of whitespace or grouping.
    const auto later=cpp_local_int_override(
        start+"aforo=(aforo+0)+0;aforo=aforo+(3+0);"+output,
        {"aforo",4,{},true});
    require(later&&
        later->find("aforo=aforo+(3+0);\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
            std::string::npos,
        "A neutral prefix prevented the later genuine increase from being probed");
    const auto genuine=cpp_local_int_override(
        start+"aforo=aforo+0+3;"+output,
        {"aforo",4,{},true});
    require(genuine&&
        genuine->find("aforo=aforo+0+3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
            std::string::npos,
        "An expression with a real nonzero term was incorrectly ignored");
    const auto nonzero=cpp_local_int_override(
        start+"aforo=aforo+(2-3);"+output,
        {"aforo",4,{},true});
    require(nonzero&&
        nonzero->find("aforo=aforo+(2-3);\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
            std::string::npos,
        "A subtraction of different integer literals was mistaken for no-op");
    for(const auto* genuine_additive:{
        "aforo=aforo+2-1;",
        "aforo=aforo+3-2-2;",
        "aforo+=(2+3-4);",
        "aforo=aforo*(3-1);",
        "aforo+=(9/3-2);",
        "aforo=aforo+(7%3);"
    }) {
        const auto source=start+std::string(genuine_additive)+output;
        const auto variant=cpp_local_int_override(source,{"aforo",4,{},true});
        require(variant&&
            variant->find(std::string(genuine_additive)+"\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
                std::string::npos,
            "A nonzero additive change was confused with exact cancellation");
    }
}

void opaque_noop_requires_runtime_state_change() {
    const std::string start=
        "#include <iostream>\nint main(){int aforo{5};int anterior=aforo;";
    const std::string output=
        "std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo+3;}";
    constexpr std::string_view marker="lgaed_local_int_before_6cb8e71d";
    for(const auto* statement:{
        "aforo=aforo^0;", "aforo=aforo|0;", "aforo=aforo<<0;",
        "aforo=aforo&(-1);"
    }) {
        const auto altered=cpp_local_int_override(start+statement+output,
            {"aforo",4,{},true});
        require(altered&&altered->find("const int "+std::string(marker)+
            " = aforo;")!=std::string::npos&&
            altered->find(std::string(statement)+"\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
                std::string::npos&&
            altered->find(" - (4LL) <= static_cast<long long>("+
                std::string(marker))!=std::string::npos,
            "Opaque unchanged state lost its runtime guard");
    }
    const auto genuine=cpp_local_int_override(
        start+"aforo=(aforo|0)+3;"+output,{"aforo",4,{},true});
    require(genuine&&genuine->find("aforo=(aforo|0)+3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
        std::string::npos,"A genuine arithmetic increase was rejected");
    require(!cpp_local_int_override(
        start+"int lgaed_local_int_before_6cb8e71d=9;aforo+=3;"+output,
        {"aforo",4,{},true}),
        "A learner declaration shadowed a private snapshot");
}


void private_recompile_cannot_consume_build_markers_or_exit_early() {
    const std::string beginning=
        "#include <iostream>\n#include <cstdlib>\n#include <source_location>\n"
        "int main(){int llaves{7};llaves+=3;\n";
    const std::string ending="std::cout<<llaves;return 0;}\n";
    // Both initial replacement and after-update deltas must use the same
    // protected source analysis, not trust a location-dependent report.
    for(const auto* hostile:{
        "__LINE__;", "__FILE__;", "__COUNTER__;", "__BASE_FILE__;",
        "__INCLUDE_LEVEL__;", "__DATE__;", "__TIME__;", "__TIMESTAMP__;",
        "__func__;", "__PRETTY_FUNCTION__;", "__FUNCSIG__;",
        "__builtin_LINE();", "__builtin_FILE();",
        "std::source_location::current().line();",
        "std::exit(0);", "std::quick_exit(0);", "std::_Exit(0);",
        "ExitProcess(0);", "TerminateProcess(0,0);"
    }) {
        const auto candidate=beginning+hostile+ending;
        require(!cpp_local_int_override(candidate,{"llaves",13,{},false})&&
                !cpp_local_int_override(candidate,{"llaves",4,{},true}),
            "A build-derived observation or early exit bypassed the private int gate");
    }
    const std::string harmless=beginning+
        "// __LINE__, __FILE__, std::exit(0), source_location\n"
        "const char* plain=\"__COUNTER__ __DATE__ quick_exit\";\n"
        "const char* raw=R\"help(__FILE__ __builtin_LINE() std::exit(0))help\";\n"
        "(void)plain;(void)raw;\n"+ending;
    const auto original=harmless;
    const auto initial=cpp_local_int_override(harmless,{"llaves",13,{},false});
    const auto after=cpp_local_int_override(harmless,{"llaves",4,{},true});
    require(initial&&after&&harmless==original&&
            initial->find("__COUNTER__ __DATE__ quick_exit")!=std::string::npos&&
            after->find("R\"help(__FILE__ __builtin_LINE() std::exit(0))help\"")!=
                std::string::npos&&
            after->find("llaves += 4;")!=std::string::npos,
        "Inert comments or literals were confused with an active build marker");
}

void private_marker_in_comments_and_strings_is_not_an_identifier() {
    const std::string identifier="lgaed_local_int_before_6cb8e71d";
    const std::string source=
        "#include <iostream>\n"
        "int main(){int aforo{5};int anterior=aforo;\n"
        "// lgaed_local_int_before_6cb8e71d no es una variable\n"
        "const char* nota=\"lgaed_local_int_before_6cb8e71d\";\n"
        "const char* crudo=R\"note(lgaed_local_int_before_6cb8e71d)note\";\n"
        "(void)nota;(void)crudo;\n"
        "aforo+=3;\n"
        "std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo;}\n";
    const auto original=source;
    const auto variant=cpp_local_int_override(source,{"aforo",4,{},true});
    require(variant&&source==original&&
        variant->find("// "+identifier+" no es una variable")!=std::string::npos&&
        variant->find("const char* nota=\""+identifier+"\";")!=std::string::npos&&
        variant->find("R\"note("+identifier+")note\"")!=std::string::npos&&
        variant->find("const int "+identifier+" = aforo;")!=std::string::npos,
        "A harmless comment or literal mentioning the probe caused a false collision");
    const auto actual_name=source.substr(0,source.find("aforo+=3;"))+
        "int "+identifier+"=1;\n"+source.substr(source.find("aforo+=3;"));
    require(!cpp_local_int_override(actual_name,{"aforo",4,{},true}),
        "A real local identifier collision was mistaken for inert text");
}

void exact_authored_increase_cannot_be_hidden_in_stdout() {
    using lgaed::curriculum::CppLocalIntCheck;
    using lgaed::curriculum::cpp_local_int_check_valid;
    const CppLocalIntProbe exact{"aforo",4,{"Antes: 5\nAhora: 12"},true,3};
    require(cpp_local_int_check_valid({{exact}}),
        "An independently authored positive int increase was rejected");
    const auto valid = cpp_local_int_override(
        "#include <iostream>\nint main(){int aforo{5};int anterior=aforo;"
        "aforo+=3;std::cout<<anterior<<aforo;}",exact);
    require(valid && valid->find(
        " - static_cast<long long>(lgaed_local_int_before_6cb8e71d) "
        "!= 3LL) return 1;")!=std::string::npos,
        "The exact original increase was not verified before the private delta");
    const auto equivalent = cpp_local_int_override(
        "int main(){int aforo{5};aforo=aforo+2+1;std::cout<<aforo;}",exact);
    require(equivalent && equivalent->find("aforo=aforo+2+1;")!=
        std::string::npos,"A different valid spelling of +3 lost its private proof");
    const auto legacy = cpp_local_int_override(
        "int main(){int aforo{5};aforo+=3;std::cout<<aforo;}",
        {"aforo",4,{},true});
    require(legacy && legacy->find("!= 3LL) return 1;")==std::string::npos,
        "The new stronger opt-in altered an older published afterUpdate probe");

    auto forged = exact;
    forged.after_update=false;
    require(!cpp_local_int_check_valid({{forged}}),
        "An expectedIncrease was applied to an initializer replacement");
    forged=exact;
    forged.expected_increase=0;
    require(!cpp_local_int_check_valid({{forged}}),
        "An expectedIncrease of zero claimed to prove a positive increase");
    forged.expected_increase=-3;
    require(!cpp_local_int_check_valid({{forged}}),
        "A negative expectedIncrease was allowed as a required increase");
}

void exact_increment_accepts_consecutive_updates_without_reordering() {
    // A valid learner solution may express +3 as two ordinary statements.
    // No private mutation may be injected between them or before a saved
    // copy. Comments are copied literally, not parsed as update statements.
    const std::string source =
        "#include <iostream>\nint main(){int aforo{5};int anterior=aforo;"
        "aforo+=1; /* primer cambio */\n"
        "aforo=aforo+2;"
        "std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo;}";
    const auto original=source;
    const CppLocalIntProbe exact{"aforo",4,{},true,3};
    const auto changed=cpp_local_int_override(source,exact);
    require(changed&&source==original &&
        changed->find("aforo+=1;")!=std::string::npos &&
        changed->find("/* primer cambio */")!=std::string::npos &&
        changed->find("aforo=aforo+2;")!=std::string::npos,
        "Two genuine consecutive updates lost source or could not be probed");
    const auto first=changed->find("aforo+=1;");
    const auto second=changed->find("aforo=aforo+2;");
    const auto witness=changed->find("!= 3LL) return 1;");
    const auto delta=changed->find("aforo += 4;");
    const auto output=changed->find("std::cout<<");
    require(first<second&&second<witness&&witness<delta&&delta<output &&
        changed->find("const int lgaed_local_int_before_6cb8e71d = aforo;")<first,
        "The private exact +3 check was inserted between the student's updates");
    const auto previous=cpp_local_int_override(source,{"aforo",4,{},true});
    require(previous&&previous->find("aforo += 4;")<
        previous->find("aforo=aforo+2;")&&
        previous->find("!= 3LL) return 1;")==std::string::npos,
        "The original afterUpdate contract was silently widened");
    const std::string interrupted=
        "int main(){int aforo{5};aforo+=1;"
        "int anterior=aforo;aforo+=2;std::cout<<anterior<<aforo;}";
    const auto protected_copy=cpp_local_int_override(interrupted,exact);
    require(protected_copy &&
        protected_copy->find("!= 3LL) return 1;")<
        protected_copy->find("int anterior=aforo;"),
        "The exact test incorrectly crossed an intervening saved copy");
}

void private_delta_respects_signed_int_bounds() {
    // The chosen learner update itself is valid. Only the *extra* private
    // delta would overflow int. This must fail before += executes, rather
    // than relying on undefined signed overflow and coincidental stdout.
    const std::string hi=
        "int main(){int aforo{2147483646};aforo+=1;return aforo==0;}";
    const std::string lo=
        "int main(){int aforo{-2147483646};aforo-=1;return aforo==0;}";
    for(const auto& entry:std::array{
            std::pair{hi,4},std::pair{lo,-4}}) {
        const auto altered=cpp_local_int_override(
            entry.first,{"aforo",entry.second,{},true});
        require(altered && altered->find(
            ") + ("+std::to_string(entry.second)+
            "LL) > 2147483647LL")!=std::string::npos &&
            altered->find("LL) < -2147483648LL) return 1;")!=
                std::string::npos &&
            altered->find("aforo += "+std::to_string(entry.second)+";")!=
                std::string::npos,
            "The private integer probe omitted its before-addition overflow barrier");
    }
}

void crlf_after_update_preserves_source_and_comment() {
    // The Windows editor may hold CRLF. The private instrumented file may
    // use LF for new lines, but no byte of the learner's source is rewritten.
    const std::string input =
        "#include <iostream>\r\n"
        "int main() {\r\n"
        "    int aforo{5};\r\n"
        "    int anterior=aforo;\r\n"
        "    aforo+=3; // comentario de la actualización\r\n"
        "    std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo;\r\n"
        "}\r\n";
    const auto original=input;
    const auto varied=cpp_local_int_override(input,{"aforo",4,{},true});
    require(varied.has_value()&&input==original,
        "A private Windows CRLF probe changed the student's source");
    constexpr std::string_view snapshot="lgaed_local_int_before_6cb8e71d";
    require(varied->find("const int "+std::string(snapshot)+" = aforo;")!=
                std::string::npos &&
            varied->find("aforo+=3;\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=std::string::npos &&
            varied->find("// comentario de la actualización\r\n")!=
                std::string::npos &&
            varied->find("std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo;")!=
                std::string::npos &&
            varied->find("\r\n")!=std::string::npos,
        "A Windows CRLF or an original comment was lost during isolated probing");
}

void numeric_literal_identity_distinguishes_one_from_ten() {
    using lgaed::curriculum::cpp_local_int_probe_detail::int_probe_literal_is;
    for(const auto* literal:{
        "number:1", "number:01", "number:0001",
        "number:0x01", "number:0X1", "number:0b001",
        "number:0B001", "number:0x0'1UL"}) {
        require(int_probe_literal_is(literal,'1'),
            "An integer literal equal to one was not recognized");
    }
    for(const auto* literal:{
        "number:10", "number:010", "number:0x10", "number:0X10",
        "number:0b10", "number:0B10", "number:0x100",
        "number:1'0", "number:0x1'0UL"}) {
        require(!int_probe_literal_is(literal,'1'),
            "A non-unit multiplier was incorrectly classified as one");
    }
    for(const auto* statement:{
        "aforo=aforo*10;", "aforo=aforo*010;", "aforo=aforo*0x10;",
        "aforo=aforo*0b10;", "aforo=aforo*0B10;"}) {
        const auto source=std::string(
            "#include <iostream>\nint main(){int aforo{5};")
            +statement+"std::cout<<aforo;}";
        const auto variant=cpp_local_int_override(
            source,{"aforo",4,{},true});
        require(variant&&
            variant->find(std::string(statement)+"\n    if (static_cast<long long>(aforo) + (4LL) > 2147483647LL || static_cast<long long>(aforo) + (4LL) < -2147483648LL) return 1;\n    aforo += 4;\n")!=
                std::string::npos,
            "A genuine multiplication update was mistaken for a neutral assignment");
    }
    for(const auto* statement:{
        "aforo=aforo*1;", "aforo=aforo*01;",
        "aforo=aforo*0x01;", "aforo=aforo*0b01;",
        "aforo=aforo*0B01;", "aforo=aforo*0x0'1UL;"}) {
        const auto source=std::string(
            "#include <iostream>\nint main(){int aforo{5};")
            +statement+"std::cout<<aforo;}";
        require(!cpp_local_int_override(source,{"aforo",4,{},true}),
            "A multiplication by literal one was credited as an increase");
    }
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
    check.probes[0].expected_stdout = {"Llaves: 13\n", "Llaves: 13\r\n"};
    require(!cpp_local_int_check_valid(check),
        "Equivalent native CRLF/LF spellings were counted as two observations");
    check.probes[0].expected_stdout = {"Llaves: 13\n"};
    require(cpp_local_int_expected_output(check.probes[0], "Llaves: 13\r\n"),
        "Native CRLF must remain equivalent to the authored LF spelling");
    check.probes[0].expected_stdout = {};
    require(!cpp_local_int_check_valid(check), "Missing independent output was admitted");
}

CppLocalIntProbe typed_probe(std::string_view variable,std::int32_t replacement,
                            CppLocalValueType type) {
    return {std::string(variable),replacement,{"private"},false,std::nullopt,type};
}

void typed_local_initializers_preserve_source_and_exact_type() {
    const auto character=typed_probe("pasillo",68,CppLocalValueType::character);
    const auto boolean=typed_probe("abierta",0,CppLocalValueType::boolean);
    const char* character_declarations[]{
        "char pasillo = 'C';", "char pasillo{'C'};",
        "char pasillo('C');", "char pasillo = {'C'};",
        "char pasillo = '\\x43';", "char pasillo = '\\103';",
        "char pasillo = static_cast<char>('A' + 2);",
        "char pasillo = [] { return 'C'; }();",
        "char pasillo; pasillo = 'C';",
        "char otro{'A'}, pasillo('C'), siguiente='E';"
    };
    const char* boolean_declarations[]{
        "bool abierta = true;", "bool abierta{true};",
        "bool abierta(true);", "bool abierta = {true};",
        "bool abierta = !false;", "bool abierta = (2 > 1);",
        "bool abierta = static_cast<bool>(1);",
        "bool abierta = [] { return true; }();",
        "bool abierta; abierta = (2 > 1);",
        "bool antes=false, abierta(true), despues{false};"
    };
    for(const auto* declaration:character_declarations) {
        const auto source=std::string("#include <iostream>\nint main(){\n")
            +declaration+"\nstd::cout << pasillo;\n}\n";
        const auto original=source;
        const auto varied=cpp_local_int_override(source,character);
        require(varied&&source==original&&
            varied->find(std::string(declaration)+
                "\n    pasillo = static_cast<char>(68);\n")!=std::string::npos&&
            varied->find("std::cout << pasillo;")!=std::string::npos,
            "An equivalent char initializer was changed or could not be probed");
    }
    for(const auto* declaration:boolean_declarations) {
        const auto source=std::string("#include <iostream>\nint main(){\n")
            +declaration+"\nstd::cout << abierta;\n}\n";
        const auto original=source;
        const auto varied=cpp_local_int_override(source,boolean);
        require(varied&&source==original&&
            varied->find(std::string(declaration)+
                "\n    abierta = false;\n")!=std::string::npos&&
            varied->find("std::cout << abierta;")!=std::string::npos,
            "An equivalent bool initializer was changed or could not be probed");
    }
    const auto true_variant=cpp_local_int_override(
        "int main(){bool abierta=false;return abierta;}",
        typed_probe("abierta",1,CppLocalValueType::boolean));
    require(true_variant&&true_variant->find("abierta = true;")!=std::string::npos,
        "A true bool replacement was emitted as a different value");
}

void mixed_character_and_boolean_probes_are_independent() {
    const std::string source=
        "#include <iostream>\r\n"
        "auto main() noexcept(noexcept(7)) -> int {\r\n"
        "    char pasillo{'C'};\r\n"
        "    bool abierta{true};\r\n"
        "    // pasillo and abierta are not identifiers in this comment.\r\n"
        "    std::cout << \"Pasillo: \" << pasillo << \"\\nAbierta: \" << abierta;\r\n"
        "}\r\n";
    auto character=typed_probe("pasillo",68,CppLocalValueType::character);
    character.expected_stdout={"Pasillo: D\nAbierta: 1","Pasillo: D\nAbierta: 1\n"};
    auto boolean=typed_probe("abierta",0,CppLocalValueType::boolean);
    boolean.expected_stdout={"Pasillo: C\nAbierta: 0","Pasillo: C\nAbierta: 0\n"};
    const auto original=source;
    const auto passage=cpp_local_int_override(source,character);
    const auto opened=cpp_local_int_override(source,boolean);
    require(passage&&opened&&source==original&&
        passage->find("char pasillo{'C'};\n    pasillo = static_cast<char>(68);\n")
            !=std::string::npos&&
        passage->find("bool abierta{true};\r\n")!=std::string::npos&&
        passage->find("abierta = false;")==std::string::npos&&
        opened->find("char pasillo{'C'};\r\n")!=std::string::npos&&
        opened->find("bool abierta{true};\n    abierta = false;\n")!=std::string::npos&&
        opened->find("pasillo = static_cast<char>")==std::string::npos,
        "A typed probe rewrote the other variable, its initializer or the draft");
    require(lgaed::curriculum::cpp_local_int_check_valid({{character,boolean}})&&
        lgaed::curriculum::cpp_local_int_expected_output(
            character,"Pasillo: D\nAbierta: 1")&&
        lgaed::curriculum::cpp_local_int_expected_output(
            boolean,"Pasillo: C\r\nAbierta: 0\r\n")&&
        !lgaed::curriculum::cpp_local_int_expected_output(
            character,"Pasillo: C\nAbierta: 1")&&
        !lgaed::curriculum::cpp_local_int_expected_output(
            boolean,"Pasillo: D\nAbierta: 0"),
        "Typed expectations lost optional final newlines or independent exact output");
}

void typed_probes_reject_other_types_and_ambiguous_owners() {
    const auto character=typed_probe("valor",68,CppLocalValueType::character);
    const auto boolean=typed_probe("valor",0,CppLocalValueType::boolean);
    for(const auto* declaration:{
            "int valor=67;", "bool valor=true;", "signed char valor='C';",
            "unsigned char valor='C';", "wchar_t valor=L'C';",
            "char8_t valor=u8'C';", "char16_t valor=u'C';",
            "auto valor='C';", "const char valor='C';",
            "volatile char valor='C';", "static char valor='C';",
            "char* valor=nullptr;", "char& valor=otro;", "char valor[2]{};",
            "char valor='C', valor='D';", "char valor='C', copia=valor;",
            "{char valor='C';}", "char valor; valor=valor;",
            "char valor; if(true){valor='C';}"}) {
        require(!cpp_local_int_override(std::string("int main(){")+declaration+"}",
                                      character),
            "A char probe accepted another type, scope or ambiguous initialization");
    }
    for(const auto* declaration:{
            "int valor=1;", "char valor='C';", "unsigned valor=1;",
            "auto valor=true;", "const bool valor=true;",
            "volatile bool valor=true;", "static bool valor=true;",
            "bool* valor=nullptr;", "bool& valor=otra;", "bool valor[2]{};",
            "bool valor=true, valor=false;", "bool valor=true, copia=valor;",
            "{bool valor=true;}", "bool valor; valor=!valor;",
            "bool valor; if(true){valor=true;}"}) {
        require(!cpp_local_int_override(std::string("int main(){")+declaration+"}",
                                      boolean),
            "A bool probe accepted another type, scope or ambiguous initialization");
    }
    require(!cpp_local_int_override("char valor='C';int main(){return valor;}",character)&&
        !cpp_local_int_override("bool valor=true;int main(){return valor;}",boolean)&&
        !cpp_local_int_override("int main(){char valor='C';}",{"valor",68})&&
        !cpp_local_int_override("int main(){bool valor=true;}",{"valor",0}),
        "An old int contract accepted a new type or a global was treated as local");
    for(const auto& probe:{character,boolean}) {
        const std::string declaration=probe.value_type==CppLocalValueType::character
            ? "char valor='C';" : "bool valor=true;";
        for(const auto* observation:{"return __LINE__;", "exit(0);", "return __COUNTER__;"})
            require(!cpp_local_int_override(std::string("int main(){")+declaration+
                    observation+"}",probe),
                "A typed variation bypassed the private-recompile observation guard");
    }
}

void typed_probe_value_contract_is_strict_and_consistent() {
    using lgaed::curriculum::cpp_local_int_check_valid;
    const std::string chars="int main(){char valor='C';}";
    const std::string bools="int main(){bool valor=true;}";
    for(const auto value:{32,39,92,126}) {
        const auto probe=typed_probe("valor",value,CppLocalValueType::character);
        const auto varied=cpp_local_int_override(chars,probe);
        require(cpp_local_int_check_valid({{probe}})&&varied&&
            varied->find("static_cast<char>("+std::to_string(value)+")")!=std::string::npos,
            "A printable ASCII byte, quote or slash was not represented safely");
    }
    for(const auto value:{INT32_MIN,-1,0,31,127,128,255,INT32_MAX}) {
        const auto probe=typed_probe("valor",value,CppLocalValueType::character);
        require(!cpp_local_int_check_valid({{probe}})&&!cpp_local_int_override(chars,probe),
            "An out-of-contract char byte reached source instrumentation");
    }
    for(const auto value:{INT32_MIN,-1,2,68,INT32_MAX}) {
        const auto probe=typed_probe("valor",value,CppLocalValueType::boolean);
        require(!cpp_local_int_check_valid({{probe}})&&!cpp_local_int_override(bools,probe),
            "An arbitrary integer was silently converted to a bool probe");
    }
    for(const auto type:{CppLocalValueType::character,CppLocalValueType::boolean}) {
        auto probe=typed_probe("valor",type==CppLocalValueType::character?68:0,type);
        const auto& source=type==CppLocalValueType::character?chars:bools;
        probe.after_update=true;
        require(!cpp_local_int_check_valid({{probe}})&&!cpp_local_int_override(source,probe),
            "A non-int probe admitted afterUpdate");
        probe.expected_increase=3;
        require(!cpp_local_int_check_valid({{probe}})&&!cpp_local_int_override(source,probe),
            "A non-int probe admitted an expected increase");
        probe.after_update=false;
        require(!cpp_local_int_check_valid({{probe}})&&!cpp_local_int_override(source,probe),
            "An expected increase without afterUpdate reached a typed override");
    }
    auto unknown=typed_probe("valor",68,static_cast<CppLocalValueType>(99));
    require(!cpp_local_int_check_valid({{unknown}})&&!cpp_local_int_override(chars,unknown),
        "An unrecognized scalar type was silently treated as int");
    const auto character=typed_probe("valor",68,CppLocalValueType::character);
    const auto integer=typed_probe("valor",68,CppLocalValueType::integer);
    const auto boolean=typed_probe("valor",0,CppLocalValueType::boolean);
    require(!cpp_local_int_check_valid({{character,character}})&&
        !cpp_local_int_check_valid({{character,integer}})&&
        !cpp_local_int_check_valid({{character,boolean}})&&
        !cpp_local_int_check_valid({{integer,boolean}}),
        "One variable admitted duplicate probes or conflicting declared types");
}

void old_int_aggregate_and_emitted_source_remain_identical() {
    CppLocalIntProbe previous{"llaves",13,{"Llaves: 13"}};
    auto explicit_type=previous;
    explicit_type.value_type=CppLocalValueType::integer;
    const std::string source="int main(){int llaves=7;std::cout<<llaves;}";
    const std::string expected=
        "int main(){int llaves=7;\n    llaves = 13;\nstd::cout<<llaves;}";
    require(previous.value_type==CppLocalValueType::integer&&previous==explicit_type&&
        cpp_local_int_override(source,previous)==expected&&
        cpp_local_int_override(source,explicit_type)==expected&&
        !lgaed::curriculum::cpp_local_int_check_valid({{previous,explicit_type}}),
        "Adding explicit scalar types changed an old int aggregate or its bytes");
}

} // namespace

int main() {
    try {
        equivalent_initializers_do_not_change_original_source();
        noexcept_main_preserves_local_dependency_checks();
        trailing_return_main_supports_equivalent_int_entrypoints();
        independently_probe_two_variables();
        grouped_integer_declarations_keep_independent_data();
        separate_assignment_and_copy_before_update();
        comments_literals_and_similar_names_are_inert();
        unsafe_ambiguous_and_other_scope_are_rejected();
        local_probe_names_follow_real_cpp_token_boundaries();
        int32_limits_and_old_text_are_preserved();
        expected_outputs_are_independent_and_reversible();
        changed_increase_is_observed_at_the_actual_update();
        neutral_updates_cannot_fake_u2l3_increase();
        composed_neutral_updates_cannot_fake_u2l3_increase();
        opaque_noop_requires_runtime_state_change();
        private_marker_in_comments_and_strings_is_not_an_identifier();
        private_recompile_cannot_consume_build_markers_or_exit_early();
        exact_authored_increase_cannot_be_hidden_in_stdout();
        exact_increment_accepts_consecutive_updates_without_reordering();
        private_delta_respects_signed_int_bounds();
        crlf_after_update_preserves_source_and_comment();
        numeric_literal_identity_distinguishes_one_from_ten();
        typed_local_initializers_preserve_source_and_exact_type();
        mixed_character_and_boolean_probes_are_independent();
        typed_probes_reject_other_types_and_ambiguous_owners();
        typed_probe_value_contract_is_strict_and_consistent();
        old_int_aggregate_and_emitted_source_remain_identical();
        std::cout << "PASS local int probes: equivalent initialization, independent "
                     "variables, copy state, safe token positions.\n"
                     "PASS typed local probes: char/bool initialization, independent "
                     "values, strict types and preserved int contracts.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL local int probes: " << error.what() << '\n';
        return 1;
    }
}
