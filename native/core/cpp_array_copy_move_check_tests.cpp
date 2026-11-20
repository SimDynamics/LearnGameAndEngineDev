// Compile a genuine U6L11 private witness from the published original plus
// a candidate with only two editable assignment bodies. No student state.
#include "core/cpp_array_copy_move_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open original or candidate U6L11 source");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view source) {
    std::ofstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot create temporary array-copy witness");
    file.write(source.data(),static_cast<std::streamsize>(source.size()));
    if(!file)throw std::runtime_error("Cannot finish temporary array-copy witness");
}
}
int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error(
        "Usage: array-copy-move-generator ORIGINAL_CPP CANDIDATE_CPP PRIVATE_CPP");
    const auto original=read(argv[1]);
    const auto candidate=read(argv[2]);
    const lgaed::curriculum::CppArrayCopyMovePolicy spec{
        original,"Tramo","datos_","cantidad_","otra","otra",
        "cantidad","acceso","configurar","leer","escribir","vaciar","sumar"};
    if(!lgaed::curriculum::cpp_array_copy_move_matches(spec,candidate))
        throw std::runtime_error(
            "Changes outside the two published Tramo operator= bodies were rejected");
    const auto private_source=
        lgaed::curriculum::cpp_array_copy_move_private_driver(spec,candidate);
    if(!private_source)throw std::runtime_error("Cannot bind private array driver");
    write(argv[3],*private_source);
    std::cout<<"PASS unchanged original U6L11 except two operator bodies\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL U6L11 protected source: "<<error.what()<<'\n';
    return 1;
}
