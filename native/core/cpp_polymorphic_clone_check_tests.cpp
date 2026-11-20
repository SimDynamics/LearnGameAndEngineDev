// Generate a disposable semantic U8L10 driver from the student's three bodies.
#include "core/cpp_polymorphic_clone_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot read U8L10 original/candidate");
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& p,std::string_view value) {
    std::ofstream f(p,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot open U8L10 disposable witness");
    f.write(value.data(),static_cast<std::streamsize>(value.size()));
    if(!f)throw std::runtime_error("Cannot write U8L10 disposable witness");
}
}

int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: polymorphic-clone-generator ORIGINAL_CPP CANDIDATE_CPP WITNESS_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppPolymorphicClonePolicy policy{
        original,"Muestra","MuestraSimple","MuestraIdentificada",
        "Muestras","clonar","duplicar","numero","cambiar_numero",
        "escribir","codigo","origen","salida"
    };
    if(!lgaed::curriculum::cpp_polymorphic_clone_matches(policy,candidate))
        throw std::runtime_error(
            "U8L10 candidate changed protected C++ or unsafe editable bodies");
    const auto generated=lgaed::curriculum::cpp_polymorphic_clone_private_driver(
        policy,candidate);
    if(!generated)throw std::runtime_error("Failed to build private U8L10 driver");
    write(argv[3],*generated);
    std::cout<<"PASS U8L10 original hierarchy preserved; three bodies edited\n";
    return 0;
}catch(const std::exception& e) {
    std::cerr<<"FAIL U8L10 source guard: "<<e.what()<<'\n';
    return 1;
}
