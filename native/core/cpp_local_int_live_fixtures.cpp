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
    const std::array<Scenario,6> examples{{
        {"u2l1_llaves_7",
            R"CPP(#include <iostream>
int main(){int llaves=3+4; std::cout<<"Llaves: "<<llaves;})CPP",
            "Llaves: 7",
            {{{"llaves",13,{}},{},{}}},
            {{"Llaves: 13", "", ""}},1},
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
            {{"Antes: 13\nAhora: 13","Antes: 11\nAhora: 8","Antes: 12\nAhora: 12"}},3}
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
    if(emitted!=20)throw std::runtime_error("Unexpected live fixture count");
    std::cout<<"PASS generated 20 isolated C++ executions for six U2 owners and two adversaries\n";
    return 0;
}catch(const std::exception& e) {
    std::cerr<<"FAIL local-int real compiler fixtures: "<<e.what()<<'\n';
    return 1;
}
