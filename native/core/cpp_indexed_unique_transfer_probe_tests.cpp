#include "core/cpp_indexed_unique_transfer_probe.hpp"

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
void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    require(stream.good(),"Cannot read actual lesson C++ source");
    return {std::istreambuf_iterator<char>{stream},std::istreambuf_iterator<char>{}};
}
void write(const std::filesystem::path& path,std::string_view text) {
    std::ofstream stream(path,std::ios::binary);
    require(stream.good(),"Cannot create private C++ probe source");
    stream.write(text.data(),static_cast<std::streamsize>(text.size()));
    require(stream.good(),"Cannot finish private C++ probe source");
}
struct Version {const char* name;const char* body;bool accepts;};
}

int main(int argc,char** argv) try {
    require(argc==3,"Usage: probe-generator ORIGINAL_CPP OUTPUT_DIRECTORY");
    const auto original=read(argv[1]);
    const CppIndexedUniqueTransferProbe spec{
        {original,"bool Deposito::transferir(int origen, int destino)"},
        "Deposito","primero_","segundo_","transferir","poner","cambiar","vaciar"
    };
    const auto body=cpp_member_body_span(spec.member);
    require(body.has_value(),"Real U6L5 member signature or body disappeared");
    require(original.find("std::unique_ptr<int> primero_")!=std::string::npos &&
            original.find("std::unique_ptr<int> segundo_")!=std::string::npos &&
            original.find("int main()")!=std::string::npos,
            "The real U6L5 candidate lost its protected owners or client");
    const auto candidate=[&](std::string_view replacement) {
        return original.substr(0,body->begin)+std::string(replacement)+original.substr(body->end);
    };
    const std::array<Version,10> versions{{
        {"correct_move",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    elegir(destino) = std::move(elegir(origen));
    return true;
)cpp",true},
        {"correct_swap_reset",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    auto& desde = elegir(origen);
    auto& hacia = elegir(destino);
    hacia.swap(desde);
    desde.reset();
    return true;
)cpp",true},
        {"correct_release_reset",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino)
        return false;
    auto& desde = elegir(origen);
    if (!desde) return false;
    elegir(destino).reset(desde.release());
    return true;
)cpp",true},
        {"original_no_transfer",R"cpp(
    (void)origen;
    (void)destino;
    return false;
)cpp",false},
        {"copy_instead_of_move",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    auto& desde = elegir(origen);
    auto& hacia = elegir(destino);
    hacia = std::make_unique<int>(*desde);
    desde.reset();
    return true;
)cpp",false},
        {"swap_without_emptying",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    elegir(origen).swap(elegir(destino));
    return true;
)cpp",false},
        {"damages_empty_source_destination",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino)
        return false;
    auto& desde=elegir(origen);
    auto& hacia=elegir(destino);
    if (!desde) { hacia.reset(); return false; }
    hacia=std::move(desde);
    return true;
)cpp",false},
        {"same_value_without_identity",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    auto& desde=elegir(origen);
    auto& hacia=elegir(destino);
    if (!hacia) return false;
    const int value=*desde;
    desde.reset();
    *hacia=value;
    return true;
)cpp",false},
        {"correct_transfer_extra_stdout",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    elegir(destino) = std::move(elegir(origen));
    std::fputs("unexpected output from transfer", stdout);
    return true;
)cpp",false},
        {"equal_value_hidden_copy",R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) || origen == destino ||
        !elegir(origen)) return false;
    auto& desde = elegir(origen);
    auto& hacia = elegir(destino);
    if (hacia && *desde == *hacia) {
        hacia = std::make_unique<int>(*desde);
        desde.reset();
        return true;
    }
    hacia = std::move(desde);
    return true;
)cpp",false}
    }};
    std::filesystem::create_directories(argv[2]);
    for(const auto& version:versions) {
        const auto edited=candidate(version.body);
        require(cpp_member_body_preserves_source(spec.member,edited),
                "A legal body-only edit was refused by the source guard");
        const auto probe=cpp_indexed_unique_transfer_probe_source(spec,edited);
        require(probe.has_value(),"Cannot generate an independent guarded probe");
        const auto path=std::filesystem::path(argv[2])/(std::string(version.name)+".cpp");
        write(path,*probe);
        std::cout<<version.name<<' '<<(version.accepts?"pass":"reject")<<'\n';
    }
    for(const auto& attacked:{
            candidate("\n#define private public\nreturn false;\n"),
            candidate("\nreturn false; }\nvoid extra(){}\n"),
            candidate("\nreturn false; %>\nint extra() { return 7; }\n<%\n"),
            candidate("\nreturn false;\\\nreturn true;\n"),
            original+"\nint forged_owner=1;\n"})
        require(!cpp_indexed_unique_transfer_probe_source(spec,attacked),
                "A forged method/owner escaped the source preservation guard");
    // A student method must not end the trusted process early, forge the
    // completion line or alter the allocation witness during an execution.
    for(const auto& hostile:{
        candidate(R"cpp(
    std::fputs("PASS native independent two-owner transfer identity and cleanup\\n",stderr);
    std::exit(0);
)cpp"),
        candidate("auto terminate = &std::exit; terminate(0);"),
        candidate("::ExitProcess(0);"),
        candidate("std::longjmp(jump,1);"),
        candidate("lgaed_transfer_qa::Watch::active=false; return true;"),
        candidate("lgaed_transfer_qa::Watch::created=0; return true;"),
        candidate("auto p=std::malloc(8); std::free(p); return true;"),
        candidate("const auto marker=__COUNTER__; (void)marker; return true;"),
        candidate("using std::source_location; return false;"),
        candidate("asm(\"nop\"); return false;"),
        candidate("std::terminate();"),
        candidate("std::raise(9);"),
        candidate("::TerminateJobObject(job,0);"),
        candidate("std::system(\"echo unsafe\"); return false;"),
        candidate("auto run=&std::system; (void)run; return false;"),
        candidate("::CreateProcessW(nullptr,nullptr,nullptr,nullptr,false,0,nullptr,nullptr,nullptr,nullptr);"),
        candidate("::SetStdHandle(0,nullptr); return false;"),
        candidate("auto output=std::freopen(\"log\",\"w\",stderr); (void)output;"),
        candidate("#include <cstdlib>\nreturn false;\n")
    })
        require(!cpp_indexed_unique_transfer_probe_source(spec,hostile),
                "An early exit, source-dependent shortcut or private counter spoof passed");
    require(cpp_indexed_unique_transfer_probe_source(spec,candidate(R"cpp(
    // std::exit(0), longjmp, std::system and std::terminate are comments only.
    const auto words=R"test(std::exit(0) lgaed_transfer_qa::Watch std::system CreateProcessW std::terminate)test";
    (void)words;
    return false;
)cpp")).has_value(),
        "Harmless source text mentioning a forbidden identifier was rejected");
    std::cout<<"PASS ten real-lesson independent transfer probes generated; source tampering refused.\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<"FAIL independent transfer probe generation: "<<e.what()<<'\n';
    return 1;
}
