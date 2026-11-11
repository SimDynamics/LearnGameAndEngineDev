#include "platform/windows/cpp_runner.hpp"
#include "core/lesson.hpp"
#include "core/order_source_text.hpp"
#include "core/rejection_diagnostic_facts.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "native_build_config.hpp"

#include <windows.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace lgaed::windows;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing runner artifact");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void successful(const CppRunResult& result) {
    require(result.error_utf8.empty(), "Adapter error: " + result.error_utf8);
    require(result.compilation.succeeded(), "Compilation: " +
        result.compilation.stdout_bytes + result.compilation.stderr_bytes);
    require(result.execution.succeeded(), "Execution failed");
}

std::string program(std::string_view message) {
    return "#include <iostream>\n\nint main() {\n    std::cout << \"" +
        std::string(message) + "\";\n    return 0;\n}\n";
}
std::string cpp23_contract() {
    return R"cpp(#include <expected>
#include <iostream>
#include <utility>
static_assert(__cplusplus == 202302L, "The educational compiler must expose C++23, not 20 or latest");
static_assert(_MSVC_LANG == 202302L, "The actual MSVC language mode must be C++23");
constexpr int selected_phase(int value) {
    if consteval { return value + 1; }
    else { return value + 2; }
}
static_assert(selected_phase(40) == 41);
enum class Answer : int { accepted = 42 };
inline std::expected<int, int> checked_value(int value, bool accept) {
    if (!accept) return std::unexpected(-9);
    return selected_phase(value);
}
)cpp";
}
} // namespace

int main(int argc, char** argv) {
    constexpr std::string_view diagnostic_test = "MSVC tool diagnostics preserve UTF-8 accents while program streams remain byte-exact";
    const bool diagnostic_only = argc == 2 && std::string_view(argv[1]) == "--diagnostic-utf8";
    const bool cpp23_only = argc == 2 && std::string_view(argv[1]) == "--cpp23";
    const bool wasi_only = argc == 2 && std::string_view(argv[1]) == "--wasi";
    if (argc > 1 && !diagnostic_only && !cpp23_only && !wasi_only) return 2;
    const lgaed::LaunchOptions options{lgaed::ProfileId::Codex,
        L"cpp-runner-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()), false};
    const auto environment = lgaed::resolve_windows_environment(options);
    lgaed::initialize_profile_directories(environment.paths);
    // Real metacharacters, spaces and Unicode are path data, never shell code.
    const auto projects = environment.paths.projects / L"Código & (espacio)";
    std::uint64_t sequence = 0;
    const auto request = [&](std::string source, std::string input = "") {
        CppRunRequest value;
        value.request_id = ++sequence;
        value.revision = 27 + sequence;
        value.source_utf8 = std::move(source);
        value.stdin_utf8 = std::move(input);
        value.projects_directory = projects;
        return value;
    };
    unsigned passed = 0;
    unsigned failed = 0;
    const auto test = [&](std::string_view name, const std::function<void()>& action) {
        if (diagnostic_only && name != diagnostic_test) return;
        if (cpp23_only && !name.starts_with("C++23 ")) return;
        if (wasi_only && !name.starts_with("WASI ")) return;
        if (!wasi_only && name.starts_with("WASI ")) return;
        try {
            action();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    test("WASI copied order keeps real C++ macro, raw-string and line-comment boundaries", [&] {
        using lgaed::curriculum::ordered_source_text;
        const std::vector<std::string> fragments{
            "#include <iostream>\n",
            "#define DOUBLE(x) \\\n",
            "    ((x) * 2)\n",
            "",
            "int main() {\n",
            "    const char* text = R\"tag(first\nsecond)tag\";\n",
            "    // The following output remains executable\n",
            "    std::cout << DOUBLE(3) << ':' << text;\n",
            "}\n"
        };
        const std::vector<std::size_t> original{0,1,2,3,4,5,6,7,8};
        const auto source=ordered_source_text(fragments,original);
        require(source.find("#define DOUBLE(x) \\\n    ((x) * 2)\n")!=std::string::npos&&
                source.find("first\nsecond")!=std::string::npos&&
                source.find("// The following output remains executable\n    std::cout")!=std::string::npos,
            "Reconstruction inserted or removed a physical C++ source boundary");
        auto current=request(source);
        current.backend=CppRunRequest::Backend::wasi;
        const auto result=run_cpp(current);
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                result.execution.succeeded()&&
                result.execution.stdout_bytes=="6:first\nsecond"&&
                result.execution.stderr_bytes.empty(),
            "Copied source did not preserve compiled C++23 meaning: "+
            result.error_utf8+result.compilation.stderr_bytes+result.execution.stderr_bytes);
        require(read(result.run_directory/L"main.cpp")==source,
            "Execution altered source indentation or physical newline bytes");
    });

    test("WASI C++23 four independent lesson inputs without unsigned program.exe", [&] {
        // Real current U2L9 page-10 program and its four authored cases.
        const std::string source=R"cpp(#include <iostream>
#include <string>
int main() {
    std::string nombre;
    int paq=0,piez=0;
    std::cin >> nombre >> paq >> piez;
    std::cout << nombre << ": " << paq*5 + piez;
    return 0;
}
)cpp";
        const std::vector<std::pair<std::string,std::string>> cases{
            {"Iris 4 2","Iris: 22"}, {"Leo 0 4","Leo: 4"},
            {"Vera 3 1","Vera: 16"}, {"Sol 20 4","Sol: 104"}};
        for(const auto& [input,expected]:cases){
            auto current=request(source,input);
            current.backend=CppRunRequest::Backend::wasi;
            const auto result=run_cpp(current);
            require(result.error_utf8.empty(),"WASI launcher: "+result.error_utf8);
            require(result.compilation.succeeded(),
                "WASI clang failed: "+result.compilation.stdout_bytes+result.compilation.stderr_bytes);
            require(result.execution.succeeded(),
                "WASI runtime failed: "+result.execution.stdout_bytes+result.execution.stderr_bytes);
            require(result.execution.stdout_bytes==expected && result.execution.stderr_bytes.empty(),
                "WASI did not preserve the authored stdout and stderr channels for "+input);
            require(result.target_type=="WASI_COMMAND" &&
                result.artifact==result.run_directory/L"program.wasm" &&
                std::filesystem::is_regular_file(result.artifact) &&
                !std::filesystem::exists(result.run_directory/L"program.exe"),
                "The WASI path accidentally created a new native executable");
            const auto captured=capture_cpp_execution(current,result);
            lgaed::curriculum::OutputCheck check;
            check.cases.push_back({input,{expected}});
            require(evaluate_cpp_output_check(check,captured,current.revision).verdict==
                lgaed::curriculum::OutputVerdict::correct,
                "A real valid WASI execution did not satisfy the exact student-output contract");
            check.cases.front().outputs={"incorrecto"};
            require(evaluate_cpp_output_check(check,captured,current.revision).verdict==
                lgaed::curriculum::OutputVerdict::incorrect,
                "The WASI backend fabricated a correct verdict from a wrong output");
        }
        auto invalid=request("int main( { return 0; }");
        invalid.backend=CppRunRequest::Backend::wasi;
        const auto rejected=run_cpp(invalid);
        require(rejected.error_utf8.empty() && !rejected.compilation.succeeded() &&
            rejected.execution.status==CppPhaseStatus::not_started,
            "WASI compilation errors must remain compiler diagnostics, never execution credit");
    });

    test("WASI C++23 exception handling and stream extraction execute with the pinned SDK", [&] {
        const auto code=R"cpp(#include <iostream>
#include <sstream>
#include <stdexcept>
int main() {
    std::istringstream input{"42 end"};
    int number=0;
    std::string label;
    if(!(input >> number >> label))return 7;
    try {
        if(number==42&&label=="end")throw std::runtime_error("detected");
        return 8;
    } catch(const std::runtime_error& error) {
        std::cout << error.what() << ':' << number;
    }
    return 0;
}
)cpp";
        auto current=request(code);
        current.backend=CppRunRequest::Backend::wasi;
        const auto result=run_cpp(current);
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                result.execution.succeeded()&&result.execution.stdout_bytes=="detected:42"&&
                result.execution.stderr_bytes.empty(),
            "Pinned WASI C++23 exception ABI or stream reading failed: "+
            result.error_utf8+result.compilation.stderr_bytes+result.execution.stderr_bytes);
    });

    test("WASI real U3L8 original report editors preserve lines, duplicates, format and all counters", [&] {
        using namespace lgaed::curriculum;
        constexpr auto path=
            "app/src/main/assets/content/es/programming/programming_practice_tools/"
            "comprobaciones_repetibles/lesson_automatizar_la_revision_sin_borrar_evidencia.json";
        const auto original=lgaed::windows::load_lesson(
            std::filesystem::path(lgaed::build::source_root)/path);
        require(original.pages.size()==9&&
                !original.pages[3].blocks[3].output_check&&
                !original.pages[6].blocks[3].output_check,
            "The published U3L8 original editors differ from the QA baseline");
        auto json=original.raw_json;
        constexpr std::string_view marker="  \"stateLayout\": {";
        const auto at=json.find(marker);
        require(at!=json.npos&&json.find(marker,at+1)==json.npos,
            "U3L8 has no unique canonical state-map root");
        json.insert(at,R"json("compilerRuntimeChecks": [
          {"activity":"block_0003_0003","kind":"observationCompleteV1"},
          {"activity":"block_0006_0003","kind":"observationDifferencesV1"}
        ],
        )json");
        const auto reviewed=lgaed::windows::parse_lesson_json(json,"es");
        const auto progress=lgaed::windows::parse_lesson_progress_metadata(json,"es");
        constexpr auto complete_main=R"cpp(int main() {
    std::string linea;
    int lineas{0};
    int validos{0}, coinciden{0}, difieren{0}, formato{0};
    while (std::getline(std::cin, linea)) {
        ++lineas;
        std::string caso;
        int esperado{0}, obtenido{0};
        if (!leer_registro(linea, caso, esperado, obtenido)) {
            ++formato;
            std::cerr << "Linea=" << lineas << ":FORMATO\n";
            continue;
        }
        ++validos;
        const bool coincide = esperado == obtenido;
        if (coincide) ++coinciden;
        else ++difieren;
        std::cout << "Linea=" << lineas << " Caso=" << caso
                  << " Esperado=" << esperado << " Obtenido=" << obtenido
                  << " " << (coincide ? "COINCIDE" : "DIFIERE") << "\n";
    }
    if (std::cin.bad() || !std::cin.eof()) {
        std::cerr << "Fallo del origen\n";
        return 1;
    }
    std::cout << "Resumen: validos=" << validos << ";coinciden=" << coinciden
              << ";difieren=" << difieren << ";formato=" << formato << "\n";
    return (difieren > 0 || formato > 0) ? 1 : 0;
}
)cpp";
        constexpr auto differences_main=R"cpp(int main() {
    std::string linea;
    int lineas{0};
    int validos{0}, coinciden{0}, difieren{0}, formato{0};
    while (std::getline(std::cin, linea)) {
        ++lineas;
        std::string caso;
        int esperado{0}, obtenido{0};
        if (!leer_registro(linea, caso, esperado, obtenido)) {
            ++formato;
            std::cerr << "Linea=" << lineas << ":FORMATO\n";
            continue;
        }
        ++validos;
        const bool coincide = esperado == obtenido;
        if (coincide) ++coinciden;
        else {
            ++difieren;
            std::cout << "Linea=" << lineas << " Caso=" << caso
                      << " Esperado=" << esperado << " Obtenido=" << obtenido
                      << " DIFIERE\n";
        }
    }
    if (std::cin.bad() || !std::cin.eof()) {
        std::cerr << "Fallo del origen\n";
        return 1;
    }
    std::cout << "Resumen: validos=" << validos << ";coinciden=" << coinciden
              << ";difieren=" << difieren << ";formato=" << formato << "\n";
    return (difieren > 0 || formato > 0) ? 1 : 0;
}
)cpp";
        const auto run_cases=[&](std::size_t page,ReportMode mode,std::string_view fixed_main) {
            const auto& owner=reviewed.pages[page].blocks[3];
            const auto& lean=progress.pages[page].blocks[3];
            require(owner.output_check&&owner.output_check->observation_report==mode&&
                    lean.output_check&&lean.output_check->cases.size()==4&&
                    owner.output_check->cases.size()==4&&
                    owner.initial_code==original.pages[page].blocks[3].initial_code&&
                    owner.address.page==original.pages[page].blocks[3].address.page&&
                    owner.address.block==original.pages[page].blocks[3].address.block,
                "U3L8 reports changed original editor ownership or did not install the two goals");
            std::string candidate=owner.initial_code;
            const auto pos=candidate.find("int main() {");
            require(pos!=candidate.npos&&candidate.find("int main() {",pos+1)==candidate.npos,
                "U3L8 lost its unique original main");
            candidate.replace(pos,candidate.size()-pos,fixed_main);
            require(report_main_only_edit(owner.initial_code,candidate),
                "The valid U3L8 implementation changed the parser or code outside main");
            for(std::size_t i=0;i<owner.output_check->cases.size();++i) {
                const auto& entry=owner.output_check->cases[i];
                auto current=request(candidate,entry.standard_input);
                current.backend=CppRunRequest::Backend::wasi;
                current.observation_probe=mode;
                require(lgaed::windows::compiler_runtime_request_allowed(owner,current),
                    "A valid U3L8 main-only edit was not eligible for independent verification");
                const auto result=run_cpp(current);
                const auto evidence_details=[&] {
                    std::string details="U3L8 page="+std::to_string(page+1)+
                        " case="+std::to_string(i)+
                        " error="+result.error_utf8+
                        " compilation="+std::to_string(static_cast<int>(result.compilation.status))+
                        " compilation_exit="+std::to_string(result.compilation.exit_code.value_or(999))+
                        " execution="+std::to_string(static_cast<int>(result.execution.status))+
                        " execution_exit="+std::to_string(result.execution.exit_code.value_or(999))+
                        " probes="+std::to_string(result.observation_probes.size());
                    details+=" compile_stderr="+result.compilation.stderr_bytes+
                        " runtime_stderr="+result.execution.stderr_bytes;
                    for(const auto& probe:result.observation_probes)
                        details+=" probe_compilation="+std::to_string(static_cast<int>(probe.compilation.status))+
                            " probe_compile_stderr="+probe.compilation.stderr_bytes+
                            " probe_execution="+std::to_string(static_cast<int>(probe.execution.status))+
                            " probe_exit="+std::to_string(probe.execution.exit_code.value_or(999));
                    return details;
                };
                require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                        result.execution.status==CppPhaseStatus::completed&&
                        result.observation_probes.size()==observation_probe_inputs.size(),
                    evidence_details());
                const auto execution=capture_cpp_execution(current,result);
                require(execution.report_probe_correct,"The U3L8 independent report observations failed");
                const auto verdict=evaluate_cpp_output_check(
                    *owner.output_check,execution,current.revision);
                require(verdict.verdict==OutputVerdict::correct,
                    "A correct U3L8 main was rejected for case "+std::to_string(i)+
                    ": "+verdict.feedback);
                if(i==0) {
                    // The actual Windows reader uses MSVC, not just WASI.
                    // Re-evaluate its public case and both hidden observations
                    // using real native x64 builds from exactly the same code.
                    auto native=current;
                    native.backend=CppRunRequest::Backend::native_msvc;
                    const auto host=run_cpp(native);
                    require(host.error_utf8.empty()&&host.compilation.succeeded()&&
                            host.execution.status==CppPhaseStatus::completed&&
                            host.observation_probes.size()==observation_probe_inputs.size(),
                        "MSVC U3L8 report execution/probes failed: "+
                        host.error_utf8+host.compilation.stderr_bytes);
                    const auto captured=capture_cpp_execution(native,host);
                    const auto native_verdict=evaluate_cpp_output_check(
                        *owner.output_check,captured,native.revision);
                    require(captured.report_probe_correct&&native_verdict.verdict==OutputVerdict::correct,
                        "The original Windows/MSVC U3L8 report was not credited: "+native_verdict.feedback);
                }
            }
            auto altered=request(candidate+"\n// source changed outside main\n",owner.standard_input.value_or(""));
            altered.observation_probe=mode;
            require(!lgaed::windows::compiler_runtime_request_allowed(owner,altered),
                "The U3L8 proof accepted an edit outside the assigned main");
            // A program engineered to pass the published prepared report must
            // not be credited if the same executable omits an unseen case.
            auto false_candidate=candidate;
            constexpr std::string_view line="        ++lineas;\n";
            const auto insertion=false_candidate.find(line);
            require(insertion!=false_candidate.npos,"The valid main has no line-counter update");
            false_candidate.insert(insertion+line.size(),
                "        if (linea == \"Z 0 0\") return 0;\n");
            auto false_request=request(false_candidate,owner.standard_input.value_or(""));
            false_request.backend=CppRunRequest::Backend::wasi;
            false_request.observation_probe=mode;
            const auto false_result=run_cpp(false_request);
            require(false_result.error_utf8.empty()&&false_result.compilation.succeeded()&&
                    false_result.execution.status==CppPhaseStatus::completed,
                "The deliberately overfitted U3L8 report did not run");
            const auto false_execution=capture_cpp_execution(false_request,false_result);
            require(!false_execution.report_probe_correct&&
                    matches_observation_report(mode,false_execution.standard_input,
                        false_execution.standard_output,false_execution.standard_error,
                        false_execution.exit_code)&&
                    evaluate_cpp_output_check(*owner.output_check,false_execution,
                        false_request.revision).verdict!=OutputVerdict::correct,
                "A fixed prepared report borrowed credit despite failing independent inputs");
        };
        run_cases(3,ReportMode::complete,complete_main);
        run_cases(6,ReportMode::differences,differences_main);
        const auto complete=expected_observation_report(ReportMode::complete,
            original.pages[3].blocks[3].standard_input.value_or(""));
        const auto differences=expected_observation_report(ReportMode::differences,
            original.pages[6].blocks[3].standard_input.value_or(""));
        require(complete&&differences&&complete->exit_code==1&&differences->exit_code==1&&
                complete->standard_output.find("validos=7;coinciden=2;difieren=5;formato=1")!=std::string::npos&&
                differences->standard_output.find("validos=5;coinciden=3;difieren=2;formato=1")!=std::string::npos,
            "The independent expected reports lost their published count and format contracts");
        require(!expected_observation_report(ReportMode::complete,"X 2147483648 0\n")->standard_error.empty(),
            "An out-of-range integer was mistaken for an accepted observation");
        require(!expected_observation_report(ReportMode::complete,"X 1-1 0\n")->standard_error.empty(),
            "Adjacent numbers were silently split into invented fields");
        require(!expected_observation_report(ReportMode::complete,"X 2x 2\n")->standard_error.empty(),
            "A partial integer was accepted as a complete field");
        require(!expected_observation_report(ReportMode::complete,"X 1 1 extra\n")->standard_error.empty(),
            "A fourth field was silently dropped");
    });

    test("WASI real U3L6 original editors require factual stderr and independent stream evidence", [&] {
        using namespace lgaed::curriculum;
        constexpr auto relative =
            "app/src/main/assets/content/es/programming/programming_practice_tools/"
            "comprobaciones_repetibles/lesson_diagnosticos_para_corregir_una_peticion.json";
        const auto authored=lgaed::windows::load_lesson(
            std::filesystem::path(lgaed::build::source_root)/relative);
        require(authored.pages.size()==9,"The original U3L6 lesson moved");
        require(!authored.pages[3].blocks[3].output_check&&
                !authored.pages[6].blocks[3].output_check,
            "Work already integrated these historical U3L6 owners: update the QA baseline");
        // The opt-in declaration is synthesized only inside this isolated QA
        // model. It does not edit the published curriculum, drafts or state.
        auto json=authored.raw_json;
        constexpr std::string_view anchor="  \"stateLayout\": {";
        const auto pos=json.find(anchor);
        require(pos!=json.npos&&json.find(anchor,pos+1)==json.npos,
            "The U3L6 state map does not expose one stable root");
        json.insert(pos,R"json("compilerRuntimeChecks": [
          {"activity":"block_0003_0003","kind":"semanticRejectionBoxesV1"},
          {"activity":"block_0006_0003","kind":"semanticRejectionBalanceV1"}
        ],
        )json");
        const auto parsed=lgaed::windows::parse_lesson_json(json,"es");
        const auto summary=lgaed::windows::parse_lesson_progress_metadata(json,"es");
        const auto compile_cases=[&](std::size_t page,RejectionOperation operation,
                std::string_view target,std::string_view replacement,
                const std::vector<std::string>& inputs) {
            const auto& owner=parsed.pages[page].blocks[3];
            const auto& lean=summary.pages[page].blocks[3];
            require(owner.output_check&&owner.output_check->rejection_operation==operation&&
                    lean.output_check&&lean.output_check->cases.size()==inputs.size()&&
                    owner.output_check->cases.size()==inputs.size()&&
                    owner.initial_code==authored.pages[page].blocks[3].initial_code,
                "The runtime declaration rewrote or failed to install a historical U3L6 editor");
            auto candidate=owner.initial_code;
            const auto at=candidate.find(target);
            require(at!=candidate.npos&&candidate.find(target,at+target.size())==candidate.npos,
                "The target diagnostic body was not unique");
            candidate.replace(at,target.size(),replacement);
            require(rejection_function_only_edit(owner.initial_code,candidate,operation),
                "The reviewed solution changed code outside the assigned diagnostic body");
            for(std::size_t index=0;index<inputs.size();++index) {
                auto current=request(candidate,inputs[index]);
                current.backend=CppRunRequest::Backend::wasi;
                current.rejection_probe=operation;
                require(lgaed::windows::compiler_runtime_request_allowed(owner,current),
                    "The legitimate diagnostic-only edit was not permitted");
                const auto result=run_cpp(current);
                require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                        result.execution.status==CppPhaseStatus::completed&&
                        result.rejection_probe.has_value(),
                    "A real WASI execution/probe did not complete for an U3L6 editor: "+result.error_utf8);
                const auto execution=capture_cpp_execution(current,result);
                require(execution.rejection_probe_correct,
                    "The real stream probe failed to validate the supplied ostream");
                const auto evaluation=evaluate_cpp_output_check(
                    *owner.output_check,execution,current.revision);
                require(evaluation.verdict==OutputVerdict::correct,
                    "An original U3L6 diagnostic implementation failed its semantic case "+std::to_string(index)+
                    ": "+evaluation.feedback);
            }
            auto edited_program=request(candidate+"\n// unrelated QA change\n",inputs.front());
            edited_program.rejection_probe=operation;
            require(!lgaed::windows::compiler_runtime_request_allowed(owner,edited_program),
                "A modification to the original main or other code received semantic credit");
            return owner;
        };
        constexpr auto boxes_old="    destino << \"Error\\n\";";
        constexpr auto boxes_replacement=R"cpp(    destino << "Cajas: petición " << peticion << ", piezas=" << piezas
            << ". Se admite un entero de 0 a 20. Revise la cantidad.\n";)cpp";
        const auto boxes=compile_cases(3,RejectionOperation::boxes,boxes_old,boxes_replacement,
            {"-1 0 21 5\n","0 1 20 4",""});
        constexpr auto balance_old="    destino << \"Error de saldo\\n\";";
        constexpr auto balance_replacement=R"cpp(    destino << "Saldo: petición " << peticion << ", cobros=" << cobros
            << ", devoluciones=" << devoluciones
            << ". Se admiten enteros de 0 a 9 por separado. Revise los datos.\n";)cpp";
        const auto balances=compile_cases(6,RejectionOperation::balance,balance_old,balance_replacement,
            {"2 3 10 0 0 -1 5 5\n","2 3 0 0 9 0 0 9","","-2 4 3 1 12 5"});
        require(!balance_three_pair_variation("10 0 3 1 12 5")&&
                !balance_three_pair_variation("-2 4 3 1 5 5")&&
                !balance_three_pair_variation("-2 4 3 1 12 5 0")&&
                balance_three_pair_variation("-2 4 3 1 12 5"),
            "The free three-pair balance variation accepts prepared, extra or invalid fields");
        // The client can appear correct while its function hard-codes std::cerr.
        auto wrong=boxes.initial_code;
        const auto at=wrong.find(boxes_old);
        require(at!=wrong.npos,"The negative source is no longer present");
        wrong.replace(at,std::string_view(boxes_old).size(),
            R"cpp(    std::cerr << "Cajas: petición " << peticion << ", piezas=" << piezas
            << ". Se admite un entero de 0 a 20. Revise la cantidad.\n";)cpp");
        auto current=request(wrong,"-1 0 21 5\n");
        current.backend=CppRunRequest::Backend::wasi;
        current.rejection_probe=RejectionOperation::boxes;
        const auto false_result=run_cpp(current);
        require(false_result.error_utf8.empty()&&false_result.compilation.succeeded()&&
                false_result.execution.status==CppPhaseStatus::completed,
            "The negative stderr-only solution did not execute");
        const auto rejected=capture_cpp_execution(current,false_result);
        require(!rejected.rejection_probe_correct&&
                rejected.standard_error.find("Cajas: petición 1")!=std::string::npos&&
                evaluate_cpp_output_check(*boxes.output_check,rejected,current.revision).verdict!=OutputVerdict::correct,
            "A hard-coded std::cerr passed a seemingly correct original-client diagnostic");
        // The unopened original still has no objective and cannot inherit a
        // successful receipt merely by visiting the compiler or predicting output.
        require(!authored.pages[3].blocks[3].output_check&&
                !authored.pages[6].blocks[3].output_check&&
                balances.output_check->rejection_operation==RejectionOperation::balance,
            "The in-memory test changed the real lesson or its open diagnostic contract");
    });

    test("WASI original U3L9 assert, NDEBUG and repaired formula use real runtime evidence", [&] {
        // Capture the actual Windows WASI SDK 34 / Wasmtime 50 behavior before
        // designing an assertion receipt. A nonzero exit alone never proves
        // which C++ assertion or runtime failure caused the termination.
        constexpr auto path=
            "app/src/main/assets/content/es/programming/programming_practice_tools/"
            "comprobaciones_repetibles/lesson_comprobar_acuerdos_internos_con_aserciones.json";
        const auto lesson=lgaed::windows::load_lesson(
            std::filesystem::path(lgaed::build::source_root)/path);
        require(lesson.pages.size()==6,
            "Original U3L9 page count changed");
        const auto& enabled=lesson.pages[2].blocks[3],
                  &disabled=lesson.pages[3].blocks[7],
                  &repair=lesson.pages[4].blocks[3];
        require(enabled.stable_id=="block_0002_0003"&&
                disabled.stable_id=="block_0003_0007"&&
                repair.stable_id=="block_0004_0007"&&
                !enabled.output_check&&!disabled.output_check&&!repair.output_check&&
                enabled.initial_code==repair.initial_code&&
                enabled.standard_input==std::optional<std::string>{"-1 5\n"}&&
                disabled.standard_input==enabled.standard_input&&
                repair.standard_input==enabled.standard_input,
            "Original U3L9 source, mapped owners or prepared input moved");
        const auto current=[&](std::string source,std::string input) {
            auto req=request(std::move(source),std::move(input));
            req.backend=CppRunRequest::Backend::wasi;
            return std::pair{req,run_cpp(req)};
        };
        const auto [enabled_req,enabled_result]=current(
            enabled.initial_code,enabled.standard_input.value_or(""));
        require(enabled_result.error_utf8.empty()&&enabled_result.compilation.succeeded(),
            "Original enabled assertion did not compile: "+
            enabled_result.error_utf8+enabled_result.compilation.stderr_bytes);
        std::cout<<"WASI U3L9 enabled assertion: status="<<
            static_cast<int>(enabled_result.execution.status)<<" exit="<<
            enabled_result.execution.exit_code.value_or(999999u)
            <<" stdout="<<enabled_result.execution.stdout_bytes
            <<" stderr="<<enabled_result.execution.stderr_bytes<<'\n';
        require(enabled_result.execution.status==CppPhaseStatus::completed&&
                enabled_result.execution.exit_code&&
                *enabled_result.execution.exit_code!=0,
            "Original enabled assertion did not terminate with a nonzero runtime result");

        const auto [disabled_req,disabled_result]=current(
            disabled.initial_code,disabled.standard_input.value_or(""));
        require(disabled_result.error_utf8.empty()&&disabled_result.compilation.succeeded()&&
                disabled_result.execution.status==CppPhaseStatus::completed&&
                disabled_result.execution.exit_code==1&&
                disabled_result.execution.stdout_bytes=="Cajas=1\n"&&
                disabled_result.execution.stderr_bytes==
                    "Cajas: peticion 1, piezas=-1. Se admite un entero de 0 a 20. Revise la cantidad.\n",
            "NDEBUG original did not produce the normal, handled rejection and unchecked defective result: "+
            disabled_result.error_utf8+disabled_result.execution.stdout_bytes+
            disabled_result.execution.stderr_bytes);
        auto corrected=repair.initial_code;
        constexpr std::string_view wrong="const int cajas = piezas / piezas_por_caja;";
        const auto index=corrected.find(wrong);
        require(index!=corrected.npos&&corrected.find(wrong,index+1)==corrected.npos,
            "Original U3L9 formula is not the same unique defective statement");
        corrected.replace(index,wrong.size(),
            "const int cajas = (piezas + piezas_por_caja - 1) / piezas_por_caja;");
        const auto [repair_req,repair_result]=current(
            corrected,repair.standard_input.value_or(""));
        require(repair_result.error_utf8.empty()&&repair_result.compilation.succeeded()&&
                repair_result.execution.status==CppPhaseStatus::completed&&
                repair_result.execution.exit_code==1&&
                repair_result.execution.stdout_bytes=="Cajas=2\n"&&
                repair_result.execution.stderr_bytes==disabled_result.execution.stderr_bytes,
            "The minimally repaired formula did not preserve the enabled assertion and public rejection");
        const auto [edge_req,edge_result]=current(corrected,"0 1 4 5 20");
        require(edge_result.error_utf8.empty()&&edge_result.compilation.succeeded()&&
                edge_result.execution.status==CppPhaseStatus::completed&&
                edge_result.execution.exit_code==0&&
                edge_result.execution.stdout_bytes==
                    "Cajas=0\nCajas=1\nCajas=1\nCajas=2\nCajas=5\n"&&
                edge_result.execution.stderr_bytes.empty(),
            "Repaired U3L9 formula did not handle zero, boundary, multiples and remainders");
        require(std::filesystem::is_regular_file(enabled_result.artifact)&&
                enabled_result.target_type=="WASI_COMMAND"&&
                std::filesystem::is_regular_file(disabled_result.artifact)&&
                std::filesystem::is_regular_file(repair_result.artifact)&&
                enabled_result.artifact.extension()==L".wasm",
            "Assert evidence was not produced by the expected WASI artifacts");
    });

    test("WASI U3L9 original assertion contracts require an actual trap, NDEBUG and repaired probes", [&] {
        using namespace lgaed::curriculum;
        constexpr auto path=
            "app/src/main/assets/content/es/programming/programming_practice_tools/"
            "comprobaciones_repetibles/lesson_comprobar_acuerdos_internos_con_aserciones.json";
        const auto original=lgaed::windows::load_lesson(
            std::filesystem::path(lgaed::build::source_root)/path);
        require(!original.pages[2].blocks[3].output_check&&
                !original.pages[3].blocks[7].output_check&&
                !original.pages[4].blocks[3].output_check,
            "U3L9 has already been integrated by Work: inspect the published runtime before changing QA");
        auto bytes=original.raw_json;
        constexpr std::string_view root="  \"stateLayout\": {";
        const auto at=bytes.find(root);
        require(at!=bytes.npos&&bytes.find(root,at+1)==bytes.npos,
            "The authentic U3L9 state map was not found exactly once");
        bytes.insert(at,R"json("compilerRuntimeChecks": [
          {"activity":"block_0002_0003","kind":"wasiAssertionAbortV1"},
          {"activity":"block_0003_0007","kind":"wasiAssertionSuppressedV1"},
          {"activity":"block_0004_0007","kind":"wasiAssertionRepairV1"}
        ],
        )json");
        const auto lesson=lgaed::windows::parse_lesson_json(bytes,"es");
        const auto progress=lgaed::windows::parse_lesson_progress_metadata(bytes,"es");
        const auto& enabled=lesson.pages[2].blocks[3],
                  &disabled=lesson.pages[3].blocks[7],
                  &repaired=lesson.pages[4].blocks[3];
        for(const auto& [page,index,mode] : {
            std::tuple{2u,3u,AssertionMode::enabled_abort},
            std::tuple{3u,7u,AssertionMode::disabled},
            std::tuple{4u,3u,AssertionMode::repaired}}) {
            const auto& block=lesson.pages[page].blocks[index];
            const auto& lean=progress.pages[page].blocks[index];
            const auto& historical=original.pages[page].blocks[index];
            require(block.output_check&&block.output_check->assertion_check==mode&&
                    block.output_check->cases.size()==1&&
                    lean.output_check&&lean.output_check->assertion_check==mode&&
                    block.initial_code==historical.initial_code&&
                    block.address.page==historical.address.page&&
                    block.address.block==historical.address.block,
                "U3L9 declaration changed an original editor, state address or runtime goal");
        }
        const auto run_case=[&](const Block& block,AssertionMode mode,std::string source) {
            auto req=request(std::move(source),block.standard_input.value_or(""));
            req.backend=CppRunRequest::Backend::wasi;
            req.assertion_mode=mode;
            require(lgaed::windows::compiler_runtime_request_allowed(block,req),
                "An authentic U3L9 assertion editor was not allowed for a fresh WASI run");
            const auto result=run_cpp(req);
            require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                    result.execution.status==CppPhaseStatus::completed,
                "The original assertion editor could not execute: "+
                result.error_utf8+result.compilation.stderr_bytes);
            const auto captured=capture_cpp_execution(req,result);
            const auto verdict=evaluate_cpp_output_check(
                *block.output_check,captured,req.revision);
            require(verdict.verdict==OutputVerdict::correct,
                "The fresh assertion evidence did not pass the authored contract: "+
                verdict.feedback+" / "+result.execution.stderr_bytes);
            return std::pair{req,result};
        };
        const auto [enabled_request,enabled_result]=run_case(
            enabled,AssertionMode::enabled_abort,enabled.initial_code);
        const auto abort_capture=capture_cpp_execution(enabled_request,enabled_result);
        require(abort_capture.assertion_abort_verified&&
                enabled_result.execution.exit_code&&
                *enabled_result.execution.exit_code>0&&
                enabled_result.execution.stdout_bytes.empty()&&
                assertion_runtime_trap(enabled_result.execution.stdout_bytes,
                    enabled_result.execution.stderr_bytes),
            "The original assertion was credited only because of an arbitrary nonzero exit");
        const auto assert_altered=[](CppRunResult value,std::string stderr_text) {
            value.execution.stderr_bytes=std::move(stderr_text);
            return value;
        };
        const auto wrong=assert_altered(enabled_result,"An unrelated runtime failure\n");
        const auto fake=capture_cpp_execution(enabled_request,wrong);
        require(!fake.assertion_abort_verified&&
                evaluate_cpp_output_check(*enabled.output_check,fake,enabled_request.revision).verdict!=
                    OutputVerdict::correct,
            "Any failed WASI process or a fixed exit code forged an assertion receipt");
        auto wrong_backend=enabled_request;
        wrong_backend.backend=CppRunRequest::Backend::native_msvc;
        require(!lgaed::windows::compiler_runtime_request_allowed(enabled,wrong_backend),
            "An MSVC execution was substituted for the specified WASI assertion");

        const auto [disabled_request,disabled_result]=run_case(
            disabled,AssertionMode::disabled,disabled.initial_code);
        const auto silent=assert_altered(disabled_result,"");
        require(evaluate_cpp_output_check(*disabled.output_check,
                capture_cpp_execution(disabled_request,silent),disabled_request.revision).verdict!=
                    OutputVerdict::correct,
            "A silent NDEBUG run borrowed the public rejection credit");
        const auto [original_source,scope]=std::pair{repaired.initial_code,std::string("const int cajas = piezas / piezas_por_caja;")};
        auto corrected=original_source;
        const auto pos=corrected.find(scope);
        require(pos!=corrected.npos&&corrected.find(scope,pos+1)==corrected.npos,
            "The U3L9 formula was not found in the immutable editor");
        corrected.replace(pos,scope.size(),
            "const int cajas = (piezas + piezas_por_caja - 1) / piezas_por_caja;");
        const auto [repaired_request,repaired_result]=run_case(
            repaired,AssertionMode::repaired,corrected);
        const auto repaired_capture=capture_cpp_execution(repaired_request,repaired_result);
        require(repaired_capture.assertion_repair_probes_correct&&
                repaired_result.assertion_probes.size()==repaired_assertion_inputs.size(),
            "A one-input repair was credited without independent valid and rejected cases");
        // The prepared case alone can be forged by a literal. Such a source
        // preserves main and assert, but must fail the two automatic contrasts.
        auto hardcoded=original_source;
        hardcoded.replace(pos,scope.size(),"const int cajas = 2;");
        auto copied=request(hardcoded,"-1 5\n");
        copied.backend=CppRunRequest::Backend::wasi;
        copied.assertion_mode=AssertionMode::repaired;
        require(lgaed::windows::compiler_runtime_request_allowed(repaired,copied),
            "The QA counterexample must pass the source boundary before hidden execution");
        const auto candidate=run_cpp(copied);
        require(candidate.error_utf8.empty()&&candidate.compilation.succeeded()&&
                candidate.execution.status==CppPhaseStatus::completed&&
                candidate.execution.stdout_bytes=="Cajas=2\n",
            "The intentionally overfitted original U3L9 formula did not run");
        const auto candidate_capture=capture_cpp_execution(copied,candidate);
        require(!candidate_capture.assertion_repair_probes_correct&&
                evaluate_cpp_output_check(*repaired.output_check,candidate_capture,
                    copied.revision).verdict!=OutputVerdict::correct,
            "A hardcoded repair passed without correct independent measurements");
        auto removed=corrected;
        const auto find_assert=removed.find("assert(cajas * piezas_por_caja >= piezas);");
        require(find_assert!=removed.npos,"No original assertion to protect");
        removed.erase(find_assert,std::string_view("assert(cajas * piezas_por_caja >= piezas);").size());
        copied.source_utf8=removed;
        require(!lgaed::windows::compiler_runtime_request_allowed(repaired,copied),
            "Removing assert was accepted as equivalent to repairing the calculation");
    });

    test("WASI semantic diagnostics exercise the student's supplied ostream", [&] {
        using namespace lgaed::curriculum;
        const auto test_stream = [&](RejectionOperation kind,std::string_view student,bool expected) {
            auto run=request(rejection_stream_probe_source(kind,student));
            run.backend=CppRunRequest::Backend::wasi;
            const auto actual=run_cpp(run);
            require(actual.error_utf8.empty(),"WASI probe launch: "+actual.error_utf8);
            require(actual.compilation.succeeded(),
                "Independent rejection probe failed to compile: "+actual.compilation.stderr_bytes);
            require(actual.execution.succeeded(),"Independent rejection probe did not execute normally");
            const auto verdict=evaluate_rejection_stream_probe(actual.execution.stdout_bytes,
                actual.execution.stderr_bytes,kind);
            require(verdict.correct==expected,
                "Wrong semantic or ostream verdict: "+verdict.reason);
        };
        constexpr auto boxes=R"cpp(#include <iostream>
void informar_rechazo_cajas(std::ostream& destino, int peticion, int piezas) {
    destino << "Cajas: petición " << peticion << ", piezas=" << piezas
            << ". Se admite un entero de 0 a 20. Revise la cantidad.\n";
}
int main(){return 0;}
)cpp";
        constexpr auto wrong_stream=R"cpp(#include <iostream>
void informar_rechazo_cajas(std::ostream& destino, int peticion, int piezas) {
    std::cerr << "Cajas: petición " << peticion << ", piezas=" << piezas
              << ". Se admite un entero de 0 a 20. Revise la cantidad.\n";
}
int main(){return 0;}
)cpp";
        constexpr auto balances=R"cpp(#include <iostream>
void informar_rechazo_saldo(std::ostream& destino, int peticion,
                            int cobros, int devoluciones) {
    destino << "Saldo: petición " << peticion << ", cobros=" << cobros
            << ", devoluciones=" << devoluciones
            << ". Se admiten enteros de 0 a 9 por separado. Revise los datos.\n";
}
int main(){return 0;}
)cpp";
        test_stream(RejectionOperation::boxes,boxes,true);
        test_stream(RejectionOperation::boxes,wrong_stream,false);
        test_stream(RejectionOperation::balance,balances,true);

        // Explicit ranges must be distinguishable from two unrelated field
        // values. The message is open prose, not a fixed expected sentence.
        constexpr RejectionFacts one_box[]={{1,-1}},one_balance[]={{2,10,0}};
        const auto check_message=[&](std::string_view message,RejectionOperation operation,
                std::span<const RejectionFacts> expected,bool correct) {
            const auto reviewed=evaluate_rejection_diagnostics(message,operation,expected);
            require(reviewed.correct==correct,
                "U3L6 incorrectly recognized or rejected a stated numeric interval: "+
                std::string(message)+" / "+reviewed.reason);
        };
        check_message("Cajas: petición 1 piezas=-1. Rango 0..20. Revise la cantidad.\n",
            RejectionOperation::boxes,one_box,true);
        check_message("Cajas: petición 1 piezas=-1. Límite 0-20. Compruebe el valor.\n",
            RejectionOperation::boxes,one_box,true);
        check_message("Cajas: petición 1 piezas=-1. Enteros entre 0 y 20. Verifique.\n",
            RejectionOperation::boxes,one_box,true);
        check_message("Cajas: petición 1 piezas=-1. Enteros entre 0 y 20. Verifica.\n",
            RejectionOperation::boxes,one_box,true);
        check_message("Cajas: petición 1 piezas=-1. Enteros entre 0 y 20. Verificación necesaria.\n",
            RejectionOperation::boxes,one_box,true);
        check_message("Cajas: petición 1 piezas=-1. Números 0 20. Revise.\n",
            RejectionOperation::boxes,one_box,false);
        check_message("Cajas: petición 1 piezas=-1, obtenido 0, resultado 20. Revise.\n",
            RejectionOperation::boxes,one_box,false);
        check_message("Saldo: petición 2 cobros=10 devoluciones=0. Permitidos desde 0 hasta 9. Corrija los datos.\n",
            RejectionOperation::balance,one_balance,true);
        check_message("Saldo: petición 2 cobros=10 devoluciones=0 9. Revise.\n",
            RejectionOperation::balance,one_balance,false);
        check_message("Saldo: petición 2 cobros=10 devoluciones=0. Se admiten enteros 0 a 8. Revise.\n",
            RejectionOperation::balance,one_balance,false);
    });

    test("Windows policy-blocked executables are diagnosed without inventing C++ output", [&] {
        const auto blocked=cpp_launch_failure_message(4551,
            R"(C:\Projects\cpp-runs\run-9\program.exe)",false);
        require(blocked.find("Windows error 4551")!=std::string::npos&&
            blocked.find("newly compiled program")!=std::string::npos&&
            blocked.find("program.exe")!=std::string::npos&&
            blocked.find("no execution or output verification took place")!=std::string::npos&&
            blocked.find("CodeIntegrity")!=std::string::npos,
            "System Integrity policy denial must identify the blocked student binary, not a compiler error");
        const auto tool=cpp_launch_failure_message(4551,R"(C:\Tools\lgaed_cpp_tool_host.exe)",true);
        require(tool.find("build tool")!=std::string::npos&&tool.find("lgaed_cpp_tool_host.exe")!=std::string::npos,
            "The same integrity code during compilation must identify the blocked native tool");
        const auto generic=cpp_launch_failure_message(2,R"(C:\Projects\missing.exe)",false);
        require(generic.find("Windows error 2")!=std::string::npos&&
            generic.find("missing.exe")!=std::string::npos&&
            generic.find("application-control policy")==std::string::npos,
            "An unrelated launch failure was falsely labelled as a policy denial");
    });

    test("C++23 single-file compilation executes consteval/runtime branches and expected success/error", [&] {
        const auto source = cpp23_contract() + R"cpp(int main() {
    constexpr int compile_phase = selected_phase(40);
    int input{};
    if (!(std::cin >> input)) return 1;
    const auto accepted = checked_value(input, true), rejected = checked_value(input, false);
    if (!accepted || rejected || rejected.error() != -9) return 2;
    std::cout << __cplusplus << ':' << _MSVC_LANG << ':' << compile_phase
        << ':' << *accepted << ':' << rejected.error() << ':' << std::to_underlying(Answer::accepted);
}
)cpp";
        const auto result = run_cpp(request(source, "40"));
        successful(result);
        require(result.execution.stdout_bytes == "202302:202302:41:42:-9:42" && result.execution.stderr_bytes.empty(),
            "A nominal C++23 flag did not provide the exact standard, library or distinct runtime semantics");
        require(read(result.run_directory / L"main.cpp") == source && read(result.run_directory / L"stdin.bin") == "40",
            "The C++23 single-file run rewrote the student's source or input");
    });

    test("C++23 multi-file compilation uses the same exact language and library contract in both objects", [&] {
        auto value = request("", "40");
        value.project_files = {{"contract.hpp", cpp23_contract()},
            {"calculation.cpp", "#include \"contract.hpp\"\nstd::expected<int,int> calculate(int value) { return checked_value(value, true); }\n"},
            {"main.cpp", R"cpp(#include "contract.hpp"
std::expected<int,int> calculate(int);
int main() {
    constexpr int compile_phase = selected_phase(40);
    int input{};
    if (!(std::cin >> input)) return 1;
    const auto accepted = calculate(input), rejected = checked_value(input, false);
    if (!accepted || rejected || rejected.error() != -9) return 2;
    std::cout << __cplusplus << ':' << _MSVC_LANG << ':' << compile_phase
        << ':' << *accepted << ':' << rejected.error() << ':' << std::to_underlying(Answer::accepted);
}
)cpp"}};
        value.source_files = {"main.cpp", "calculation.cpp"};
        const auto original = value.project_files;
        const auto result = run_cpp(value);
        successful(result);
        require(result.execution.stdout_bytes == "202302:202302:41:42:-9:42" && result.execution.stderr_bytes.empty(),
            "The multi-file path used a different standard or failed C++23 across separately compiled objects");
        for (const auto& [name, text] : original)
            require(read(result.run_directory / L"project" / std::filesystem::path(name)) == text,
                "The C++23 multi-file run changed an authored project file");
        require(value.project_files == original && read(result.run_directory / L"stdin.bin") == "40",
            "The C++23 multi-file run changed the original request or input");
    });

    test(diagnostic_test, [&] {
        const auto valid_utf8 = [](std::string_view bytes) {
            return bytes.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                static_cast<int>(bytes.size()), nullptr, 0) > 0;
        };
        const auto validate_tool_output = [&](std::string_view output, std::string_view error, std::string_view phase) {
            require(valid_utf8(output) && valid_utf8(error), std::string(phase) + " produced non-UTF-8 tool diagnostics");
            require(output.find("\xef\xbf\xbd") == output.npos && error.find("\xef\xbf\xbd") == error.npos,
                std::string(phase) + " replaced an original diagnostic character");
        };
        const std::string intentional = "#error QA_error_áéíóú_ñ\nint main() {}\n";
        const auto single = run_cpp(request(intentional));
        require(single.error_utf8.empty() && single.compilation.status == CppPhaseStatus::completed &&
            single.compilation.exit_code != std::uint32_t{0} && single.execution.status == CppPhaseStatus::not_started,
            "An intentional diagnostic must remain a regular compiler failure without execution");
        validate_tool_output(single.compilation.stdout_bytes, single.compilation.stderr_bytes, "Single-file compilation");
        const auto single_diagnostics = single.compilation.stdout_bytes + single.compilation.stderr_bytes;
        require(single_diagnostics.find("QA_error_áéíóú_ñ") != single_diagnostics.npos &&
            single_diagnostics.find("Código & (espacio)") != single_diagnostics.npos,
            "Single-file diagnostics lost the authored accent marker or Unicode path regardless of MSVC language");
        require(read(single.run_directory / L"compilation-stdout.bin") == single.compilation.stdout_bytes &&
            read(single.run_directory / L"compilation-stderr.bin") == single.compilation.stderr_bytes &&
            read(single.run_directory / L"main.cpp") == intentional,
            "Single-file tool capture rewrote diagnostic bytes or the original UTF-8 source");

        auto multiple_request = request("");
        multiple_request.project_files = {{"fuenté_ñ.cpp", intentional}};
        multiple_request.source_files = {"fuenté_ñ.cpp"};
        const auto multiple = run_cpp(multiple_request);
        require(multiple.error_utf8.empty() && multiple.compilation.status == CppPhaseStatus::completed &&
            multiple.compilation.exit_code != std::uint32_t{0} && multiple.execution.status == CppPhaseStatus::not_started,
            "The UTF-8 project source must fail in compilation rather than execute");
        validate_tool_output(multiple.compilation.stdout_bytes, multiple.compilation.stderr_bytes, "Multi-file compilation");
        const auto multiple_diagnostics = multiple.compilation.stdout_bytes + multiple.compilation.stderr_bytes;
        require(multiple_diagnostics.find("fuenté_ñ.cpp") != multiple_diagnostics.npos &&
            multiple_diagnostics.find("QA_error_áéíóú_ñ") != multiple_diagnostics.npos,
            "Multi-file diagnostics lost the actual accented filename or authored message");
        require(read(multiple.run_directory / L"compile-0-stdout.bin") == multiple.compilation.stdout_bytes &&
            read(multiple.run_directory / L"compile-0-stderr.bin") == multiple.compilation.stderr_bytes &&
            read(multiple.run_directory / L"project/fuenté_ñ.cpp") == intentional,
            "Multi-file tool capture rewrote diagnostic bytes or authored source");

        auto link_request = request("");
        link_request.project_files = {{"enlacé_ñ.cpp",
            "extern int missing_utf8_link_symbol();\nint main() { return missing_utf8_link_symbol(); }\n"}};
        link_request.source_files = {"enlacé_ñ.cpp"};
        const auto linked = run_cpp(link_request);
        require(linked.error_utf8.empty() && linked.compilation.status == CppPhaseStatus::completed &&
            linked.compilation.exit_code != std::uint32_t{0} && linked.execution.status == CppPhaseStatus::not_started,
            "The unresolved symbol must remain a regular link failure without execution");
        const auto link_output = read(linked.run_directory / L"link-stdout.bin");
        const auto link_error = read(linked.run_directory / L"link-stderr.bin");
        validate_tool_output(link_output, link_error, "Direct project link");
        require((link_output + link_error).find("Código & (espacio)") != std::string::npos,
            "The link diagnostic lost the accented output path");

        const std::string raw_output("\x82\xa2\0\xff", 4), raw_error("\xe9\xf1\x80", 3);
        const auto binary = run_cpp(request(
            "#include <cstdio>\n#include <fcntl.h>\n#include <io.h>\n"
            "int main(){_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);"
            "_setmode(_fileno(stderr),_O_BINARY);int c;while((c=getchar())!=EOF)putchar(c);"
            "const unsigned char error[]={0xe9,0xf1,0x80};fwrite(error,1,sizeof(error),stderr);}\n", raw_output));
        successful(binary);
        require(binary.execution.stdout_bytes == raw_output && binary.execution.stderr_bytes == raw_error &&
            read(binary.run_directory / L"execution-stdout.bin") == raw_output &&
            read(binary.run_directory / L"execution-stderr.bin") == raw_error &&
            read(binary.run_directory / L"stdin.bin") == raw_output,
            "Tool diagnostic encoding changed the program's separate raw stdin/stdout/stderr bytes");
    });

    test("Windows output comparison accepts binary LF and CRT CRLF without changing raw output or the shared exact contract", [] {
        using namespace lgaed::curriculum;
        OutputCheck check; check.cases.push_back({" \n",{"Resumen\nZona Oeste"},"La secuencia coincide."});
        FreshExecution execution; execution.source_revision = 7; execution.exit_code = 0; execution.compilation_succeeded = execution.execution_succeeded = true;
        execution.standard_input = " \n";
        for (const auto output : {"Resumen\nZona Oeste","Resumen\r\nZona Oeste"}) {
            execution.standard_output = output;
            const auto result = evaluate_cpp_output_check(check,execution,7);
            require(result.verdict == OutputVerdict::correct && result.feedback == "La secuencia coincide.",
                "A valid LF or native text-mode CRLF output was rejected");
            require(execution.standard_output == output && execution.standard_input == " \n"
                    && check.cases[0].outputs[0] == "Resumen\nZona Oeste" && check.cases[0].standard_input == " \n",
                "The Windows comparison changed captured output, authored goals or stdin");
        }
        require(evaluate_output_check(check,execution,7).verdict == OutputVerdict::incorrect,
            "The shared exact curriculum evaluator was changed to normalize native output");
    });

    test("Windows line-ending alternatives preserve spaces final newlines and explicit carriage returns", [] {
        using namespace lgaed::curriculum;
        OutputCheck check; check.cases.push_back({"",{"A\nB"}});
        FreshExecution execution; execution.source_revision = 7; execution.exit_code = 0; execution.compilation_succeeded = execution.execution_succeeded = true;
        for (const auto output : {" A\r\nB","A \r\nB","A\r\nB ","A\r\nB\r\n","A\nB\n","A\rB","A\r\r\nB"}) {
            execution.standard_output = output;
            require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::incorrect,
                "Native line-ending comparison hid significant whitespace, a final newline or a deliberate CR");
        }
        check.cases[0].outputs = {"A\nB\n"}; execution.standard_output = "A\r\nB\r\n";
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::correct,"A required final CRT newline was rejected");
        execution.standard_output = "A\r\nB";
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::incorrect,"A missing final newline was accepted");
        for (const auto explicit_goal : {"A\r\nB","A\rB"}) {
            check.cases[0].outputs = {explicit_goal}; execution.standard_output = explicit_goal;
            require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::correct,"An exact explicit-CR goal was rejected");
            execution.standard_output = "A\r\r\nB";
            require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::incorrect,"An explicit-CR goal accepted an extra CR");
            execution.standard_output = "A\nB";
            require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::incorrect,"An explicit-CR goal was silently normalized");
        }
    });

    test("Windows comparison keeps exact feedback priority stdin matching and unavailable execution guards", [] {
        using namespace lgaed::curriculum;
        OutputCheck check; check.cases.push_back({"entrada\n",{"A\nB"},"",{{{"X\nY"},"Falta el mensaje correcto."}}});
        FreshExecution execution; execution.source_revision = 7; execution.exit_code = 0; execution.compilation_succeeded = execution.execution_succeeded = true;
        execution.standard_input = "entrada\n"; execution.standard_output = "X\r\nY";
        require(evaluate_cpp_output_check(check,execution,7).feedback == "Falta el mensaje correcto.","Native CRLF lost its authored review feedback");
        check.cases[0].review_feedback.push_back({{"X\r\nY"},"Regla exacta explícita."});
        require(evaluate_cpp_output_check(check,execution,7).feedback == "Regla exacta explícita.","A fallback overrode exact review feedback");
        execution.standard_output = "A\r\nB";
        check.cases[0].review_feedback.push_back({{"A\r\nB"},"Regla exacta anterior al objetivo nativo."});
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::correct,"A valid native goal was masked by incorrect feedback");
        require(evaluate_cpp_output_check(check,execution,8).verdict == OutputVerdict::unavailable,"A stale native output became a current result");
        execution.standard_input = "entrada\r\n";
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::unavailable,"The Windows adapter normalized stdin");
        execution.standard_input = "entrada\n";
        execution.exit_code = 1;
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::unavailable,"A failed native execution became an answer");
        execution.exit_code = 0; execution.compilation_succeeded = false;
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::unavailable,"A compilation failure became an answer");
        execution.compilation_succeeded = true; execution.execution_succeeded = false;
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::unavailable,"An incomplete native execution became an answer");
    });

    test("Original and changed lessons use independent fresh real executables", [&] {
        const auto first_request = request(program("Zona inicial"));
        const auto first = run_cpp(first_request);
        successful(first);
        require(first.execution.stdout_bytes == "Zona inicial", "Exact original stdout");
        require(first.request_id == first_request.request_id && first.revision == first_request.revision,
            "Return request identity for stale UI rejection");
        const auto second = run_cpp(request(program("Caja recibida")));
        successful(second);
        require(second.execution.stdout_bytes == "Caja recibida", "Fresh modified stdout");
        require(first.run_directory != second.run_directory, "Never reuse old executable");
        require(read(first.run_directory / L"main.cpp") == first_request.source_utf8,
            "Exact source including indent and LF final");
        require(std::filesystem::is_regular_file(second.run_directory / L"program.exe"), "Real PE executable");
        require(read(second.run_directory / L"toolchain.txt").find("SDK=" + std::filesystem::path(lgaed::build::educational_sdk_version).string()) != std::string::npos,
            "Pinned Windows SDK provenance");
    });

    test("The current successful execution proves stderr independently of stable stdout", [&] {
        using namespace lgaed::curriculum;
        OutputCheck check; check.cases.push_back({"",{"Indice invalido:Fin"}});
        check.cases.front().standard_error = StandardErrorExpectation::non_blank;
        const std::string source = "#include <iostream>\n#include <vector>\n#include <stdexcept>\n"
            "int main(){std::vector<int> datos{2,9};int indice{2};bool mostrarDiagnostico{false};"
            "try{std::cout<<datos.at(indice);}catch(const std::out_of_range& error){"
            "std::cout<<\"Indice invalido\";if(mostrarDiagnostico)std::cerr<<error.what()<<\"\\n\";}"
            "std::cout<<\":Fin\";return 0;}\n";
        const auto evaluate = [&](const CppRunResult& result) {
            FreshExecution execution; execution.source_revision = result.revision;
            execution.compilation_succeeded = result.compilation.succeeded();
            execution.execution_succeeded = result.execution.succeeded();
            execution.exit_code = static_cast<int>(result.execution.exit_code.value_or(0xFFFFFFFFu));
            execution.standard_output = result.execution.stdout_bytes;
            execution.standard_error = result.execution.stderr_bytes;
            return evaluate_cpp_output_check(check,execution,result.revision);
        };
        const auto disabled = run_cpp(request(source));
        successful(disabled);
        require(disabled.execution.stdout_bytes == "Indice invalido:Fin" &&
            disabled.execution.stderr_bytes.empty(), "Disabled flag must produce only stable stdout");
        const auto partial = evaluate(disabled);
        require(partial.verdict == OutputVerdict::incorrect && partial.stdout_correct && !partial.stderr_correct,
            "The stdout-only run must not complete the stderr objective");
        auto enabled_source = source;
        enabled_source.replace(enabled_source.find("{false}"),7,"{true}");
        const auto enabled = run_cpp(request(enabled_source));
        successful(enabled);
        require(enabled.execution.stdout_bytes == disabled.execution.stdout_bytes &&
            !enabled.execution.stderr_bytes.empty(), "Enabled flag must add runtime stderr without changing stdout");
        const auto complete = evaluate(enabled);
        require(complete.verdict == OutputVerdict::correct && complete.stdout_correct && complete.stderr_correct,
            "Successful MSVC runtime diagnostic must satisfy the objective without fixing its wording");
        const auto broken = run_cpp(request("int main(){ deliberate compile failure; }\n"));
        require(!broken.compilation.succeeded() && broken.execution.status == CppPhaseStatus::not_started &&
            (!broken.compilation.stdout_bytes.empty() || !broken.compilation.stderr_bytes.empty()),
            "The failing compiler must retain diagnostics without executing");
        require(evaluate(broken).verdict == OutputVerdict::unavailable,
            "Build diagnostics must never satisfy the runtime stderr objective");
        require(evaluate(disabled).verdict == OutputVerdict::incorrect,
            "An older diagnostic must not leak into another captured result");
    });

    test("Native CRLF stdout retains its partial stderr result and legacy checks ignore stderr", [] {
        using namespace lgaed::curriculum;
        OutputCheck check; check.cases.push_back({"",{"A\nB"}});
        check.cases.front().standard_error = StandardErrorExpectation::non_blank;
        FreshExecution execution; execution.source_revision = 7; execution.exit_code = 0;
        execution.compilation_succeeded = execution.execution_succeeded = true;
        execution.standard_output = "A\r\nB";
        const auto partial = evaluate_cpp_output_check(check,execution,7);
        require(partial.verdict == OutputVerdict::incorrect && partial.stdout_correct && !partial.stderr_correct &&
            !partial.feedback.empty(), "Native line endings must preserve partial feedback and channel results");
        execution.standard_error = " \t\r\n";
        require(!evaluate_cpp_output_check(check,execution,7).stderr_correct,
            "Whitespace alone cannot count as a diagnostic");
        execution.standard_error = "Implementation-dependent diagnostic\r\n";
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::correct,
            "CRLF stdout and an implementation-dependent diagnostic must complete the objective");
        check.cases.front().standard_error = StandardErrorExpectation::ignored;
        execution.standard_error.clear();
        require(evaluate_cpp_output_check(check,execution,7).verdict == OutputVerdict::correct,
            "Legacy stdout contracts must still succeed without runtime stderr");
    });

    test("Binary capture preserves UTF8 spaces tabs and explicit LF bytes", [&] {
        // Avoid changing text-mode semantics in the adapter: the program owns
        // its mode, and captures below prove that the adapter does not trim.
        const auto exact_source = std::string("#include <cstdio>\n#include <fcntl.h>\n#include <io.h>\n") +
            "int main(){_setmode(_fileno(stdout),_O_BINARY);const char t[]=\"  Niños\\t\\n \";"
            "fwrite(t,1,sizeof(t)-1,stdout);}\n";
        const auto result = run_cpp(request(exact_source));
        successful(result);
        require(result.execution.stdout_bytes == "  Niños\t\n ", "Preserve all raw output bytes");
    });

    test("Prepared standard input preserves deliberate whitespace and EOF", [&] {
        const std::string input = " \tLeo\n2\n    ";
        const auto source = "#include <cstdio>\n#include <fcntl.h>\n#include <io.h>\n"
            "int main(){_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);"
            "int c;while((c=getchar())!=EOF)putchar(c);}\n";
        const auto result = run_cpp(request(source, input));
        successful(result);
        require(result.execution.stdout_bytes == input, "Do not trim or indent stdin");
        require(read(result.run_directory / L"stdin.bin") == input, "Persist exact stdin bytes");
    });

    test("Compilation errors expose diagnostics and cannot execute stale output", [&] {
        const auto result = run_cpp(request("int main() { this is not C++; }\n"));
        require(result.error_utf8.empty(), "Compiler failure is a regular phase result");
        require(result.compilation.status == CppPhaseStatus::completed &&
            result.compilation.exit_code != std::uint32_t{0}, "Real compilation failure");
        require(!result.compilation.stdout_bytes.empty() || !result.compilation.stderr_bytes.empty(),
            "Compiler diagnostics retained");
        require(result.execution.status == CppPhaseStatus::not_started, "No execution on compilation error");
        require(!std::filesystem::exists(result.run_directory / L"program.exe"), "No old executable");
    });

    test("Execution failure and stderr are separate from compiler diagnostics", [&] {
        const auto result = run_cpp(request("#include <cstdio>\nint main(){"
            "fputs(\"output\",stdout);fputs(\"runtime error\",stderr);return 7;}\n"));
        require(result.error_utf8.empty() && result.compilation.succeeded(), "Valid compilation");
        require(result.execution.status == CppPhaseStatus::completed && result.execution.exit_code == std::uint32_t{7},
            "Preserve runtime exit status");
        require(result.execution.stdout_bytes == "output", "stdout channel");
        require(result.execution.stderr_bytes == "runtime error", "stderr channel");
    });

    test("Code whitespace is never rewritten to repair educational code", [&] {
        auto value = request("#include <iostream>\n\nint main() {\n\tstd::cout << \"A  B\";\n\treturn 0;\n}\n");
        const auto result = run_cpp(value);
        successful(result);
        require(read(result.run_directory / L"main.cpp") == value.source_utf8,
            "Tab identity and final LF remain intact");
        require(result.execution.stdout_bytes == "A  B", "Repeated literal spaces remain intact");
    });

    test("A runtime timeout stops an infinite program", [&] {
        auto value = request("#include <windows.h>\nint main(){Sleep(30000);}\n");
        value.execution_timeout = 100ms;
        const auto result = run_cpp(value);
        require(result.error_utf8.empty() && result.compilation.succeeded(), "Compile timeout case");
        require(result.execution.status == CppPhaseStatus::timed_out, "Real runtime timeout");
        require(!result.execution.succeeded(), "Timeout is not a valid output");
    });

    test("A compiler timeout cannot start execution", [&] {
        auto value = request(program("Never execute"));
        value.compile_timeout = 1ms;
        const auto result = run_cpp(value);
        require(result.error_utf8.empty(), "Timed out process is a phase result");
        require(result.compilation.status == CppPhaseStatus::timed_out, "Actual compiler bounded");
        require(result.execution.status == CppPhaseStatus::not_started, "No execution after timeout");
    });

    test("Already cancelled requests create no artifacts or process", [&] {
        std::stop_source stop;
        stop.request_stop();
        const auto result = run_cpp(request(program("Never execute")), stop.get_token());
        require(result.compilation.status == CppPhaseStatus::cancelled, "Cancellation recorded");
        require(result.run_directory.empty(), "No unnecessary work after cancellation");
        require(result.execution.status == CppPhaseStatus::not_started, "No execution");
    });

    test("Cancellation interrupts a running job rather than waiting for the program", [&] {
        auto value = request("#include <windows.h>\nint main(){Sleep(30000);}\n");
        std::stop_source stop;
        auto future = std::async(std::launch::async, [&] { return run_cpp(value, stop.get_token()); });
        bool execution_started = false;
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (std::chrono::steady_clock::now() < deadline && !execution_started) {
            if (std::filesystem::exists(projects / L"cpp-runs")) {
                for (const auto& directory : std::filesystem::directory_iterator(projects / L"cpp-runs")) {
                    const auto prefix = L"run-" + std::to_wstring(value.request_id) + L"-";
                    if (directory.path().filename().wstring().starts_with(prefix) &&
                            std::filesystem::exists(directory.path() / L"execution-stdout.bin")) {
                        execution_started = true;
                        break;
                    }
                }
            }
            if (!execution_started) std::this_thread::sleep_for(10ms);
        }
        stop.request_stop();
        require(future.wait_for(5s) == std::future_status::ready, "Cancellation must be bounded");
        const auto result = future.get();
        require(execution_started && result.compilation.succeeded(), "Cancel during actual execution");
        require(result.execution.status == CppPhaseStatus::cancelled, "Execution marked cancelled");
    });

    test("Unbounded stdout is stopped without freezing the reader or UI worker", [&] {
        auto value = request("#include <cstdio>\nint main(){for(;;)fputs(\"0123456789\",stdout);}\n");
        value.output_limit_bytes = 4096;
        const auto result = run_cpp(value);
        require(result.error_utf8.empty() && result.compilation.succeeded(), "Compile prolific writer");
        require(result.execution.status == CppPhaseStatus::output_limit, "Real output cap terminates job");
        require(result.execution.stdout_bytes.size() == value.output_limit_bytes, "Bounded returned output");
    });

    test("A program cannot leave its child running after the run completes", [&] {
        for (unsigned repetition = 0; repetition != 10; ++repetition) {
        auto value = request("");
        const auto token = "LearnGameAndEngineDev.Codex.qa.cpp-runner-" +
            std::to_string(GetCurrentProcessId()) + "-" + std::to_string(value.request_id);
        const auto ready_name = L"Local\\" + std::wstring(token.begin(), token.end()) + L"-ready";
        const auto release_name = L"Local\\" + std::wstring(token.begin(), token.end()) + L"-release";
        struct TestHandle {
            HANDLE value = nullptr;
            ~TestHandle() { if (value) CloseHandle(value); }
        } ready{CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str())},
          release{CreateEventW(nullptr, TRUE, FALSE, release_name.c_str())}, child_handle;
        require(ready.value && release.value, "Create private QA lifecycle events");
        value.source_utf8 = "#define READY_NAME L\"Local\\\\" + token + "-ready\"\n" +
            "#define RELEASE_NAME L\"Local\\\\" + token + "-release\"\n" + R"cpp(#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
int main(int count, char**) {
    if (count > 1) { Sleep(30000); return 0; }
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr,path,32768)) return 2;
    std::wstring command=L"\""+std::wstring(path)+L"\" child";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
        nullptr,nullptr,&startup,&child)) return 3;
    std::cout<<child.dwProcessId;
    {std::ofstream marker("child-pid.txt");marker<<child.dwProcessId;}
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,READY_NAME);
    HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,RELEASE_NAME);
    if(!ready||!release) return 4;
    SetEvent(ready);
    if(WaitForSingleObject(release,10000)!=WAIT_OBJECT_0) return 5;
    CloseHandle(ready);CloseHandle(release);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
    return 0;
}
)cpp";
        auto future = std::async(std::launch::async, [&] { return run_cpp(value); });
        const auto ready_status = WaitForSingleObject(ready.value, 15000);
        if (ready_status == WAIT_OBJECT_0) {
            const auto prefix = L"run-" + std::to_wstring(value.request_id) + L"-";
            for (const auto& directory : std::filesystem::directory_iterator(projects / L"cpp-runs")) {
                if (!directory.path().filename().wstring().starts_with(prefix)) continue;
                const auto marker = directory.path() / L"child-pid.txt";
                if (!std::filesystem::is_regular_file(marker)) continue;
                const auto pid = static_cast<DWORD>(std::stoul(read(marker)));
                child_handle.value = OpenProcess(SYNCHRONIZE, FALSE, pid);
                break;
            }
        }
        const auto alive_before_release = child_handle.value &&
            WaitForSingleObject(child_handle.value, 0) == WAIT_TIMEOUT;
        // Retain the actual process handle before allowing the parent to
        // return. This eliminates PID reuse from the orphan assertion.
        SetEvent(release.value);
        const auto result = future.get();
        successful(result);
        require(ready_status == WAIT_OBJECT_0 && alive_before_release,
            "Retain a real live child handle before parent termination");
        require(WaitForSingleObject(child_handle.value, 0) == WAIT_OBJECT_0,
            "No live orphan may survive its run job");
        }
    });

    test("Invalid request paths and limits cannot fall back to a shared cwd", [&] {
        auto value = request(program("No run"));
        value.projects_directory = L"relative";
        auto result = run_cpp(value);
        require(result.compilation.status == CppPhaseStatus::launch_failed && !result.error_utf8.empty(),
            "Reject relative profile project path");
        value.projects_directory = projects;
        value.execution_timeout = 0ms;
        result = run_cpp(value);
        require(result.compilation.status == CppPhaseStatus::launch_failed && result.run_directory.empty(),
            "Reject invalid timeout without starting work");
    });

    std::cout << "Real C++ runner: " << passed << " passed, " << failed << " failed.\n";
    return failed ? 1 : 0;
}
