// Reuse the actual core source gate and private driver in compiler-backed QA.
// This utility reads the published original and a candidate, never rewriting
// lesson assets, saved drafts, state receipts or the student's source.
#include "core/cpp_array_owner_read_check.hpp"

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
    if(!file)throw std::runtime_error("Cannot open original/candidate U6L10 source");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& p,std::string_view s) {
    std::ofstream file(p,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot create private QA driver");
    file.write(s.data(),static_cast<std::streamsize>(s.size()));
    if(!file)throw std::runtime_error("Cannot write complete private QA driver");
}
}
int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error(
        "Usage: array-owner-generator ORIGINAL_CPP CANDIDATE_CPP PRIVATE_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppArrayOwnerReadPolicy spec{
        original,"Lote","datos_","cantidad_","cantidad","indice","resultado",
        "configurar","leer","cantidad","acceso","escribir","vaciar","sumar"
    };
    if(!lgaed::curriculum::cpp_array_owner_read_matches(spec,candidate))
        throw std::runtime_error("Changed protected source or invalid method bodies");
    const auto probe=lgaed::curriculum::cpp_array_owner_read_private_driver(
        spec,candidate);
    if(!probe)throw std::runtime_error("Cannot bind independent array probe");
    write(argv[3],*probe);
    std::cout<<"PASS genuine original U6L10 multi-method source protection\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL native U6L10 array authoring: "<<error.what()<<'\n';
    return 1;
}
