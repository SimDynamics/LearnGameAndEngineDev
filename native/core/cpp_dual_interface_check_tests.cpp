// U10L1: emit a private semantic program from only two authored method bodies.
#include "core/cpp_dual_interface_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read original/candidate C++");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& file,std::string_view source) {
    std::ofstream out(file,std::ios::binary);
    if(!out)throw std::runtime_error("Cannot open private dual-interface C++");
    out.write(source.data(),static_cast<std::streamsize>(source.size()));
    if(!out)throw std::runtime_error("Cannot write private dual-interface C++");
}
}

int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: dual-interface-generator ORIGINAL_CPP CANDIDATE_CPP DRIVER_CPP");
    const auto original=read(argv[1]);
    const auto candidate=read(argv[2]);
    const lgaed::curriculum::CppDualInterfacePolicy policy{
        original,"ConValor","ConInforme","Muestra",
        "valor","escribir","cambiar","salida","nuevo",
        "codigo_","valor_","codigo","valor","="
    };
    if(!lgaed::curriculum::cpp_dual_interface_matches(policy,candidate))
        throw std::runtime_error(
            "Protected dual-interface declarations, supplied data, "
            "consumer, or main were modified");
    const auto driver=lgaed::curriculum::cpp_dual_interface_private_driver(
        policy,candidate);
    if(!driver)throw std::runtime_error("Cannot create U10L1 private driver");
    write(argv[3],*driver);
    std::cout<<"PASS U10L1 two original method bodies, protected program intact\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL U10L1 source guard: "<<error.what()<<'\n';
    return 1;
}
