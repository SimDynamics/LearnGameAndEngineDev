// Compile one private U8L8 proof from the exact student-owned four bodies.
// This executable only writes its output into a disposable QA file.
#include "core/cpp_abstract_interface_check.hpp"

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
    if(!input)throw std::runtime_error("Cannot read original or candidate U8L8 source");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view data) {
    std::ofstream output(path,std::ios::binary);
    if(!output)throw std::runtime_error("Cannot create U8L8 semantic driver");
    output.write(data.data(),static_cast<std::streamsize>(data.size()));
    if(!output)throw std::runtime_error("Cannot write U8L8 semantic driver");
}
void lexical_inline_owner_regression() {
    using lgaed::curriculum::cpp_abstract_interface_detail::inline_body;
    constexpr std::string_view signature=
        "void escribir(std::ostream& salida) const override";
    // The apparent class and method in this literal are NOT definitions.
    // A tutorial may quote the exact signature without granting it ownership.
    const std::string source=R"lgaed(
const char* example=R"comment(
class MuestraSimple : public Muestra {
    void escribir(std::ostream& salida) const override { fake(); }
};
)comment";
/* class MuestraSimple : public Muestra {
    void escribir(std::ostream& salida) const override { fake(); }
}; */
class MuestraSimple : public Muestra /* author note */ {
public:
    /* A second signature in documentation is not an overloaded method:
       void escribir(std::ostream& salida) const override { fake(); } */
    void escribir(std::ostream& salida) const override /* unchanged note */ {
        const auto example = R"quoted( } { )quoted";
        salida << example;
    }
};
)lgaed";
    const auto actual=inline_body(source,"MuestraSimple","Muestra",signature);
    if(!actual||source.substr(actual->begin,actual->end-actual->begin).find(
            "salida << example;")==std::string::npos)
        throw std::runtime_error(
            "A quoted/commented inline definition stole the real C++ owner");
    const auto duplicated=source+R"lgaed(
class MuestraSimple : public Muestra {
public:
    void escribir(std::ostream& salida) const override { salida << 1; }
};
)lgaed";
    if(inline_body(duplicated,"MuestraSimple","Muestra",signature))
        throw std::runtime_error("Two real inline classes were treated as one owner");
    const auto class_start=source.rfind("\nclass MuestraSimple : public Muestra");
    if(class_start==std::string::npos)
        throw std::runtime_error("Missing real class in inline scanner fixture");
    const auto quoted_only=source.substr(0,class_start);
    if(inline_body(quoted_only,"MuestraSimple","Muestra",signature))
        throw std::runtime_error("A quoted class acquired a fabricated method owner");
    const auto repeated=source.substr(0,source.rfind("\n};"))+
        R"lgaed(
    void escribir(std::ostream& salida) const override { salida << 2; }
};
)lgaed";
    if(inline_body(repeated,"MuestraSimple","Muestra",signature))
        throw std::runtime_error("A repeated real virtual method passed the source gate");

    // A nested helper may use the same virtual signature. Its body cannot
    // replace the one belonging directly to the enclosing concrete class.
    auto nested=source;
    const auto class_declaration=nested.rfind("\nclass MuestraSimple : public Muestra");
    const auto access=nested.find("public:\n",class_declaration);
    if(class_declaration==std::string::npos||access==std::string::npos)
        throw std::runtime_error("Missing class access in nested owner fixture");
    const auto insertion=access+std::string("public:\n").size();
    nested.insert(insertion,R"lgaed(
    class Inner : public Muestra {
    public:
        void escribir(std::ostream& salida) const override { salida << "nested"; }
    };
)lgaed");
    const auto direct=inline_body(nested,"MuestraSimple","Muestra",signature);
    if(!direct||nested.substr(direct->begin,direct->end-direct->begin).find(
            "salida << example;")==std::string::npos)
        throw std::runtime_error("A nested virtual override hid the genuine outer method");
    // Locate the direct implementation after its actual body statement.
    // A nearer occurrence is deliberately present inside an author comment.
    const auto outer=nested.rfind(signature,
        nested.find("salida << example;"));
    if(outer==std::string::npos)
        throw std::runtime_error("Missing outer method in nested source fixture");
    nested.replace(outer,signature.size(),"void escribir_otro(std::ostream& salida) const");
    if(inline_body(nested,"MuestraSimple","Muestra",signature))
        throw std::runtime_error("A nested override impersonated the missing outer method");
}
} // namespace

int main(int argc,char** argv) try {
    lexical_inline_owner_regression();
    if(argc!=4)throw std::runtime_error(
        "Usage: abstract-interface-generator ORIGINAL_CPP CANDIDATE_CPP DRIVER_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppAbstractInterfacePolicy policy{
        original,
        "Muestra","MuestraSimple","MuestraIdentificada",
        "crear","presentar","numero","codigo","escribir",
        "salida","tipo","numero","codigo","dato"
    };
    if(!lgaed::curriculum::cpp_abstract_interface_matches(policy,candidate))
        throw std::runtime_error(
            "The candidate changed abstract declarations, getters, constructors "
            "or code outside four authored function bodies");
    const auto driver=lgaed::curriculum::cpp_abstract_interface_private_driver(
        policy,candidate);
    if(!driver)throw std::runtime_error("U8L8 source did not produce a private driver");
    write(argv[3],*driver);
    std::cout<<"PASS U8L8 authentic interface source and four editable function bodies\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL U8L8 abstract interface source: "<<error.what()<<'\n';
    return 1;
}
