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
    // Synthetic authoring attacks only. These altered originals are never
    // compiled or published; a comment must not authenticate a missing
    // original virtual contract, public base or vector owner.
    const auto reject=[&](std::string source,std::string_view reason) {
        auto changed=policy;
        changed.original_source=std::move(source);
        if(lgaed::curriculum::cpp_interface_collection_detail::valid(changed))
            throw std::runtime_error(std::string(reason));
    };
    {
        auto source=original;
        const std::string text="virtual ~Muestra() = default;";
        const auto at=source.find(text);
        if(at==source.npos)throw std::runtime_error("Original virtual destructor missing");
        source.replace(at,text.size(),"~Muestra() = default;");
        reject("// "+text+"\n"+source,
               "A commented virtual destructor authenticated the interface");
    }
    {
        auto source=original;
        const std::string text="class MuestraSimple : public Muestra {";
        const auto at=source.find(text);
        if(at==source.npos)throw std::runtime_error("Original public subtype missing");
        source.replace(at,text.size(),"class MuestraSimple : private Muestra {");
        reject("// "+text+"\n"+source,
               "A comment impersonated the actual public inheritance");
    }
    {
        auto source=original;
        const std::string text=
            "using Muestras = std::vector<std::unique_ptr<Muestra>>;";
        const auto at=source.find(text);
        if(at==source.npos)throw std::runtime_error("Original owner vector missing");
        source.replace(at,text.size(),
            "using Muestras = std::vector<std::unique_ptr<MuestraSimple>>;");
        reject("// "+text+"\n"+source,
               "A comment impersonated the actual polymorphic owner alias");
    }
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
