// Generate a private U8L6 C++ probe only from the original source with
// two authenticated editable bodies. No lesson files or live state change.
#include "core/cpp_virtual_lifetime_dispatch_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read U8L6 source");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view source) {
    std::ofstream output(path,std::ios::binary);
    if(!output)throw std::runtime_error("Cannot create U8L6 private probe");
    output.write(source.data(),static_cast<std::streamsize>(source.size()));
    if(!output)throw std::runtime_error("Cannot write U8L6 private probe");
}
}
int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: virtual-lifetime-generator ORIGINAL_CPP CANDIDATE_CPP PROBE_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppVirtualLifetimeDispatchPolicy spec{
        {original,{
            {"void Medida::registrar(const char* etapa, std::ostream& salida) const",true},
            {"void MedidaIdentificada::escribir(std::ostream& salida) const",true}
        }},
        "Medida","MedidaIdentificada","registrar","escribir",
        "etapa","salida","salida"
    };
    if(!lgaed::curriculum::cpp_virtual_lifetime_dispatch_matches(spec,candidate))
        throw std::runtime_error(
            "Altered original class, signatures, main or unapproved function bodies");
    const auto driver=lgaed::curriculum::cpp_virtual_lifetime_dispatch_private_driver(
        spec,candidate);
    if(!driver)throw std::runtime_error("Cannot generate U8L6 independent dispatch proof");
    write(argv[3],*driver);
    std::cout<<"PASS source-bound U8L6 two-body virtual lifetime probe\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL U8L6 source-bound probe: "<<error.what()<<'\n';
    return 1;
}
