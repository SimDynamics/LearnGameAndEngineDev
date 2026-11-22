// Generate a disposable semantic U8L10 driver from the student's three bodies.
#include "core/cpp_polymorphic_clone_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot read U8L10 original/candidate");
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& p,std::string_view value) {
    std::ofstream f(p,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot open U8L10 disposable witness");
    f.write(value.data(),static_cast<std::streamsize>(value.size()));
    if(!f)throw std::runtime_error("Cannot write U8L10 disposable witness");
}
void check_original_virtual_declarations(
        const lgaed::curriculum::CppPolymorphicClonePolicy& policy) {
    namespace guard=lgaed::curriculum::cpp_polymorphic_clone_detail;
    if(!guard::valid(policy))
        throw std::runtime_error("Original polymorphic clone source lost its real contracts");
    const auto falsify=[&](std::string_view old,std::string_view changed,
                           std::string_view reason) {
        auto forged=policy;
        const auto at=forged.original_source.find(old);
        if(at==std::string::npos)
            throw std::runtime_error("Missing original U8L10 lexical fixture");
        forged.original_source.replace(at,old.size(),changed);
        if(guard::valid(forged)||
           lgaed::curriculum::cpp_polymorphic_clone_matches(
               forged,forged.original_source))
            throw std::runtime_error(std::string(reason));
    };
    falsify("virtual ~Muestra() = default;",
            "~Muestra() = default; // virtual ~Muestra() = default;",
            "Quoted virtual destructor concealed a nonvirtual base");
    falsify("virtual std::unique_ptr<Muestra> clonar() const = 0;",
            "virtual std::unique_ptr<Muestra> clonar() const; "
            "/* virtual std::unique_ptr<Muestra> clonar() const = 0; */",
            "A comment pretended that clone remained abstract");
    falsify("virtual int numero() const noexcept = 0;",
            "virtual int numero() const noexcept;\n"
            "    const char* note = R\"info(virtual int numero() const noexcept = 0;)info\";",
            "A literal pretended that the number query remained abstract");
    falsify("virtual void cambiar_numero(int valor) noexcept = 0;",
            "virtual void cambiar_numero(int valor) noexcept; "
            "// virtual void cambiar_numero(int valor) noexcept = 0;",
            "A quoted source changed the required virtual mutation");
    falsify("virtual void escribir(std::ostream& salida) const = 0;",
            "virtual void escribir(std::ostream& salida) const; "
            "/* virtual void escribir(std::ostream& salida) const = 0; */",
            "A comment replaced the actual pure virtual reporting method");
    falsify("int numero() const noexcept override { return numero_; }",
            "int numero_aux() const noexcept { return numero_; } "
            "// int numero() const noexcept override",
            "Only the second class contained the genuine number getter");
    falsify("int codigo() const noexcept { return codigo_; }",
            "int codigo_aux() const noexcept { return codigo_; } "
            "// int codigo() const noexcept",
            "A quoted getter appeared without the concrete code query");
    // The last unprotected source prerequisite used a substring lookup.
    // Preserve only the real alias, not matching characters in comments,
    // raw literals, disabled code or inside an unrelated function.
    constexpr std::string_view alias="using Muestras = std::vector<std::unique_ptr<Muestra>>;";
    falsify(alias,
            "using Muestras = std::vector<Muestra*>; // using Muestras = std::vector<std::unique_ptr<Muestra>>;",
            "An alias mentioned in a comment passed as an owner collection");
    falsify(alias,
            "using Muestras = std::vector<Muestra*>;\n"
            "const char* note = R\"proof(using Muestras = std::vector<std::unique_ptr<Muestra>>;)proof\";",
            "A raw literal impersonated the required owning vector alias");
    falsify(alias,
            "#if 0\nusing Muestras = std::vector<std::unique_ptr<Muestra>>;\n#endif\n"
            "using Muestras = std::vector<Muestra*>;",
            "A disabled alias was mistaken for the compiled owning vector");
    falsify(alias,
            "using Muestras = std::vector<std::unique_ptr<Muestra>>;\nusing Muestras = std::vector<std::unique_ptr<Muestra>>;",
            "Two competing top-level aliases were accepted");
    auto formatted=policy;
    const auto at=formatted.original_source.find(alias);
    if(at==std::string::npos)
        throw std::runtime_error("The vector alias vanished from original QA");
    formatted.original_source.replace(at,alias.size(),
        "using  Muestras /* kept source note */=std::vector<std::unique_ptr<Muestra>>;");
    if(!guard::valid(formatted))
        throw std::runtime_error("Safe C++ whitespace/comment formatting of the alias was rejected");
    // The class gate used by abstract interfaces and cloning has one
    // global owner. A similarly named namespace-local type remains inert.
    {
        const std::string unrelated=
            "namespace LgaedOther { class Muestra { public: "
            "virtual ~Muestra() = default; }; }\n";
        auto documented=policy;
        documented.original_source=unrelated+policy.original_source;
        if(!guard::valid(documented))
            throw std::runtime_error(
                "A namespaced base concealed the valid global clone interface");
        auto missing=policy;
        const auto class_position=missing.original_source.find("\nclass Muestra {");
        const auto class_closing=class_position==std::string::npos?
            std::string::npos:missing.original_source.find("\n};",class_position);
        if(class_position==std::string::npos||class_closing==std::string::npos)
            throw std::runtime_error("Original global polymorphic base vanished");
        missing.original_source.erase(
            class_position,class_closing+3-class_position);
        missing.original_source+=unrelated;
        if(guard::valid(missing))
            throw std::runtime_error(
                "A namespaced base replaced the missing global cloning owner");
    }
}

}

int main(int argc,char** argv) try {
    if(argc!=4)throw std::runtime_error(
        "Usage: polymorphic-clone-generator ORIGINAL_CPP CANDIDATE_CPP WITNESS_CPP");
    const auto original=read(argv[1]),candidate=read(argv[2]);
    const lgaed::curriculum::CppPolymorphicClonePolicy policy{
        original,"Muestra","MuestraSimple","MuestraIdentificada",
        "Muestras","clonar","duplicar","numero","cambiar_numero",
        "escribir","codigo","origen","salida"
    };
    check_original_virtual_declarations(policy);
    if(!lgaed::curriculum::cpp_polymorphic_clone_matches(policy,candidate))
        throw std::runtime_error(
            "U8L10 candidate changed protected C++ or unsafe editable bodies");
    const auto generated=lgaed::curriculum::cpp_polymorphic_clone_private_driver(
        policy,candidate);
    if(!generated)throw std::runtime_error("Failed to build private U8L10 driver");
    write(argv[3],*generated);
    std::cout<<"PASS U8L10 original hierarchy preserved; three bodies edited\n";
    return 0;
}catch(const std::exception& e) {
    std::cerr<<"FAIL U8L10 source guard: "<<e.what()<<'\n';
    return 1;
}
