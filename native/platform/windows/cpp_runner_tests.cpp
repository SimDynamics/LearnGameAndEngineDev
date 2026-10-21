#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"

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
} // namespace

int main() {
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
        try {
            action();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

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
        require(read(second.run_directory / L"toolchain.txt").find("SDK=10.0.26100.0") != std::string::npos,
            "Pinned Windows SDK provenance");
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
