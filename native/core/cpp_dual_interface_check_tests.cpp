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
    // Invalid author metadata must not borrow virtual declarations,
    // ordered inheritance or protected int fields from a comment.
    const auto reject_source=[&](std::string source,std::string_view why) {
        auto forged=policy;
        forged.original_source=std::move(source);
        if(lgaed::curriculum::cpp_dual_interface_detail::valid(forged))
            throw std::runtime_error(std::string(why));
    };
    {
        auto source=original;
        const std::string text="virtual int valor() const = 0;";
        const auto at=source.find("\n    "+text);
        if(at==source.npos)throw std::runtime_error("Original value interface missing");
        source.replace(at+5,text.size(),"virtual int valor() const;");
        reject_source("// "+text+"\n"+source,
                      "A commented pure virtual function replaced its interface");
    }
    {
        auto source=original;
        const std::string text=
            "class Muestra : public ConValor, public ConInforme {";
        const auto at=source.find("\n"+text);
        if(at==source.npos)throw std::runtime_error("Original multiple base missing");
        source.replace(at+1,text.size(),
                       "class Muestra : private ConValor, public ConInforme {");
        reject_source("// "+text+"\n"+source,
                      "A comment impersonated public multiple inheritance");
    }
    {
        auto source=original;
        const std::string text="int codigo_;";
        const auto at=source.find(text);
        if(at==source.npos)throw std::runtime_error("Original code member missing");
        source.replace(at,text.size(),"long codigo_; // int codigo_;");
        reject_source(source,"A comment impersonated a direct int member");
    }
    // Only a translation-unit class can satisfy the two-base authoring
    // contract. Token-matched types in a function or another class have a
    // different owner, even when they list the two public bases verbatim.
    {
        const auto require_nested_rejected=[&](std::string_view sample,
                                                std::string_view reason) {
            const auto lexical=lgaed::curriculum::cpp_class_structure::lexical(
                sample);
            if(!lexical||lgaed::curriculum::cpp_class_structure::
                    defined_public_bases(*lexical,"Muestra",
                                         {"ConValor","ConInforme"}))
                throw std::runtime_error(std::string(reason));
        };
        require_nested_rejected(
            "void local() { class Muestra : public ConValor, "
            "public ConInforme {}; }",
            "Function-local dual inheritance impersonated the global Muestra");
        require_nested_rejected(
            "class Holder { class Muestra : public ConValor, "
            "public ConInforme {}; };",
            "Nested dual inheritance impersonated the global Muestra");
        require_nested_rejected(
            "namespace Other { class Muestra : public ConValor, "
            "public ConInforme {}; }",
            "Namespaced dual inheritance impersonated the global Muestra");
        auto permitted=policy;
        permitted.original_source=original+
            "\nnamespace LgaedOther { class Muestra : public ConValor, "
            "public ConInforme {}; }\n";
        if(!lgaed::curriculum::cpp_dual_interface_detail::valid(permitted))
            throw std::runtime_error(
                "An unrelated namespaced Muestra blocked the true dual owner");
    }
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
