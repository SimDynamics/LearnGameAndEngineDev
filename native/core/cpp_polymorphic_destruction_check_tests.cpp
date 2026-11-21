// Generate an independent polymorphic-lifetime probe from a current C++
// candidate while proving all original declarations/main are byte-identical.
#include "core/cpp_polymorphic_destruction_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read original/candidate source");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, std::string_view value) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot create private C++ probe");
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!file) throw std::runtime_error("Cannot write private C++ probe");
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 4)
        throw std::runtime_error(
            "Usage: polymorphic-generator ORIGINAL_CPP CANDIDATE_CPP PROBE_CPP");
    const auto original = read(argv[1]);
    const auto candidate = read(argv[2]);
    using namespace lgaed::curriculum;
    const CppPolymorphicDestructionPolicy spec{
        {original, {
            {"Medida::~Medida() noexcept", true},
            {"MedidaIdentificada::~MedidaIdentificada() noexcept", true},
            {"std::unique_ptr<Medida> crear_medida(int numero, int codigo, Ciclo& ciclo)",
             false},
        }},
        "Medida", "MedidaIdentificada", "Ciclo", "Ficha", "ficha_",
        "crear_medida", "escribir",
        "bases_vivas", "derivadas_vivas", "fichas_vivas",
        "bases_destruidas", "derivadas_destruidas", "fichas_destruidas",
    };
    // Source metadata cannot borrow a counter from a comment.
    const auto at=original.find("    int bases_vivas = 0;");
    if(at==std::string::npos)
        throw std::runtime_error("Authentic Ciclo counter disappeared");
    auto fake_original=original;
    fake_original.insert(at,"    // int autor_solo_en_comentario = 0;\n");
    auto bad_authoring=spec;
    bad_authoring.editable.original_source=fake_original;
    bad_authoring.base_alive="autor_solo_en_comentario";
    if(cpp_polymorphic_destruction_detail::valid(bad_authoring))
        throw std::runtime_error("A comment impersonated a Ciclo counter");
    if (!cpp_polymorphic_destruction_matches(spec, candidate))
        throw std::runtime_error(
            "The submitted source changed protected class, main, "
            "signatures or unapproved code");
    const auto driver=cpp_polymorphic_destruction_private_driver(spec,candidate);
    if (!driver)
        throw std::runtime_error("Cannot generate genuine private proof");
    write(argv[3],*driver);
    std::cout<<"PASS authenticated polymorphic U8L5 three-body source\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL U8L5 source preservation: "<<error.what()<<'\n';
    return 1;
}
