// A read-only, compiler-backed bridge for the generic U6L8 private move probe.
// The real curriculum and saved profiles are never modified by this utility.
#include "core/cpp_move_assignment_invariant_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed::curriculum;
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read U6L8 original/candidate");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& file,std::string_view content) {
    std::ofstream output(file,std::ios::binary);
    if(!output)throw std::runtime_error("Cannot create private move probe");
    output.write(content.data(),static_cast<std::streamsize>(content.size()));
    if(!output)throw std::runtime_error("Cannot write private move probe");
}
}
int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error(
        "Usage: move-invariant-generator ORIGINAL_CPP CANDIDATE_CPP PROBE_CPP");
    const auto original=read(argv[1]);
    const auto candidate=read(argv[2]);
    const CppMoveAssignmentInvariantPolicy p{
        original,"Muestra","dato_","etiqueta_","otra",
        "hay","valor","etiqueta","acceso","coherente",
        "poner","cambiar","reetiquetar","vaciar"};
    if(!cpp_move_invariant_matches(p,candidate))
        throw std::runtime_error(
            "Protected source was changed, or original move member is unavailable");
    const auto probe=cpp_move_invariant_private_driver(p,candidate);
    if(!probe)throw std::runtime_error("Cannot bind independent move witness");
    write(argv[3],*probe);
    std::cout<<"PASS U6L8 private move driver preserves original source\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL independent move authoring: "<<error.what()<<'\n';
    return 1;
}
