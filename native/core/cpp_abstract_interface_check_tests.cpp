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
void source_declaration_regression(
        const lgaed::curriculum::CppAbstractInterfacePolicy& original) {
    namespace check=lgaed::curriculum::cpp_abstract_interface_detail;
    if(!check::valid(original))
        throw std::runtime_error("Original interface lost its real pure virtual declarations");
    const auto invalid=[&](std::string_view before,std::string_view after,
                           std::string_view reason) {
        auto altered=original;
        const auto pos=altered.original_source.find(before);
        if(pos==std::string::npos)
            throw std::runtime_error("The original C++ declaration fixture changed");
        altered.original_source.replace(pos,before.size(),after);
        if(check::valid(altered)||
           lgaed::curriculum::cpp_abstract_interface_matches(
               altered,altered.original_source))
            throw std::runtime_error(std::string(reason));
    };
    // All old substrings remain in a COMMENT or a literal. A raw substring
    // lookup would credit a missing virtual/pure declaration.
    invalid("virtual ~Muestra() = default;",
            "~Muestra() = default; // virtual ~Muestra() = default;",
            "A comment impersonated the virtual base destructor");
    invalid("virtual int numero() const noexcept = 0;",
            "virtual int numero() const noexcept;\n"
            "    const char* note = R\"fake(virtual int numero() const noexcept = 0;)fake\";",
            "A raw literal impersonated a pure virtual number getter");
    invalid("virtual void escribir(std::ostream& salida) const = 0;",
            "virtual void escribir(std::ostream& salida) const; "
            "/* virtual void escribir(std::ostream& salida) const = 0; */",
            "A comment impersonated the pure virtual stream interface");
    invalid("int numero() const noexcept override { return numero_; }",
            "int numero_aux() const noexcept { return numero_; } "
            "// int numero() const noexcept override",
            "A getter in the second class impersonated the first class getter");
    invalid("int codigo() const noexcept { return codigo_; }",
            "int codigo_aux() const noexcept { return codigo_; } "
            "// int codigo() const noexcept",
            "A commented code getter impersonated the real identified getter");
    // The base-class declaration belongs to the global lesson type.
    // A class with the same name in a namespace must neither replace nor
    // make the real global class ambiguous.
    {
        const auto extra=std::string(
            "namespace LgaedOther { class Muestra { "
            "virtual ~Muestra() = default; }; }\n");
        const auto only_nested=check::direct_class_declaration(
            extra,"Muestra","virtual ~Muestra() = default;");
        if(only_nested)
            throw std::runtime_error(
                "Namespaced Muestra impersonated the required global interface");
        auto documented=original;
        documented.original_source=extra+original.original_source;
        if(!check::valid(documented))
            throw std::runtime_error(
                "Unrelated namespaced Muestra displaced the real base class");
        auto missing_global=original;
        // Remove the entire global base declaration rather than merely
        // changing its name: no real curricular Muestra may remain in scope.
        const auto name=missing_global.original_source.find("\nclass Muestra {");
        const auto closing=name==std::string::npos?std::string::npos:
            missing_global.original_source.find("\n};",name);
        if(name==std::string::npos||closing==std::string::npos)
            throw std::runtime_error("Missing original global Muestra declaration");
        missing_global.original_source.erase(name,closing+3-name);
        missing_global.original_source+=extra;
        const auto scan=lgaed::curriculum::cpp_class_structure::lexical(
            missing_global.original_source);
        if(!scan)throw std::runtime_error("Cannot tokenize missing global Muestra fixture");
        const auto owner=lgaed::curriculum::cpp_class_structure::defined_class(
            *scan,"Muestra");
        if(owner) {
            std::string witnessed;
            for(std::size_t i=0;i+1<scan->size();++i)
                if((*scan)[i]=="id:class"&&(*scan)[i+1]=="id:Muestra")
                    witnessed+="class Muestra at token "+std::to_string(i)+
                        " (open="+std::to_string(owner->opening)+"); ";
            throw std::runtime_error(
                "A namespaced class was falsely global: "+witnessed);
        }
        if(check::valid(missing_global))
            throw std::runtime_error(
                "Namespaced class authenticated a missing global Muestra owner");
    }
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
    source_declaration_regression(policy);
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
