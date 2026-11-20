// Compile the original U6L12 Tramo and an independent copy/take driver.
// Only the two free-function bodies may change; all supplied class code and
// public output remain immutable. This tool never edits curricular assets.
#include "core/cpp_array_return_take_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string read(const std::filesystem::path& p) {
    std::ifstream file(p,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read original/candidate U6L12 source");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& p,std::string_view text) {
    std::ofstream file(p,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot create independent U6L12 C++ source");
    file.write(text.data(),static_cast<std::streamsize>(text.size()));
    if(!file)throw std::runtime_error("Cannot write independent U6L12 C++ source");
}
}
int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error(
        "Usage: array-return-take-generator ORIGINAL_CPP CANDIDATE_CPP PROBE_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppArrayReturnTakePolicy spec{
        original,"Tramo","datos_","cantidad_","copia_modificada","tomar_si_cabe",
        "origen","indice","valor","destino","origen","limite",
        "cantidad","acceso","configurar","leer","escribir","vaciar"
    };
    if(!lgaed::curriculum::cpp_array_return_take_matches(spec,candidate))
        throw std::runtime_error(
            "Changed published class/main or one of the protected free-function signatures");
    const auto probe=lgaed::curriculum::cpp_array_return_take_private_driver(
        spec,candidate);
    if(!probe)throw std::runtime_error("Cannot generate independent U6L12 witness");
    write(argv[3],*probe);
    std::cout<<"PASS U6L12 original source preserved outside two free function bodies\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL native U6L12 source check: "<<error.what()<<'\n';
    return 1;
}
