#include "platform/windows/cpp_runner.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::windows {
namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept { return std::exchange(value_, nullptr); }
    void reset(HANDLE value = nullptr) noexcept {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Invalid UTF-16 Windows path");
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

[[noreturn]] void windows_error(const char* operation) {
    const auto error = GetLastError();
    throw std::runtime_error(std::string(operation) + " (Windows error " +
        std::to_string(error) + ")");
}

// Microsoft CRT command-line quoting. No command shell is used for source,
// paths, input, or arguments; shell metacharacters have no special meaning.
std::wstring quote_argument(std::wstring_view argument) {
    std::wstring result{L'"'};
    std::size_t slashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++slashes;
        } else {
            result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
            slashes = 0;
            if (character == L'"') result.push_back(L'\\');
            result.push_back(character);
        }
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

struct CaseInsensitive {
    bool operator()(const std::wstring& left, const std::wstring& right) const {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};

std::vector<wchar_t> compiler_environment() {
    std::map<std::wstring, std::wstring, CaseInsensitive> values;
    const auto block = GetEnvironmentStringsW();
    if (!block) windows_error("GetEnvironmentStringsW");
    for (auto entry = block; *entry; entry += std::wcslen(entry) + 1) {
        const std::wstring value(entry);
        const auto equals = value.find(L'=', value.front() == L'=' ? 1 : 0);
        if (equals != std::wstring::npos) values[value.substr(0, equals)] = value.substr(equals + 1);
    }
    FreeEnvironmentStringsW(block);
    const std::filesystem::path vc{build::educational_vc_root};
    const std::filesystem::path sdk{build::educational_sdk_root};
    const std::wstring version{build::educational_sdk_version};
    values[L"INCLUDE"] = (vc / L"include").wstring() + L";" +
        (sdk / L"Include" / version / L"ucrt").wstring() + L";" +
        (sdk / L"Include" / version / L"um").wstring() + L";" +
        (sdk / L"Include" / version / L"shared").wstring() + L";" +
        (sdk / L"Include" / version / L"winrt").wstring();
    values[L"LIB"] = (vc / L"lib/x64").wstring() + L";" +
        (sdk / L"Lib" / version / L"ucrt/x64").wstring() + L";" +
        (sdk / L"Lib" / version / L"um/x64").wstring();
    values[L"PATH"] = (vc / L"bin/Hostx64/x64").wstring() + L";" +
        (sdk / L"bin" / version / L"x64").wstring() + L";" + values[L"PATH"];
    values[L"VSLANG"] = L"1033";
    for (const auto variable : {L"CL", L"_CL_", L"LINK", L"_LINK_", L"LIBPATH"}) values.erase(variable);
    std::vector<wchar_t> result;
    for (const auto& [key, value] : values) {
        result.insert(result.end(), key.begin(), key.end());
        result.push_back(L'=');
        result.insert(result.end(), value.begin(), value.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

void write_exact(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Cannot create run file: " + utf8(path.wstring()));
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("Cannot write run file: " + utf8(path.wstring()));
}

std::string read_exact(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read run output");
    std::string result;
    std::array<char, 8192> buffer{};
    while (result.size() < limit && stream) {
        const auto count = std::min(buffer.size(), limit - result.size());
        stream.read(buffer.data(), static_cast<std::streamsize>(count));
        result.append(buffer.data(), static_cast<std::size_t>(stream.gcount()));
    }
    return result;
}

Handle redirected_file(const std::filesystem::path& path, bool input) {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    Handle file(CreateFileW(path.c_str(), input ? GENERIC_READ : GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
        input ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) windows_error("CreateFileW redirected stream");
    return file;
}

std::uint64_t file_size(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) windows_error("GetFileSizeEx");
    return static_cast<std::uint64_t>(size.QuadPart);
}

class AttributeList {
public:
    explicit AttributeList(std::array<HANDLE, 3>& handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        storage_.resize(bytes);
        value_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(value_, 1, 0, &bytes))
            windows_error("InitializeProcThreadAttributeList");
        initialized_ = true;
        if (!UpdateProcThreadAttribute(value_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                handles.data(), sizeof(handles), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(value_);
            initialized_ = false;
            windows_error("UpdateProcThreadAttribute");
        }
    }
    ~AttributeList() { if (initialized_) DeleteProcThreadAttributeList(value_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept { return value_; }
private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST value_ = nullptr;
    bool initialized_ = false;
};

void terminate_and_drain_job(HANDLE job) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    // Do not start with TerminateJobObject: it can remove members from the
    // accounting/PID list before their process handles become signaled. Keep
    // real handles before requesting termination, then wait on every one.
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Child job termination could not be confirmed");
        std::vector<ULONG_PTR> storage(130);
        auto list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
        for (;;) {
            if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, list,
                    static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR)), nullptr)) break;
            if (GetLastError() != ERROR_MORE_DATA) windows_error("QueryInformationJobObject process list");
            if (storage.size() >= 4096 || std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Child job process list did not stabilize");
            storage.resize(storage.size() * 2);
            list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
        }
        if (list->NumberOfProcessIdsInList == 0) return;
        std::vector<Handle> members;
        for (DWORD index = 0; index < list->NumberOfProcessIdsInList; ++index) {
            const auto pid = static_cast<DWORD>(list->ProcessIdList[index]);
            Handle member(OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, pid));
            if (!member) {
                // A naturally exited process may have disappeared between
                // the snapshot and OpenProcess. Other failures are not proof
                // of successful teardown and must fail closed.
                if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
                windows_error("OpenProcess child job member");
            }
            BOOL belongs = FALSE;
            if (!IsProcessInJob(member.get(), job, &belongs)) windows_error("IsProcessInJob");
            // Validate membership after opening: never act on an unrelated
            // process if a PID was recycled during the snapshot race.
            if (belongs) members.push_back(std::move(member));
        }
        for (const auto& member : members) {
            if (WaitForSingleObject(member.get(), 0) == WAIT_OBJECT_0) continue;
            if (!TerminateProcess(member.get(), ERROR_CANCELLED)) {
                const auto error = GetLastError();
                // A process already exiting can reject another termination
                // request with ERROR_ACCESS_DENIED. This is not success: its
                // retained handle must still pass the bounded wait below.
                if (error == ERROR_ACCESS_DENIED) continue;
                if (WaitForSingleObject(member.get(), 0) == WAIT_OBJECT_0) continue;
                SetLastError(error);
                windows_error("TerminateProcess child job member");
            }
        }
        for (const auto& member : members) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            const auto waited = WaitForSingleObject(member.get(),
                static_cast<DWORD>(std::max<std::int64_t>(0, remaining)));
            if (waited == WAIT_FAILED) windows_error("WaitForSingleObject child job member");
            if (waited != WAIT_OBJECT_0)
                throw std::runtime_error("Child job process did not terminate");
        }
        // A member can create another child between the snapshot and its
        // termination. Repeat until no unobserved members remain. No breakaway
        // flag is enabled; RAII KillOnClose covers exceptional stop paths.
    }
}

CppPhaseResult run_phase(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& directory,
    const std::filesystem::path& input_path, std::wstring_view phase,
    std::chrono::milliseconds timeout, std::size_t limit, bool compilation,
    std::stop_token stop) {
    CppPhaseResult result;
    if (stop.stop_requested()) {
        result.status = CppPhaseStatus::cancelled;
        return result;
    }
    const auto output_path = directory / (std::wstring(phase) + L"-stdout.bin");
    const auto error_path = directory / (std::wstring(phase) + L"-stderr.bin");
    auto input = redirected_file(input_path, true);
    auto output = redirected_file(output_path, false);
    auto error = redirected_file(error_path, false);
    std::array<HANDLE, 3> inherited{input.get(), output.get(), error.get()};
    AttributeList attributes(inherited);
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) windows_error("CreateJobObjectW");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_JOB_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = compilation ? 16 : 4;
    limits.JobMemoryLimit = static_cast<SIZE_T>(compilation ? 2048ULL : 512ULL) * 1024 * 1024;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        windows_error("SetInformationJobObject");
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& argument : arguments) command += L" " + quote_argument(argument);
    auto environment = compiler_environment();
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = error.get();
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION information{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                EXTENDED_STARTUPINFO_PRESENT,
            environment.data(), directory.c_str(), &startup.StartupInfo, &information))
        windows_error("CreateProcessW");
    Handle process(information.hProcess);
    Handle thread(information.hThread);
    // Assign before any user instruction can run or spawn a child.
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const auto code = GetLastError();
        TerminateProcess(process.get(), code);
        WaitForSingleObject(process.get(), 5000);
        SetLastError(code);
        windows_error("AssignProcessToJobObject");
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) windows_error("ResumeThread");
    thread.reset();
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const auto waited = WaitForSingleObject(process.get(), 10);
        if (waited == WAIT_FAILED) windows_error("WaitForSingleObject");
        const auto oversized = file_size(output.get()) > limit || file_size(error.get()) > limit;
        if (stop.stop_requested()) result.status = CppPhaseStatus::cancelled;
        else if (oversized) result.status = CppPhaseStatus::output_limit;
        else if (waited == WAIT_OBJECT_0) result.status = CppPhaseStatus::completed;
        else if (std::chrono::steady_clock::now() - start >= timeout)
            result.status = CppPhaseStatus::timed_out;
        else continue;
        // Parent completion is not descendant completion. Collect and wait
        // on member handles before their identities disappear from the job.
        terminate_and_drain_job(job.get());
        if (WaitForSingleObject(process.get(), 5000) != WAIT_OBJECT_0)
            throw std::runtime_error("Child process did not terminate");
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process.get(), &exit_code)) windows_error("GetExitCodeProcess");
        result.exit_code = exit_code;
        break;
    }
    output.reset();
    error.reset();
    input.reset();
    result.stdout_bytes = read_exact(output_path, limit);
    result.stderr_bytes = read_exact(error_path, limit);
    return result;
}

std::filesystem::path create_run_directory(const CppRunRequest& request) {
    if (!request.projects_directory.is_absolute())
        throw std::invalid_argument("Student projects directory must be absolute");
    const auto parent = request.projects_directory / L"cpp-runs";
    std::filesystem::create_directories(parent);
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create run identity");
    std::array<wchar_t, 40> token{};
    if (!StringFromGUID2(id, token.data(), static_cast<int>(token.size())))
        throw std::runtime_error("Cannot format run identity");
    const auto name = L"run-" + std::to_wstring(request.request_id) + L"-" +
        std::to_wstring(request.revision) + L"-" + std::wstring(token.data() + 1, 36);
    const auto path = parent / name;
    if (!std::filesystem::create_directory(path))
        throw std::runtime_error("Fresh run directory collision");
    return path;
}

} // namespace

CppRunResult run_cpp(const CppRunRequest& request, std::stop_token stop) {
    CppRunResult result;
    result.request_id = request.request_id;
    result.revision = request.revision;
    try {
        if (stop.stop_requested()) {
            result.compilation.status = CppPhaseStatus::cancelled;
            return result;
        }
        if (request.compile_timeout.count() <= 0 || request.execution_timeout.count() <= 0 ||
                request.output_limit_bytes == 0 || request.output_limit_bytes > 16 * 1024 * 1024 ||
                request.source_utf8.size() > 4 * 1024 * 1024 || request.stdin_utf8.size() > 4 * 1024 * 1024)
            throw std::invalid_argument("Invalid educational execution limits");
        result.run_directory = create_run_directory(request);
        const auto source = result.run_directory / L"main.cpp";
        const auto input = result.run_directory / L"stdin.bin";
        const auto compiler_input = result.run_directory / L"compiler-stdin.bin";
        const auto executable = result.run_directory / L"program.exe";
        const std::filesystem::path vc{build::educational_vc_root};
        const auto compiler = vc / L"bin/Hostx64/x64/cl.exe";
        if (!std::filesystem::is_regular_file(compiler))
            throw std::runtime_error("Pinned educational MSVC compiler is unavailable");
        write_exact(source, request.source_utf8);
        write_exact(input, request.stdin_utf8);
        write_exact(compiler_input, "");
        write_exact(result.run_directory / L"toolchain.txt", "MSVC=14.51.36231\nSDK=" +
            utf8(build::educational_sdk_version) + "\ncompiler=" + utf8(compiler.wstring()) + "\n");
        const std::vector<std::wstring> arguments{
            L"/nologo", L"/EHsc", L"/std:c++20", L"/permissive-", L"/utf-8", L"/W4",
            L"/diagnostics:column", L"/Fe:" + executable.wstring(),
            L"/Fo:" + (result.run_directory / L"program.obj").wstring(), source.wstring(),
            L"/link", L"/INCREMENTAL:NO", L"/MACHINE:X64", L"/SUBSYSTEM:CONSOLE",
            L"/MANIFEST:EMBED", L"/MANIFESTUAC:level='asInvoker' uiAccess='false'"};
        result.compilation = run_phase(compiler, arguments, result.run_directory,
            compiler_input, L"compilation", request.compile_timeout,
            request.output_limit_bytes, true, stop);
        if (result.compilation.succeeded()) {
            result.execution = run_phase(executable, {}, result.run_directory, input,
                L"execution", request.execution_timeout, request.output_limit_bytes, false, stop);
        }
    } catch (const std::exception& exception) {
        result.error_utf8 = exception.what();
        auto& phase = result.compilation.succeeded() ? result.execution : result.compilation;
        phase.status = CppPhaseStatus::launch_failed;
    }
    return result;
}

} // namespace lgaed::windows
