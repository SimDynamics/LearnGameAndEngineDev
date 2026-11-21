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
    // The protected original cannot borrow a virtual method or inheritance
    // from a comment, even when the editable function signatures stay intact.
    const auto reject_original=[&](std::string modified,std::string_view why) {
        auto falsified=spec;
        falsified.editable.original_source=std::move(modified);
        if(lgaed::curriculum::cpp_virtual_lifetime_detail::valid(falsified))
            throw std::runtime_error(std::string(why));
    };
    {
        auto changed=original;
        const auto declaration="virtual void escribir(std::ostream& salida) const";
        const auto at=changed.find(declaration);
        if(at==changed.npos)throw std::runtime_error("Original virtual method missing");
        changed.replace(at,std::string_view(declaration).size(),
                        "void escribir(std::ostream& salida) const");
        reject_original("// "+std::string(declaration)+"\n"+changed,
                        "Comment faked a base virtual method");
    }
    {
        auto changed=original;
        const auto declaration="class MedidaIdentificada : public Medida {";
        // The documented variant deliberately has the *same text* in a
        // preceding comment. Edit only the actual line-leading definition,
        // otherwise this negative QA accidentally leaves the real base intact.
        const auto at=changed.find("\n"+std::string(declaration));
        if(at==changed.npos)throw std::runtime_error("Original derived class missing");
        changed.replace(at+1,std::string_view(declaration).size(),
                        "class MedidaIdentificada {");
        const auto documented="// "+std::string(declaration)+"\n"+changed;
        const auto lexed=lgaed::curriculum::cpp_class_structure::lexical(documented);
        if(!lexed)throw std::runtime_error("Cannot tokenize documented derived class");
        const auto inherited=lgaed::curriculum::cpp_class_structure::defined_class(
            *lexed,"MedidaIdentificada","Medida");
        if(inherited) {
            std::string tokens;
            for(std::size_t i=0;i+1<lexed->size();++i) {
                if((*lexed)[i]!="id:class"||
                   (*lexed)[i+1]!="id:MedidaIdentificada")continue;
                for(std::size_t j=i;j<lexed->size()&&j<i+8;++j)
                    tokens+=(*lexed)[j]+" ";
                tokens+=" | ";
            }
            throw std::runtime_error(
                "Tokenizer counted commented public inheritance as real: "+tokens);
        }
        reject_original(documented,"Comment faked public inheritance");
    }
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
