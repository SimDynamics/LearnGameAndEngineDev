// Portable U8L9 source gate. It emits a disposable private C++ test
// from the actual candidate, never altering curricular files or user state.
#include "core/cpp_interface_collection_check.hpp"

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
    if(!input)throw std::runtime_error("Cannot open original or candidate C++");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view data) {
    std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("Cannot create private C++ source");
    out.write(data.data(),static_cast<std::streamsize>(data.size()));
    if(!out)throw std::runtime_error("Cannot write private C++ source");
}
} // namespace

int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: interface-collection-generator ORIGINAL CPP_CANDIDATE DRIVER");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppInterfaceCollectionPolicy policy{
        original,"Muestra","MuestraSimple","MuestraIdentificada",
        "Muestras","agregar","informar","escribir","numero",
        "destino","fuente","datos","salida",
        "Cantidad:",":","Numero:",";Codigo:"
    };
    if(!lgaed::curriculum::cpp_interface_collection_matches(policy,candidate))
        throw std::runtime_error(
            "Edited source modified an immutable declaration, factory, "
            "vector alias, main or an unowned function body");
    const auto driver=lgaed::curriculum::cpp_interface_collection_private_driver(
        policy,candidate);
    if(!driver)throw std::runtime_error("No U8L9 independent probe generated");
    write(argv[3],*driver);
    std::cout<<"PASS U8L9 two editable bodies, all supplied declarations intact\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL U8L9 interface collection: "<<error.what()<<'\n';
    return 1;
}
