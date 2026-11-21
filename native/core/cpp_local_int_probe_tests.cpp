#include "core/cpp_local_int_probe.hpp"

#include <array>
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
        updated->find("aforo+=3;\n    aforo += 4;\n") != std::string::npos &&
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
        update->find("aforo+=3;\n    aforo += 4;\n")!=std::string::npos,
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
            "aforo=aforo+datos.aforo;\n    aforo += 4;\n")!=std::string::npos,
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
        later->find("aforo+=3;\n    aforo += 4;\n")!=std::string::npos&&
        later->find("aforo=aforo+0;")<
            later->find("aforo += 4;"),
        "A harmless earlier neutral expression obscured the later real increase");
    const auto combined=cpp_local_int_override(
        prefix+"aforo=aforo+0+3;std::cout<<aforo;}",
        {"aforo",4,{},true});
    require(combined&&
        combined->find("aforo=aforo+0+3;\n    aforo += 4;\n")!=std::string::npos,
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
        later->find("aforo=aforo+(3+0);\n    aforo += 4;\n")!=
            std::string::npos,
        "A neutral prefix prevented the later genuine increase from being probed");
    const auto genuine=cpp_local_int_override(
        start+"aforo=aforo+0+3;"+output,
        {"aforo",4,{},true});
    require(genuine&&
        genuine->find("aforo=aforo+0+3;\n    aforo += 4;\n")!=
            std::string::npos,
        "An expression with a real nonzero term was incorrectly ignored");
    const auto nonzero=cpp_local_int_override(
        start+"aforo=aforo+(2-3);"+output,
        {"aforo",4,{},true});
    require(nonzero&&
        nonzero->find("aforo=aforo+(2-3);\n    aforo += 4;\n")!=
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
            variant->find(std::string(genuine_additive)+"\n    aforo += 4;\n")!=
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
            altered->find(std::string(statement)+"\n    aforo += 4;\n")!=
                std::string::npos&&
            altered->find(" - (4LL) <= static_cast<long long>("+
                std::string(marker))!=std::string::npos,
            "Opaque unchanged state lost its runtime guard");
    }
    const auto genuine=cpp_local_int_override(
        start+"aforo=(aforo|0)+3;"+output,{"aforo",4,{},true});
    require(genuine&&genuine->find("aforo=(aforo|0)+3;\n    aforo += 4;\n")!=
        std::string::npos,"A genuine arithmetic increase was rejected");
    require(!cpp_local_int_override(
        start+"int lgaed_local_int_before_6cb8e71d=9;aforo+=3;"+output,
        {"aforo",4,{},true}),
        "A learner declaration shadowed a private snapshot");
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
            varied->find("aforo+=3;\n    aforo += 4;\n")!=std::string::npos &&
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
            variant->find(std::string(statement)+"\n    aforo += 4;\n")!=
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
        private_delta_respects_signed_int_bounds();
        crlf_after_update_preserves_source_and_comment();
        numeric_literal_identity_distinguishes_one_from_ten();
        std::cout << "PASS local int probes: equivalent initialization, independent "
                     "variables, copy state, safe token positions.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL local int probes: " << error.what() << '\n';
        return 1;
    }
}
