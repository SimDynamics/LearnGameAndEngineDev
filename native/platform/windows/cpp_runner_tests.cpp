#include "platform/windows/cpp_runner.hpp"
#include "core/lesson.hpp"
#include "platform/windows/environment.hpp"
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
    if (argc > 1 && !diagnostic_only && !cpp23_only) return 2;
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
        try {
            action();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

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
