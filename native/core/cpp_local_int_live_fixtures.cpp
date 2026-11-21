// Integration fixtures for a *real* C++ compiler, without touching a student's
// source, progress, curriculum or the app's pinned Windows toolchain.
// The Python driver builds each emitted variant and compares the raw stdout.
#include "core/cpp_local_int_probe.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::CppLocalIntProbe;
using lgaed::curriculum::cpp_local_int_override;

struct Scenario {
    const char* name;
    std::string_view source;
    std::string_view visible;
    std::array<CppLocalIntProbe,3> probes;
    std::array<std::string_view,3> expected;
    std::size_t probe_count;
};

void write(const std::filesystem::path& path,std::string_view bytes) {
    std::ofstream stream(path,std::ios::binary | std::ios::trunc);
    if(!stream)throw std::runtime_error("Cannot create isolated compiler fixture");
    stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!stream)throw std::runtime_error("Cannot write isolated compiler fixture");
}

void emit(const std::filesystem::path& directory,std::string_view name,
        std::string_view source,std::string_view expected,bool must_match) {
    const auto prefix=directory/std::string(name);
    write(prefix.string()+".cpp",source);
    std::string verdict=must_match?"A\n":"R\n";
    verdict+=expected;
    write(prefix.string()+".expected",verdict);
}
}
int main(int argc,char** argv) try {
    if(argc!=2)return 2;
    const std::filesystem::path directory=argv[1];
    std::filesystem::create_directories(directory);
    const std::array<Scenario,13> examples{{
        {"u2l1_llaves_7",
            R"CPP(#include <iostream>
int main(){int llaves=3+4; std::cout<<"Llaves: "<<llaves;})CPP",
            "Llaves: 7",
            {{{"llaves",13,{}},{},{}}},
            {{"Llaves: 13", "", ""}},1},
        {"u2l1_trailing_main",
            R"CPP(#include <iostream>
auto main() noexcept -> int {int llaves{7};
std::cout<<"Llaves: "<<llaves;})CPP",
            "Llaves: 7",
            {{{"llaves",13,{}},{},{}}},
            {{"Llaves: 13","",""}},1},
        {"u2l1_member_named_llaves",
            R"CPP(#include <iostream>
struct Datos {int llaves{7};};
int main(){Datos datos;int llaves;int origen=datos.llaves;
llaves=origen;std::cout<<"Llaves: "<<llaves;})CPP",
            "Llaves: 7",
            {{{"llaves",13,{}},{},{}}},
            {{"Llaves: 13","",""}},1},
        {"u2l1_llaves_11",
            R"CPP(#include <iostream>
int main(){int llaves(11); std::cout<<"Llaves: "<<llaves;})CPP",
            "Llaves: 11",
            {{{"llaves",19,{}},{},{}}},
            {{"Llaves: 19", "", ""}},1},
        {"u2l2_cajas4_sobres9",
            R"CPP(#include <iostream>
int main(){int cajas{4}; int sobres=9;
std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;})CPP",
            "Cajas: 4\nSobres: 9",
            {{{"cajas",13,{}},{"sobres",17,{}},{}}},
            {{"Cajas: 13\nSobres: 9","Cajas: 4\nSobres: 17",""}},2},
        {"u2l2_cajas0_sobres2",
            R"CPP(#include <iostream>
int main(){int cajas=0; int sobres{2};
std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;})CPP",
            "Cajas: 0\nSobres: 2",
            {{{"cajas",6,{}},{"sobres",17,{}},{}}},
            {{"Cajas: 6\nSobres: 2","Cajas: 0\nSobres: 17",""}},2},
        {"u2l2_grouped_int_braces",
            R"CPP(#include <iostream>
int main(){int cajas{4}, sobres{9};
std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;})CPP",
            "Cajas: 4\nSobres: 9",
            {{{"cajas",13,{}},{"sobres",17,{}},{}}},
            {{"Cajas: 13\nSobres: 9","Cajas: 4\nSobres: 17",""}},2},
        {"u2l2_grouped_assignments",
            R"CPP(#include <iostream>
int main(){int cajas, sobres; cajas=4; sobres=9;
std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;})CPP",
            "Cajas: 4\nSobres: 9",
            {{{"cajas",13,{}},{"sobres",17,{}},{}}},
            {{"Cajas: 13\nSobres: 9","Cajas: 4\nSobres: 17",""}},2},
        {"u2l2_signed_type_spellings",
            R"CPP(#include <iostream>
int main(){int signed cajas{4}; signed sobres=9;
std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;})CPP",
            "Cajas: 4\nSobres: 9",
            {{{"cajas",13,{}},{"sobres",17,{}},{}}},
            {{"Cajas: 13\nSobres: 9","Cajas: 4\nSobres: 17",""}},2},
        {"u2l3_copy_before",
            R"CPP(#include <iostream>
int main(){int aforo{5}; int anterior=aforo; aforo+=3;
std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo;})CPP",
            "Antes: 5\nAhora: 8",
            {{{"aforo",10,{}},{"anterior",11,{}},{"aforo",4,{},true}}},
            {{"Antes: 10\nAhora: 13","Antes: 11\nAhora: 8","Antes: 5\nAhora: 12"}},3},
        {"u2l3_copy_after",
            R"CPP(#include <iostream>
int main(){int aforo=5; aforo+=3; int anterior(aforo);
std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo;})CPP",
            "Antes: 8\nAhora: 8",
            {{{"aforo",10,{}},{"anterior",11,{}},{"aforo",4,{},true}}},
            {{"Antes: 13\nAhora: 13","Antes: 11\nAhora: 8","Antes: 12\nAhora: 12"}},3},
        {"u2l5_division_variable",
            R"CPP(#include <iostream>

int main() {
    double longitud_metros = 9.0;
    int partes = 2;
    double longitud_por_parte = longitud_metros / partes;
    std::cout << "Parte: " << longitud_por_parte << " m";
    return 0;
}
)CPP",
            "Parte: 4.5 m",
            {{{"partes",4,{}},{"partes",8,{}},{}}},
            {{"Parte: 2.25 m", "Parte: 1.125 m", ""}},2},
        {"u2l5_suma_agrupada",
            R"CPP(#include <iostream>

int main() {
    double longitud_metros = 4.5;
    int tramo_adicional_metros = 1;
    int partes = 2;
    double longitud_por_parte = (longitud_metros + tramo_adicional_metros) / partes;
    std::cout << "Parte: " << longitud_por_parte << " m";
    return 0;
}
)CPP",
            "Parte: 2.75 m",
            {{{"tramo_adicional_metros",3,{}},{"partes",4,{}},{}}},
            {{"Parte: 3.75 m", "Parte: 1.375 m", ""}},2}
    }};
    std::size_t emitted=0;
    for(const auto& example:examples) {
        emit(directory,example.name,example.source,example.visible,true);
        ++emitted;
        for(std::size_t i=0;i<example.probe_count;++i) {
            const auto& probe=example.probes[i];
            const auto variant=cpp_local_int_override(example.source,probe);
            if(!variant)throw std::runtime_error(std::string("Cannot instrument ")+example.name);
            emit(directory,std::string(example.name)+"_probe_"+std::to_string(i),
                *variant,example.expected[i],true);
            ++emitted;
        }
    }
    // A hard-coded report passes the visible case but is unchanged by probing
    // the declared int. Its independently compiled variant MUST be rejected.
    constexpr std::string_view fixed=R"CPP(#include <iostream>
int main(){int llaves=7; std::cout<<"Llaves: 7";})CPP";
    const auto fixed_probe=cpp_local_int_override(fixed,{"llaves",13,{}});
    if(!fixed_probe)throw std::runtime_error("Cannot probe a fixed-output adversary");
    emit(directory,"reject_fixed_output",*fixed_probe,"Llaves: 13",false);
    ++emitted;
    // This program prints a constant prior state, rather than copying aforo.
    // Its visible output is correct, but the after-initialization mutation
    // must reveal the missing dependency of Antes on aforo.
    constexpr std::string_view fake_copy=R"CPP(#include <iostream>
int main(){int aforo{5}; int anterior{5}; aforo+=3;
std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo;})CPP";
    const auto bad_copy=cpp_local_int_override(fake_copy,{"aforo",10,{}});
    if(!bad_copy)throw std::runtime_error("Cannot probe a fake-copy adversary");
    emit(directory,"reject_fake_copy",*bad_copy,"Antes: 10\nAhora: 13",false);
    ++emitted;
    // A hard-coded "Ahora" might reproduce the public result despite an
    // authentic increase and copy. Changing the increase must change Ahora.
    constexpr std::string_view fixed_increase=R"CPP(#include <iostream>
int main(){int aforo=5; int anterior=aforo; aforo+=3;
std::cout<<"Antes: "<<anterior<<"\nAhora: "<<8;})CPP";
    const auto bad_increase=cpp_local_int_override(fixed_increase,
        {"aforo",4,{},true});
    if(!bad_increase)throw std::runtime_error("Cannot probe a fixed increase adversary");
    emit(directory,"reject_fixed_increase",*bad_increase,
        "Antes: 5\nAhora: 12",false);
    ++emitted;
    // A student might declare and update anterior but continue printing
    // aforo twice. U2L3's second original case displays 8/8 either way.
    // Altering anterior privately must affect the first line only.
    constexpr std::string_view repeated_aforo=R"CPP(#include <iostream>
int main(){int aforo{5}; aforo+=3; int anterior=aforo;
std::cout<<"Antes: "<<aforo<<"\nAhora: "<<aforo;})CPP";
    const auto missing_copy=cpp_local_int_override(repeated_aforo,
        {"anterior",11,{}});
    if(!missing_copy)throw std::runtime_error("Cannot probe a repeated aforo adversary");
    emit(directory,"reject_print_aforo_twice",*missing_copy,
        "Antes: 11\nAhora: 8",false);
    ++emitted;
    // Visible stdout alone is insufficient when the divider is printed as a literal.
    // The production local-int mutation must reveal its independence from partes.
    constexpr std::string_view fixed_division=R"CPP(#include <iostream>
int main() {
    double longitud_metros = 9.0;
    int partes = 2;
    std::cout << "Parte: 4.5 m";
})CPP";
    const auto missing_divisor=cpp_local_int_override(fixed_division,{"partes",4,{}});
    if(!missing_divisor)throw std::runtime_error("Cannot probe hard-coded U2L5 division");
    emit(directory,"reject_u2l5_fixed_division",*missing_divisor,"Parte: 2.25 m",false);
    ++emitted;
    // Deliberately keep the correct visible 2.75 but ignore the declared
    // extra length: the private variation of tramo_adicional_metros must fail.
    constexpr std::string_view fixed_addend=R"CPP(#include <iostream>
int main() {
    double longitud_metros = 4.5;
    int tramo_adicional_metros = 1;
    int partes = 2;
    double longitud_por_parte = (longitud_metros + 1) / partes;
    std::cout << "Parte: " << longitud_por_parte << " m";
})CPP";
    const auto missing_addend=cpp_local_int_override(fixed_addend,
        {"tramo_adicional_metros",3,{}});
    if(!missing_addend)throw std::runtime_error("Cannot probe hard-coded U2L5 addend");
    emit(directory,"reject_u2l5_fixed_addend",*missing_addend,"Parte: 3.75 m",false);
    ++emitted;
    // Varying cajas after a multi-declarator statement would be too late
    // to observe a sibling initialized from cajas. Reject this structurally.
    if(cpp_local_int_override(
        "int main(){int cajas=4, sobres=cajas+5; std::cout<<cajas<<sobres;}",
        {"cajas",13,{}}))
        throw std::runtime_error("A grouped dependent int could receive false credit");
    // A source-only guard: compiling and running an uninitialized read would
    // itself have undefined behavior, so it must be rejected BEFORE emitting
    // a variant or counting a compiler execution.
    if(cpp_local_int_override(
        "int main(){int llaves; llaves=llaves+7; std::cout<<llaves;}",
        {"llaves",13,{}}))
        throw std::runtime_error("A self-read before int initialization received a probe");
    // The public U2L3 answer can look correct even when the student
    // replaces aforo with a fixed 8 instead of increasing its prior value.
    // Inserting a +4 AFTER this reset would incorrectly produce the expected
    // private 12, so no after-update source must be generated at all.
    constexpr std::string_view fixed_increment=R"CPP(#include <iostream>
int main(){int aforo=5;int anterior=aforo;aforo=8;
std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo;})CPP";
    if(cpp_local_int_override(fixed_increment,{"aforo",4,{},true}))
        throw std::runtime_error("A constant replacement masqueraded as an increase");
    // A deliberately deceptive U2L3 source passes all three existing stdout
    // variations by adding the missing 3 only in its output expression.
    // Its alleged update is an identity, so reject it before compiling.
    constexpr std::string_view neutral_update=R"CPP(#include <iostream>
int main(){int aforo{5};int anterior=aforo;aforo=aforo+0;
std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo+3;})CPP";
    if(cpp_local_int_override(neutral_update,{"aforo",4,{},true}))
        throw std::runtime_error(
            "A neutral U2L3 update passed every private output variation");
    // Compound cancellation is another false increase: the visible report
    // can be reproduced by computing +3 only at print time, and a naive
    // hidden +4 would still appear to establish the required dependency.
    for(const auto* neutral:{
        "aforo=aforo+2-2;",
        "aforo=aforo-2+2;",
        "aforo+=(2+3-5);",
        "aforo=aforo+(+6)-6;"
    }) {
        const auto source=std::string(
            "#include <iostream>\nint main(){int aforo{5};int anterior=aforo;")
            +neutral+
            "std::cout<<\"Antes: \"<<anterior<<\"\\nAhora: \"<<aforo+3;}";
        if(cpp_local_int_override(source,{"aforo",4,{},true}))
            throw std::runtime_error(
                "A compound neutral operation was credited as U2L3 increase");
    }
    // A multiplier such as 10 (decimal), 010 (octal), 0x10 (hex) or
    // 0b10 (binary) is not the identity multiplier one. A lexical
    // shortcut must not reject a valid after-update variation.
    for(const auto* multiplier:{"10","010","0x10","0b10","0B10"}) {
        const auto src=std::string(
            "int main(){int aforo{5};aforo=aforo*")+
            multiplier+";std::cout<<aforo;}";
        if(!cpp_local_int_override(src,{"aforo",4,{},true}))
            throw std::runtime_error(
                "A non-unit multiplier was incorrectly rejected as a no-op");
    }
    if(emitted!=43)throw std::runtime_error("Unexpected live fixture count");
    std::cout<<"PASS generated 43 isolated C++ executions for U2 integer owners, grouped declarations, homonyms and six adversaries\n";
    return 0;
}catch(const std::exception& e) {
    std::cerr<<"FAIL local-int real compiler fixtures: "<<e.what()<<'\n';
    return 1;
}
