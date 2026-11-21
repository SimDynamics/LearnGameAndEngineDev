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
        const auto at=source.find("\n"+text);
        if(at==source.npos)throw std::runtime_error("Original public subtype missing");
        source.replace(at+1,text.size(),"class MuestraSimple : private Muestra {");
        reject("// "+text+"\n"+source,
               "A comment impersonated the actual public inheritance");
    }
    {
        auto source=original;
        const std::string text=
            "using Muestras = std::vector<std::unique_ptr<Muestra>>;";
        const auto at=source.find("\n"+text);
        if(at==source.npos)throw std::runtime_error("Original owner vector missing");
        source.replace(at+1,text.size(),
            "using Muestras = std::vector<std::unique_ptr<MuestraSimple>>;");
        reject("// "+text+"\n"+source,
               "A comment impersonated the actual polymorphic owner alias");
    }
    {
        // A global vector of the wrong owner cannot borrow a correctly
        // spelled alias from a local helper or unrelated namespace. The old
        // unscoped token search admitted both of these authoring forgeries.
        const std::string alias=
            "using Muestras = std::vector<std::unique_ptr<Muestra>>;";
        const auto wrong=
            "using Muestras = std::vector<std::unique_ptr<MuestraSimple>>;";
        auto forged=original;
        const auto at=forged.find("\n"+alias);
        if(at==std::string::npos)
            throw std::runtime_error("Published global Muestras alias missing");
        forged.replace(at+1,alias.size(),wrong);
        reject(forged+
               "\nvoid auxiliar_no_relacionado() { "+alias+" }\n",
               "A local owner alias impersonated the wrong global vector");
        reject(forged+
               "\nnamespace OtroAlcance { "+alias+" }\n",
               "A namespace owner alias impersonated the wrong global vector");
        auto duplicated=original;
        duplicated+="\n"+alias+"\n";
        reject(duplicated,"Two global aliases were accepted as one owner");
    }
    // A class nested inside another class, function or namespace cannot
    // impersonate the global polymorphic base declared by the curriculum.
    // These are deliberately non-compilable metadata attacks: do not run
    // them as student C++ or modify any published source file.
    {
        const std::string header="class Muestra {";
        const auto start=original.find(header);
        const auto end=start==std::string::npos?std::string::npos:
            original.find("\n};",start);
        if(end==std::string::npos)
            throw std::runtime_error("Cannot isolate original Muestra definition");
        const auto declaration=original.substr(start,end+3-start);
        auto forged=original;
        forged.replace(start,declaration.size(),"class LgaedDecoy {};");
        reject(forged+"\nvoid lgaed_local() {\n"+declaration+"\n}\n",
               "A function-local class impersonated the global Muestra interface");
        reject(forged+"\nstruct LgaedHolder {\n"+declaration+"\n};\n",
               "A nested member class impersonated the global Muestra interface");
        reject(forged+"\nnamespace LgaedOther {\n"+declaration+"\n}\n",
               "A namespace-local class impersonated the global Muestra interface");
        auto permitted=policy;
        permitted.original_source=original+
            "\nnamespace LgaedOther { class Muestra {}; }\n";
        if(!lgaed::curriculum::cpp_interface_collection_detail::valid(permitted))
            throw std::runtime_error(
                "An unrelated nested type rejected the valid global Muestra");
    }
    // The compiled program has no usable global polymorphic owner when
    // its only correct alias is disabled by preprocessing. The old lexical
    // search counted tokens beneath #if 0 as if they were compiled.
    {
        const std::string alias=
            "using Muestras = std::vector<std::unique_ptr<Muestra>>;";
        auto forged=policy;
        const auto position=forged.original_source.find("\n"+alias);
        if(position==std::string::npos)
            throw std::runtime_error("Authentic global Muestras alias missing");
        forged.original_source.erase(position+1,alias.size());
        forged.original_source+="\n#if 0\n"+alias+"\n#endif\n";
        if(lgaed::curriculum::cpp_interface_collection_detail::valid(forged))
            throw std::runtime_error(
                "An alias in a disabled preprocessor branch authenticated an owner");
        auto documented=policy;
        documented.original_source=
            "// #if 0\n// #define Muestras Fake\n"+
            documented.original_source;
        if(!lgaed::curriculum::cpp_interface_collection_detail::valid(documented))
            throw std::runtime_error(
                "Inert preprocessing words in a comment rejected a real owner");
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
