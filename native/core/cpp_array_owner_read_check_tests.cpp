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
    // These temporary metadata attacks never execute the invalid program.
    // A namespaced owner, a nested owner or a disabled declaration cannot
    // replace one of the authenticated global Lote fields.
    const auto reject_original=[&](std::string source,std::string_view reason) {
        auto forged=spec;
        forged.original_source=std::move(source);
        if(lgaed::curriculum::cpp_array_owner_read_detail::
                original_fields(forged))
            throw std::runtime_error(std::string(reason));
    };
    {
        auto source=original;
        const std::string field="    std::unique_ptr<int[]> datos_{};";
        const auto at=source.find("\n"+field);
        if(at==std::string::npos)
            throw std::runtime_error("Original Lote owner field missing");
        source.replace(at+1,field.size(),"    std::unique_ptr<int[]> otro_{};");
        source+="\nnamespace LgaedForeign { class Lote { "
                "std::unique_ptr<int[]> datos_{}; int cantidad_ = 0; }; }\n";
        reject_original(source,
                        "Namespaced Lote impersonated the global owner field");
    }
    {
        auto source=original;
        const std::string field="    int cantidad_ = 0;";
        const auto at=source.find("\n"+field);
        if(at==std::string::npos)
            throw std::runtime_error("Original Lote count field missing");
        source.replace(at+1,field.size(),"    int otra_cantidad_ = 0;");
        source+="\nclass LgaedWrapper { class Lote { int cantidad_ = 0; }; };\n";
        reject_original(source,
                        "Nested Lote impersonated the global element count");
    }
    {
        auto source=original;
        const std::string field="    std::unique_ptr<int[]> datos_{};";
        const auto at=source.find("\n"+field);
        if(at==std::string::npos)
            throw std::runtime_error("Original Lote owner field missing");
        source.replace(at+1,field.size(),"    std::unique_ptr<int[]> otro_{};");
        source+="\n#if 0\n"+field+"\n#endif\n";
        reject_original(source,
                        "Preprocessor-disabled array field satisfied the contract");
    }
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
