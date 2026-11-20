// A standalone authoring bridge used by GCC, Clang and MSVC QA.
// It never edits a published source or a student's saved program.
#include "core/cpp_independent_copy_assignment_check.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed::curriculum;
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read protected U6L7 file");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view content) {
    std::ofstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open private probe output");
    file.write(content.data(),static_cast<std::streamsize>(content.size()));
    if(!file)throw std::runtime_error("Cannot finish private probe output");
}
}

int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error(
        "Usage: copy-assignment-generator ORIGINAL_CPP CANDIDATE_CPP PROBE_CPP");
    const auto original=read(argv[1]);
    const auto candidate=read(argv[2]);
    const CppIndependentCopyAssignmentPolicy spec{
        original,"Celda","dato_","otra","hay","valor","acceso",
        "poner","cambiar","vaciar"};
    if(!cpp_independent_copy_matches(spec,candidate))
        throw std::runtime_error(
            "Rejected: class declarations, member/main or original operator signature changed");
    const auto driver=cpp_independent_copy_probe_source(spec,candidate);
    if(!driver)throw std::runtime_error("Cannot generate sealed independent test");
    write(argv[3],*driver);
    std::cout<<"PASS original U6L7 C++ source outside operator= unchanged\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL independent copy authoring: "<<error.what()<<'\n';
    return 1;
}
