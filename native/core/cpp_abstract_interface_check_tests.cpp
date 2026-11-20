// Compile one private U8L8 proof from the exact student-owned four bodies.
// This executable only writes its output into a disposable QA file.
#include "core/cpp_abstract_interface_check.hpp"

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
    if(!input)throw std::runtime_error("Cannot read original or candidate U8L8 source");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view data) {
    std::ofstream output(path,std::ios::binary);
    if(!output)throw std::runtime_error("Cannot create U8L8 semantic driver");
    output.write(data.data(),static_cast<std::streamsize>(data.size()));
    if(!output)throw std::runtime_error("Cannot write U8L8 semantic driver");
}
} // namespace

int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: abstract-interface-generator ORIGINAL_CPP CANDIDATE_CPP DRIVER_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppAbstractInterfacePolicy policy{
        original,
        "Muestra","MuestraSimple","MuestraIdentificada",
        "crear","presentar","numero","codigo","escribir",
        "salida","tipo","numero","codigo","dato"
    };
    if(!lgaed::curriculum::cpp_abstract_interface_matches(policy,candidate))
        throw std::runtime_error(
            "The candidate changed abstract declarations, getters, constructors "
            "or code outside four authored function bodies");
    const auto driver=lgaed::curriculum::cpp_abstract_interface_private_driver(
        policy,candidate);
    if(!driver)throw std::runtime_error("U8L8 source did not produce a private driver");
    write(argv[3],*driver);
    std::cout<<"PASS U8L8 authentic interface source and four editable function bodies\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL U8L8 abstract interface source: "<<error.what()<<'\n';
    return 1;
}
