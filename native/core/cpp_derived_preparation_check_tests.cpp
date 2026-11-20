// Generate an independent U8L7 C++ property check from a protected original
// lesson and a student's edited sustituir body. No curriculum or saved state
// is changed by this test-only adapter.
#include "core/cpp_derived_preparation_check.hpp"

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
    if(!input)throw std::runtime_error("Cannot read U8L7 source");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view content) {
    std::ofstream output(path,std::ios::binary);
    if(!output)throw std::runtime_error("Cannot create U8L7 private source");
    output.write(content.data(),static_cast<std::streamsize>(content.size()));
    if(!output)throw std::runtime_error("Cannot write U8L7 private source");
}
}

int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: derived-preparation-generator ORIGINAL_CPP CANDIDATE_CPP PRIVATE_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppDerivedPreparationPolicy policy{
        {original,{{"void sustituir(std::unique_ptr<Medida>& destino, int numero, "
                    "int codigo, int modo, Balance& balance)",false}}},
        "Medida","MedidaIdentificada","Balance","Recurso",
        "sustituir","destino","numero","codigo","modo","balance","escribir",
        "recursos_vivos","bases_completadas","derivadas_completadas",
        "recursos_completados","bases_destruidas","derivadas_destruidas",
        "recursos_destruidos"
    };
    if(!lgaed::curriculum::cpp_derived_preparation_matches(policy,candidate))
        throw std::runtime_error(
            "Student edited protected declarations, constructors, other bodies or main");
    const auto driver=lgaed::curriculum::cpp_derived_preparation_private_driver(
        policy,candidate);
    if(!driver)throw std::runtime_error("Cannot generate private property probe");
    write(argv[3],*driver);
    std::cout<<"PASS U8L7 authentic protected owner and substitute body\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL U8L7 source preservation: "<<error.what()<<'\n';
    return 1;
}
