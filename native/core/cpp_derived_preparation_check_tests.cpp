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
    // A missing direct Balance counter must not be supplied by prose, and
    // a comment cannot turn a non-derived class into a public subtype.
    const auto reject_original=[&](std::string modified,std::string_view why) {
        auto falsified=policy;
        falsified.editable.original_source=std::move(modified);
        if(lgaed::curriculum::cpp_derived_preparation_detail::valid(falsified))
            throw std::runtime_error(std::string(why));
    };
    {
        auto changed=original;
        const auto declaration="int recursos_vivos = 0;";
        const auto at=changed.find(declaration);
        if(at==changed.npos)throw std::runtime_error("Original Balance field missing");
        changed.replace(at,std::string_view(declaration).size(),
                        "int otros_recursos = 0; // int recursos_vivos = 0;");
        reject_original(changed,"A comment impersonated a direct Balance field");
    }
    {
        auto changed=original;
        const auto declaration="class MedidaIdentificada : public Medida {";
        const auto at=changed.find(declaration);
        if(at==changed.npos)throw std::runtime_error("Original public subtype missing");
        changed.replace(at,std::string_view(declaration).size(),
                        "class MedidaIdentificada {");
        reject_original("// "+std::string(declaration)+"\n"+changed,
                        "A comment impersonated derived inheritance");
    }
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
