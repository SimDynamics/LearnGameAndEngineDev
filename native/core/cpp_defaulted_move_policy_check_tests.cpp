#include "core/cpp_defaulted_move_policy_check.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed::curriculum;
void require(bool value,std::string_view what) {
    if(!value)throw std::runtime_error(std::string(what));
}
std::string read(const std::filesystem::path& name) {
    std::ifstream input(name,std::ios::binary);
    require(input.good(),"Cannot open original U6L6 source");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& name,std::string_view source) {
    std::ofstream output(name,std::ios::binary);
    require(output.good(),"Cannot create independent move-policy source");
    output.write(source.data(),static_cast<std::streamsize>(source.size()));
    require(output.good(),"Cannot finish independent move-policy source");
}
struct Variation {std::string_view name,insert;bool valid;};
}

int main(int argc,char** argv)try {
    require(argc==3,"Usage: defaulted-move-generator ORIGINAL_CPP QA_OUTPUT");
    const auto original=read(argv[1]);
    const CppDefaultedMovePolicy spec{
        original,"Ficha","dato_","poner","cambiar","vaciar"
    };
    const auto public_label=original.find("public:\n");
    require(public_label!=std::string::npos&&
            original.find("public:\n",public_label+1)==std::string::npos,
            "Published U6L6 class has no unique public authoring section");
    const auto edit=[&](std::string_view declarations) {
        auto result=original;
        result.insert(public_label+std::string_view("public:\n").size(),declarations);
        return result;
    };
    constexpr std::array<Variation,10> variations{{
        {"correct_defaulted",R"cpp(
    Ficha() = default;
    Ficha(Ficha&&) = default;
    Ficha& operator=(Ficha&&) = default;
)cpp",true},
        {"correct_reordered_named_noexcept",R"cpp(
    // Equivalent declarations in another order and with named parameters.
    Ficha& operator=(Ficha&& otra) noexcept = default;
    Ficha()=default;
    Ficha(Ficha&& otra) noexcept=default;
)cpp",true},
        {"missing_move_assignment",R"cpp(
    Ficha()=default;
    Ficha(Ficha&&)=default;
)cpp",false},
        {"manual_move_assignment",R"cpp(
    Ficha()=default;
    Ficha(Ficha&&)=default;
    Ficha& operator=(Ficha&& otra) { return *this; }
)cpp",false},
        {"duplicate_defaulted_move",R"cpp(
    Ficha()=default;
    Ficha(Ficha&&)=default;
    Ficha& operator=(Ficha&&)=default;
    Ficha& operator=(Ficha&&)=default;
)cpp",false},
        {"removed_default_constructor",R"cpp(
    Ficha()=delete;
    Ficha(Ficha&&)=default;
    Ficha& operator=(Ficha&&)=default;
)cpp",false},
        {"noexcept_false_move",R"cpp(
    Ficha()=default;
    Ficha(Ficha&&) noexcept(false)=default;
    Ficha& operator=(Ficha&&)=default;
)cpp",false},
        {"bad_return_type",R"cpp(
    Ficha()=default;
    Ficha(Ficha&&)=default;
    void operator=(Ficha&&)=default;
)cpp",false},
        {"invalid_copy_move",R"cpp(
    Ficha()=default;
    Ficha(const Ficha&)=default;
    Ficha& operator=(Ficha&&)=default;
)cpp",false},
        {"new_preprocessor_override",R"cpp(
    #define private public
    Ficha()=default;
    Ficha(Ficha&&)=default;
    Ficha& operator=(Ficha&&)=default;
)cpp",false}
    }};
    std::filesystem::create_directories(argv[2]);
    for(const auto& variation:variations) {
        const auto candidate=edit(variation.insert);
        const bool accepted=cpp_defaulted_move_policy_matches(spec,candidate);
        require(accepted==variation.valid,
            std::string("Incorrect structural verdict: ")+std::string(variation.name));
        if(!accepted)continue;
        for(const auto& [mode,extension]:
                std::array<std::pair<CppDefaultedMoveProbe,std::string_view>,3>{{
                   {CppDefaultedMoveProbe::success,"runtime"},
                   {CppDefaultedMoveProbe::rejected_copy_constructor,"copy-ctor"},
                   {CppDefaultedMoveProbe::rejected_copy_assignment,"copy-assign"}}) {
            const auto driver=cpp_defaulted_move_policy_probe_source(spec,candidate,mode);
            require(driver.has_value(),"Private driver unavailable for valid source");
            write(std::filesystem::path(argv[2])/
                (std::string(variation.name)+"-"+std::string(extension)+".cpp"),*driver);
        }
        std::cout<<variation.name<<" PASS\n";
    }
    const auto good=edit(variations.front().insert);
    auto changed=good;
    const auto member=changed.find("std::make_unique<int>(4)");
    require(member!=std::string::npos,"Published unique_ptr initializer vanished");
    changed.replace(member,std::string_view("std::make_unique<int>(4)").size(),
                    "std::make_unique<int>(5)");
    require(!cpp_defaulted_move_policy_matches(spec,changed),
        "A changed initial owner value was accepted");
    changed=good;
    const auto copy=changed.find("Ficha(const Ficha&) = delete;");
    require(copy!=std::string::npos,"Published deleted-copy declaration vanished");
    changed.replace(copy,std::string_view("Ficha(const Ficha&) = delete;").size(),
                    "Ficha(const Ficha&) = default;");
    require(!cpp_defaulted_move_policy_matches(spec,changed),
        "Removing the copy prohibition passed");
    changed=good;
    const auto client=changed.find("std::cout << \"Inicio:\"");
    require(client!=std::string::npos,"Published main changed");
    changed.replace(client,std::string_view("std::cout << \"Inicio:\"").size(),
                    "std::cout << \"Fingido:\"");
    require(!cpp_defaulted_move_policy_matches(spec,changed),
        "Replacing supplied main reports passed");
    changed=good;
    const auto position=changed.find("private:\n");
    require(position!=std::string::npos,"Original private region missing");
    changed.replace(position,std::string_view("private:\n").size(),"public:\n");
    require(!cpp_defaulted_move_policy_matches(spec,changed),
        "Changing private member visibility passed");
    const auto private_insert=original.find("private:\n");
    require(private_insert!=std::string::npos,"Original privacy marker missing");
    changed=original;
    changed.insert(private_insert+std::string_view("private:\n").size(),
                  variations.front().insert);
    require(!cpp_defaulted_move_policy_matches(spec,changed),
        "Defining generated public policy privately passed");
    std::cout<<"PASS genuine U6L6 defaulted move policies: two equivalent declarations, "
        "eight rejected declarations, protected members/copies/main and private "
        "runtime/compilation witnesses generated.\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL defaulted move authoring: "<<error.what()<<'\n';
    return 1;
}
