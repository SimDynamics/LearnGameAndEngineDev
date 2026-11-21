#include "platform/windows/cpp_runner.hpp"
#include "core/lesson.hpp"
#include "core/cmake_message_trace.hpp"
#include "core/cpp_expression_repair.hpp"
#include "core/named_rule_constant_variation_check.hpp"
#include "core/cpp_indexed_unique_transfer_probe.hpp"
#include "core/cpp_defaulted_move_policy_check.hpp"
#include "core/cpp_independent_copy_assignment_check.hpp"
#include "core/cpp_move_assignment_invariant_check.hpp"
#include "core/cpp_array_owner_read_check.hpp"
#include "core/cpp_array_copy_move_check.hpp"
#include "core/cpp_array_return_take_check.hpp"
#include "core/cpp_polymorphic_destruction_check.hpp"
#include "core/cpp_virtual_lifetime_dispatch_check.hpp"
#include "core/cpp_derived_preparation_check.hpp"
#include "core/cpp_abstract_interface_check.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
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

std::filesystem::path cpp_tool_host(){
    std::vector<wchar_t> name(512);
    for(;;){const auto size=GetModuleFileNameW(nullptr,name.data(),static_cast<DWORD>(name.size()));
        if(!size)break;if(size<name.size()){const auto beside=std::filesystem::path(std::wstring(name.data(),size)).parent_path()/L"lgaed_cpp_tool_host.exe";
            if(std::filesystem::is_regular_file(beside))return beside;break;}name.resize(name.size()*2);}
    return std::filesystem::path(build::cpp_tool_host);
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
    for (const auto variable : {L"CL", L"_CL_", L"LINK", L"_LINK_", L"LIBPATH",L"CMAKE_TOOLCHAIN_FILE",L"CMAKE_GENERATOR",L"CMAKE_GENERATOR_PLATFORM",L"CMAKE_GENERATOR_TOOLSET",L"CMAKE_GENERATOR_INSTANCE"}) values.erase(variable);
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
    std::stop_token stop,bool utf8_console=false,
    const std::vector<wchar_t>* fixed_environment=nullptr) {
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
    auto launcher=executable;
    if(utf8_console){launcher=cpp_tool_host();if(!std::filesystem::is_regular_file(launcher))throw std::runtime_error("The native UTF-8 compiler host is unavailable");}
    std::wstring command = quote_argument(launcher.wstring());
    if(utf8_console)command+=L" "+quote_argument(executable.wstring());
    for (const auto& argument : arguments) command += L" " + quote_argument(argument);
    auto environment = fixed_environment?*fixed_environment:compiler_environment();
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow=SW_HIDE;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = error.get();
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION information{};
    if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | (utf8_console?CREATE_NEW_CONSOLE:CREATE_NO_WINDOW) | CREATE_UNICODE_ENVIRONMENT |
                EXTENDED_STARTUPINFO_PRESENT,
            environment.data(), directory.c_str(), &startup.StartupInfo, &information)) {
        // Preserve GetLastError before UTF-8 conversion or formatting calls.
        const DWORD code=GetLastError();
        throw std::runtime_error(cpp_launch_failure_message(code,utf8(launcher.wstring()),compilation));
    }
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
    state::require_safe_path(parent);std::filesystem::create_directories(parent);state::require_safe_path(parent);
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
    state::require_safe_path(path);
    return path;
}

using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;
std::filesystem::path project_path(std::string_view path){return std::filesystem::path(winrt::to_hstring(path).c_str());}
std::string fingerprint(const CppRunRequest& request,bool configuration=false){return state::sha256(curriculum::cpp_project_frame(request.project_files,request.source_files,request.cmake,configuration)+utf8(build::educational_vc_root)+utf8(build::educational_sdk_version)+utf8(build::educational_cmake)+utf8(build::educational_ninja)
    +"/windows-cpp-profile-v1/"+std::to_string(build::cpp_standard)+'/'+utf8(build::cpp_standard_option)+"/Zc:__cplusplus/cmake-rules-v1");}
// Private integer variations bind the exact student source, input and lesson
// draft identity to all independently authored expected results. Never reuse
// an ordinary output receipt as proof of these hidden observations.
std::string local_int_fingerprint(const CppRunRequest& request) {
    if(!request.local_int_check||request.local_string_check||
       !curriculum::cpp_local_int_check_valid(*request.local_int_check)||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||
       request.diagnostic||request.cmake||!request.project_files.empty()||
       !request.source_files.empty()||request.named_rule_probe||
       request.open_battery_probe||request.boolean_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.indexed_unique_transfer_probe||
       request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.derived_preparation_probe||request.abstract_interface_probe||
       request.interface_collection_probe||request.polymorphic_clone_probe||
       request.dual_interface_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.cmake_graph_check||request.capture_cmake_graph)return {};
    const auto field=[](std::string_view part) {
        return std::to_string(part.size())+":"+std::string(part);
    };
    std::string frame="compiler-local-int-dependency-v1/"+field(request.project_key)+
        field(request.source_utf8)+field(request.stdin_utf8);
    for(const auto& probe:request.local_int_check->probes) {
        const auto variant=curriculum::cpp_local_int_override(request.source_utf8,probe);
        if(!variant)return {};
        frame+=field(probe.variable)+field(std::to_string(probe.replacement))+
            field(state::sha256(*variant));
        if(probe.after_update)frame+=field("after-update-v1");
        if(probe.expected_increase)
            frame+=field("exact-increase-v1")+
                field(std::to_string(*probe.expected_increase));
        for(const auto& expected:probe.expected_stdout)
            frame+=field(expected);
    }
    return state::sha256(frame);
}

std::string local_string_fingerprint(const CppRunRequest& request) {
    if(!request.local_string_check||request.local_int_check||
       !curriculum::cpp_local_string_check_valid(*request.local_string_check)||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||
       request.diagnostic||request.cmake||!request.project_files.empty()||
       !request.source_files.empty()||request.named_rule_probe||
       request.open_battery_probe||request.boolean_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.indexed_unique_transfer_probe||
       request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.derived_preparation_probe||request.abstract_interface_probe||
       request.interface_collection_probe||request.polymorphic_clone_probe||
       request.dual_interface_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.cmake_graph_check||request.capture_cmake_graph)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    std::string frame="compiler-local-string-dependency-v1/"+
        field(request.project_key)+field(request.source_utf8)+field(request.stdin_utf8);
    for(const auto& probe:request.local_string_check->probes) {
        const auto instrumented=curriculum::cpp_local_string_override(request.source_utf8,probe);
        if(!instrumented)return {};
        frame+=field(probe.variable)+field(probe.replacement)+
            field(std::to_string(static_cast<int>(probe.moment)))+
            field(state::sha256(*instrumented));
        for(const auto& expected:probe.expected_stdout)frame+=field(expected);
    }
    return state::sha256(frame);
}

// This separate domain never alters a historical CMake/source fingerprint.
// The exact source, trusted owner, request/revision, input and driver all bind
// the private execution to this specific student attempt.

// Dedicated fingerprint: the complete submitted source, immutable authoring
// and all three generated driver hashes are sealed to one request/revision.
// Existing single-file and CMake fingerprints stay byte-for-byte unchanged.
std::string defaulted_move_policy_fingerprint(const CppRunRequest& request) {
    if(!request.defaulted_move_policy_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.defaulted_move_policy_probe;
    std::array<std::string,3> driver_hashes;
    const std::array modes{
        curriculum::CppDefaultedMoveProbe::success,
        curriculum::CppDefaultedMoveProbe::rejected_copy_constructor,
        curriculum::CppDefaultedMoveProbe::rejected_copy_assignment};
    for(std::size_t i=0;i<modes.size();++i) {
        const auto driver=curriculum::cpp_defaulted_move_policy_probe_source(
            spec,request.source_utf8,modes[i]);
        if(!driver)return {};
        driver_hashes[i]=state::sha256(*driver);
    }
    const auto field=[](std::string_view value){
        return std::to_string(value.size())+":"+std::string(value);
    };
    auto frame=std::string("windows-defaulted-move-policy-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.class_name)+field(spec.member_name)+
        field(spec.put_method)+field(spec.change_method)+
        field(spec.clear_method)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes));
    for(const auto& digest:driver_hashes)frame+=field(digest);
    return state::sha256(frame);
}
std::string independent_copy_assignment_fingerprint(const CppRunRequest& request) {
    if(!request.independent_copy_assignment_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.named_rule_probe||request.boolean_battery_probe||
       request.open_battery_probe||request.reduction_probe||
       request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||
       request.assertion_mode||request.capture_cmake_graph||
       request.cmake_graph_check)return {};
    const auto& spec=*request.independent_copy_assignment_probe;
    const auto driver=curriculum::cpp_independent_copy_probe_source(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view value){
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-independent-copy-assignment-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.class_name)+field(spec.member_name)+
        field(spec.parameter_name)+field(spec.has_method)+
        field(spec.value_method)+field(spec.access_method)+
        field(spec.put_method)+field(spec.change_method)+
        field(spec.clear_method)+field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string move_assignment_invariant_fingerprint(const CppRunRequest& request) {
    if(!request.move_assignment_invariant_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.move_assignment_invariant_probe;
    const auto driver=curriculum::cpp_move_invariant_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text) {
        return std::to_string(text.size())+":"+std::string(text);
    };
    return state::sha256(std::string("windows-move-assignment-invariants-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.class_name)+field(spec.member_name)+
        field(spec.label_member)+field(spec.parameter_name)+
        field(spec.has_method)+field(spec.value_method)+field(spec.label_method)+
        field(spec.access_method)+field(spec.invariant_method)+
        field(spec.put_method)+field(spec.change_method)+field(spec.retag_method)+
        field(spec.clear_method)+field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string array_owner_read_fingerprint(const CppRunRequest& request) {
    if(!request.array_owner_read_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.array_owner_read_probe;
    const auto driver=curriculum::cpp_array_owner_read_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text){
        return std::to_string(text.size())+":"+std::string(text);
    };
    return state::sha256(std::string("windows-array-owner-read-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.class_name)+field(spec.owner_member)+field(spec.count_member)+
        field(spec.quantity_parameter)+field(spec.index_parameter)+
        field(spec.output_parameter)+field(spec.configure_method)+
        field(spec.read_method)+field(spec.quantity_method)+
        field(spec.access_method)+field(spec.write_method)+
        field(spec.clear_method)+field(spec.sum_method)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string array_copy_move_fingerprint(const CppRunRequest& request) {
    if(!request.array_copy_move_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.named_rule_probe||request.boolean_battery_probe||
       request.open_battery_probe||request.reduction_probe||
       request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.array_copy_move_probe;
    const auto driver=curriculum::cpp_array_copy_move_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text){
        return std::to_string(text.size())+":"+std::string(text);
    };
    return state::sha256(std::string("windows-array-copy-move-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.class_name)+field(spec.owner_member)+field(spec.count_member)+
        field(spec.copy_parameter)+field(spec.move_parameter)+
        field(spec.quantity_method)+field(spec.access_method)+
        field(spec.configure_method)+field(spec.read_method)+
        field(spec.write_method)+field(spec.clear_method)+field(spec.sum_method)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string array_return_take_fingerprint(const CppRunRequest& request) {
    if(!request.array_return_take_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.array_return_take_probe;
    const auto driver=curriculum::cpp_array_return_take_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text){
        return std::to_string(text.size())+":"+std::string(text);
    };
    return state::sha256(std::string("windows-array-return-take-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.class_name)+field(spec.owner_member)+field(spec.count_member)+
        field(spec.copy_function)+field(spec.take_function)+
        field(spec.source_parameter)+field(spec.index_parameter)+
        field(spec.value_parameter)+field(spec.destination_parameter)+
        field(spec.offered_parameter)+field(spec.limit_parameter)+
        field(spec.quantity_method)+field(spec.access_method)+
        field(spec.configure_method)+field(spec.read_method)+
        field(spec.write_method)+field(spec.clear_method)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string polymorphic_destruction_fingerprint(const CppRunRequest& request) {
    if(!request.polymorphic_destruction_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.named_rule_probe||request.boolean_battery_probe||
       request.open_battery_probe||request.reduction_probe||
       request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.polymorphic_destruction_probe;
    const auto driver=curriculum::cpp_polymorphic_destruction_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text) {
        return std::to_string(text.size())+":"+std::string(text);
    };
    std::string key=std::string("windows-polymorphic-destruction-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.editable.original_source)+
        field(spec.base_class)+field(spec.derived_class)+field(spec.cycle_class)+
        field(spec.owned_class)+field(spec.owned_member)+
        field(spec.factory_function)+field(spec.write_method)+
        field(spec.base_alive)+field(spec.derived_alive)+field(spec.owned_alive)+
        field(spec.base_destroyed)+field(spec.derived_destroyed)+
        field(spec.owned_destroyed)+field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes));
    for(const auto& body:spec.editable.bodies)
        key+=field(body.signature)+field(body.qualified?"qualified":"free");
    return state::sha256(key);
}

std::string virtual_lifetime_fingerprint(const CppRunRequest& request) {
    if(!request.virtual_lifetime_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||
       request.named_rule_probe||request.boolean_battery_probe||
       request.open_battery_probe||request.reduction_probe||
       request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.virtual_lifetime_probe;
    const auto driver=curriculum::cpp_virtual_lifetime_dispatch_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text) {
        return std::to_string(text.size())+":"+std::string(text);
    };
    std::string key=std::string("windows-virtual-lifetime-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.editable.original_source)+
        field(spec.base_class)+field(spec.derived_class)+
        field(spec.register_method)+field(spec.write_method)+
        field(spec.phase_parameter)+field(spec.register_stream)+
        field(spec.write_stream)+field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes));
    for(const auto& body:spec.editable.bodies)
        key+=field(body.signature)+field(body.qualified?"qualified":"free");
    return state::sha256(key);
}

std::string derived_preparation_fingerprint(const CppRunRequest& request) {
    if(!request.derived_preparation_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.named_rule_probe||request.boolean_battery_probe||
       request.open_battery_probe||request.reduction_probe||
       request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.derived_preparation_probe;
    const auto driver=curriculum::cpp_derived_preparation_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view text) {
        return std::to_string(text.size())+":"+std::string(text);
    };
    std::string key=std::string("windows-derived-preparation-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.editable.original_source)+
        field(spec.base_class)+field(spec.derived_class)+
        field(spec.balance_class)+field(spec.resource_class)+
        field(spec.replacement_function)+field(spec.destination_parameter)+
        field(spec.number_parameter)+field(spec.code_parameter)+
        field(spec.mode_parameter)+field(spec.balance_parameter)+
        field(spec.print_method)+field(spec.resource_alive)+
        field(spec.base_complete)+field(spec.derived_complete)+
        field(spec.resource_complete)+field(spec.base_destroyed)+
        field(spec.derived_destroyed)+field(spec.resource_destroyed)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes));
    for(const auto& body:spec.editable.bodies)
        key+=field(body.signature)+field(body.qualified?"qualified":"free");
    return state::sha256(key);
}

std::string abstract_interface_fingerprint(const CppRunRequest& request) {
    if(!request.abstract_interface_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.derived_preparation_probe||request.interface_collection_probe||
       request.polymorphic_clone_probe||request.dual_interface_probe||
       request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.abstract_interface_probe;
    const auto driver=curriculum::cpp_abstract_interface_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-abstract-interface-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.base_class)+field(spec.simple_class)+
        field(spec.identified_class)+field(spec.create_function)+
        field(spec.present_function)+field(spec.number_method)+
        field(spec.code_method)+field(spec.write_method)+
        field(spec.stream_parameter)+field(spec.type_parameter)+
        field(spec.number_parameter)+field(spec.code_parameter)+
        field(spec.object_parameter)+field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string interface_collection_fingerprint(const CppRunRequest& request) {
    if(!request.interface_collection_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.abstract_interface_probe||request.polymorphic_clone_probe||
       request.dual_interface_probe||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.derived_preparation_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.interface_collection_probe;
    const auto driver=curriculum::cpp_interface_collection_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-interface-collection-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.base_class)+field(spec.simple_class)+
        field(spec.identified_class)+field(spec.vector_alias)+
        field(spec.add_function)+field(spec.report_function)+
        field(spec.write_method)+field(spec.number_method)+
        field(spec.destination_parameter)+field(spec.source_parameter)+
        field(spec.data_parameter)+field(spec.stream_parameter)+
        field(spec.count_label)+field(spec.index_separator)+
        field(spec.number_label)+field(spec.code_label)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string polymorphic_clone_fingerprint(const CppRunRequest& request) {
    if(!request.polymorphic_clone_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.abstract_interface_probe||request.interface_collection_probe||
       request.dual_interface_probe||request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.derived_preparation_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.polymorphic_clone_probe;
    const auto driver=curriculum::cpp_polymorphic_clone_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-polymorphic-clone-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.base_class)+field(spec.simple_class)+
        field(spec.identified_class)+field(spec.vector_alias)+
        field(spec.clone_method)+field(spec.duplicate_function)+
        field(spec.number_method)+field(spec.change_method)+
        field(spec.write_method)+field(spec.code_method)+
        field(spec.source_parameter)+field(spec.stream_parameter)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string dual_interface_fingerprint(const CppRunRequest& request) {
    if(!request.dual_interface_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.cmake||!request.project_files.empty()||!request.source_files.empty()||
       request.abstract_interface_probe||request.interface_collection_probe||
       request.polymorphic_clone_probe||
       request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
       request.independent_copy_assignment_probe||
       request.move_assignment_invariant_probe||request.array_owner_read_probe||
       request.array_copy_move_probe||request.array_return_take_probe||
       request.polymorphic_destruction_probe||request.virtual_lifetime_probe||
       request.derived_preparation_probe||request.named_rule_probe||
       request.boolean_battery_probe||request.open_battery_probe||
       request.reduction_probe||request.integer_battery_probe||
       request.adjustment_project_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||
       request.capture_cmake_graph||request.cmake_graph_check)return {};
    const auto& spec=*request.dual_interface_probe;
    const auto driver=curriculum::cpp_dual_interface_private_driver(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-dual-interface-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.original_source)+
        field(spec.value_interface)+field(spec.report_interface)+
        field(spec.concrete_class)+field(spec.value_method)+
        field(spec.write_method)+field(spec.change_method)+
        field(spec.stream_parameter)+field(spec.change_parameter)+
        field(spec.code_member)+field(spec.value_member)+
        field(spec.constructor_code_parameter)+
        field(spec.constructor_value_parameter)+field(spec.report_separator)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes)));
}

std::string indexed_unique_transfer_fingerprint(const CppRunRequest& request) {
    if(!request.indexed_unique_transfer_probe ||
       request.backend!=CppRunRequest::Backend::native_msvc ||
       request.action!=CppRunRequest::Action::compile_run || request.diagnostic ||
       request.cmake || !request.project_files.empty() ||
       !request.source_files.empty() || request.named_rule_probe ||
       // An independent identity witness must never share a request with a
       // different curricular semantic probe, even before the lesson grader
       // verifies the sealed owner. Otherwise two unrelated attestations
       // could run in the same nested native worker.
       request.defaulted_move_policy_probe ||
       request.independent_copy_assignment_probe ||
       request.move_assignment_invariant_probe ||
       request.array_owner_read_probe || request.array_copy_move_probe ||
       request.array_return_take_probe || request.polymorphic_destruction_probe ||
       request.virtual_lifetime_probe || request.derived_preparation_probe ||
       request.abstract_interface_probe || request.interface_collection_probe ||
       request.polymorphic_clone_probe || request.dual_interface_probe ||
       request.local_int_check ||
       request.open_battery_probe || request.boolean_battery_probe ||
       request.reduction_probe || request.integer_battery_probe ||
       request.adjustment_project_probe || request.rejection_probe ||
       request.observation_probe || request.assertion_mode ||
       request.capture_cmake_graph || request.cmake_graph_check)
        return {};
    const auto& spec=*request.indexed_unique_transfer_probe;
    const auto driver=curriculum::cpp_indexed_unique_transfer_probe_source(
        spec,request.source_utf8);
    if(!driver)return {};
    const auto field=[](std::string_view value){
        return std::to_string(value.size())+":"+std::string(value);
    };
    auto frame=std::string("win32-indexed-unique-transfer-proof-v1/")+
        field(std::to_string(request.request_id))+
        field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(request.project_key)+field(spec.member.original_source)+
        field(spec.member.signature)+field(spec.class_name)+
        field(spec.first_member)+field(spec.second_member)+
        field(spec.transfer_method)+field(spec.put_method)+
        field(spec.change_method)+field(spec.clear_method)+
        field(state::sha256(*driver))+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes));
    return state::sha256(frame);
}

// A separate domain preserves every existing CMake fingerprint. Source and
// prepared input must participate when no projectFiles exist.
bool named_rule_battery_requested(const CppRunRequest& request) {
    return request.named_rule_probe&&request.named_rule_probe->battery_check.has_value();
}
std::string named_rule_battery_fingerprint(const CppRunRequest& request) {
    if(!named_rule_battery_requested(request)||
       !curriculum::named_rule_battery_spec_valid(*request.named_rule_probe->battery_check))
        return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    const auto& probe=*request.named_rule_probe;
    auto frame=std::string("windows-named-rule-battery-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+field(fingerprint(request))+
        field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(probe.harness_path)+field(probe.header)+
        field(std::to_string(probe.cases.size()));
    for(const auto& item:probe.cases)
        frame+=field(item.function)+field(std::to_string(item.input))+
            field(std::to_string(item.expected));
    frame+=field(curriculum::named_rule_battery_spec_frame(*probe.battery_check));
    return state::sha256(frame);
}
bool named_rule_battery_context(const CppRunRequest& request) {
    if(!named_rule_battery_requested(request)||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||!request.stdin_utf8.empty()||
       request.diagnostic||request.open_battery_probe||request.boolean_battery_probe||
       request.reduction_probe||request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.cmake_graph_check||request.capture_cmake_graph||!request.source_files.empty())return false;
    const auto& spec=*request.named_rule_probe->battery_check;
    if(!curriculum::named_rule_battery_spec_valid(spec))return false;
    if(request.cmake)return !request.project_files.empty()&&!spec.source_path.empty()&&
        !request.named_rule_probe->harness_path.empty()&&
        spec.source_path!=request.named_rule_probe->harness_path;
    return request.project_files.empty()&&spec.source_path.empty()&&
        request.named_rule_probe->harness_path.empty()&&request.named_rule_probe->header.empty();
}
bool named_rule_constant_variations_requested(const CppRunRequest& request) {
    return request.named_rule_probe&&
        request.named_rule_probe->constant_variations.has_value();
}
std::string named_rule_constant_variation_fingerprint(const CppRunRequest& request) {
    if(!named_rule_constant_variations_requested(request))return {};
    const auto& probe=*request.named_rule_probe;
    const auto& spec=*probe.constant_variations;
    if(!curriculum::named_rule_constant_variation_spec_valid(spec,probe.cases))return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    auto frame=std::string("windows-named-rule-constant-variation-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+field(fingerprint(request))+
        field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(probe.harness_path)+field(probe.header)+
        field(std::to_string(probe.cases.size()));
    for(const auto& item:probe.cases)
        frame+=field(item.function)+field(std::to_string(item.input))+
            field(std::to_string(item.expected));
    frame+=field(curriculum::named_rule_constant_variation_spec_frame(spec));
    if(probe.battery_check)
        frame+=field(curriculum::named_rule_battery_spec_frame(*probe.battery_check));
    return state::sha256(frame);
}
bool named_rule_constant_variation_context(const CppRunRequest& request) {
    if(!named_rule_constant_variations_requested(request)||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       request.open_battery_probe||request.boolean_battery_probe||
       request.reduction_probe||request.integer_battery_probe||request.adjustment_project_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.cmake_graph_check||request.capture_cmake_graph||!request.source_files.empty())return false;
    const auto& probe=*request.named_rule_probe;
    const auto& spec=*probe.constant_variations;
    if(!curriculum::named_rule_constant_variation_spec_valid(spec,probe.cases))return false;
    if(request.cmake)return !request.project_files.empty()&&!spec.source_path.empty()&&
        !probe.harness_path.empty()&&spec.source_path!=probe.harness_path;
    return request.project_files.empty()&&spec.source_path.empty()&&
        probe.harness_path.empty()&&probe.header.empty();
}
struct NamedRuleConstantVariationRequest {
    CppRunRequest request;
    std::string driver_sha256,program_sha256;
};
std::optional<NamedRuleConstantVariationRequest> named_rule_constant_variation_request(
        const CppRunRequest& original,std::size_t index) {
    if(!named_rule_constant_variation_context(original))return std::nullopt;
    const auto& probe=*original.named_rule_probe;
    const auto& spec=*probe.constant_variations;
    const auto varied_probe=curriculum::named_rule_constant_variation_probe(probe,index);
    if(!varied_probe)return std::nullopt;
    std::string_view original_source=original.source_utf8;
    if(original.cmake) {
        const auto source=std::ranges::find(original.project_files,spec.source_path,
            &curriculum::CppProjectFiles::value_type::first);
        if(source==original.project_files.end())return std::nullopt;
        original_source=source->second;
    }
    const auto varied=curriculum::named_rule_constant_variation_source(original_source,probe,index);
    if(!varied)return std::nullopt;
    const auto driver=original.cmake?curriculum::named_rule_probe_source(*varied_probe):
        curriculum::named_rule_single_file_probe_source(*varied_probe,*varied);
    if(!driver)return std::nullopt;
    NamedRuleConstantVariationRequest phase{original,{},{}};
    auto& request=phase.request;
    request.named_rule_probe.reset();
    request.stdin_utf8.clear();
    request.request_id=~original.request_id^(static_cast<std::uint64_t>(index)+1);
    // Include the exact original source and contract in the private workspace
    // identity. A fast next attempt cannot inherit an incremental old object.
    request.project_key="named-rule-constant-variation-v1/"+
        named_rule_constant_variation_fingerprint(original)+"/"+std::to_string(index);
    if(original.cmake) {
        const auto source=std::ranges::find(request.project_files,spec.source_path,
            &curriculum::CppProjectFiles::value_type::first);
        const auto harness=std::ranges::find(request.project_files,probe.harness_path,
            &curriculum::CppProjectFiles::value_type::first);
        if(source==request.project_files.end()||harness==request.project_files.end())
            return std::nullopt;
        source->second=*varied;
        harness->second=*driver;
        phase.program_sha256=state::sha256(curriculum::cpp_project_frame(
            request.project_files,request.source_files,request.cmake));
    }else{
        request.source_utf8=*driver;
        phase.program_sha256=state::sha256(*varied);
    }
    phase.driver_sha256=state::sha256(*driver);
    return phase;
}
bool named_rule_probe_completed(const CppNamedRuleProbe& probe) {
    return probe.compilation.succeeded()&&probe.execution.succeeded()&&
        probe.execution.stdout_bytes.empty()&&
        probe.execution.stderr_bytes==curriculum::named_rule_probe_completion_marker;
}
std::string boolean_battery_fingerprint(const CppRunRequest& request) {
    if(!request.boolean_battery_probe)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    const auto& spec=*request.boolean_battery_probe;
    auto frame=std::string("windows-boolean-battery-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(fingerprint(request))+field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(spec.function_name)+field(spec.protected_definition)+
        field(std::to_string(static_cast<int>(spec.published_result)))+
        field(std::to_string(spec.cases.size()));
    for(const auto& item:spec.cases)
        frame+=field(std::to_string(item.first))+field(std::to_string(item.second))+
            field(item.expected?"true":"false");
    return state::sha256(frame);
}
std::string reduction_fingerprint(const CppRunRequest& request) {
    if(!request.reduction_probe||!curriculum::reduction_spec_valid(*request.reduction_probe))return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    const auto frame=std::string("windows-reduction-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+
        field(fingerprint(request))+field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(curriculum::reduction_spec_frame(*request.reduction_probe));
    return state::sha256(frame);
}
std::string integer_battery_fingerprint(const CppRunRequest& request) {
    if(!request.integer_battery_probe||
       !curriculum::integer_battery_spec_valid(*request.integer_battery_probe))return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-integer-battery-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+field(fingerprint(request))+
        field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(curriculum::integer_battery_spec_frame(*request.integer_battery_probe))+
        field(request.integer_battery_prerequisite));
}
bool integer_battery_context(const CppRunRequest& request) {
    return request.integer_battery_probe&&request.cmake&&!request.project_files.empty()&&
        request.source_files.empty()&&request.backend==CppRunRequest::Backend::native_msvc&&
        !request.diagnostic&&!request.boolean_battery_probe&&!request.reduction_probe&&
        !request.open_battery_probe&&!request.named_rule_probe&&!request.rejection_probe&&
        !request.observation_probe&&!request.assertion_mode&&!request.cmake_graph_check&&
        !request.adjustment_project_probe&&
        !request.capture_cmake_graph&&
        curriculum::integer_battery_spec_valid(*request.integer_battery_probe);
}
// New capture domain only: existing project, objective and receipt frames are
// deliberately unchanged. Every authored graph field participates so a weaker
// graph cannot borrow a proof captured for a different construction contract.
std::string adjustment_graph_frame(const curriculum::CmakeGraphRequirement& graph) {
    std::string result;
    const auto field=[&](std::string_view value) {
        result+=std::to_string(value.size())+":"+std::string(value);
    };
    const auto strings=[&](const std::vector<std::string>& values) {
        field(std::to_string(values.size()));for(const auto& value:values)field(value);
    };
    const auto pairs=[&](const std::vector<std::pair<std::string,std::string>>& values) {
        field(std::to_string(values.size()));
        for(const auto& [name,value]:values){field(name);field(value);}
    };
    field("adjustment-cmake-graph-v1");field(graph.project_name);
    field(graph.minimum_cmake_version);
    field(graph.minimum_cmake_version_at_least?"floor":"exact");
    field(std::to_string(graph.targets.size()));
    for(const auto& target:graph.targets) {
        field(target.name);field(target.type);strings(target.sources);strings(target.links_to);
        field(target.exact_sources?"exact":"subset");
        field(target.require_explicit_cxx20?"cxx20":"inherited-cxx");
        field(target.require_explicit_extensions_off?"extensions-off":"inherited-extensions");
        strings(target.public_include_roots);strings(target.private_include_roots);
        field(target.source_directory);strings(target.authored_compile_options);
        field(target.require_explicit_msvc_warnings?"msvc-warnings":"no-warning-requirement");
    }
    field(graph.option_selection?"option":"no-option");
    if(graph.option_selection) {
        const auto& option=*graph.option_selection;
        field(option.name);field(option.library_target);
        field(option.disabled_source);field(option.disabled_stdout);
    }
    pairs(graph.source_integrity);pairs(graph.configure_message_order);
    field(graph.target_list?"list":"no-list");
    if(graph.target_list) {
        strings(graph.target_list->targets);
        field(graph.target_list->require_append?"append":"no-append");
    }
    // An absent repair retains every byte of the earlier graph frame.
    if(graph.expression_repair)
        field(curriculum::cpp_expression_repair_frame(*graph.expression_repair));
    if(graph.initial_observation)
        field(curriculum::cmake_initial_observation_frame(*graph.initial_observation));
    return result;
}
void validate_request_project(const CppRunRequest& request);
bool authored_cmake_path(std::string_view value);
bool cmake_initial_observation_context(const CppRunRequest& request) {
    if(!request.cmake||!request.cmake_graph_check||!request.capture_cmake_graph||
       request.backend!=CppRunRequest::Backend::native_msvc||request.diagnostic||
       request.boolean_battery_probe||request.reduction_probe||request.integer_battery_probe||
       request.open_battery_probe||request.named_rule_probe||request.rejection_probe||
       request.observation_probe||request.assertion_mode||request.adjustment_project_probe||
       !curriculum::cmake_initial_observation_valid(*request.cmake_graph_check))return false;
    try { validate_request_project(request); }
    catch(const std::exception&) { return false; }
    catch(const winrt::hresult_error&) { return false; }
    return true;
}
std::string cmake_initial_observation_component(const CppRunRequest& request) {
    if(!cmake_initial_observation_context(request))return {};
    const auto& graph=*request.cmake_graph_check;
    const auto& spec=*graph.initial_observation;
    const auto& repair=*graph.expression_repair;
    if(request.cmake->target!=spec.executable)return {};
    auto normalized=request;
    bool expression=false;
    std::vector<std::string> protected_paths;
    for(const auto& [path,digest]:graph.source_integrity) {
        if(path==repair.path||authored_cmake_path(path)||digest.size()!=64||
           std::ranges::find(protected_paths,path)!=protected_paths.end())return {};
        const auto file=std::ranges::find_if(request.project_files,
            [&](const auto& item){return item.first==path;});
        if(file==request.project_files.end()||state::sha256(file->second)!=digest)return {};
        protected_paths.push_back(path);
    }
    for(auto& [path,source]:normalized.project_files) {
        if(path==repair.path) {
            if(expression||!curriculum::cpp_expression_repair_matches(repair,source))return {};
            source=repair.original_source;expression=true;
        }else if(!authored_cmake_path(path)&&
                 std::ranges::find(protected_paths,path)==protected_paths.end())return {};
    }
    if(!expression)return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-cmake-initial-observation-component-v1/")+
        field(fingerprint(normalized))+field(fingerprint(normalized,true))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(adjustment_graph_frame(graph)));
}
std::string cmake_initial_observation_fingerprint(const CppRunRequest& request) {
    if(!cmake_initial_observation_context(request)||
       (request.action!=CppRunRequest::Action::build&&
        request.action!=CppRunRequest::Action::compile_run&&
        request.action!=CppRunRequest::Action::execute))return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-cmake-initial-observation-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+field(fingerprint(request))+
        field(fingerprint(request,true))+
        field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(adjustment_graph_frame(*request.cmake_graph_check))+
        field(request.cmake_initial_observation_prerequisite));
}
std::string cmake_graph_fingerprint(const CppRunRequest& request) {
    if(!request.cmake||!request.cmake_graph_check||!request.capture_cmake_graph||
       request.project_files.empty()||request.backend!=CppRunRequest::Backend::native_msvc||
       request.diagnostic||
       (request.action!=CppRunRequest::Action::compile_run&&
        request.action!=CppRunRequest::Action::execute)||
       (request.cmake_graph_check->expression_repair&&
        !curriculum::cpp_expression_repair_valid(
            *request.cmake_graph_check->expression_repair))||
       (request.cmake_graph_check->initial_observation&&
        !curriculum::cmake_initial_observation_valid(*request.cmake_graph_check)))return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    auto frame=std::string("windows-cmake-graph-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+field(fingerprint(request))+
        field(fingerprint(request,true))+
        field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(adjustment_graph_frame(*request.cmake_graph_check));
    if(request.cmake_graph_check->initial_observation)
        frame+=field(request.cmake_initial_observation_prerequisite);
    return state::sha256(frame);
}
std::string adjustment_project_fingerprint(const CppRunRequest& request) {
    if(!request.adjustment_project_probe||!request.cmake_graph_check||
       !curriculum::adjustment_project_spec_valid(*request.adjustment_project_probe))return {};
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-adjustment-project-capture-v1/")+
        field(std::to_string(request.request_id))+field(std::to_string(request.revision))+
        field(request.source_utf8)+field(request.stdin_utf8)+field(fingerprint(request))+
        field(std::to_string(static_cast<int>(request.backend)))+
        field(std::to_string(static_cast<int>(request.action)))+
        field(utf8(request.projects_directory.wstring()))+field(request.project_key)+
        field(std::to_string(request.compile_timeout.count()))+
        field(std::to_string(request.execution_timeout.count()))+
        field(std::to_string(request.output_limit_bytes))+
        field(curriculum::adjustment_project_spec_frame(*request.adjustment_project_probe))+
        field(adjustment_graph_frame(*request.cmake_graph_check))+
        field(request.capture_cmake_graph?"capture-graph":"no-graph"));
}
bool adjustment_project_sources_use_interface(const CppRunRequest& request) {
    if(!request.adjustment_project_probe)return false;
    const auto& spec=*request.adjustment_project_probe;
    for(const auto* path:{&spec.library_path,&spec.client_path,&spec.test_path}) {
        const auto found=std::ranges::find_if(request.project_files,
            [&](const auto& file){return file.first==*path;});
        if(found==request.project_files.end()||
           !curriculum::adjustment_project_source_includes_header(found->second,spec))return false;
    }
    return true;
}
bool adjustment_project_context(const CppRunRequest& request) {
    return request.adjustment_project_probe&&request.cmake&&request.cmake_graph_check&&
        request.capture_cmake_graph&&!request.project_files.empty()&&request.source_files.empty()&&
        request.backend==CppRunRequest::Backend::native_msvc&&request.stdin_utf8.empty()&&
        !request.diagnostic&&!request.boolean_battery_probe&&!request.reduction_probe&&
        !request.open_battery_probe&&!request.integer_battery_probe&&!request.named_rule_probe&&
        !request.rejection_probe&&!request.observation_probe&&!request.assertion_mode&&
        curriculum::adjustment_project_spec_valid(*request.adjustment_project_probe)&&
        curriculum::adjustment_project_graph_valid(*request.adjustment_project_probe,
            *request.cmake_graph_check)&&
        adjustment_project_sources_use_interface(request)&&
        request.cmake->target==request.adjustment_project_probe->test_target;
}
void validate_request_project(const CppRunRequest& request){
    if(!request.projects_directory.is_absolute())throw std::invalid_argument("Student projects directory must be absolute");
    std::map<std::wstring,bool,CaseInsensitive> paths;for(const auto& [name,text]:request.project_files)if(!paths.emplace(project_path(name).generic_wstring(),true).second)throw std::invalid_argument("Case-colliding Windows project paths");
    for(const auto& [name,unused]:paths)for(auto slash=name.find(L'/');slash!=name.npos;slash=name.find(L'/',slash+1))if(paths.contains(name.substr(0,slash)))throw std::invalid_argument("A project path is both a Windows file and a directory");
    if(request.cmake){curriculum::validate_cpp_project_files(request.project_files);curriculum::validate_cmake_config(*request.cmake);std::size_t bytes=0;for(const auto& [path,text]:request.project_files)bytes+=text.size();if(request.project_files.size()>128||bytes>8*1024*1024)throw std::invalid_argument("CMake project exceeds its original128-file/8MiB limits");
        if(request.project_key.empty()||request.project_key.size()>16384)throw std::invalid_argument("CMake needs the stable existing draft identity");
        if(!request.source_files.empty())throw std::invalid_argument("CMake owns its source selection");
    }else curriculum::validate_cpp_project(request.project_files,request.source_files);
}
std::string cmake_workspace_identity(const CppRunRequest& request){
    const auto identity="cpp-cmake-workspace-v1/"+std::to_string(request.project_key.size())+":"+request.project_key+fingerprint(request,true);
    return state::sha256(identity);
}
std::filesystem::path cmake_workspace(const CppRunRequest& request){
    // Child MSVC tools still need room for CMakeScratch/TryCompile paths.
    // The shortened directory is only an address; the full digest below is
    // mandatory before reading or modifying any generated project data.
    return request.projects_directory/project_path("c"+cmake_workspace_identity(request).substr(0,32));
}
bool verify_workspace_identity(const CppRunRequest& request,bool create){
    const auto workspace=cmake_workspace(request),marker=workspace/L"identity.sha256";
    state::require_safe_path(workspace);
    if(!std::filesystem::exists(workspace)){
        if(!create)return false;
        std::filesystem::create_directories(workspace);state::require_safe_path(workspace);
    }
    state::require_safe_path(marker);
    const auto expected=cmake_workspace_identity(request)+"\n";
    const auto verify=[&]{
        if(!std::filesystem::is_regular_file(marker)||read_exact(marker,128)!=expected)
            throw std::runtime_error("CMake workspace identity collision or invalid ownership manifest");
    };
    if(std::filesystem::exists(marker)){verify();return true;}
    if(!create)return false;
    if(!std::filesystem::is_empty(workspace))throw std::runtime_error("CMake workspace has data without a valid ownership manifest");
    // CREATE_NEW never overwrites a concurrently established owner. Its writer
    // denies sharing until the complete digest has been flushed and closed.
    Handle owner(CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!owner){if(GetLastError()==ERROR_FILE_EXISTS){verify();return true;}windows_error("Create CMake ownership manifest");}
    DWORD written=0;
    if(!WriteFile(owner.get(),expected.data(),static_cast<DWORD>(expected.size()),&written,nullptr)||written!=expected.size()||!FlushFileBuffers(owner.get()))
        windows_error("Write CMake ownership manifest");
    return true;
}
JsonObject read_json(const std::filesystem::path& path){state::require_safe_path(path);const auto bytes=state::read_bytes(path,8*1024*1024);return JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));}
std::string json_text(const JsonObject& object,const wchar_t* key){return winrt::to_string(object.GetNamedString(key,L""));}
void save_json(const std::filesystem::path& path,const JsonObject& value){state::require_safe_path(path);write_exact(path,winrt::to_string(value.Stringify()));}
void materialize(const curriculum::CppProjectFiles& files,const std::filesystem::path& directory){
    state::require_safe_path(directory);std::filesystem::create_directories(directory);state::require_safe_path(directory);
    for(const auto& [name,text]:files){const auto path=directory/project_path(name);state::require_safe_path(path);std::filesystem::create_directories(path.parent_path());state::require_safe_path(path);
        if(std::filesystem::is_regular_file(path)&&std::filesystem::file_size(path)==text.size()&&read_exact(path,text.size())==text)continue;write_exact(path,text);}
}
std::optional<std::string> materialized_project_mismatch(
        const curriculum::CppProjectFiles& files,const std::filesystem::path& directory) {
    state::require_safe_path(directory);
    // Only the already validated, bounded request files belong to this check.
    // Additional generated sources remain supported; File API paths alone do
    // not authenticate the contents of the learner's materialized sources.
    for(const auto& [name,text]:files) {
        const auto path=directory/project_path(name);state::require_safe_path(path);
        if(!std::filesystem::is_regular_file(path)||
           std::filesystem::file_size(path)!=text.size()||read_exact(path,text.size())!=text)
            return name;
    }
    return {};
}
void require_materialized_project(const CppRunRequest& request,
        const std::filesystem::path& source,const std::filesystem::path& state_file,
        std::string_view phase) {
    const auto changed=materialized_project_mismatch(request.project_files,source);
    if(!changed)return;
    // An observed mutation invalidates generated availability only. Preserve
    // the files for diagnosis, retained workspace data, drafts and run history.
    auto saved=read_json(state_file);
    for(const auto key:{L"configuration",L"linkProfile",L"built",L"artifact",L"artifactSha256"})
        if(saved.HasKey(key))saved.Remove(key);
    save_json(state_file,saved);
    throw std::runtime_error("El archivo "+*changed+
        " no coincide con el código del editor al comprobar "+std::string(phase)+
        ". No se ejecutará el programa. Configura y construye de nuevo conservando los archivos del editor.");
}
std::shared_ptr<std::timed_mutex> project_mutex(const std::filesystem::path& workspace){static std::mutex lock;static std::map<std::filesystem::path,std::weak_ptr<std::timed_mutex>> locks;
    const std::lock_guard guard(lock);auto& entry=locks[workspace];if(const auto found=entry.lock())return found;auto value=std::make_shared<std::timed_mutex>();entry=value;return value;}
struct Target{std::string type;std::vector<std::filesystem::path> artifacts;};
std::filesystem::path contained(const std::filesystem::path& root,const std::filesystem::path& path){const auto absolute=(path.is_absolute()?path:root/path).lexically_normal();const auto relative=absolute.lexically_relative(root.lexically_normal());
    if(relative.empty()||relative.is_absolute()||*relative.begin()==L"..")throw std::runtime_error("CMake artifact or reply escapes its owned build directory");state::require_safe_path(absolute);return absolute;
}
Target cmake_target(const std::filesystem::path& workspace,const curriculum::CmakeConfig& config){const auto& build=workspace;const auto reply=build/L".cmake/api/v1/reply";state::require_safe_path(reply);
    std::filesystem::path latest;for(const auto& file:std::filesystem::directory_iterator(reply))if(file.is_regular_file()&&file.path().filename().wstring().starts_with(L"index-")&&file.path().extension()==L".json"&&(latest.empty()||file.path().filename()>latest.filename()))latest=file.path();
    if(latest.empty())throw std::runtime_error("CMake did not return a File API index");const auto index=read_json(latest);std::optional<JsonObject> model;
    for(const auto& entry:index.GetNamedArray(L"objects")){const auto item=entry.GetObject();if(json_text(item,L"kind")=="codemodel"&&item.GetNamedObject(L"version").GetNamedNumber(L"major")==2){model=read_json(contained(reply,project_path(json_text(item,L"jsonFile"))));break;}}
    if(!model)throw std::runtime_error("CMake did not return codemodel version2");
    for(const auto& entry:model->GetNamedArray(L"configurations")){const auto configuration=entry.GetObject();const auto name=json_text(configuration,L"name");if(!name.empty()&&name!=config.build_type)continue;
        if(!configuration.HasKey(L"targets"))continue;for(const auto& reference:configuration.GetNamedArray(L"targets")){const auto item=reference.GetObject();if(json_text(item,L"name")!=config.target)continue;const auto target=read_json(contained(reply,project_path(json_text(item,L"jsonFile"))));Target result;result.type=json_text(target,L"type");
            if(target.HasKey(L"artifacts"))for(const auto& artifact:target.GetNamedArray(L"artifacts"))result.artifacts.push_back(contained(build,project_path(json_text(artifact.GetObject(),L"path"))));return result;}}
    return {};
}
// Read the complete generated target graph, never a student's stdout or
// a regular-expression guess about the CMakeLists.txt source.
std::optional<int> msvc_warning_level(std::string_view option) {
    if(option.size()<2||(option.front()!='/'&&option.front()!='-'))return {};
    option.remove_prefix(1);
    if(option=="w"||option=="W0")return 0;
    if(option=="Wall")return 5;
    if(option.size()==2&&option.front()=='W'&&option[1]>='1'&&option[1]<='4')
        return option[1]-'0';
    return {};
}
CppCmakeGraphEvidence snapshot_cmake_graph(
        const std::filesystem::path& workspace,
        const CppRunRequest& request,
        std::string_view project_fingerprint,
        std::string_view configuration_fingerprint) {
    const auto reply=workspace/L".cmake/api/v1/reply";
    state::require_safe_path(reply);
    std::filesystem::path latest;
    for(const auto& file:std::filesystem::directory_iterator(reply))
        if(file.is_regular_file()&&file.path().filename().wstring().starts_with(L"index-")&&
           file.path().extension()==L".json"&&
           (latest.empty()||file.path().filename()>latest.filename()))
            latest=file.path();
    if(latest.empty())throw std::runtime_error("CMake File API index is missing");
    const auto index=read_json(latest);
    std::optional<JsonObject> model;
    for(const auto& entry:index.GetNamedArray(L"objects")) {
        const auto item=entry.GetObject();
        if(json_text(item,L"kind")!="codemodel"||
           item.GetNamedObject(L"version").GetNamedNumber(L"major")!=2)continue;
        model=read_json(contained(reply,project_path(json_text(item,L"jsonFile"))));
        break;
    }
    if(!model)throw std::runtime_error("CMake File API did not supply codemodel v2");
    CppCmakeGraphEvidence graph;
    graph.project_fingerprint=std::string(project_fingerprint);
    graph.configuration_fingerprint=std::string(configuration_fingerprint);
    bool matched=false;
    for(const auto& record:model->GetNamedArray(L"configurations")) {
        const auto configuration=record.GetObject();
        if(json_text(configuration,L"name")!=request.cmake->build_type)continue;
        if(matched)throw std::runtime_error("CMake returned repeated configurations");
        matched=true;
        if(configuration.HasKey(L"projects")&&
           configuration.GetNamedArray(L"projects").Size())
            graph.project_name=json_text(
                configuration.GetNamedArray(L"projects").GetObjectAt(0),L"name");
        if(!configuration.HasKey(L"targets"))continue;
        const auto references=configuration.GetNamedArray(L"targets");
        const auto directories=configuration.GetNamedArray(L"directories");
        // Codemodel v2 reports the minimum from the top-level CMake
        // directory, independently of the runner's newer toolchain defaults.
        if(directories.Size()) {
            const auto top=directories.GetObjectAt(0);
            if(top.HasKey(L"minimumCMakeVersion"))
                graph.minimum_cmake_version=json_text(
                    top.GetNamedObject(L"minimumCMakeVersion"),L"string");
        }
        std::map<std::string,std::string> names;
        for(const auto& reference:references) {
            const auto item=reference.GetObject();
            const auto id=json_text(item,L"id"),name=json_text(item,L"name");
            if(id.empty()||name.empty()||!names.emplace(id,name).second)
                throw std::runtime_error("CMake target ID is missing or duplicated");
        }
        for(const auto& reference:references) {
            const auto item=reference.GetObject();
            const auto target=read_json(contained(reply,
                project_path(json_text(item,L"jsonFile"))));
            CppCmakeTargetEvidence fact;
            fact.name=json_text(target,L"name");
            fact.type=json_text(target,L"type");
            if(fact.name!=json_text(item,L"name")||
               json_text(target,L"id")!=json_text(item,L"id"))
                throw std::runtime_error("CMake target does not match its index");
            if(item.HasKey(L"directoryIndex")) {
                const auto value=item.GetNamedNumber(L"directoryIndex");
                if(!std::isfinite(value)||value<0||
                   value>=static_cast<double>(directories.Size())||
                   std::trunc(value)!=value)
                    throw std::runtime_error("CMake directory index is invalid");
                fact.source_directory=json_text(
                    directories.GetObjectAt(static_cast<std::uint32_t>(value)),L"source");
            }
            if(target.HasKey(L"sources"))
                for(const auto& item_source:target.GetNamedArray(L"sources")) {
                    const auto source=item_source.GetObject();
                    if(source.HasKey(L"compileGroupIndex"))
                        fact.compiled_sources.push_back(json_text(source,L"path"));
                }
            bool cxx_group_seen=false,all_warning_groups=true;
            if(target.HasKey(L"compileGroups"))
                for(const auto& group_value:target.GetNamedArray(L"compileGroups")) {
                    const auto group=group_value.GetObject();
                    if(json_text(group,L"language")!="CXX")continue;
                    cxx_group_seen=true;
                    std::optional<int> last_warning_level;
                    if(group.HasKey(L"includes"))
                        for(const auto& item_include:group.GetNamedArray(L"includes")) {
                            const auto path=json_text(item_include.GetObject(),L"path");
                            if(!path.empty()&&std::ranges::find(
                                    fact.include_directories,path)==
                                    fact.include_directories.end())
                                fact.include_directories.push_back(path);
                        }
                    // File API commands confirm effective flags independently
                    // from the student-authored target_compile_options trace.
                    if(group.HasKey(L"compileCommandFragments"))
                        for(const auto& part:group.GetNamedArray(L"compileCommandFragments")) {
                            const auto fragment=json_text(part.GetObject(),L"fragment");
                            std::string_view remaining(fragment);
                            while(!remaining.empty()) {
                                const auto begin=remaining.find_first_not_of(" \t\r\n");
                                if(begin==remaining.npos)break;
                                remaining.remove_prefix(begin);
                                const auto end=remaining.find_first_of(" \t\r\n");
                                const std::string option(remaining.substr(0,end));
                                if(const auto level=msvc_warning_level(option))
                                    last_warning_level=level;
                                if(std::ranges::find(fact.effective_compile_options,option)==
                                        fact.effective_compile_options.end())
                                    fact.effective_compile_options.push_back(option);
                                if(end==remaining.npos)break;
                                remaining.remove_prefix(end);
                            }
                        }
                    all_warning_groups=all_warning_groups&&last_warning_level&&
                        *last_warning_level>0;
                }
            fact.msvc_warnings_effective=cxx_group_seen&&all_warning_groups;
            if(target.HasKey(L"artifacts"))
                for(const auto& artifact:target.GetNamedArray(L"artifacts"))
                    fact.artifacts.push_back(json_text(artifact.GetObject(),L"path"));
            if(target.HasKey(L"dependencies"))
                for(const auto& dependency:target.GetNamedArray(L"dependencies")) {
                    const auto id=json_text(dependency.GetObject(),L"id");
                    // Utility/generated dependencies are not proof of a
                    // particular student-authored library link.
                    if(const auto known=names.find(id);known!=names.end())
                        fact.build_dependencies.push_back(known->second);
                }
            if(target.HasKey(L"link")) {
                const auto link=target.GetNamedObject(L"link");
                if(link.HasKey(L"commandFragments"))
                    for(const auto& fragment:link.GetNamedArray(L"commandFragments")) {
                        const auto item_fragment=fragment.GetObject();
                        if(json_text(item_fragment,L"role")=="libraries")
                            fact.link_libraries.push_back(
                                json_text(item_fragment,L"fragment"));
                    }
            }
            graph.targets.push_back(std::move(fact));
        }
    }
    if(!matched)throw std::runtime_error("CMake requested configuration has no generated graph");
    return graph;
}
// Only an explicitly authored target requirement needs this private trace.
// No ordinary compilation, free laboratory, or independent probe collects it.
bool requires_authored_cmake_trace(const CppRunRequest& request) {
    return request.capture_cmake_graph&&request.cmake_graph_check&&
        (request.cmake_graph_check->option_selection||
         request.cmake_graph_check->target_list||
         !request.cmake_graph_check->configure_message_order.empty()||
         std::ranges::any_of(request.cmake_graph_check->targets,
            [](const auto& target) {
                return target.require_explicit_cxx20||
                    target.require_explicit_extensions_off||
                    target.require_explicit_msvc_warnings||
                    !target.authored_compile_options.empty()||
                    !target.public_include_roots.empty()||
                    !target.private_include_roots.empty();
            }));
}
bool authored_cmake_path(std::string_view value) {
    const auto path=project_path(value);
    return path.filename()==L"CMakeLists.txt"||path.extension()==L".cmake";
}
std::filesystem::path authored_cmake_trace_path(
        const std::filesystem::path& workspace) {
    return workspace/L"lgaed-authored-cmake-trace.jsonl";
}
// An executed CMake trace event pins the actual command line. Only its
// unquoted first variable argument can establish list-based application.
std::optional<std::string> raw_cmake_list_reference(
        std::string_view file,std::size_t line,std::string_view command) {
    if(!line||line>1000000)return {};
    std::size_t offset=0;
    for(std::size_t current=1;current<line;++current) {
        const auto next=file.find('\n',offset);
        if(next==file.npos)return {};
        offset=next+1;
    }
    if(offset>=file.size())return {};
    const auto raw=file.substr(offset,
        std::min<std::size_t>(file.size()-offset,4096));
    std::size_t cursor=0;
    const auto skip=[&] {
        while(cursor<raw.size()&&(raw[cursor]==' '||raw[cursor]=='\t'||
            raw[cursor]=='\r'||raw[cursor]=='\n'))++cursor;
    };
    const auto word=[&](std::string_view name) {
        if(raw.size()-cursor<name.size())return false;
        for(std::size_t i=0;i<name.size();++i) {
            auto a=static_cast<unsigned char>(raw[cursor+i]);
            auto b=static_cast<unsigned char>(name[i]);
            if(a>='A'&&a<='Z')a+=32;
            if(b>='A'&&b<='Z')b+=32;
            if(a!=b)return false;
        }
        cursor+=name.size();
        return true;
    };
    skip();
    if(!word(command))return {};
    skip();
    if(cursor>=raw.size()||raw[cursor++]!='(')return {};
    skip();
    if(command=="set_property") {
        if(!word("TARGET"))return {};
        skip();
    }
    if(cursor+2>raw.size()||raw[cursor]!='$'||raw[cursor+1]!='{')
        return {};
    cursor+=2;
    const auto first=cursor;
    if(cursor==raw.size()||!((raw[cursor]>='A'&&raw[cursor]<='Z')||
        (raw[cursor]>='a'&&raw[cursor]<='z')||raw[cursor]=='_'))return {};
    while(cursor<raw.size()&&((raw[cursor]>='A'&&raw[cursor]<='Z')||
        (raw[cursor]>='a'&&raw[cursor]<='z')||
        (raw[cursor]>='0'&&raw[cursor]<='9')||raw[cursor]=='_'))++cursor;
    const auto end=cursor;
    if(cursor==raw.size()||raw[cursor++]!='}')return {};
    if(cursor==raw.size()||(raw[cursor]!=' '&&raw[cursor]!='\n'&&
       raw[cursor]!='\r'&&raw[cursor]!='\t'&&raw[cursor]!=')'))return {};
    return std::string(raw.substr(first,end-first));
}

// Recover argument delimiters without evaluating source expressions. CMake's
// expanded trace retains one value per syntactic argument, including semicolons
// which expand only when the corresponding source argument is unquoted.
std::optional<std::vector<std::string>> cmake_effective_trace_arguments(
        std::string_view source,std::size_t line,std::string_view command,
        const std::vector<std::string>& expanded) {
    if(std::ranges::none_of(expanded,[](const auto& value) {
            return value.empty()||value.find(';')!=std::string::npos;
        }))return expanded;
    if(!line||line>1000000)return {};
    std::size_t offset=0;
    for(std::size_t current=1;current<line;++current) {
        const auto newline=source.find('\n',offset);
        if(newline==source.npos)return {};
        offset=newline+1;
    }
    const auto delimiters=curriculum::cmake_message_trace_detail::Delimiters(
        source,offset).command(command);
    if(!delimiters||delimiters->size()!=expanded.size())return {};
    std::vector<std::string> result;
    for(std::size_t i=0;i<expanded.size();++i) {
        if((*delimiters)[i])
            curriculum::cmake_message_trace_detail::append_unquoted(
                expanded[i],result);
        else result.push_back(expanded[i]);
    }
    return result;
}

// This is a provenance tracker, not a second CMake evaluator. It follows
// normal bindings and their scopes; the trace supplies the effective values.
// Unobserved writers lose proof instead of inheriting it from equal strings.
// CMake 4.4.5: cmMakefile::PrintCommandTrace, cmFunctionBlocker::IsFunctionBlocked,
// cmStateSnapshot::RaiseScope/SetPolicy, and cmSetCommand.
// https://github.com/Kitware/CMake/tree/v4.4.5/Source
class CmakeListProvenance {
    struct NormalScope {
        std::map<std::string,bool> bindings; // present normal; bool = APPEND
        std::shared_ptr<NormalScope> parent;
    };
    struct PolicyScope {
        std::optional<bool> cache_new; // unknown is never assumed NEW
        bool weak=false;
        std::shared_ptr<PolicyScope> parent;
    };
    enum class Kind { root,call,block,opaque_loop };
    struct Context {
        std::size_t depth=1;
        Kind kind=Kind::root;
        std::shared_ptr<NormalScope> normal;
        std::shared_ptr<PolicyScope> policy,policy_floor;
        std::filesystem::path source_directory,expected_file;
        std::vector<std::string> textual_parameters,propagate,write_on_return;
        bool opaque=false;
    };
    struct Definition {
        bool macro=false;
        std::vector<std::string> parameters;
        std::optional<bool> cache_new;
        std::filesystem::path file;
        bool trusted=true;
    };
    // typed_unknown is known to exist with a type, but may be INTERNAL.
    enum class Cache { absent,untyped,typed,internal,typed_unknown,unknown };
    std::vector<Context> contexts_;
    std::map<std::string,Definition> definitions_;
    std::map<std::string,Cache> cache_;
    std::vector<std::filesystem::path> authored_files_;

    static std::string lower(std::string value) {
        for(auto& c:value)if(c>='A'&&c<='Z')c+=('a'-'A');
        return value;
    }
    static bool same_path(const std::filesystem::path& a,
            const std::filesystem::path& b) {
        const CaseInsensitive compare;
        const auto left=a.lexically_normal().generic_wstring();
        const auto right=b.lexically_normal().generic_wstring();
        return !compare(left,right)&&!compare(right,left);
    }
    bool authored(const std::filesystem::path& path) const {
        return std::ranges::any_of(authored_files_,
            [&](const auto& file){return same_path(file,path);});
    }
    static std::shared_ptr<NormalScope> nested_normal(
            const std::shared_ptr<NormalScope>& parent) {
        auto child=std::make_shared<NormalScope>();
        child->bindings=parent->bindings;child->parent=parent;
        return child;
    }
    static std::shared_ptr<PolicyScope> nested_policy(
            const std::shared_ptr<PolicyScope>& parent,bool weak=false,
            std::optional<bool> captured={}) {
        auto child=std::make_shared<PolicyScope>();
        child->cache_new=weak?captured:parent->cache_new;
        child->weak=weak;child->parent=parent;
        return child;
    }
    void set_policy(std::optional<bool> value) {
        for(auto scope=contexts_.back().policy;scope;scope=scope->parent) {
            scope->cache_new=value;
            if(!scope->weak)break;
        }
    }
    static std::optional<bool> version_policy(std::string_view version) {
        if(const auto range=version.find("...");range!=version.npos)
            version.remove_prefix(range+3);
        unsigned major=0,minor=0;
        const auto first=std::from_chars(version.data(),
            version.data()+version.size(),major);
        if(first.ec!=std::errc{}||first.ptr==version.data()+version.size()||
           *first.ptr!='.')return {};
        const auto second=std::from_chars(first.ptr+1,
            version.data()+version.size(),minor);
        if(second.ec!=std::errc{})return {};
        return major>3||(major==3&&minor>=21);
    }
    static bool typed(Cache value) {
        return value==Cache::typed||value==Cache::internal||
            value==Cache::typed_unknown;
    }
    void set_cache(const std::string& name,Cache requested,bool force) {
        const auto found=cache_.find(name);
        const auto before=found==cache_.end()?Cache::unknown:found->second;
        const bool no_write=typed(before)&&!force&&requested==Cache::typed;
        if(!no_write&&contexts_.back().policy->cache_new!=true)
            contexts_.back().normal->bindings.erase(name);
        if(no_write)return;
        // A non-forced write can preserve an older typed cache entry. It is
        // still typed afterwards, but its precise type need not be ours.
        if(force||requested==Cache::internal||before==Cache::absent||
           before==Cache::untyped)
            cache_[name]=requested==Cache::unknown?
                Cache::typed_unknown:requested;
        else cache_[name]=Cache::typed_unknown;
    }
    void close_context() {
        auto current=std::move(contexts_.back());
        contexts_.pop_back();
        if(current.kind==Kind::block&&current.normal->parent) {
            for(const auto& name:current.propagate) {
                const auto found=current.normal->bindings.find(name);
                if(found==current.normal->bindings.end())
                    current.normal->parent->bindings.erase(name);
                else current.normal->parent->bindings[name]=found->second;
            }
        }
        if(current.kind==Kind::opaque_loop)
            current.normal->bindings.clear();
        for(const auto& name:current.write_on_return)
            contexts_.back().normal->bindings[name]=false;
    }
    void change_normal(const std::string& name,bool present,bool parent) {
        auto scope=contexts_.back().normal;
        if(parent)scope=scope->parent;
        if(!scope)return;
        if(present)scope->bindings[name]=false;
        else scope->bindings.erase(name);
    }
    void handle_set(const std::vector<std::string>& args) {
        if(args.empty())return;
        const auto& name=args[0];
        if(name.starts_with("ENV{"))return;
        if(name.starts_with("CACHE{")&&name.ends_with('}')&&name.size()>7) {
            const auto value=std::ranges::find(args,"VALUE");
            if(value==args.end()) {opaque();return;}
            const auto key=name.substr(6,name.size()-7);
            auto type=Cache::unknown;
            const auto existing=cache_.find(key);
            if(existing!=cache_.end()) {
                if(existing->second==Cache::internal)type=Cache::internal;
                else if(existing->second!=Cache::typed_unknown&&
                        existing->second!=Cache::unknown)type=Cache::typed;
            }
            bool force=false;
            for(auto at=args.begin()+1;at!=value;++at) {
                if(*at=="FORCE")force=true;
                else if(*at=="TYPE"&&at+1!=value)
                    type=*(++at)=="INTERNAL"?Cache::internal:Cache::typed;
                else if(*at=="HELP") {
                    while(at+1!=value&&*(at+1)!="TYPE"&&
                          *(at+1)!="FORCE")++at;
                }
            }
            set_cache(key,type,force);
        }else if(args.size()>1&&args.back()=="PARENT_SCOPE") {
            change_normal(name,args.size()>2,true);
        }else {
            const bool force=args.size()>4&&args.back()=="FORCE";
            const auto suffix=3+(force?1u:0u);
            if(args.size()>suffix&&args[args.size()-suffix]=="CACHE") {
                const auto type=args[args.size()-suffix+1]=="INTERNAL"?
                    Cache::internal:Cache::typed;
                set_cache(name,type,force);
            }else change_normal(name,args.size()>1,false);
        }
    }
public:
    CmakeListProvenance(const std::filesystem::path& source,
            const CppRunRequest& request) {
        Context root;
        root.normal=std::make_shared<NormalScope>();
        root.policy=std::make_shared<PolicyScope>();
        root.policy_floor=root.policy;
        root.source_directory=source;
        root.expected_file=source/L"CMakeLists.txt";
        contexts_.push_back(std::move(root));
        for(const auto& file:request.project_files)
            if(authored_cmake_path(file.first))
                authored_files_.push_back(source/project_path(file.first));
        if(request.cmake)
            for(const auto& option:request.cmake->options)
                cache_[option.first]=Cache::untyped; // runner passes -Dname=value
    }
    void opaque() {
        // An opaque call can write a parent or the cache. Clearing normal
        // proof is also required when requireAppend is false.
        for(auto& context:contexts_) {
            for(auto scope=context.normal;scope;scope=scope->parent)
                scope->bindings.clear();
            for(auto policy=context.policy;policy;policy=policy->parent)
                policy->cache_new.reset();
        }
        cache_.clear();
        // A shadowed builtin must never become a builtin again merely because
        // its current body can no longer be attributed to authored source.
        for(auto& definition:definitions_)definition.second.trusted=false;
    }
    bool observe(std::size_t depth,std::string command,
            const std::vector<std::string>& args,
            const std::filesystem::path& origin) {
        command=lower(std::move(command));
        while(contexts_.size()>1&&depth<contexts_.back().depth)
            close_context();
        if(depth!=contexts_.back().depth) {
            opaque();
            Context unknown=contexts_.back();
            unknown.depth=depth;unknown.kind=Kind::call;unknown.opaque=true;
            unknown.normal=nested_normal(unknown.normal);
            unknown.policy=nested_policy(unknown.policy);
            unknown.policy_floor=unknown.policy;
            unknown.expected_file=origin;
            unknown.propagate.clear();unknown.write_on_return.clear();
            contexts_.push_back(std::move(unknown));
        }
        if(!same_path(contexts_.back().expected_file,origin)) {
            opaque();contexts_.back().opaque=true;
        }
        auto& current=contexts_.back();
        if(const auto found=definitions_.find(command);
                found!=definitions_.end()) {
            const auto definition=found->second;
            if(!definition.trusted) {opaque();return true;}
            Context call=current;
            call.depth=depth+1;call.kind=Kind::call;
            call.expected_file=definition.file;
            call.propagate.clear();call.write_on_return.clear();
            call.textual_parameters.clear();
            call.policy=nested_policy(current.policy,true,definition.cache_new);
            call.policy_floor=call.policy;
            if(definition.macro)
                call.textual_parameters=definition.parameters;
            else {
                call.normal=nested_normal(current.normal);
                for(const auto& name:definition.parameters)
                    call.normal->bindings[name]=false;
            }
            for(const auto* name:{"ARGC","ARGV","ARGN",
                    "CMAKE_CURRENT_FUNCTION","CMAKE_CURRENT_FUNCTION_LIST_DIR",
                    "CMAKE_CURRENT_FUNCTION_LIST_FILE",
                    "CMAKE_CURRENT_FUNCTION_LIST_LINE"}) {
                if(definition.macro)call.textual_parameters.push_back(name);
                else call.normal->bindings[name]=false;
            }
            for(std::size_t i=0;i<args.size();++i) {
                const auto name="ARGV"+std::to_string(i);
                if(definition.macro)call.textual_parameters.push_back(name);
                else call.normal->bindings[name]=false;
            }
            contexts_.push_back(std::move(call));
            return true; // this event is a scripted call, never a builtin fact
        }
        if(command=="function"||command=="macro") {
            if(args.empty()) {opaque();return false;}
            definitions_[lower(args[0])]={command=="macro",
                std::vector<std::string>(args.begin()+1,args.end()),
                current.policy->cache_new,origin};
        }else if(command=="endfunction"||command=="endmacro") {
            // These end events finish the definition, not a later invocation.
        }else if(command=="include"||command=="add_subdirectory") {
            if(args.empty()) {opaque();return false;}
            Context call=current;
            call.depth=depth+1;call.kind=Kind::call;
            call.propagate.clear();call.write_on_return.clear();
            call.textual_parameters.clear();
            auto target=project_path(args[0]);
            if(target.is_relative())target=current.source_directory/target;
            if(command=="add_subdirectory") {
                call.source_directory=target;
                call.normal=nested_normal(current.normal);
                target/=L"CMakeLists.txt";
            }else {
                for(std::size_t i=1;i+1<args.size();++i)
                    if(args[i]=="RESULT_VARIABLE")
                        call.write_on_return.push_back(args[i+1]);
            }
            call.expected_file=target.lexically_normal();
            if(command=="add_subdirectory"||
               std::ranges::find(args,"NO_POLICY_SCOPE")==args.end())
                call.policy=nested_policy(current.policy);
            call.policy_floor=call.policy;
            if(!authored(call.expected_file)) {
                opaque();call.opaque=true;
                call.normal->bindings.clear();call.policy->cache_new.reset();
            }
            contexts_.push_back(std::move(call));
        }else if(command=="block") {
            Context block=current;
            block.kind=Kind::block;
            block.propagate.clear();block.write_on_return.clear();
            bool variables=true,policies=true,scope_seen=false;
            bool propagate_seen=false,valid=true;
            enum class Reading { none,scope,propagate };
            auto reading=Reading::none;
            for(const auto& argument:args) {
                if(argument=="SCOPE_FOR") {
                    if(scope_seen)valid=false;
                    scope_seen=true;variables=false;policies=false;
                    reading=Reading::scope;
                }else if(argument=="PROPAGATE") {
                    if(propagate_seen)valid=false;
                    propagate_seen=true;reading=Reading::propagate;
                }else if(reading==Reading::scope) {
                    if(argument=="VARIABLES")variables=true;
                    else if(argument=="POLICIES")policies=true;
                    else if(argument!="DIAGNOSTICS")valid=false;
                }else if(reading==Reading::propagate)
                    block.propagate.push_back(argument);
                else valid=false;
            }
            if(!valid) {
                opaque();block.opaque=true;
                variables=true;policies=true;block.propagate.clear();
            }
            if(variables)block.normal=nested_normal(current.normal);
            else block.propagate.clear();
            if(policies)block.policy=nested_policy(current.policy);
            block.policy_floor=block.policy;
            contexts_.push_back(std::move(block));
        }else if(command=="endblock") {
            if(contexts_.size()<2||current.kind!=Kind::block)opaque();
            else close_context();
        }else if(command=="foreach") {
            // The trace omits each implicit loop-variable assignment. Do not
            // carry proof across this unsupported writer/control lifetime.
            opaque();
            Context loop=current;
            loop.kind=Kind::opaque_loop;loop.opaque=true;
            loop.propagate.clear();loop.write_on_return.clear();
            contexts_.push_back(std::move(loop));
        }else if(command=="endforeach") {
            if(contexts_.size()<2||current.kind!=Kind::opaque_loop)opaque();
            else close_context();
        }else if(command=="set")handle_set(args);
        else if(command=="unset"&&!args.empty()) {
            const auto& name=args[0];
            if(name.starts_with("ENV{"))return false;
            if(name.starts_with("CACHE{")&&name.ends_with('}')&&name.size()>7)
                cache_[name.substr(6,name.size()-7)]=Cache::absent;
            else if(args.size()==2&&args[1]=="CACHE")cache_[name]=Cache::absent;
            else change_normal(name,false,
                args.size()==2&&args[1]=="PARENT_SCOPE");
        }else if(command=="list"&&args.size()>=2&&args[0]=="APPEND") {
            if(args.size()==2)return false; // CMake does not create a normal binding
            auto& proof=current.normal->bindings[args[1]];
            std::vector<std::string> added;
            for(auto at=args.begin()+2;at!=args.end();++at)
                curriculum::cmake_message_trace_detail::append_unquoted(*at,added);
            if(!added.empty())proof=true;
        }else if(command=="cmake_minimum_required") {
            const auto version=std::ranges::find(args,"VERSION");
            set_policy(version!=args.end()&&version+1!=args.end()?
                version_policy(*(version+1)):std::optional<bool>{});
        }else if(command=="cmake_policy") {
            if(args.size()==1&&args[0]=="PUSH")
                current.policy=nested_policy(current.policy);
            else if(args.size()==1&&args[0]=="POP") {
                if(current.policy==current.policy_floor)opaque();
                else current.policy=current.policy->parent;
            }else if(args.size()==3&&args[0]=="SET"&&args[1]=="CMP0126")
                set_policy(args[2]=="NEW"?std::optional<bool>{true}:
                    args[2]=="OLD"?std::optional<bool>{false}:std::nullopt);
            else if(args.size()==2&&args[0]=="VERSION")
                set_policy(version_policy(args[1]));
            else if(args.size()==3&&args[0]=="GET")
                current.normal->bindings[args[2]]=false;
            else if(args.size()==3&&args[0]=="SET") {
                // Other policies are not inferred from their number.
            }else opaque();
        }else if(command=="return"&&!args.empty()) {
            // PROPAGATE also depends on CMP0140 and crosses enclosing blocks.
            // Without evaluating that control flow, never copy APPEND to parents.
            for(const auto& name:args)
                for(auto scope=current.normal;scope;scope=scope->parent)
                    scope->bindings.erase(name);
        }else if(command=="set_property"&&!args.empty()&&args[0]!="TARGET") {
            opaque(); // CACHE and other property scopes may alter tracked metadata
        }else if(command=="option"&&!args.empty()) {
            current.normal->bindings.erase(args[0]);
            cache_[args[0]]=Cache::unknown; // CMP0077 is outside this proof
        }else {
            static constexpr std::string_view non_writers[]={
                "message","if","elseif","else","endif","while","endwhile",
                "break","continue","return","add_library","add_executable",
                "target_sources","target_link_libraries","target_link_options",
                "target_include_directories","target_compile_options",
                "target_compile_features","target_compile_definitions",
                "set_target_properties","set_property","add_dependencies",
                "enable_testing","add_test"};
            if(std::ranges::find(non_writers,command)==std::end(non_writers))
                opaque();
        }
        return false;
    }
    std::optional<bool> normal(std::string_view name) const {
        const auto& current=contexts_.back();
        if(current.opaque||std::ranges::find(current.textual_parameters,name)!=
                current.textual_parameters.end())return {};
        const auto found=current.normal->bindings.find(std::string(name));
        if(found==current.normal->bindings.end())return {};
        return found->second;
    }
};

// The JSON CMake trace lists commands actually invoked at configure time,
// with expanded arguments. Ignore all non-student source locations, including
// CMake modules and compiler/toolchain defaults. This evidence supplements
// generated File API target/source/link facts, never replaces them.
void attach_authored_cmake_trace(const std::filesystem::path& workspace,
        const CppRunRequest& request,CppCmakeGraphEvidence& graph) {
    const auto path=authored_cmake_trace_path(workspace);
    state::require_safe_path(path);
    if(!std::filesystem::is_regular_file(path)||
       std::filesystem::file_size(path)>4*1024*1024)return;
    std::ifstream input(path,std::ios::binary);
    if(!input)return;
    const auto source=workspace/L"p";
    const auto equals=[](std::string_view left,std::string_view right) {
        return left.size()==right.size()&&
            std::ranges::equal(left,right,[](unsigned char a,unsigned char b) {
                if(a>='a'&&a<='z')a=static_cast<unsigned char>(a-'a'+'A');
                if(b>='a'&&b<='z')b=static_cast<unsigned char>(b-'a'+'A');
                return a==b;
            });
    };
    const auto off=[&](std::string_view value) {
        return equals(value,"OFF")||equals(value,"FALSE")||
            equals(value,"NO")||equals(value,"N")||value=="0";
    };
    // Parse into a temporary graph: malformed/partial traces never
    // certify an early prefix of the student's CMake commands.
    auto observed=graph.targets;
    auto observed_options=graph.authored_option_defaults;
    auto observed_messages=graph.authored_status_messages;
    auto observed_lists=graph.authored_target_lists;
    CmakeListProvenance list_provenance(source,request);
    const auto target_for=[&](std::string_view name)->CppCmakeTargetEvidence* {
        const auto it=std::ranges::find_if(observed,
            [&](const auto& item){return item.name==name;});
        return it==observed.end()?nullptr:&*it;
    };
    // A minimum requested by compile features and a selected CXX_STANDARD
    // are independent CMake requirements. A later standard property must not
    // erase an active feature minimum, nor may a removed feature keep credit.
    struct AuthoredCxxRequirement {
        bool feature_minimum=false,selected_standard=false;
    };
    std::map<std::string,AuthoredCxxRequirement> cxx_requirements;
    const auto at_least_cxx20=[](std::string_view value) {
        // C++98 is not newer than C++20; do not compare the suffix numerically.
        return value=="20"||value=="23"||value=="26";
    };
    const auto feature_minimum=[&](std::string_view values) {
        for(;;) {
            const auto separator=values.find(';');
            const auto value=values.substr(0,separator);
            if(value.starts_with("cxx_std_")&&
               at_least_cxx20(value.substr(8)))return true;
            if(separator==values.npos)return false;
            values.remove_prefix(separator+1);
        }
    };
    const auto apply_property=[&](std::string_view name,
            std::string_view key,std::string_view value) {
        auto* target=target_for(name);
        if(!target)return;
        if(equals(key,"CXX_EXTENSIONS"))
            target->explicit_extensions_off=off(value);
        else if(equals(key,"CXX_STANDARD")||equals(key,"COMPILE_FEATURES")) {
            auto& authored=cxx_requirements[target->name];
            if(equals(key,"CXX_STANDARD"))
                authored.selected_standard=at_least_cxx20(value);
            else
                authored.feature_minimum=feature_minimum(value);
            target->explicit_cxx20=
                authored.feature_minimum||authored.selected_standard;
        }
    };
    std::string line;
    std::size_t records=0;
    try {
        while(std::getline(input,line)) {
            if(++records>50000||line.size()>65536)return;
            const auto event=JsonObject::Parse(winrt::to_hstring(line));
            if(!event.HasKey(L"cmd"))continue; // trace format header
            if(!event.HasKey(L"file")||!event.HasKey(L"args"))return;
            const auto origin=project_path(json_text(event,L"file")).lexically_normal();
            std::string authored_path;
            std::string_view authored_text;
            for(const auto& file:request.project_files) {
                if(!authored_cmake_path(file.first))continue;
                const auto expected=(source/project_path(file.first))
                    .lexically_normal().generic_wstring();
                const auto observed_path=origin.generic_wstring();
                const CaseInsensitive cmp;
                if(!cmp(expected,observed_path)&&
                   !cmp(observed_path,expected)) {
                    authored_path=file.first;
                    authored_text=file.second;
                    break;
                }
            }
            if(authored_path.empty())continue;
            auto command=json_text(event,L"cmd");
            for(auto& c:command)if(c>='A'&&c<='Z')c+=('a'-'A');
            std::vector<std::string> args,property_args;
            for(const auto& value:event.GetNamedArray(L"args")) {
                const auto expanded=winrt::to_string(value.GetString());
                property_args.push_back(expanded);
                std::size_t begin=0;
                for(;;) {
                    const auto end=expanded.find(';',begin);
                    args.push_back(expanded.substr(begin,end==std::string::npos?
                        end:end-begin));
                    if(end==std::string::npos)break;
                    begin=end+1;
                }
            }
            if(!event.HasKey(L"line")||!event.HasKey(L"global_frame"))return;
            const auto number=event.GetNamedNumber(L"line");
            const auto depth=event.GetNamedNumber(L"global_frame");
            if(!std::isfinite(number)||number<1||number>1000000||
               std::trunc(number)!=number||!std::isfinite(depth)||depth<1||
               depth>2048||std::trunc(depth)!=depth)return;
            const auto effective=cmake_effective_trace_arguments(authored_text,
                static_cast<std::size_t>(number),command,property_args);
            if(effective) {
                if(list_provenance.observe(static_cast<std::size_t>(depth),
                        command,*effective,origin))continue;
            }else list_provenance.opaque();
            const auto record_list=[&](std::size_t count,bool off_value,
                    std::size_t first_argument=0) {
                if(!off_value||!event.HasKey(L"line"))return;
                const auto number=event.GetNamedNumber(L"line");
                if(!std::isfinite(number)||number<1||
                   std::trunc(number)!=number||number>1000000)return;
                const auto original=std::ranges::find_if(
                    request.project_files,[&](const auto& file) {
                        return file.first==authored_path;
                    });
                if(original==request.project_files.end())return;
                const auto name=raw_cmake_list_reference(
                    original->second,static_cast<std::size_t>(number),command);
                if(!name)return;
                const auto proof=list_provenance.normal(*name);
                if(!proof||first_argument>=property_args.size())return;
                std::vector<std::string> supplied_targets;
                curriculum::cmake_message_trace_detail::append_unquoted(
                    property_args[first_argument],supplied_targets);
                // The one authored variable must supply the whole target list;
                // an extra literal target cannot complete an APPEND proof.
                if(supplied_targets.empty()||supplied_targets.size()!=count||
                   first_argument>args.size()||
                   args.size()-first_argument<count||
                   !std::equal(supplied_targets.begin(),supplied_targets.end(),
                       args.begin()+first_argument))return;
                observed_lists.push_back({std::move(supplied_targets),*proof});
            };
            if(equals(command,"message")) {
                if(!event.HasKey(L"line"))continue;
                const auto number=event.GetNamedNumber(L"line");
                if(!std::isfinite(number)||number<1||
                   std::trunc(number)!=number||number>1000000)continue;
                // The trace has expanded values but not list delimiters.
                // Resolve ambiguous arguments from this exact authored call.
                if(auto text=curriculum::cmake_status_message_from_trace(
                        authored_text,static_cast<std::size_t>(number),property_args))
                    observed_messages.emplace_back(authored_path,std::move(*text));
            }else if(equals(command,"option")&&args.size()>=2) {
                observed_options.emplace_back(args[0],
                    args.size()==2||off(args[2])?"OFF":"ON");
            }else if(equals(command,"target_include_directories")&&
                     args.size()>=3) {
                auto* target=target_for(args.front());
                if(!target)continue;
                bool is_public=false,is_private=false;
                for(std::size_t i=1;i<args.size();++i) {
                    if(equals(args[i],"PUBLIC")) {
                        is_public=true;is_private=false;
                    }else if(equals(args[i],"PRIVATE")) {
                        is_private=true;is_public=false;
                    }else if(equals(args[i],"INTERFACE")) {
                        is_public=false;is_private=false;
                    }else if(equals(args[i],"BEFORE")||
                             equals(args[i],"AFTER")||
                             equals(args[i],"SYSTEM"))continue;
                    else if(is_public||is_private) {
                        // CMake expands source-dir variables in the trace.
                        // Simple relative roots resolve beside the CMake file.
                        if(args[i].find("$<")!=std::string::npos)continue;
                        auto root=project_path(args[i]);
                        if(root.is_relative())root=origin.parent_path()/root;
                        root=root.lexically_normal();
                        const auto resolved=utf8(root.generic_wstring());
                        if(is_public)
                            target->authored_public_include_directories
                                .push_back(resolved);
                        else
                            target->authored_private_include_directories
                                .push_back(resolved);
                    }
                }
            }else if(equals(command,"target_compile_options")&&args.size()>2) {
                auto* target=target_for(args.front());
                if(!target)continue;
                bool applies_to_target=false;
                for(std::size_t i=1;i<args.size();++i) {
                    if(equals(args[i],"PRIVATE")||equals(args[i],"PUBLIC"))
                        applies_to_target=true;
                    else if(equals(args[i],"INTERFACE"))
                        applies_to_target=false;
                    else if(equals(args[i],"BEFORE"))continue;
                    else if(applies_to_target&&
                            args[i].find("$<")==std::string::npos&&
                            std::ranges::find(target->authored_compile_options,
                                args[i])==target->authored_compile_options.end())
                        target->authored_compile_options.push_back(args[i]);
                }
            }else if(equals(command,"target_compile_features")&&args.size()>2) {
                auto* target=target_for(args.front());
                if(!target)continue;
                bool applies_to_target=false;
                for(std::size_t i=1;i<args.size();++i) {
                    if(equals(args[i],"PRIVATE")||equals(args[i],"PUBLIC"))
                        applies_to_target=true;
                    else if(equals(args[i],"INTERFACE"))
                        applies_to_target=false;
                    else if(applies_to_target&&feature_minimum(args[i])) {
                        cxx_requirements[target->name].feature_minimum=true;
                        target->explicit_cxx20=true;
                    }
                }
            }else if(equals(command,"set_target_properties")&&property_args.size()>=4) {
                const auto separator=std::ranges::find_if(property_args,
                    [&](const auto& arg){return equals(arg,"PROPERTIES");});
                if(separator==property_args.end())continue;
                const auto pos=static_cast<std::size_t>(separator-property_args.begin());
                const auto expanded_separator=std::ranges::find_if(args,
                    [&](const auto& arg){return equals(arg,"PROPERTIES");});
                const auto target_count=static_cast<std::size_t>(expanded_separator-args.begin());
                if(pos==0||expanded_separator==args.end())continue;
                bool explicit_off=false;
                for(auto i=pos+1;i+1<property_args.size();i+=2) {
                    if(equals(property_args[i],"CXX_EXTENSIONS")&&off(property_args[i+1]))
                        explicit_off=true;
                    for(std::size_t t=0;t<target_count;++t)
                        apply_property(args[t],property_args[i],property_args[i+1]);
                }
                record_list(target_count,explicit_off);
            }else if(equals(command,"set_property")&&property_args.size()>=4&&
                     equals(property_args.front(),"TARGET")) {
                const auto separator=std::ranges::find_if(property_args,
                    [&](const auto& arg){return equals(arg,"PROPERTY");});
                if(separator==property_args.end())continue;
                const auto pos=static_cast<std::size_t>(separator-property_args.begin());
                const auto expanded_separator=std::ranges::find_if(args,
                    [&](const auto& arg){return equals(arg,"PROPERTY");});
                const auto target_end=static_cast<std::size_t>(expanded_separator-args.begin());
                if(pos<2||pos+2>property_args.size()||expanded_separator==args.end())continue;
                bool append=false;
                for(std::size_t i=1;i<pos;++i)
                    append=append||equals(property_args[i],"APPEND")||
                        equals(property_args[i],"APPEND_STRING");
                if(append)continue;
                std::string value;
                for(std::size_t i=pos+2;i<property_args.size();++i) {
                    if(i>pos+2)value+=';';
                    value+=property_args[i];
                }
                for(std::size_t i=1;i<target_end;++i)
                    apply_property(args[i],property_args[pos+1],value);
                record_list(target_end-1,equals(property_args[pos+1],"CXX_EXTENSIONS")&&
                    off(value),1);
            }
        }
    }catch(const std::exception&) {
        return;
    }catch(const winrt::hresult_error&) {
        return;
    }
    if(input.bad()||!records)return;
    for(std::size_t i=0;i<observed.size();++i) {
        graph.targets[i].explicit_cxx20=observed[i].explicit_cxx20;
        graph.targets[i].explicit_extensions_off=
            observed[i].explicit_extensions_off;
        graph.targets[i].authored_public_include_directories=
            std::move(observed[i].authored_public_include_directories);
        graph.targets[i].authored_private_include_directories=
            std::move(observed[i].authored_private_include_directories);
        graph.targets[i].authored_compile_options=
            std::move(observed[i].authored_compile_options);
    }
    graph.authored_option_defaults=std::move(observed_options);
    graph.authored_status_messages=std::move(observed_messages);
    graph.authored_target_lists=std::move(observed_lists);
}
bool executable64(const std::filesystem::path& path){state::require_safe_path(path);DWORD kind=0;return std::filesystem::is_regular_file(path)&&GetBinaryTypeW(path.c_str(),&kind)&&kind==SCS_64BIT_BINARY;}
// This generated-workspace capability is separate from curricular identities
// and receipt fingerprints. Older caches need configuring with the current
// linker rules before their artifacts can be built or executed again.
constexpr std::string_view cmake_link_profile="msvc-full-link-v1";
CppProjectAvailability inspect_unlocked(const CppRunRequest& request){CppProjectAvailability result;result.project_fingerprint=fingerprint(request);result.configuration_fingerprint=fingerprint(request,true);result.workspace=cmake_workspace(request);
    if(!verify_workspace_identity(request,false))return result;const auto path=result.workspace/L"state.json";if(!std::filesystem::is_regular_file(path))return result;
    try{const auto saved=read_json(path);result.configured=json_text(saved,L"configuration")==result.configuration_fingerprint&&json_text(saved,L"linkProfile")==cmake_link_profile&&std::filesystem::is_regular_file(result.workspace/L"build.ninja");
        if(!result.configured)return result;const auto target=cmake_target(result.workspace,*request.cmake);result.target_type=target.type;
        if(json_text(saved,L"built")!=result.project_fingerprint||result.target_type!="EXECUTABLE"||
           materialized_project_mismatch(request.project_files,result.workspace/L"p"))return result;
        const auto artifact=contained(result.workspace,project_path(json_text(saved,L"artifact")));
        if(std::ranges::find(target.artifacts,artifact)==target.artifacts.end()||!executable64(artifact)||state::sha256(state::read_bytes(artifact,128*1024*1024))!=json_text(saved,L"artifactSha256"))return result;
        result.artifact=artifact;result.built=true;
    }catch(const std::exception&){result.built=false;result.configured=false;}catch(const winrt::hresult_error&){result.built=false;result.configured=false;}return result;
}
void merge_phase(CppPhaseResult& total,CppPhaseResult part,std::size_t limit){const bool exceeded=part.stdout_bytes.size()>limit-total.stdout_bytes.size()||part.stderr_bytes.size()>limit-total.stderr_bytes.size();
    total.stdout_bytes.append(part.stdout_bytes,0,std::min(part.stdout_bytes.size(),limit-total.stdout_bytes.size()));total.stderr_bytes.append(part.stderr_bytes,0,std::min(part.stderr_bytes.size(),limit-total.stderr_bytes.size()));total.status=exceeded?CppPhaseStatus::output_limit:part.status;total.exit_code=part.exit_code;}
// WASIp1 runs inside Wasmtime without generating an unsigned PE per exercise.
// Toolchain and runtime are confined to the user-authorized, project-specific
// C:\\LGAED-Dep root configured at build time (isolated CI excepted).
void run_wasi_single(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& source,const std::filesystem::path& input,
        const std::filesystem::path& compiler_input,std::stop_token stop) {
    if(request.diagnostic||request.cmake||!request.project_files.empty())
        throw std::invalid_argument("WASI single-file execution cannot replace native diagnostics or CMake.");
    const std::filesystem::path deps{build::educational_dependencies_root};
    const auto sdk=deps/L"WASI-SDK/34.0";
    const auto clang=sdk/L"bin/clang++.exe";
    const auto sysroot=sdk/L"share/wasi-sysroot";
    const auto wasmtime=deps/L"Wasmtime/50.0.1/wasmtime.exe";
    if(!std::filesystem::is_regular_file(clang)||!std::filesystem::is_directory(sysroot)
        ||!std::filesystem::is_regular_file(wasmtime))
        throw std::runtime_error("WASI SDK 34 / Wasmtime 50.0.1 no están instalados. "
            "Reconstruye la aplicación con Build-Native.ps1 para instalar las herramientas oficiales "
            "solo en C:\\LGAED-Dep, sin modificar Seguridad de Windows.");
    const auto wasm=result.run_directory/L"program.wasm";
    write_exact(source,request.source_utf8);
    write_exact(result.run_directory/L"toolchain.txt",
        "backend=WASI (wasm32-wasip1)\nwasi-sdk=34.0\nwasmtime=50.0.1\n"
        "clang="+utf8(clang.wstring())+"\n");
    // wasi-sdk 34 ships a separate libc++/libc++abi for WebAssembly EH.
    // Enabling the standard EH proposal and linking libunwind is required
    // for legitimate C++23 stream operations and user try/catch/throw.
    // Do not silently compile the student's source with exceptions disabled.
    const std::vector<std::wstring> compile_args{
        L"--target=wasm32-wasip1",
        L"--sysroot="+sysroot.wstring(),L"-std=c++23",L"-Wall",L"-Wextra",
        L"-fwasm-exceptions",L"-mllvm",L"-wasm-use-legacy-eh=false",
        L"-fno-color-diagnostics",L"-o",wasm.wstring(),source.wstring(),L"-lunwind"
    };
    // Clang emits UTF-8 diagnostics on Windows. Launch it directly so an
    // additional, locally built unsigned compiler-host PE is never necessary.
    result.compilation=run_phase(clang,compile_args,result.run_directory,compiler_input,
        L"compilation",request.compile_timeout,request.output_limit_bytes,true,stop,false);
    if(!result.compilation.succeeded())return;
    result.artifact=wasm;
    result.target_type="WASI_COMMAND";
    // Wasmtime receives stdin from the same exact prepared-input file and
    // keeps stdout and stderr independent; only the owned run folder is
    // available for guest file operations.
    const std::vector<std::wstring> run_args{L"run",L"--dir",L".",wasm.wstring()};
    result.execution=run_phase(wasmtime,run_args,result.run_directory,input,
        L"execution",request.execution_timeout,request.output_limit_bytes,false,stop,false);
}
void run_multifile(const CppRunRequest& request,CppRunResult& result,const std::filesystem::path& input,const std::filesystem::path& compiler_input,std::stop_token stop){
    validate_request_project(request);result.project_fingerprint=fingerprint(request);const auto source=result.run_directory/L"project",objects=result.run_directory/L"objects",executable=result.run_directory/L"program.exe";
    result.diagnostic_source_directory=source;
    materialize(request.project_files,source);std::filesystem::create_directory(objects);const std::filesystem::path compiler=std::filesystem::path(build::educational_vc_root)/L"bin/Hostx64/x64/cl.exe";std::vector<std::wstring> link{L"/nologo",L"/Fe:"+executable.wstring()};
    const auto start=std::chrono::steady_clock::now();const auto remaining=[&]{return request.compile_timeout-std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start);};
    std::size_t sequence=0;for(const auto& file:request.source_files){if(remaining().count()<=0){result.compilation.status=CppPhaseStatus::timed_out;return;}const auto object=objects/(std::to_wstring(sequence)+L".obj");const std::vector<std::wstring> args{L"/nologo",L"/c",L"/EHsc",build::cpp_standard_option,L"/Zc:__cplusplus",L"/permissive-",L"/utf-8",L"/W4",L"/diagnostics:column",L"/I"+source.wstring(),L"/Fo:"+object.wstring(),(source/project_path(file)).wstring()};
        merge_phase(result.compilation,run_phase(compiler,args,result.run_directory,compiler_input,L"compile-"+std::to_wstring(sequence++),remaining(),request.output_limit_bytes,true,stop,true),request.output_limit_bytes);if(!result.compilation.succeeded())return;link.push_back(object.wstring());}
    link.insert(link.end(),{L"/link",L"/INCREMENTAL:NO",L"/MACHINE:X64",L"/SUBSYSTEM:CONSOLE",L"/MANIFEST:EMBED",L"/MANIFESTUAC:level='asInvoker' uiAccess='false'"});
    if(remaining().count()<=0){result.compilation.status=CppPhaseStatus::timed_out;return;}merge_phase(result.compilation,run_phase(compiler,link,result.run_directory,compiler_input,L"link",remaining(),request.output_limit_bytes,true,stop,true),request.output_limit_bytes);
    if(result.compilation.succeeded()){result.artifact=executable;result.target_type="EXECUTABLE";result.execution=run_phase(executable,{},result.run_directory,input,L"execution",request.execution_timeout,request.output_limit_bytes,false,stop);}
}
// The ordinary wrapper keeps its existing lock and behavior. Only the opt-in
// integer battery holds that same lock across several builds and executions.
void run_cmake_unlocked(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& input,const std::filesystem::path& compiler_input,
        std::stop_token stop,const std::vector<wchar_t>* fixed_environment=nullptr,
        bool defer_execution=false) {
    const auto workspace=cmake_workspace(request);
    result.project_fingerprint=fingerprint(request);result.configuration_fingerprint=fingerprint(request,true);const auto& build_directory=workspace;const auto source=workspace/L"p",state_file=workspace/L"state.json";
    result.diagnostic_source_directory=source;result.diagnostic_build_directory=build_directory;
    auto availability=inspect_unlocked(request);const auto action=request.action;
    const bool authored_trace=requires_authored_cmake_trace(request);
    const auto trace_file=authored_cmake_trace_path(workspace);
    const bool missing_trace=authored_trace&&
        !std::filesystem::is_regular_file(trace_file);
    const bool configure=action==CppRunRequest::Action::configure||
        (action==CppRunRequest::Action::compile_run&&
         (!availability.configured||missing_trace));
    const auto cmake=std::filesystem::path(build::educational_cmake),ninja=std::filesystem::path(build::educational_ninja),compiler=std::filesystem::path(build::educational_vc_root)/L"bin/Hostx64/x64/cl.exe";
    if(!std::filesystem::is_regular_file(cmake)||!std::filesystem::is_regular_file(ninja)||!std::filesystem::is_regular_file(compiler))throw std::runtime_error("Pinned CMake, Ninja or MSVC is unavailable");
    if(configure){(void)verify_workspace_identity(request,true);materialize(request.project_files,source);state::require_safe_path(build_directory);std::filesystem::create_directories(build_directory);state::require_safe_path(build_directory);
        JsonObject saved;save_json(state_file,saved);const auto query=build_directory/L".cmake/api/v1/query/client-lgaed";state::require_safe_path(query);std::filesystem::create_directories(query);state::require_safe_path(query);write_exact(query/L"codemodel-v2","");
        const auto language_rules=workspace/L"cpp-language.cmake";
        state::require_safe_path(language_rules);
        write_exact(language_rules,"set(CMAKE_CXX23_STANDARD_COMPILE_OPTION \""+utf8(build::cpp_standard_option)+"\")\n"
            "set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION \""+utf8(build::cpp_standard_option)+"\")\n"
            "string(APPEND CMAKE_CXX_FLAGS_INIT \" /Zc:__cplusplus\")\n");
        // Use the direct native runner's full-link policy for changed images;
        // each LINK pass consumes the current object/library inputs. Ninja
        // keeps its incremental compilation. The global hook also precedes
        // linker cache initialization when C is enabled before CXX. Per-config
        // changes preserve /debug and every unrelated platform option.
        const auto linker_rules=workspace/L"cpp-linker.cmake";
        state::require_safe_path(linker_rules);
        write_exact(linker_rules,R"cmake(foreach(_lgaed_link_kind IN ITEMS EXE SHARED MODULE)
  foreach(_lgaed_link_config IN ITEMS DEBUG RELWITHDEBINFO)
    set(_lgaed_link_flags "CMAKE_${_lgaed_link_kind}_LINKER_FLAGS_${_lgaed_link_config}_INIT")
    string(REGEX REPLACE "(^|[ \t])/INCREMENTAL(:YES|:NO)?([ \t]|$)" "\\1\\3"
      ${_lgaed_link_flags} "${${_lgaed_link_flags}}")
    string(APPEND ${_lgaed_link_flags} " /INCREMENTAL:NO")
  endforeach()
endforeach()
unset(_lgaed_link_flags)
unset(_lgaed_link_config)
unset(_lgaed_link_kind)
)cmake");
        std::vector<std::wstring> arguments{L"--fresh",L"-S",source.wstring(),L"-B",build_directory.wstring(),L"-G",L"Ninja",L"-DCMAKE_MAKE_PROGRAM="+ninja.wstring(),L"-DCMAKE_CXX_COMPILER="+compiler.wstring(),L"-DCMAKE_C_COMPILER="+compiler.wstring(),L"-DCMAKE_RC_COMPILER="+std::wstring(build::educational_resource_compiler),L"-DCMAKE_BUILD_TYPE="+std::wstring(winrt::to_hstring(request.cmake->build_type)),L"-DCMAKE_CXX_STANDARD="+std::to_wstring(build::cpp_standard),L"-DCMAKE_CXX_STANDARD_REQUIRED=ON",L"-DCMAKE_CXX_EXTENSIONS=OFF",L"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",L"-DCMAKE_USER_MAKE_RULES_OVERRIDE="+linker_rules.wstring(),L"-DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX="+language_rules.wstring()};
        for(const auto& [key,value]:request.cmake->options)arguments.push_back(std::wstring(L"-D")+winrt::to_hstring(key).c_str()+L"="+winrt::to_hstring(value).c_str());
        if(authored_trace) {
            state::require_safe_path(trace_file);
            arguments.push_back(L"--trace-expand");
            arguments.push_back(L"--trace-format=json-v1");
            arguments.push_back(L"--trace-redirect="+trace_file.wstring());
            bool has_authored_cmake=false;
            for(const auto& file:request.project_files) {
                if(!authored_cmake_path(file.first))continue;
                const auto owned=(source/project_path(file.first)).lexically_normal();
                state::require_safe_path(owned);
                arguments.push_back(L"--trace-source="+owned.wstring());
                has_authored_cmake=true;
            }
            if(!has_authored_cmake)
                throw std::invalid_argument(
                    "Explicit CMake rules need an original authored CMake file");
        }
        result.configuration=run_phase(cmake,arguments,result.run_directory,compiler_input,L"configuration",request.compile_timeout,request.output_limit_bytes,true,stop,true,fixed_environment);
        require_materialized_project(request,source,state_file,"la configuración de CMake");
        if(!result.configuration.succeeded())return;if(!std::filesystem::is_regular_file(build_directory/L"build.ninja"))throw std::runtime_error("CMake did not generate real Ninja rules");
        const auto target=cmake_target(workspace,*request.cmake);result.target_type=target.type;saved.Insert(L"configuration",JsonValue::CreateStringValue(winrt::to_hstring(result.configuration_fingerprint)));saved.Insert(L"linkProfile",JsonValue::CreateStringValue(winrt::to_hstring(cmake_link_profile)));save_json(state_file,saved);availability=inspect_unlocked(request);
    }
    if(availability.configured&&request.capture_cmake_graph) {
        result.cmake_graph=snapshot_cmake_graph(workspace,request,
            result.project_fingerprint,result.configuration_fingerprint);
        if(authored_trace)
            attach_authored_cmake_trace(workspace,request,*result.cmake_graph);
    }
    if(action==CppRunRequest::Action::configure)return;
    if(!availability.configured)throw std::runtime_error("Configure and generate the current project before building or running it");
    if(action==CppRunRequest::Action::build||action==CppRunRequest::Action::compile_run){materialize(request.project_files,source);auto saved=read_json(state_file);
        // An original project may already have a valid Ninja artifact from
        // before this objective was activated. A no-op build cannot emit its
        // requested warning. Refresh only this owned source copy, with its
        // identical authenticated bytes, until the original observation has
        // been obtained. Repaired sources and other objectives stay incremental;
        // no generated cache, other target, learner draft or history is erased.
        if(availability.built&&request.cmake_initial_observation_prerequisite.empty()&&
           request.cmake_graph_check&&request.cmake_graph_check->initial_observation&&
           request.cmake_graph_check->initial_observation->warning&&
           !cmake_initial_observation_component(request).empty()) {
            const auto& repair=*request.cmake_graph_check->expression_repair;
            const auto original=std::ranges::find_if(request.project_files,
                [&](const auto& file){return file.first==repair.path;});
            if(original!=request.project_files.end()&&original->second==repair.original_source) {
                const auto owned_source=source/project_path(repair.path);
                state::require_safe_path(owned_source);
                write_exact(owned_source,original->second);
            }
        }
        if(saved.HasKey(L"artifact")&&saved.HasKey(L"artifactSha256")){const auto old=contained(build_directory,project_path(json_text(saved,L"artifact")));const auto current_target=cmake_target(workspace,*request.cmake);
            // An unchanged Ninja build cannot certify a tampered old binary.
            // Remove only that proven changed generated artifact, owned by the
            // selected File API target, so Ninja must produce it again.
            if(std::ranges::find(current_target.artifacts,old)!=current_target.artifacts.end()&&std::filesystem::is_regular_file(old)&&state::sha256(state::read_bytes(old,128*1024*1024))!=json_text(saved,L"artifactSha256"))std::filesystem::remove(old);}
        for(const auto key:{L"built",L"artifact",L"artifactSha256"})if(saved.HasKey(key))saved.Remove(key);save_json(state_file,saved);
        result.compilation=run_phase(cmake,{L"--build",build_directory.wstring(),L"--target",std::wstring(winrt::to_hstring(request.cmake->target)),L"--parallel",L"2"},result.run_directory,compiler_input,L"build",request.compile_timeout,request.output_limit_bytes,true,stop,true,fixed_environment);
        require_materialized_project(request,source,state_file,"la construcción de CMake");
        if(!result.compilation.succeeded())return;const auto target=cmake_target(workspace,*request.cmake);result.target_type=target.type;
        if(target.type=="EXECUTABLE"){for(const auto& artifact:target.artifacts)if(executable64(artifact)){result.artifact=artifact;break;}if(result.artifact.empty())throw std::runtime_error("The selected CMake executable did not produce a native x64 artifact inside its build directory");
            saved.Insert(L"built",JsonValue::CreateStringValue(winrt::to_hstring(result.project_fingerprint)));saved.Insert(L"artifact",JsonValue::CreateStringValue(result.artifact.lexically_relative(build_directory).generic_wstring()));saved.Insert(L"artifactSha256",JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(state::read_bytes(result.artifact,128*1024*1024)))));save_json(state_file,saved);}
        availability=inspect_unlocked(request);
    }
    if(availability.built&&request.cmake_graph_check&&
       request.cmake_graph_check->initial_observation)
        result.cmake_artifact_fingerprint=state::sha256(
            state::read_bytes(availability.artifact,128*1024*1024));
    if(action==CppRunRequest::Action::build)return;
    if(action==CppRunRequest::Action::execute&&availability.configured&&
       json_text(read_json(state_file),L"built")==result.project_fingerprint)
        require_materialized_project(request,source,state_file,"la reutilización del ejecutable");
    if(!availability.built)throw std::runtime_error("Build the executable of the current project revision before running it");
    require_materialized_project(request,source,state_file,"el inicio de la ejecución");
    result.compilation.status=CppPhaseStatus::completed;result.compilation.exit_code=0;result.artifact=availability.artifact;result.target_type=availability.target_type;result.executable=request.cmake->target;
    if(!defer_execution)result.execution=run_phase(result.artifact,{},result.run_directory,input,L"execution",request.execution_timeout,request.output_limit_bytes,false,stop,false,fixed_environment);
}

void run_cmake(const CppRunRequest& request,CppRunResult& result,const std::filesystem::path& input,const std::filesystem::path& compiler_input,std::stop_token stop){
    validate_request_project(request);const auto workspace=cmake_workspace(request);const auto shared_lock=project_mutex(workspace);std::unique_lock<std::timed_mutex> lock(*shared_lock,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10)))if(stop.stop_requested()){auto& phase=request.action==CppRunRequest::Action::execute?result.execution:request.action==CppRunRequest::Action::build?result.compilation:result.configuration;phase.status=CppPhaseStatus::cancelled;return;}
    run_cmake_unlocked(request,result,input,compiler_input,stop);
}

std::string_view trim_ascii(std::string_view text) {
    constexpr std::string_view blanks=" \t\r\n";
    const auto first=text.find_first_not_of(blanks);
    return first==text.npos?std::string_view{}:text.substr(first,text.find_last_not_of(blanks)-first+1);
}
std::uint32_t diagnostic_number(std::string_view text) {
    std::uint32_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()?value:0;
}
// Parse the invariant MSVC code and source coordinates, never translated
// severity labels or diagnostic prose. Only the first code on each row is a
// primary diagnostic; a code quoted in its explanatory message is not another.
std::vector<CppCompilerDiagnostic> compilation_diagnostics(std::string_view stream,
        const std::filesystem::path& directory,bool warnings) {
    std::vector<CppCompilerDiagnostic> errors;
    while(!stream.empty()) {
        const auto newline=stream.find('\n');const auto line=stream.substr(0,newline);
        for(std::size_t offset=0;offset<line.size();++offset) {
            if(offset&&line[offset-1]!=' '&&line[offset-1]!='\t')continue;
            const auto prefix=line.substr(offset).starts_with("LNK")?3u:
                line[offset]=='C'||line[offset]=='D'?1u:0u;
            if(!prefix||offset+prefix+4>=line.size()||line[offset+prefix+4]!=':')continue;
            const auto digits=line.substr(offset+prefix,4);const auto number=diagnostic_number(digits);
            if(!number)continue;
            const bool primary=warnings?(line[offset]=='C'&&number>=4000&&number<5000):
                (line[offset]=='C'&&number<4000)||(line[offset]=='D'&&number<9000)||prefix==3;
            if(primary) {
                CppCompilerDiagnostic diagnostic;diagnostic.code=std::string(line.substr(offset,prefix+4));
                const auto header=line.substr(0,offset);const auto close=header.rfind(')');
                const auto open=close==header.npos?header.npos:header.rfind('(',close);
                if(warnings) {
                    // Require a primary source-location diagnostic. A code
                    // quoted after other prose is not a warning observation.
                    // The severity word can be localized, but cannot contain
                    // quotation marks, another colon, digits or punctuation.
                    if(open==header.npos||close<=open)break;
                    auto severity=trim_ascii(header.substr(close+1));
                    if(severity.empty()||severity.front()!=':')break;
                    severity=trim_ascii(severity.substr(1));
                    if(severity.empty()||!std::ranges::all_of(severity,[](unsigned char c) {
                        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c>=128||c==' '||c=='\t';
                    }))break;
                }
                if(open!=header.npos&&close>open) {
                    const auto coordinates=header.substr(open+1,close-open-1);const auto comma=coordinates.find(',');
                    diagnostic.line=diagnostic_number(coordinates.substr(0,comma));
                    if(comma!=coordinates.npos)diagnostic.column=diagnostic_number(coordinates.substr(comma+1));
                    auto source=trim_ascii(header.substr(0,open));
                    if(source.size()>1&&source.front()=='"'&&source.back()=='"')source=source.substr(1,source.size()-2);
                    if(!source.empty()) {
                        diagnostic.source=std::filesystem::path(std::wstring(winrt::to_hstring(source)));
                        if(diagnostic.source.is_relative())diagnostic.source=directory/diagnostic.source;
                        diagnostic.source=diagnostic.source.lexically_normal();
                    }
                }
                errors.push_back(std::move(diagnostic));
            }
            break;
        }
        if(newline==stream.npos)break;stream.remove_prefix(newline+1);
    }
    return errors;
}
std::vector<CppCompilerDiagnostic> compilation_errors(std::string_view stream,const std::filesystem::path& directory) {
    return compilation_diagnostics(stream,directory,false);
}
std::vector<CppCompilerDiagnostic> compilation_errors(const CppPhaseResult& phase,const std::filesystem::path& directory) {
    auto errors=compilation_errors(phase.stdout_bytes,directory);
    auto extra=compilation_errors(phase.stderr_bytes,directory);
    errors.insert(errors.end(),std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));return errors;
}
bool same_diagnostic_source(const std::filesystem::path& left,const std::filesystem::path& right) {
    const auto first=left.lexically_normal().wstring(),second=right.lexically_normal().wstring();
    return CompareStringOrdinal(first.c_str(),-1,second.c_str(),-1,TRUE)==CSTR_EQUAL;
}
void run_diagnostic(const CppRunRequest& request,CppRunResult& result,const std::filesystem::path& compiler,
        const std::filesystem::path& compiler_input,std::stop_token stop) {
    auto& evidence=*result.diagnostic;const auto& objective=*request.diagnostic;
    evidence.source_sha256=state::sha256(request.source_utf8);evidence.control_sha256=state::sha256(objective.control_source_utf8);
    const auto control=result.run_directory/L"control",candidate=result.run_directory/L"candidate";
    result.diagnostic_source_directory=candidate;result.diagnostic_control_source_directory=control;
    std::filesystem::create_directory(control);std::filesystem::create_directory(candidate);
    write_exact(control/L"main.cpp",objective.control_source_utf8);write_exact(candidate/L"main.cpp",request.source_utf8);
    const auto started=std::chrono::steady_clock::now();
    const auto remaining=[&]{return request.compile_timeout-std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started);};
    const auto compile=[&](const std::filesystem::path& folder) {
        if(remaining().count()<=0){CppPhaseResult exhausted;exhausted.status=CppPhaseStatus::timed_out;return exhausted;}
        return run_phase(compiler,{L"/nologo",L"/c",L"/EHsc",build::cpp_standard_option,L"/Zc:__cplusplus",L"/permissive-",L"/utf-8",L"/W4",
            L"/diagnostics:column",L"/Fo:"+(folder/L"main.obj").wstring(),(folder/L"main.cpp").wstring()},
            folder,compiler_input,L"compilation",remaining(),request.output_limit_bytes,true,stop,true);
    };
    evidence.control=compile(control);
    if(!evidence.control.succeeded())return;
    if(!std::filesystem::is_regular_file(control/L"main.obj")||!std::filesystem::file_size(control/L"main.obj"))
        throw std::runtime_error("The diagnostic control did not produce its fresh object file");
    result.compilation=compile(candidate);evidence.errors=compilation_errors(result.compilation,candidate);
    evidence.expected_rejection=expected_compile_diagnostic(request,result);
}

bool unused_phase(const CppPhaseResult& phase) {
    return phase==CppPhaseResult{};
}
std::string diagnostic_project_hash(const curriculum::CppProjectFiles& files,const std::vector<std::string>& sources) {
    return state::sha256(curriculum::cpp_project_frame(files,sources,std::nullopt));
}
bool diagnostic_phase_bounded(const CppPhaseResult& phase,std::size_t limit) {
    return phase.stdout_bytes.size()<=limit&&phase.stderr_bytes.size()<=limit;
}
bool diagnostic_streams_fit(const std::vector<CppDiagnosticEvidence::Unit>& units,const CppPhaseResult& link,std::size_t limit) {
    std::size_t stdout_left=limit,stderr_left=limit;
    const auto consume=[&](const CppPhaseResult& phase){
        if(phase.stdout_bytes.size()>stdout_left||phase.stderr_bytes.size()>stderr_left)return false;
        stdout_left-=phase.stdout_bytes.size();stderr_left-=phase.stderr_bytes.size();return true;
    };
    for(const auto& unit:units)if(!consume(unit.phase))return false;
    return unused_phase(link)||consume(link);
}
CppPhaseResult diagnostic_aggregate(const std::vector<CppDiagnosticEvidence::Unit>& units,const CppPhaseResult& link,
        std::size_t limit) {
    CppPhaseResult result;bool truncated=false;
    const auto append=[&](const CppPhaseResult& phase){merge_phase(result,phase,limit);
        truncated=truncated||result.status==CppPhaseStatus::output_limit;};
    for(const auto& unit:units)append(unit.phase);
    if(!unused_phase(link))append(link);
    // A later successful unit/link must not relabel discarded diagnostics as
    // a complete capture. Preserve the bytes and final exit code as captured.
    if(truncated)result.status=CppPhaseStatus::output_limit;
    return result;
}
void run_project_diagnostic(const CppRunRequest& request,CppRunResult& result,const std::filesystem::path& compiler,
        const std::filesystem::path& compiler_input,std::stop_token stop) {
    const auto& plan=*request.diagnostic->project;auto& evidence=*result.diagnostic;
    result.diagnostic_source_directory=result.run_directory/L"candidate/project";
    result.diagnostic_control_source_directory=result.run_directory/L"control/project";
    evidence.source_sha256=diagnostic_project_hash(request.project_files,request.source_files);
    evidence.control_sha256=diagnostic_project_hash(plan.control_files,plan.control_sources);
    const auto started=std::chrono::steady_clock::now();
    const auto remaining=[&]{return request.compile_timeout-std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started);};
    const auto build_project=[&](const curriculum::CppProjectFiles& files,const std::vector<std::string>& sources,
            const std::filesystem::path& folder,std::vector<CppDiagnosticEvidence::Unit>& units,CppPhaseResult& link) {
        std::filesystem::create_directory(folder);const auto project=folder/L"project",objects=folder/L"objects";
        materialize(files,project);std::filesystem::create_directory(objects);
        std::vector<std::wstring> link_args{L"/nologo",L"/Fe:"+(folder/L"control.exe").wstring()};
        for(std::size_t index=0;index<sources.size();++index) {
            CppPhaseResult phase;const auto object=objects/(std::to_wstring(index)+L".obj");
            if(remaining().count()<=0)phase.status=CppPhaseStatus::timed_out;
            else phase=run_phase(compiler,{L"/nologo",L"/c",L"/EHsc",build::cpp_standard_option,L"/Zc:__cplusplus",L"/permissive-",
                L"/utf-8",L"/W4",L"/diagnostics:column",L"/I"+project.wstring(),L"/Fo:"+object.wstring(),
                (project/project_path(sources[index])).wstring()},folder,compiler_input,L"compile-"+std::to_wstring(index),
                remaining(),request.output_limit_bytes,true,stop,true);
            units.push_back({sources[index],std::move(phase)});
            if(!units.back().phase.succeeded())return;
            if(!std::filesystem::is_regular_file(object)||!std::filesystem::file_size(object))
                throw std::runtime_error("A diagnostic translation unit did not produce its fresh object");
            link_args.push_back(object.wstring());
        }
        if(!plan.link)return;
        if(remaining().count()<=0){link.status=CppPhaseStatus::timed_out;return;}
        link_args.insert(link_args.end(),{L"/link",L"/INCREMENTAL:NO",L"/MACHINE:X64",L"/SUBSYSTEM:CONSOLE",
            L"/MANIFEST:EMBED",L"/MANIFESTUAC:level='asInvoker' uiAccess='false'"});
        link=run_phase(compiler,link_args,folder,compiler_input,L"link",remaining(),request.output_limit_bytes,true,stop,true);
        if(link.succeeded()&&!executable64(folder/L"control.exe"))
            throw std::runtime_error("The diagnostic link control did not produce its fresh executable");
    };
    build_project(plan.control_files,plan.control_sources,result.run_directory/L"control",evidence.control_units,evidence.control_link);
    evidence.control=diagnostic_aggregate(evidence.control_units,evidence.control_link,request.output_limit_bytes);
    // Even an unrelated edit retains its own compiler/linker output. A failed
    // control can never confer credit, and neither project is ever executed.
    build_project(request.project_files,request.source_files,result.run_directory/L"candidate",evidence.candidate_units,evidence.candidate_link);
    result.compilation=diagnostic_aggregate(evidence.candidate_units,evidence.candidate_link,request.output_limit_bytes);
    evidence.errors=compilation_errors(result.compilation,result.run_directory/L"candidate");
    evidence.expected_rejection=expected_compile_diagnostic(request,result);
}
bool expected_project_diagnostic(const CppRunRequest& request,const CppRunResult& result) {
    if(!request.diagnostic->project||request.project_files.empty()||request.cmake||!request.stdin_utf8.empty())return false;
    const auto& plan=*request.diagnostic->project;const auto& evidence=*result.diagnostic;
    curriculum::validate_cpp_project(request.project_files,request.source_files);
    curriculum::validate_cpp_project(plan.control_files,plan.control_sources);
    if(!request.diagnostic->control_source_utf8.empty()||!request.diagnostic->expected_error_code.empty()||
            request.diagnostic->first_line||request.diagnostic->last_line||plan.error_codes.empty()||
            evidence.source_sha256!=diagnostic_project_hash(request.project_files,request.source_files)||
            evidence.control_sha256!=diagnostic_project_hash(plan.control_files,plan.control_sources))return false;
    const auto valid_units=[&](const auto& units,const auto& sources,bool control) {
        if(units.empty()||units.size()>sources.size()||(control&&units.size()!=sources.size()))return false;
        for(std::size_t n=0;n<units.size();++n){
            if(units[n].source!=sources[n]||((control||n+1<units.size())&&!units[n].phase.succeeded()))return false;
        }
        return true;
    };
    if(!valid_units(evidence.control_units,plan.control_sources,true)||!valid_units(evidence.candidate_units,request.source_files,false)||
            !diagnostic_streams_fit(evidence.control_units,evidence.control_link,request.output_limit_bytes)||
            !diagnostic_streams_fit(evidence.candidate_units,evidence.candidate_link,request.output_limit_bytes)||
            !evidence.control.succeeded()||evidence.control!=diagnostic_aggregate(evidence.control_units,evidence.control_link,request.output_limit_bytes)||
            result.compilation!=diagnostic_aggregate(evidence.candidate_units,evidence.candidate_link,request.output_limit_bytes))return false;
    const auto failed=[](const CppPhaseResult& phase){return phase.status==CppPhaseStatus::completed&&phase.exit_code&&*phase.exit_code&&*phase.exit_code<=255;};
    if(!failed(result.compilation)||!compilation_errors(evidence.control,result.run_directory/L"control").empty())return false;
    const auto errors=compilation_errors(result.compilation,result.run_directory/L"candidate");
    if(errors.empty()||errors!=evidence.errors)return false;
    const auto includes=[&](std::string_view code){return std::ranges::find(plan.error_codes,code)!=plan.error_codes.end();};
    if(!std::ranges::all_of(errors,[&](const auto& error){return includes(error.code);}))return false;
    for(const auto& code:plan.error_codes)if(!std::ranges::any_of(errors,[&](const auto& error){return error.code==code;}))return false;
    if(!plan.link) {
        if(plan.expected_file.empty()||!plan.first_line||plan.last_line<plan.first_line||!plan.symbols.empty()||
                !unused_phase(evidence.control_link)||!unused_phase(evidence.candidate_link)||
                evidence.candidate_units.back().source!=plan.expected_file||!failed(evidence.candidate_units.back().phase))return false;
        const auto file=result.run_directory/L"candidate/project"/project_path(plan.expected_file);
        return std::ranges::all_of(errors,[&](const auto& error){return error.code.starts_with('C')&&error.column&&
            error.line>=plan.first_line&&error.line<=plan.last_line&&same_diagnostic_source(error.source,file);});
    }
    if(!plan.expected_file.empty()||plan.first_line||plan.last_line||plan.symbols.empty()||
            !evidence.control_link.succeeded()||!failed(evidence.candidate_link)||
            evidence.candidate_units.size()!=request.source_files.size()||
            !std::ranges::all_of(evidence.candidate_units,[](const auto& unit){return unit.phase.succeeded();}))return false;
    // Codes and global decorated names are invariant across MSVC UI languages.
    // A summary (1120/1169) is never the primary reason for accreditation.
    std::vector<bool> seen(plan.symbols.size(),false);std::size_t primary=0,summary=0;
    auto stream=evidence.candidate_link.stdout_bytes+'\n'+evidence.candidate_link.stderr_bytes;
    std::string_view rest=stream;
    while(!rest.empty()) {
        const auto end=rest.find('\n');const auto line=rest.substr(0,end);
        const auto parsed=compilation_errors(line,result.run_directory/L"candidate");
        if(!parsed.empty()) {
            if(parsed.size()!=1)return false;const auto& code=parsed.front().code;
            if(code=="LNK1120"||code=="LNK1169")++summary;
            else {
                if(code!="LNK2019"&&code!="LNK2005")return false;
                bool attributed=false;
                for(std::size_t n=0;n<plan.symbols.size();++n)if(line.find('?'+plan.symbols[n]+"@@")!=line.npos){seen[n]=true;attributed=true;}
                if(!attributed)return false;++primary;
            }
        }
        if(end==rest.npos)break;rest.remove_prefix(end+1);
    }
    return primary==plan.symbols.size()&&summary==1&&std::ranges::all_of(seen,[](bool value){return value;});
}

} // namespace

namespace {
// A File API libraries fragment is a native linker fragment, not a target
// name. Match the full generated archive basename at a token boundary,
// avoiding false matches such as otherparts.lib or parts.lib.old.
bool link_mentions_artifact(std::string_view fragment,std::string_view artifact) {
    const auto slash=artifact.find_last_of("/\\");
    const auto basename=artifact.substr(slash==artifact.npos?0:slash+1);
    if(basename.empty())return false;
    const auto edge=[](char c) {
        return c==32||c==9||c==13||c==10||c==34||
            c==39||c==47||c==92;
    };
    for(auto at=fragment.find(basename);at!=fragment.npos;
            at=fragment.find(basename,at+1)) {
        const auto end=at+basename.size();
        if((at==0||edge(fragment[at-1]))&&
           (end==fragment.size()||edge(fragment[end])))return true;
    }
    return false;
}
}
CppCmakeGraphVerdict evaluate_cmake_graph(const CppRunRequest& request,
        const CppRunResult& result,const CppCmakeGraphRequirement& required) {
    const auto fail=[](std::string why) {
        return CppCmakeGraphVerdict{false,std::move(why)};
    };
    if(!request.cmake||!request.capture_cmake_graph||
       request.backend!=CppRunRequest::Backend::native_msvc||
       (request.action!=CppRunRequest::Action::compile_run&&
        request.action!=CppRunRequest::Action::execute)||
       request.request_id!=result.request_id||
       request.revision!=result.revision||!result.error_utf8.empty()||
       !result.compilation.succeeded()||
       result.execution.status!=CppPhaseStatus::completed||
       !result.cmake_graph||result.target_type!="EXECUTABLE"||
       result.executable!=request.cmake->target)
        return fail("Falta una construcción y ejecución nativas de esta revisión.");
    const auto& graph=*result.cmake_graph;
    if(result.project_fingerprint!=fingerprint(request)||
       result.configuration_fingerprint!=fingerprint(request,true)||
       graph.project_fingerprint!=result.project_fingerprint||
       graph.configuration_fingerprint!=result.configuration_fingerprint)
        return fail("La estructura de CMake pertenece a otra revisión del proyecto.");
    if(!required.project_name.empty()&&
       graph.project_name!=required.project_name)
        return fail("El nombre de proyecto generado por CMake no coincide.");
    if(!required.minimum_cmake_version.empty()) {
        const auto expected=curriculum::cmake_numeric_version(
            required.minimum_cmake_version);
        const auto actual=curriculum::cmake_numeric_version(
            graph.minimum_cmake_version);
        if(!expected||!actual||
           (required.minimum_cmake_version_at_least?*actual<*expected:*actual!=*expected))
            return fail(required.minimum_cmake_version_at_least?
                "La versión mínima de CMake declarada es inferior a la requerida.":
                "La versión mínima de CMake declarada en el proyecto no coincide.");
    }
    if(required.option_selection) {
        const auto& opt=*required.option_selection;
        const auto found=request.cmake->options.find(opt.name);
        if(found==request.cmake->options.end()||found->second!="ON")
            return fail("La configuración de esta práctica no selecciona ON.");
        if(!std::ranges::any_of(graph.authored_option_defaults,
                [&](const auto& entry) {
                    return entry.first==opt.name&&entry.second=="OFF";
                }))
            return fail("La opción no declara OFF por defecto en el proyecto.");
    }
    if(required.targets.empty())
        return fail("No hay objetivos de construcción declarados para comprobar.");
    if(required.target_list) {
        const auto accept=[&](const CppCmakeTargetListApplication& proof) {
            auto required_names=required.target_list->targets;
            auto observed_names=proof.targets;
            std::sort(required_names.begin(),required_names.end());
            std::sort(observed_names.begin(),observed_names.end());
            return required_names==observed_names&&
                (!required.target_list->require_append||proof.used_append);
        };
        if(!std::ranges::any_of(graph.authored_target_lists,accept))
            return fail(required.target_list->require_append?
                "Falta aplicar OFF mediante una lista ampliada con APPEND.":
                "Falta aplicar OFF mediante una variable de lista de objetivos.");
    }
    auto message=graph.authored_status_messages.cbegin();
    for(const auto& wanted:required.configure_message_order) {
        message=std::find(message,graph.authored_status_messages.cend(),wanted);
        if(message==graph.authored_status_messages.cend())
            return fail("Falta STATUS en su archivo y orden: "+wanted.first+
                " / "+wanted.second);
        ++message;
    }
    for(const auto& [path,digest]:required.source_integrity) {
        const auto found=std::ranges::find_if(request.project_files,
            [&](const auto& file){return file.first==path;});
        if(found==request.project_files.end()||
           state::sha256(found->second)!=digest)
            return fail("Se modificó un archivo original fuera de la reparación autorizada: "+path);
    }
    if(required.expression_repair) {
        const auto& repair=*required.expression_repair;
        const auto found=std::ranges::find_if(request.project_files,
            [&](const auto& file){return file.first==repair.path;});
        if(found==request.project_files.end()||
           !curriculum::cpp_expression_repair_matches(repair,found->second))
            return fail("Conserva el archivo y modifica sólo la expresión autorizada: "+
                repair.path);
    }
    const auto cmake_source=cmake_workspace(request)/L"p";
    const auto include_has=[&](const std::vector<std::string>& included,
            const std::filesystem::path& expected) {
        const auto expected_path=expected.lexically_normal().generic_wstring();
        const CaseInsensitive comparator;
        return std::ranges::any_of(included,[&](const auto& value) {
            auto candidate=project_path(value);
            if(candidate.is_relative())candidate=cmake_source/candidate;
            const auto found_path=candidate.lexically_normal().generic_wstring();
            return !comparator(expected_path,found_path)&&
                   !comparator(found_path,expected_path);
        });
    };
    std::vector<std::string> seen;
    for(const auto& item:required.targets) {
        if(item.name.empty()||item.type.empty()||
           std::ranges::find(seen,item.name)!=seen.end())
            return fail("Hay un requisito CMake vacío o duplicado.");
        seen.push_back(item.name);
        const auto found=std::ranges::find_if(graph.targets,
            [&](const auto& candidate){return candidate.name==item.name;});
        if(found==graph.targets.end()||found->type!=item.type)
            return fail("Falta el objetivo o tipo solicitado: "+item.name);
        if(!item.source_directory.empty()&&
           std::filesystem::path(found->source_directory).lexically_normal()!=
               project_path(item.source_directory).lexically_normal())
            return fail("El objetivo debe declararse en el subdirectorio CMake: "+
                item.source_directory);
        if(item.require_explicit_cxx20&&!found->explicit_cxx20)
            return fail("Falta declarar el mínimo C++20 en el objetivo: "+item.name);
        if(item.require_explicit_extensions_off&&
           !found->explicit_extensions_off)
            return fail("Falta desactivar las extensiones explícitamente en: "+item.name);
        if(item.require_explicit_msvc_warnings) {
            const bool authored=std::ranges::any_of(found->authored_compile_options,
                [](const auto& option) {
                    const auto level=msvc_warning_level(option);
                    return level&&*level>0;
                });
            if(!authored||!found->msvc_warnings_effective)
                return fail("Declara y conserva un nivel de avisos MSVC activo en: "+item.name);
        }
        for(const auto& option:item.authored_compile_options) {
            if(std::ranges::find(found->authored_compile_options,option)==
                    found->authored_compile_options.end())
                return fail("Falta declarar target_compile_options en "+
                    item.name+": "+option);
            if(std::ranges::find(found->effective_compile_options,option)==
                    found->effective_compile_options.end())
                return fail("La opción no llegó a la compilación de "+
                    item.name+": "+option);
        }
        for(const auto& root:item.public_include_roots) {
            const auto dir=cmake_source/project_path(root);
            if(!include_has(found->authored_public_include_directories,dir))
                return fail("Falta publicar la búsqueda de encabezados: "+root);
            if(!include_has(found->include_directories,dir))
                return fail("La biblioteca no compila con la raíz pública: "+root);
            for(const auto& other:required.targets)
                if(other.type=="EXECUTABLE"&&
                   std::ranges::find(other.links_to,item.name)!=other.links_to.end()) {
                    const auto linked=std::ranges::find_if(graph.targets,
                        [&](const auto& t){return t.name==other.name;});
                    if(linked==graph.targets.end()||
                       !include_has(linked->include_directories,dir))
                        return fail("La ruta PUBLIC no llega al cliente: "+root);
                }
        }
        for(const auto& root:item.private_include_roots) {
            const auto dir=cmake_source/project_path(root);
            if(!include_has(found->authored_private_include_directories,dir))
                return fail("Falta limitar la ruta interna a PRIVATE: "+root);
            if(!include_has(found->include_directories,dir))
                return fail("La biblioteca no recibe su ruta privada: "+root);
            for(const auto& other:required.targets)
                if(other.type=="EXECUTABLE"&&
                   std::ranges::find(other.links_to,item.name)!=other.links_to.end()) {
                    const auto linked=std::ranges::find_if(graph.targets,
                        [&](const auto& t){return t.name==other.name;});
                    if(linked==graph.targets.end()||
                       include_has(linked->include_directories,dir))
                        return fail("La ruta PRIVATE se filtró al cliente: "+root);
                }
        }
        for(const auto& source:item.sources)
            if(std::ranges::find(found->compiled_sources,source)==
                found->compiled_sources.end())
                return fail("El objetivo no compila la fuente: "+source);
        if(item.exact_sources&&
           (found->compiled_sources.size()!=item.sources.size()||
            !std::ranges::all_of(found->compiled_sources,[&](const auto& source) {
                return std::ranges::find(item.sources,source)!=item.sources.end();
            })))
            return fail("El objetivo compila fuentes adicionales o repetidas: "+
                item.name);
        for(const auto& name:item.links_to) {
            const auto library=std::ranges::find_if(graph.targets,
                [&](const auto& candidate){return candidate.name==name;});
            if(library==graph.targets.end()||
               library->artifacts.empty()||
               std::ranges::find(found->build_dependencies,name)==
                   found->build_dependencies.end())
                return fail("Falta la dependencia de biblioteca: "+name);
            const bool actual_link=std::ranges::any_of(library->artifacts,
                [&](const auto& artifact) {
                    return std::ranges::any_of(found->link_libraries,
                        [&](const auto& fragment) {
                            return link_mentions_artifact(fragment,artifact);
                        });
                });
            if(!actual_link)
                return fail("El objetivo no enlaza la biblioteca solicitada: "+name);
        }
    }
    return {true,{}};
}

std::string cpp_launch_failure_message(std::uint32_t error,std::string_view executable_utf8,bool build_tool) {
    auto message="CreateProcessW (Windows error "+std::to_string(error)+") while starting "
        +std::string(build_tool?"a C++ build tool: ":"a newly compiled program: ")
        +std::string(executable_utf8);
    if(error==ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION) {
        message+="\nWindows application-control policy blocked the process before it started. "
            "The source may have compiled successfully, but no execution or output verification took place. "
            "Check Windows Security's blocked-app details and the CodeIntegrity Operational log (event 3077) "
            "to identify the file and its signing status. Keep security protections enabled: "
            "a newly built executable may require a certificate from a trusted code-signing provider "
            "or a separately configured development environment.";
    }
    return message;
}

CppDiagnosticRequest prepare_cpp_diagnostic_request(const curriculum::Block& block,const CppRunRequest& request) {
    CppDiagnosticRequest check;
    if(!block.project_files.empty()) {
        if(const auto baseline=curriculum::prepare_project_compile_diagnostic_source(block,block.project_files,block.source_files)) {
            check.project=baseline->project;
            if(check.project){check.project->expected_file.clear();check.project->first_line=check.project->last_line=0;
                check.project->error_codes.clear();check.project->symbols.clear();}
        }
        if(const auto actual=curriculum::prepare_project_compile_diagnostic_source(block,request.project_files,request.source_files))
            check.project=actual->project;
        return check;
    }
    check.control_source_utf8=block.initial_code;
    if(const auto source=curriculum::prepare_compile_diagnostic_source(block,request.source_utf8)) {
        check.control_source_utf8=source->control_source;check.expected_error_code=source->expected_msvc_error;
        check.first_line=static_cast<std::uint32_t>(source->first_line);check.last_line=static_cast<std::uint32_t>(source->last_line);
    }
    return check;
}

bool expected_compile_diagnostic(const CppRunRequest& request,const CppRunResult& result) try {
    if(!request.diagnostic||!result.diagnostic||!request.stdin_utf8.empty()||request.cmake||
        request.action!=CppRunRequest::Action::compile_run||request.request_id!=result.request_id||request.revision!=result.revision||
        !result.error_utf8.empty()||result.run_directory.empty()||!result.artifact.empty()||result.executable||!result.target_type.empty())return false;
    const auto unused=[](const CppPhaseResult& phase){return phase.status==CppPhaseStatus::not_started&&!phase.exit_code&&phase.stdout_bytes.empty()&&phase.stderr_bytes.empty();};
    if(!unused(result.execution)||!unused(result.configuration))return false;
    if(request.diagnostic->project)return expected_project_diagnostic(request,result);
    if(!request.project_files.empty()||!request.source_files.empty()||!result.diagnostic->control_units.empty()||
            !result.diagnostic->candidate_units.empty()||!unused(result.diagnostic->control_link)||!unused(result.diagnostic->candidate_link))return false;
    const auto& objective=*request.diagnostic;const auto& evidence=*result.diagnostic;
    if(objective.expected_error_code.size()!=5||objective.expected_error_code.front()!='C'||
        diagnostic_number(std::string_view(objective.expected_error_code).substr(1))<2000||
        diagnostic_number(std::string_view(objective.expected_error_code).substr(1))>=4000||
        !objective.first_line||objective.last_line<objective.first_line||!evidence.control.succeeded()||
        evidence.source_sha256!=state::sha256(request.source_utf8)||evidence.control_sha256!=state::sha256(objective.control_source_utf8)||
        result.compilation.status!=CppPhaseStatus::completed||!result.compilation.exit_code||
        !*result.compilation.exit_code||*result.compilation.exit_code>255)return false;
    if(!diagnostic_phase_bounded(evidence.control,request.output_limit_bytes)||
        !diagnostic_phase_bounded(result.compilation,request.output_limit_bytes)||
        !compilation_errors(evidence.control,result.run_directory/L"control").empty())return false;
    const auto errors=compilation_errors(result.compilation,result.run_directory/L"candidate");
    if(errors.empty()||errors!=evidence.errors)return false;
    const auto source=result.run_directory/L"candidate/main.cpp";
    return std::ranges::all_of(errors,[&](const auto& item){return item.code==objective.expected_error_code&&item.column&&
        item.line>=objective.first_line&&item.line<=objective.last_line&&same_diagnostic_source(item.source,source);});
}catch(const std::exception&){return false;}catch(const winrt::hresult_error&){return false;}

bool accepted_compile_diagnostic(const CppRunRequest& request,const CppRunResult& result) try {
    if(!request.diagnostic||!result.diagnostic||!request.stdin_utf8.empty()||request.cmake||
        request.action!=CppRunRequest::Action::compile_run||request.request_id!=result.request_id||request.revision!=result.revision||
        !result.error_utf8.empty()||result.run_directory.empty()||!result.artifact.empty()||result.executable||!result.target_type.empty()||
        !unused_phase(result.execution)||!unused_phase(result.configuration)||!result.compilation.succeeded())return false;
    const auto& objective=*request.diagnostic;const auto& evidence=*result.diagnostic;
    const auto complete=[&](const CppPhaseResult& phase){return phase.succeeded()&&diagnostic_phase_bounded(phase,request.output_limit_bytes);};
    if(!complete(evidence.control)||!complete(result.compilation)||!evidence.errors.empty()||
        !compilation_errors(evidence.control,result.run_directory/L"control").empty()||
        !compilation_errors(result.compilation,result.run_directory/L"candidate").empty())return false;
    if(!objective.project)return request.project_files.empty()&&request.source_files.empty()&&
        evidence.control_units.empty()&&evidence.candidate_units.empty()&&unused_phase(evidence.control_link)&&unused_phase(evidence.candidate_link)&&
        evidence.source_sha256==state::sha256(request.source_utf8)&&evidence.control_sha256==state::sha256(objective.control_source_utf8);
    const auto& project=*objective.project;
    if(request.project_files.empty()||!objective.control_source_utf8.empty()||!objective.expected_error_code.empty()||
        objective.first_line||objective.last_line)return false;
    curriculum::validate_cpp_project(request.project_files,request.source_files);
    curriculum::validate_cpp_project(project.control_files,project.control_sources);
    if(evidence.source_sha256!=diagnostic_project_hash(request.project_files,request.source_files)||
        evidence.control_sha256!=diagnostic_project_hash(project.control_files,project.control_sources))return false;
    const auto units_complete=[&](const auto& units,const auto& sources){
        if(units.empty()||units.size()!=sources.size())return false;
        for(std::size_t n=0;n<units.size();++n)if(units[n].source!=sources[n]||!complete(units[n].phase))return false;
        return true;
    };
    if(!units_complete(evidence.control_units,project.control_sources)||!units_complete(evidence.candidate_units,request.source_files))return false;
    if(project.link){if(!complete(evidence.control_link)||!complete(evidence.candidate_link))return false;}
    else if(!unused_phase(evidence.control_link)||!unused_phase(evidence.candidate_link))return false;
    if(!diagnostic_streams_fit(evidence.control_units,evidence.control_link,request.output_limit_bytes)||
        !diagnostic_streams_fit(evidence.candidate_units,evidence.candidate_link,request.output_limit_bytes))return false;
    return evidence.control==diagnostic_aggregate(evidence.control_units,evidence.control_link,request.output_limit_bytes)&&
        result.compilation==diagnostic_aggregate(evidence.candidate_units,evidence.candidate_link,request.output_limit_bytes);
}catch(const std::exception&){return false;}catch(const winrt::hresult_error&){return false;}

// Reconstruct the independently configured default-OFF project to validate
// the second result; a single correct ON output cannot grant this objective.
CppRunRequest default_option_request(const CppRunRequest& request,
        const curriculum::CmakeOptionSelectionRequirement& option,
        const curriculum::CmakeGraphRequirement& authored) {
    auto off=request;
    // A public execute reuses only its verified ON artifact. The independent
    // OFF configuration must still build its own target before it can run.
    off.action=CppRunRequest::Action::compile_run;
    off.request_id=~request.request_id;
    off.project_key+="/cmake-option-off-v1/"+option.name;
    off.cmake->options.erase(option.name);
    off.named_rule_probe.reset();
    off.rejection_probe.reset();
    off.observation_probe.reset();
    off.assertion_mode.reset();
    off.capture_cmake_graph=true;
    auto off_required=authored;
    off_required.option_selection.reset();
    for(auto& target:off_required.targets)
        if(target.name==option.library_target) {
            target.sources={option.disabled_source};
            target.exact_sources=true;
        }
    off.cmake_graph_check=std::move(off_required);
    return off;
}
bool verified_default_option_probe(const CppRunRequest& request,
        const CppRunResult& result,
        const curriculum::CmakeGraphRequirement& required) {
    if(!required.option_selection||!result.cmake_option_probe)return false;
    const auto& option=*required.option_selection;
    const auto off=default_option_request(request,option,required);
    const auto& proof=*result.cmake_option_probe;
    CppRunResult replay;
    replay.request_id=proof.request_id;
    replay.revision=proof.revision;
    replay.compilation=proof.compilation;
    replay.configuration=proof.configuration;
    replay.execution=proof.execution;
    replay.project_fingerprint=proof.project_fingerprint;
    replay.configuration_fingerprint=proof.configuration_fingerprint;
    replay.target_type=proof.target_type;
    replay.executable=proof.executable;
    replay.error_utf8=proof.error_utf8;
    replay.cmake_graph=proof.graph;
    return replay.execution.succeeded()&&
        replay.execution.stdout_bytes==option.disabled_stdout&&
        replay.execution.stderr_bytes.empty()&&
        evaluate_cmake_graph(off,replay,*off.cmake_graph_check).correct;
}
std::string cpp_boolean_battery_request_fingerprint(const CppRunRequest& request) {
    return boolean_battery_fingerprint(request);
}
std::string cpp_named_rule_battery_request_fingerprint(const CppRunRequest& request) {
    return named_rule_battery_fingerprint(request);
}
std::string cpp_named_rule_constant_variation_request_fingerprint(const CppRunRequest& request) {
    return named_rule_constant_variation_fingerprint(request);
}
std::string cpp_reduction_request_fingerprint(const CppRunRequest& request) {
    return reduction_fingerprint(request);
}
std::string cpp_integer_battery_request_fingerprint(const CppRunRequest& request) {
    return integer_battery_fingerprint(request);
}
std::string cpp_adjustment_project_request_fingerprint(const CppRunRequest& request) {
    return adjustment_project_fingerprint(request);
}
std::string cpp_cmake_graph_request_fingerprint(const CppRunRequest& request) {
    return cmake_graph_fingerprint(request);
}
std::string cpp_cmake_initial_observation_component_fingerprint(const CppRunRequest& request) {
    return cmake_initial_observation_component(request);
}
std::string cpp_cmake_initial_observation_request_fingerprint(const CppRunRequest& request) {
    return cmake_initial_observation_fingerprint(request);
}
std::optional<CppCmakeInitialObservationCapture>
capture_cpp_cmake_initial_observation(const CppRunRequest& request,const CppRunResult& result) {
    const auto binding=cmake_initial_observation_fingerprint(request);
    const auto component=cmake_initial_observation_component(request);
    const auto digest=[](std::string_view value) {
        return value.size()==64&&std::ranges::all_of(value,[](unsigned char c) {
            return (c>='0'&&c<='9')||(c>='a'&&c<='f');
        });
    };
    if(binding.empty()||component.empty()||
       result.cmake_initial_observation_request_fingerprint!=binding||
       request.request_id!=result.request_id||request.revision!=result.revision||
       !result.error_utf8.empty()||!result.compilation.succeeded()||
       (result.configuration.status!=CppPhaseStatus::not_started&&
        !result.configuration.succeeded())||
       !result.cmake_graph||result.target_type!="EXECUTABLE"||
       result.artifact.empty()||!digest(result.cmake_artifact_fingerprint)||
       result.project_fingerprint!=fingerprint(request)||
       result.configuration_fingerprint!=fingerprint(request,true)||
       result.cmake_graph->project_fingerprint!=result.project_fingerprint||
       result.cmake_graph->configuration_fingerprint!=result.configuration_fingerprint)
        return {};
    const auto& required=*request.cmake_graph_check;
    const auto& spec=*required.initial_observation;
    const auto& repair=*required.expression_repair;
    const auto original=std::ranges::find_if(request.project_files,
        [&](const auto& file){return file.first==repair.path;});
    if(original==request.project_files.end()||original->second!=repair.original_source)return {};
    const auto& graph=*result.cmake_graph;
    const auto selected=std::ranges::find_if(graph.targets,
        [&](const auto& target){return target.name==spec.executable&&target.type=="EXECUTABLE";});
    if(selected==graph.targets.end()||!std::ranges::any_of(selected->artifacts,
       [&](const auto& artifact) {
            const auto relative=project_path(artifact);
            if(relative.empty()||relative.is_absolute()||*relative.begin()==L"..")return false;
            return same_diagnostic_source(result.artifact,cmake_workspace(request)/relative);
        }))return {};
    CppCmakeInitialObservationCapture capture{
        binding,component,result.cmake_artifact_fingerprint,false,false};
    if(spec.warning&&(request.action==CppRunRequest::Action::build||
                     request.action==CppRunRequest::Action::compile_run)) {
        const auto lines=curriculum::cmake_initial_observation_warning_lines(required);
        const bool effective=std::ranges::any_of(required.targets,[&](const auto& wanted) {
            if(std::ranges::find(wanted.sources,repair.path)==wanted.sources.end())return false;
            const auto target=std::ranges::find_if(graph.targets,
                [&](const auto& fact){return fact.name==wanted.name&&fact.type==wanted.type;});
            return target!=graph.targets.end()&&target->msvc_warnings_effective&&
                std::ranges::find(target->compiled_sources,repair.path)!=target->compiled_sources.end();
        });
        if(lines&&effective) {
            const auto source_directory=cmake_workspace(request)/L"p";
            const auto expected=source_directory/project_path(spec.warning->path);
            const auto matches=[&](std::string_view stream) {
                const auto warnings=compilation_diagnostics(stream,source_directory,true);
                return std::ranges::any_of(warnings,[&](const auto& warning) {
                    return warning.code==spec.warning->code&&warning.line>=lines->first&&
                        warning.line<=lines->second&&same_diagnostic_source(warning.source,expected);
                });
            };
            capture.warning_observed=matches(result.compilation.stdout_bytes)||
                matches(result.compilation.stderr_bytes);
        }
    }
    if(request.action==CppRunRequest::Action::compile_run||request.action==CppRunRequest::Action::execute) {
        const auto matches_output=[&] {
            if(result.execution.stdout_bytes==spec.standard_output)return true;
            if(spec.standard_output.find('\r')!=spec.standard_output.npos)return false;
            std::string native;
            native.reserve(spec.standard_output.size());
            for(const auto c:spec.standard_output) { if(c=='\n')native+='\r';native+=c; }
            return result.execution.stdout_bytes==native;
        };
        capture.failure_observed=request.stdin_utf8==spec.standard_input&&
            result.executable==spec.executable&&
            result.execution.status==CppPhaseStatus::completed&&
            result.execution.exit_code==spec.exit_code&&
            result.execution.stderr_bytes.empty()&&matches_output()&&
            evaluate_cmake_graph(request,result,required).correct;
    }
    return capture;
}
std::string cpp_integer_battery_component_fingerprint(const CppRunRequest& request) {
    if(!integer_battery_context(request)||!request.integer_battery_probe->prerequisite)
        return {};
    auto component=request;
    const auto& spec=*request.integer_battery_probe;
    const auto test=std::ranges::find_if(component.project_files,
        [&](const auto& item){return item.first==spec.test_path;});
    if(test==component.project_files.end())return {};
    test->second.clear();
    component.cmake->target=spec.prerequisite->target;
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return state::sha256(std::string("windows-integer-battery-component-v1/")+
        field(fingerprint(component))+field(component.project_key)+
        field(curriculum::integer_battery_spec_frame(spec)));
}

curriculum::FreshExecution capture_cpp_execution(const CppRunRequest& request,const CppRunResult& result) {
    curriculum::FreshExecution fresh;
    if(request.request_id!=result.request_id||request.revision!=result.revision||request.diagnostic
        ||(request.action!=CppRunRequest::Action::compile_run&&request.action!=CppRunRequest::Action::execute)
        ||!result.error_utf8.empty()
        ||(request.cmake&&result.executable!=std::optional<std::string>{request.cmake->target}))return fresh;
    if(named_rule_battery_requested(request)) {
        const auto binding=named_rule_battery_fingerprint(request);
        if(binding.empty()||result.named_rule_battery_request_fingerprint!=binding||
           (request.cmake&&(result.project_fingerprint!=fingerprint(request)||
            result.configuration_fingerprint!=fingerprint(request,true))))return fresh;
    }
    if(named_rule_constant_variations_requested(request)) {
        const auto binding=named_rule_constant_variation_fingerprint(request);
        if(binding.empty()||result.named_rule_constant_variation_request_fingerprint!=binding||
           (request.cmake&&(result.project_fingerprint!=fingerprint(request)||
            result.configuration_fingerprint!=fingerprint(request,true))))return fresh;
    }
    if(request.boolean_battery_probe&&
       result.boolean_battery_request_fingerprint!=boolean_battery_fingerprint(request))return fresh;
    if(request.reduction_probe) {
        const auto binding=reduction_fingerprint(request);
        if(binding.empty()||result.reduction_request_fingerprint!=binding)return fresh;
    }
    if(request.integer_battery_probe) {
        const auto binding=integer_battery_fingerprint(request);
        if(binding.empty()||result.integer_battery_request_fingerprint!=binding||
           result.project_fingerprint!=fingerprint(request)||
           result.configuration_fingerprint!=fingerprint(request,true))return fresh;
    }
    if(request.adjustment_project_probe) {
        const auto binding=adjustment_project_fingerprint(request);
        if(binding.empty()||result.adjustment_project_request_fingerprint!=binding||
           result.project_fingerprint!=fingerprint(request)||
           result.configuration_fingerprint!=fingerprint(request,true))return fresh;
    }
    if(request.cmake_graph_check&&
       (cmake_graph_fingerprint(request).empty()||
        result.project_fingerprint!=fingerprint(request)||
        result.configuration_fingerprint!=fingerprint(request,true)))return fresh;
    if(request.cmake_graph_check&&request.cmake_graph_check->initial_observation&&
       (cmake_initial_observation_fingerprint(request).empty()||
        result.cmake_initial_observation_request_fingerprint!=
            cmake_initial_observation_fingerprint(request)))return fresh;
    fresh.source_revision=result.revision;
    fresh.compilation_succeeded=result.compilation.succeeded()
        &&(result.configuration.status==CppPhaseStatus::not_started||result.configuration.succeeded());
    fresh.execution_succeeded=result.execution.succeeded();
    fresh.execution_completed=result.execution.status==CppPhaseStatus::completed
        &&result.execution.exit_code&&*result.execution.exit_code<=255;
    if(result.execution.exit_code&&*result.execution.exit_code<=255)fresh.exit_code=static_cast<int>(*result.execution.exit_code);
    fresh.standard_input=request.stdin_utf8;fresh.standard_output=result.execution.stdout_bytes;
    fresh.standard_error=result.execution.stderr_bytes;fresh.executable=result.executable;
    if(request.local_string_check) {
        const auto frame=local_string_fingerprint(request);
        fresh.local_string_request_fingerprint=frame;
        fresh.local_string_check_feedback=
            "La salida coincide, pero aún no se comprobó que proceda de las variables de texto.";
        if(!frame.empty()&&result.local_string_request_fingerprint==frame&&
           fresh.compilation_succeeded&&fresh.execution_succeeded&&
           result.local_string_evidence.size()==request.local_string_check->probes.size()) {
            bool passed=true;
            for(std::size_t i=0;i<request.local_string_check->probes.size();++i) {
                const auto& probe=request.local_string_check->probes[i];
                const auto& evidence=result.local_string_evidence[i];
                const auto instrumented=curriculum::cpp_local_string_override(
                    request.source_utf8,probe);
                if(!instrumented||
                   evidence.instrumented_source_sha256!=state::sha256(*instrumented)||
                   !evidence.compilation.succeeded()||
                   !evidence.execution.succeeded()||
                   !evidence.execution.stderr_bytes.empty()||
                   !curriculum::cpp_local_string_expected_output(
                        probe,evidence.execution.stdout_bytes)) {
                    passed=false;
                    fresh.local_string_check_feedback=
                        "Comprueba que el resultado mostrado depende de la variable de texto '"+
                        probe.variable+"' y no de un literal fijo. La variante independiente no produjo la salida esperada.";
                    break;
                }
            }
            fresh.local_string_check_correct=passed;
            if(passed)fresh.local_string_check_feedback.clear();
        }
    }
    if(request.local_int_check) {
        const auto frame=local_int_fingerprint(request);
        fresh.local_int_request_fingerprint=frame;
        fresh.local_int_check_feedback=
            "La salida coincide, pero no se acreditó que dependa de las variables int declaradas.";
        if(!frame.empty()&&result.local_int_request_fingerprint==frame&&
           fresh.compilation_succeeded&&fresh.execution_succeeded&&
           result.local_int_evidence.size()==request.local_int_check->probes.size()) {
            bool passed=true;
            for(std::size_t i=0;i<request.local_int_check->probes.size();++i) {
                const auto& probe=request.local_int_check->probes[i];
                const auto& captured=result.local_int_evidence[i];
                const auto variant=curriculum::cpp_local_int_override(
                    request.source_utf8,probe);
                const bool exact=variant&&
                    captured.instrumented_source_sha256==state::sha256(*variant)&&
                    captured.compilation.succeeded()&&captured.execution.succeeded()&&
                    captured.execution.stderr_bytes.empty()&&
                    curriculum::cpp_local_int_expected_output(
                        probe,captured.execution.stdout_bytes);
                if(!exact) {
                    passed=false;
                    fresh.local_int_check_feedback=
                        "Comprueba que los resultados proceden de la variable int '"+probe.variable+
                        "' y no de una cifra fija. La prueba independiente no reprodujo el cambio solicitado.";
                    break;
                }
            }
            fresh.local_int_check_correct=passed;
            if(passed)fresh.local_int_check_feedback.clear();
        }
    }
    if(request.boolean_battery_probe) {
        fresh.execution_completed=false;
        fresh.execution_succeeded=false;
        fresh.boolean_battery_probe_feedback=
            "No se obtuvo una batería independiente completa para este fuente.";
        const auto& spec=*request.boolean_battery_probe;
        const auto frame=boolean_battery_fingerprint(request);
        fresh.boolean_battery_request_fingerprint=frame;
        if(result.execution.status==CppPhaseStatus::completed&&result.execution.exit_code)
            fresh.boolean_battery_exit_code=*result.execution.exit_code;
        if(!curriculum::boolean_battery_preserves_function(request.source_utf8,spec))
            fresh.boolean_battery_probe_feedback=
                "Conserva la función publicada y construye las comprobaciones en el resto del programa.";
        if(request.backend==CppRunRequest::Backend::native_msvc&&
           request.action==CppRunRequest::Action::compile_run&&!request.cmake&&
           request.project_files.empty()&&request.source_files.empty()&&
           !request.reduction_probe&&!request.open_battery_probe&&!request.named_rule_probe&&
           !request.rejection_probe&&!request.observation_probe&&!request.assertion_mode&&
           !request.cmake_graph_check&&!request.capture_cmake_graph&&!request.adjustment_project_probe&&
           curriculum::boolean_battery_preserves_function(request.source_utf8,spec)&&
           result.boolean_battery_evidence) {
            const auto& evidence=*result.boolean_battery_evidence;
            const auto plan=curriculum::boolean_battery_mutation_plan(spec);
            if(evidence.original_program_fingerprint==frame&&plan&&
               evidence.runs.size()==plan->size()) {
                const auto& published=evidence.runs.back();
                // A normal-return witness is required even for an exit DWORD
                // numerically identical to a Windows exception code. Ordinary
                // objectives keep their pre-existing 0..255 rule above.
                const bool same_published=published.program_frame==frame&&
                    published.mutation==curriculum::BooleanBatteryMutation::published&&
                    !published.target&&published.compiled&&published.executed&&
                    published.completed_normally&&
                    result.compilation.succeeded()&&
                    result.execution.status==CppPhaseStatus::completed&&
                    result.execution.exit_code&&
                    published.exit_code==*result.execution.exit_code&&
                    published.stdout_bytes==result.execution.stdout_bytes&&
                    published.stderr_bytes==result.execution.stderr_bytes;
                if(same_published) {
                    fresh.execution_completed=true;
                    fresh.execution_succeeded=published.exit_code==0;
                    const auto verdict=curriculum::boolean_battery_verify(
                        spec,evidence.runs,frame);
                    fresh.boolean_battery_probe_correct=verdict.accepted;
                    fresh.boolean_battery_probe_feedback=verdict.reason;
                }else{
                    fresh.boolean_battery_probe_feedback=
                        "La ejecución publicada no tiene la misma salida y terminación normal que el fuente capturado.";
                }
            }
        }
    }
    if(request.reduction_probe) {
        fresh.execution_completed=false;
        fresh.execution_succeeded=false;
        fresh.reduction_probe_feedback=
            "No se obtuvo una reproducción independiente completa para este fuente.";
        const auto& spec=*request.reduction_probe;
        const auto frame=reduction_fingerprint(request);
        fresh.reduction_request_fingerprint=frame;
        const auto source_check=curriculum::reduction_source_check(request.source_utf8,spec);
        if(!source_check.accepted)fresh.reduction_probe_feedback=source_check.reason;
        if(request.backend==CppRunRequest::Backend::native_msvc&&
           request.action==CppRunRequest::Action::compile_run&&!request.cmake&&
           request.project_files.empty()&&request.source_files.empty()&&
           !request.boolean_battery_probe&&!request.open_battery_probe&&!request.named_rule_probe&&
           !request.rejection_probe&&!request.observation_probe&&!request.assertion_mode&&
           !request.cmake_graph_check&&!request.capture_cmake_graph&&!request.adjustment_project_probe&&
           result.reduction_evidence) {
            const auto& evidence=*result.reduction_evidence;
            const auto plan=curriculum::reduction_plan(spec);
            if(evidence.original_program_fingerprint==frame&&plan&&
               !evidence.runs.empty()&&evidence.runs.size()==plan->size()) {
                const auto& published=evidence.runs.front();
                // Only the public phase must return the authored zero. The
                // private interventions may legitimately use a different
                // normal exit when their delivered result changes.
                const bool same_published=published.program_frame==frame&&
                    !published.phase.replacement&&published.compiled&&published.executed&&
                    published.completed_normally&&result.compilation.succeeded()&&
                    result.execution.status==CppPhaseStatus::completed&&
                    result.execution.exit_code&&published.exit_code==*result.execution.exit_code&&
                    published.stdout_bytes==result.execution.stdout_bytes&&
                    published.stderr_bytes==result.execution.stderr_bytes;
                if(same_published) {
                    fresh.execution_completed=true;
                    fresh.execution_succeeded=published.exit_code==0;
                    const auto verdict=curriculum::reduction_verify(spec,evidence.runs,frame);
                    fresh.exit_code=static_cast<std::int32_t>(published.exit_code);
                    fresh.reduction_probe_correct=source_check.accepted&&verdict.accepted;
                    fresh.reduction_probe_feedback=source_check.accepted?verdict.reason:source_check.reason;
                }else if(source_check.accepted) {
                    fresh.reduction_probe_feedback=
                        "La reproducción publicada no tiene la misma salida y terminación normal que el fuente capturado.";
                }
            }
        }
    }
    if(request.integer_battery_probe) {
        fresh.execution_completed=false;
        fresh.execution_succeeded=false;
        const auto& spec=*request.integer_battery_probe;
        const auto frame=integer_battery_fingerprint(request);
        fresh.integer_battery_request_fingerprint=frame;
        fresh.integer_battery_probe_feedback=
            "No se obtuvo una batería independiente completa para este proyecto y objetivo.";
        if(result.execution.status==CppPhaseStatus::completed&&result.execution.exit_code)
            fresh.integer_battery_exit_code=*result.execution.exit_code;
        if(integer_battery_context(request)&&result.target_type=="EXECUTABLE"&&
           result.integer_battery_evidence&&
           result.integer_battery_evidence->original_program_fingerprint==frame) {
            const auto& runs=result.integer_battery_evidence->runs;
            const bool client=spec.prerequisite&&request.cmake->target==spec.prerequisite->target;
            const auto plan=curriculum::integer_battery_plan(spec);
            const bool count=client?runs.size()==1:plan&&runs.size()==plan->size();
            if(count&&!runs.empty()) {
                const auto& published=runs.back();
                const bool same_published=published.program_frame==frame&&
                    published.phase.mutation==curriculum::IntegerBatteryMutation::published&&
                    !published.phase.target&&published.compiled&&published.executed&&
                    published.completed_normally&&fresh.compilation_succeeded&&
                    result.execution.status==CppPhaseStatus::completed&&result.execution.exit_code&&
                    published.exit_code==*result.execution.exit_code&&
                    published.stdout_bytes==result.execution.stdout_bytes&&
                    published.stderr_bytes==result.execution.stderr_bytes;
                if(same_published) {
                    fresh.execution_completed=true;
                    fresh.execution_succeeded=published.exit_code==0;
                    if(published.exit_code<=255)fresh.exit_code=static_cast<int>(published.exit_code);
                    if(client) {
                        const auto& required=*spec.prerequisite;
                        bool calls=published.calls.size()==required.calls.size();
                        if(calls)for(std::size_t i=0;i<required.calls.size();++i)
                            if(published.calls[i].input!=required.calls[i].input||
                               published.calls[i].result!=required.calls[i].result)calls=false;
                        fresh.integer_battery_client_correct=calls&&
                            published.exit_code==required.exit_code&&
                            request.stdin_utf8==required.standard_input&&
                            published.stdout_bytes==required.standard_output&&
                            published.stderr_bytes==required.standard_error;
                        fresh.integer_battery_probe_feedback=fresh.integer_battery_client_correct
                            ?"Se verificó la ejecución del cliente; continúa con el comprobador."
                            :"La ejecución del cliente no conserva las llamadas y resultados declarados.";
                    }else if(request.cmake->target==spec.test_target) {
                        const auto verdict=curriculum::integer_battery_verify(spec,runs,frame);
                        const auto driver=std::ranges::find_if(request.project_files,
                            [&](const auto& item){return item.first==spec.test_path;});
                        const bool driver_valid=driver!=request.project_files.end()&&
                            curriculum::integer_battery_driver_valid(driver->second);
                        const bool prerequisite=!spec.prerequisite||
                            !request.integer_battery_prerequisite.empty();
                        fresh.integer_battery_probe_correct=verdict.accepted&&prerequisite&&driver_valid;
                        fresh.integer_battery_probe_feedback=!driver_valid
                            ?"Conserva un comprobador sin macros ni nombres reservados de la evaluación."
                            :prerequisite?verdict.reason:
                                "Ejecuta primero el cliente publicado y después vuelve a ejecutar el comprobador.";
                    }
                }else{
                    fresh.integer_battery_probe_feedback=
                        "La ejecución publicada no tiene la misma salida y terminación normal que el proyecto capturado.";
                }
            }
        }
    }
    if(request.cmake_graph_check) {
        const auto& required=*request.cmake_graph_check;
        fresh.cmake_graph_request_fingerprint=cmake_graph_fingerprint(request);
        fresh.cmake_graph_correct=evaluate_cmake_graph(
            request,result,required).correct;
        if(fresh.cmake_graph_correct&&required.option_selection)
            fresh.cmake_graph_correct=verified_default_option_probe(
                request,result,required);
    }
    if(request.adjustment_project_probe) {
        const auto& spec=*request.adjustment_project_probe;
        const auto frame=adjustment_project_fingerprint(request);
        fresh.adjustment_project_request_fingerprint=frame;
        fresh.adjustment_project_probe_feedback=
            "Falta verificar el componente, el cliente y la batería de esta revisión.";
        if(!adjustment_project_sources_use_interface(request))
            fresh.adjustment_project_probe_feedback=
                "Incluye la cabecera pública en la biblioteca, el cliente y el comprobador.";
        if(adjustment_project_context(request)&&fresh.compilation_succeeded&&
           fresh.execution_completed&&fresh.execution_succeeded&&fresh.cmake_graph_correct&&
           result.target_type=="EXECUTABLE"&&result.adjustment_project_evidence&&
           result.adjustment_project_evidence->original_program_fingerprint==frame) {
            const auto& runs=result.adjustment_project_evidence->runs;
            const auto published=std::ranges::find_if(runs,[](const auto& run) {
                return run.phase.part==curriculum::AdjustmentProjectPart::battery&&
                    run.phase.mutation==curriculum::OpenBatteryMutation::baseline&&
                    !run.phase.assertions_disabled;
            });
            const bool same_published=published!=runs.end()&&
                published->program_frame==frame&&published->compiled&&published->executed&&
                published->exit_code==fresh.exit_code&&published->standard_input.empty()&&
                published->stdout_bytes==result.execution.stdout_bytes&&
                published->stderr_bytes==result.execution.stderr_bytes;
            if(same_published) {
                const auto verdict=curriculum::adjustment_project_verify(spec,runs,frame);
                fresh.adjustment_project_probe_correct=verdict.accepted;
                fresh.adjustment_project_probe_feedback=verdict.reason;
            }else{
                fresh.adjustment_project_probe_feedback=
                    "La batería capturada no reproduce la salida y terminación del proyecto ejecutado.";
            }
        }
    }
    if(request.assertion_mode && request.backend==CppRunRequest::Backend::wasi &&
        result.compilation.succeeded()&&
        result.target_type=="WASI_COMMAND"&&
        result.artifact.extension()==L".wasm"&&
        std::filesystem::is_regular_file(result.artifact)) {
        if(*request.assertion_mode==curriculum::AssertionMode::enabled_abort) {
            fresh.assertion_abort_verified =
                result.execution.status==CppPhaseStatus::completed&&
                result.execution.exit_code&&*result.execution.exit_code>0&&
                *result.execution.exit_code<=255&&
                curriculum::assertion_runtime_trap(result.execution.stdout_bytes,
                    result.execution.stderr_bytes);
        }else if(*request.assertion_mode==curriculum::AssertionMode::repaired&&
                 result.assertion_probes.size()==curriculum::repaired_assertion_inputs.size()) {
            fresh.assertion_repair_probes_correct=true;
            const auto digest=state::sha256(request.source_utf8);
            for(std::size_t i=0;i<result.assertion_probes.size();++i) {
                const auto& probe=result.assertion_probes[i];
                if(probe.source_sha256!=digest||
                   probe.standard_input!=curriculum::repaired_assertion_inputs[i]||
                   !probe.compilation.succeeded()||
                   probe.execution.status!=CppPhaseStatus::completed||
                   !probe.execution.exit_code||*probe.execution.exit_code>255||
                   !curriculum::assertion_repaired_matches(probe.standard_input,
                       probe.execution.stdout_bytes,probe.execution.stderr_bytes,
                       static_cast<int>(*probe.execution.exit_code))) {
                    fresh.assertion_repair_probes_correct=false;
                    break;
                }
            }
        }
    }
    if(request.observation_probe &&
        result.observation_probes.size()==curriculum::observation_probe_inputs.size()) {
        const auto source_digest=state::sha256(request.source_utf8);
        fresh.report_probe_correct=true;
        for(std::size_t index=0;index<result.observation_probes.size();++index) {
            const auto& evidence=result.observation_probes[index];
            const bool valid=evidence.source_sha256==source_digest&&
                evidence.standard_input==curriculum::observation_probe_inputs[index]&&
                evidence.compilation.succeeded()&&
                evidence.execution.status==CppPhaseStatus::completed&&
                evidence.execution.exit_code&&*evidence.execution.exit_code<=255&&
                curriculum::matches_observation_report(
                    *request.observation_probe,evidence.standard_input,
                    evidence.execution.stdout_bytes,evidence.execution.stderr_bytes,
                    static_cast<int>(*evidence.execution.exit_code));
            if(!valid){fresh.report_probe_correct=false;break;}
        }
    }
    if(request.open_battery_probe&&request.stdin_utf8.empty()&&
       result.open_battery_evidence) {
        const auto& evidence=*result.open_battery_evidence;
        const auto frame=fingerprint(request);
        fresh.open_battery_probe_correct=
            evidence.original_project_fingerprint==frame&&
            curriculum::open_battery_probe_spec_valid(*request.open_battery_probe)&&
            curriculum::open_battery_verify(
                request.open_battery_probe->original_cases,
                request.open_battery_probe->original_case_numbers,evidence.runs,frame).accepted;
    }
    if(request.defaulted_move_policy_probe) {
        const auto frame=defaulted_move_policy_fingerprint(request);
        fresh.defaulted_move_policy_request_fingerprint=frame;
        if(!frame.empty()&&result.defaulted_move_policy_evidence) {
            const auto& proof=*result.defaulted_move_policy_evidence;
            const std::array modes{
                curriculum::CppDefaultedMoveProbe::success,
                curriculum::CppDefaultedMoveProbe::rejected_copy_constructor,
                curriculum::CppDefaultedMoveProbe::rejected_copy_assignment};
            bool bound=proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame;
            for(std::size_t i=0;bound&&i<modes.size();++i) {
                const auto driver=curriculum::cpp_defaulted_move_policy_probe_source(
                    *request.defaulted_move_policy_probe,request.source_utf8,modes[i]);
                bound=driver&&proof.phases[i].driver_sha256==state::sha256(*driver);
            }
            if(bound) {
                const auto& positive=proof.phases[0];
                bool accepted=positive.compilation.succeeded()&&
                    positive.execution.succeeded()&&
                    positive.execution.stdout_bytes.empty()&&
                    positive.execution.stderr_bytes==
                        curriculum::cpp_defaulted_move_completion_marker;
                for(std::size_t i=1;accepted&&i<proof.phases.size();++i) {
                    const auto& negative=proof.phases[i];
                    const auto& diagnostic=negative.compilation.stdout_bytes;
                    const auto& diagnostic_error=negative.compilation.stderr_bytes;
                    const bool deleted=diagnostic.find("C2280")!=std::string::npos||
                        diagnostic_error.find("C2280")!=std::string::npos||
                        diagnostic.find("C2248")!=std::string::npos||
                        diagnostic_error.find("C2248")!=std::string::npos;
                    accepted=negative.compilation.status==CppPhaseStatus::completed&&
                        negative.compilation.exit_code&&
                        *negative.compilation.exit_code!=0&&deleted&&
                        negative.execution.status==CppPhaseStatus::not_started;
                }
                fresh.defaulted_move_policy_probe_correct=accepted;
            }
        }
    }
    if(request.independent_copy_assignment_probe) {
        const auto frame=independent_copy_assignment_fingerprint(request);
        fresh.independent_copy_assignment_request_fingerprint=frame;
        const auto driver=curriculum::cpp_independent_copy_probe_source(
            *request.independent_copy_assignment_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.independent_copy_assignment_evidence) {
            const auto& proof=*result.independent_copy_assignment_evidence;
            fresh.independent_copy_assignment_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_independent_copy_completion_marker;
        }
    }
    if(request.move_assignment_invariant_probe) {
        const auto frame=move_assignment_invariant_fingerprint(request);
        fresh.move_assignment_invariant_request_fingerprint=frame;
        const auto driver=curriculum::cpp_move_invariant_private_driver(
            *request.move_assignment_invariant_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.move_assignment_invariant_evidence) {
            const auto& proof=*result.move_assignment_invariant_evidence;
            fresh.move_assignment_invariant_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_move_invariant_completion_marker;
        }
    }
    if(request.array_owner_read_probe) {
        const auto frame=array_owner_read_fingerprint(request);
        fresh.array_owner_read_request_fingerprint=frame;
        const auto driver=curriculum::cpp_array_owner_read_private_driver(
            *request.array_owner_read_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.array_owner_read_evidence) {
            const auto& evidence=*result.array_owner_read_evidence;
            fresh.array_owner_read_probe_correct=
                evidence.driver_sha256==state::sha256(*driver)&&
                evidence.program_sha256==state::sha256(request.source_utf8)&&
                evidence.request_fingerprint==frame&&
                evidence.compilation.succeeded()&&evidence.execution.succeeded()&&
                evidence.execution.stdout_bytes.empty()&&
                evidence.execution.stderr_bytes==
                    curriculum::cpp_array_owner_read_completion_marker;
        }
    }
    if(request.array_copy_move_probe) {
        const auto frame=array_copy_move_fingerprint(request);
        fresh.array_copy_move_request_fingerprint=frame;
        const auto driver=curriculum::cpp_array_copy_move_private_driver(
            *request.array_copy_move_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.array_copy_move_evidence) {
            const auto& evidence=*result.array_copy_move_evidence;
            fresh.array_copy_move_probe_correct=
                evidence.driver_sha256==state::sha256(*driver)&&
                evidence.program_sha256==state::sha256(request.source_utf8)&&
                evidence.request_fingerprint==frame&&
                evidence.compilation.succeeded()&&evidence.execution.succeeded()&&
                evidence.execution.stdout_bytes.empty()&&
                evidence.execution.stderr_bytes==
                    curriculum::cpp_array_copy_move_completion_marker;
        }
    }
    if(request.abstract_interface_probe) {
        const auto frame=abstract_interface_fingerprint(request);
        fresh.abstract_interface_request_fingerprint=frame;
        const auto driver=curriculum::cpp_abstract_interface_private_driver(
            *request.abstract_interface_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.abstract_interface_evidence) {
            const auto& proof=*result.abstract_interface_evidence;
            fresh.abstract_interface_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_abstract_interface_completion_marker;
        }
    }
    if(request.interface_collection_probe) {
        const auto frame=interface_collection_fingerprint(request);
        fresh.interface_collection_request_fingerprint=frame;
        const auto driver=curriculum::cpp_interface_collection_private_driver(
            *request.interface_collection_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.interface_collection_evidence) {
            const auto& proof=*result.interface_collection_evidence;
            fresh.interface_collection_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_interface_collection_completion_marker;
        }
    }
    if(request.polymorphic_clone_probe) {
        const auto frame=polymorphic_clone_fingerprint(request);
        fresh.polymorphic_clone_request_fingerprint=frame;
        const auto driver=curriculum::cpp_polymorphic_clone_private_driver(
            *request.polymorphic_clone_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.polymorphic_clone_evidence) {
            const auto& proof=*result.polymorphic_clone_evidence;
            fresh.polymorphic_clone_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_polymorphic_clone_completion_marker;
        }
    }
    if(request.dual_interface_probe) {
        const auto frame=dual_interface_fingerprint(request);
        fresh.dual_interface_request_fingerprint=frame;
        const auto driver=curriculum::cpp_dual_interface_private_driver(
            *request.dual_interface_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.dual_interface_evidence) {
            const auto& proof=*result.dual_interface_evidence;
            fresh.dual_interface_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_dual_interface_completion_marker;
        }
    }
    if(request.derived_preparation_probe) {
        const auto frame=derived_preparation_fingerprint(request);
        fresh.derived_preparation_request_fingerprint=frame;
        const auto driver=curriculum::cpp_derived_preparation_private_driver(
            *request.derived_preparation_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.derived_preparation_evidence) {
            const auto& proof=*result.derived_preparation_evidence;
            fresh.derived_preparation_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_derived_preparation_completion_marker;
        }
    }
    if(request.virtual_lifetime_probe) {
        const auto frame=virtual_lifetime_fingerprint(request);
        fresh.virtual_lifetime_request_fingerprint=frame;
        const auto driver=curriculum::cpp_virtual_lifetime_dispatch_private_driver(
            *request.virtual_lifetime_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.virtual_lifetime_evidence) {
            const auto& proof=*result.virtual_lifetime_evidence;
            fresh.virtual_lifetime_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_virtual_lifetime_dispatch_marker;
        }
    }
    if(request.polymorphic_destruction_probe) {
        const auto frame=polymorphic_destruction_fingerprint(request);
        fresh.polymorphic_destruction_request_fingerprint=frame;
        const auto driver=curriculum::cpp_polymorphic_destruction_private_driver(
            *request.polymorphic_destruction_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.polymorphic_destruction_evidence) {
            const auto& proof=*result.polymorphic_destruction_evidence;
            fresh.polymorphic_destruction_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_polymorphic_destruction_completion_marker;
        }
    }
    if(request.array_return_take_probe) {
        const auto frame=array_return_take_fingerprint(request);
        fresh.array_return_take_request_fingerprint=frame;
        const auto driver=curriculum::cpp_array_return_take_private_driver(
            *request.array_return_take_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.array_return_take_evidence) {
            const auto& proof=*result.array_return_take_evidence;
            fresh.array_return_take_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_array_return_take_completion_marker;
        }
    }
    if(request.indexed_unique_transfer_probe) {
        const auto frame=indexed_unique_transfer_fingerprint(request);
        fresh.indexed_unique_transfer_request_fingerprint=frame;
        const auto driver=curriculum::cpp_indexed_unique_transfer_probe_source(
            *request.indexed_unique_transfer_probe,request.source_utf8);
        if(!frame.empty()&&driver&&result.indexed_unique_transfer_evidence) {
            const auto& proof=*result.indexed_unique_transfer_evidence;
            fresh.indexed_unique_transfer_probe_correct=
                proof.driver_sha256==state::sha256(*driver)&&
                proof.program_sha256==state::sha256(request.source_utf8)&&
                proof.request_fingerprint==frame&&
                proof.compilation.succeeded()&&proof.execution.succeeded()&&
                proof.execution.stdout_bytes.empty()&&
                proof.execution.stderr_bytes==
                    curriculum::cpp_indexed_unique_transfer_completion_marker;
        }
    }
    if(request.named_rule_probe&&result.named_rule_probe) {
        const auto independent=request.cmake?
            curriculum::named_rule_probe_source(*request.named_rule_probe):
            request.project_files.empty()?
                curriculum::named_rule_single_file_probe_source(
                    *request.named_rule_probe,request.source_utf8):
                std::optional<std::string>{};
        if(independent) {
            const auto& probe=*result.named_rule_probe;
            const auto bound_source=request.cmake?
                state::sha256(curriculum::cpp_project_frame(request.project_files,
                    request.source_files,request.cmake)):
                state::sha256(request.source_utf8);
            fresh.named_rule_probe_correct=
                probe.source_sha256==state::sha256(*independent)&&
                probe.project_sha256==bound_source&&
                // A student function may exit(0) before all comparisons
                // run. Only the generated main emits this final witness.
                named_rule_probe_completed(probe);
        }
    }
    if(named_rule_constant_variations_requested(request)) {
        fresh.named_rule_constant_variation_request_fingerprint=
            named_rule_constant_variation_fingerprint(request);
        const auto& spec=*request.named_rule_probe->constant_variations;
        const bool baseline_correct=fresh.named_rule_probe_correct;
        bool complete=named_rule_constant_variation_context(request);
        std::size_t failed_index=0;
        bool observed_failure=false;
        for(std::size_t index=0;complete&&index<spec.variations.size();++index) {
            failed_index=index;
            observed_failure=false;
            if(index>=result.named_rule_constant_variation_probes.size()) {
                complete=false;
                break;
            }
            const auto expected=named_rule_constant_variation_request(request,index);
            const auto& observed=result.named_rule_constant_variation_probes[index];
            const bool bound=expected&&observed.index==index&&
                observed.proof.source_sha256==expected->driver_sha256&&
                observed.proof.project_sha256==expected->program_sha256;
            complete=bound&&named_rule_probe_completed(observed.proof);
            observed_failure=bound&&observed.proof.compilation.succeeded()&&
                observed.proof.execution.status==CppPhaseStatus::completed;
        }
        if(result.named_rule_constant_variation_probes.size()>spec.variations.size()) {
            complete=false;
            observed_failure=false;
        }
        if(baseline_correct&&!complete&&failed_index<spec.variations.size()) {
            const auto& function=spec.variations[failed_index].function;
            fresh.named_rule_constant_variation_feedback=observed_failure?
                "La constante de "+function+" no acreditó la relación pedida al cambiar su valor. "
                "Deriva las cantidades relacionadas de ese acuerdo y conserva los otros acuerdos.":
                "No se obtuvo una comprobación completa de la constante de "+function+
                " al cambiar su valor. Revisa el código y vuelve a compilar.";
        }
        fresh.named_rule_probe_correct=fresh.named_rule_probe_correct&&complete;
    }
    if(named_rule_battery_requested(request)) {
        const auto frame=named_rule_battery_fingerprint(request);
        fresh.named_rule_battery_request_fingerprint=frame;
        fresh.named_rule_battery_feedback=
            "No se obtuvo una comprobación completa de la batería de esta revisión.";
        if(named_rule_battery_context(request)&&result.target_type=="EXECUTABLE"&&
           result.named_rule_battery_evidence&&
           result.named_rule_battery_evidence->original_program_fingerprint==frame) {
            const auto& runs=result.named_rule_battery_evidence->runs;
            if(!runs.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
               runs.front().exit_code==*result.execution.exit_code&&
               runs.front().stdout_bytes==result.execution.stdout_bytes&&
               runs.front().stderr_bytes==result.execution.stderr_bytes) {
                const auto verdict=curriculum::named_rule_battery_verify(
                    *request.named_rule_probe->battery_check,runs,frame);
                fresh.named_rule_battery_correct=verdict.accepted;
                fresh.named_rule_battery_feedback=verdict.reason;
            }else{
                fresh.named_rule_battery_feedback=
                    "La batería observada no reproduce la salida y terminación del programa ejecutado.";
            }
        }
    }
    if(request.rejection_probe&&result.rejection_probe&&
        result.rejection_probe->compilation.succeeded()&&result.rejection_probe->execution.succeeded()) {
        const auto source=curriculum::rejection_stream_probe_source(
            *request.rejection_probe,request.source_utf8);
        if(result.rejection_probe->source_sha256==state::sha256(source))
            fresh.rejection_probe_correct=curriculum::evaluate_rejection_stream_probe(
                result.rejection_probe->execution.stdout_bytes,
                result.rejection_probe->execution.stderr_bytes,*request.rejection_probe).correct;
    }
    return fresh;
}

curriculum::OutputEvaluation evaluate_cpp_output_check(const curriculum::OutputCheck& check,
    const curriculum::FreshExecution& execution,std::uint64_t current_revision) {
    const auto exact = curriculum::evaluate_output_check(check,execution,current_revision);
    if (exact.verdict != curriculum::OutputVerdict::incorrect) return exact;
    auto native_check = check;
    bool changed = false;
    const auto text_mode = [&](std::string& output) {
        // An authored CR is an explicit byte objective, never a line-ending
        // convention. Only LF-only goals get the additional CRT spelling.
        if (output.find('\r') != output.npos || output.find('\n') == output.npos) return;
        std::string native; native.reserve(output.size());
        for (const auto character : output) { if (character == '\n') native += '\r'; native += character; }
        output = std::move(native); changed = true;
    };
    for (auto& item : native_check.cases) {
        for (auto& output : item.outputs) text_mode(output);
        for (auto& feedback : item.review_feedback) for (auto& output : feedback.outputs) text_mode(output);
    }
    if (!changed) return exact;
    const auto native = curriculum::evaluate_output_check(native_check,execution,current_revision);
    // A native CRLF spelling may satisfy stdout while the additional stderr
    // objective is still pending. Preserve that partial result and its guide.
    return native.verdict == curriculum::OutputVerdict::correct ||
        (native.stdout_correct && !exact.stdout_correct) || exact.feedback.empty() ? native : exact;
}

CppProjectAvailability inspect_cpp_project(const CppRunRequest& request){
    ensure_winrt_apartment();if(!request.cmake)return {};validate_request_project(request);const auto workspace=cmake_workspace(request);const auto mutex=project_mutex(workspace);std::unique_lock<std::timed_mutex> lock(*mutex,std::try_to_lock);
    if(!lock.owns_lock()){CppProjectAvailability busy;busy.workspace=workspace;busy.project_fingerprint=fingerprint(request);busy.configuration_fingerprint=fingerprint(request,true);return busy;}return inspect_unlocked(request);
}
// The independently generated harness is compiled in a fresh owned CMake
// workspace against the student's unchanged library sources. Its executable
// never reuses the student-editable test driver or changes the submitted
// project. The worker owns this work; the window thread never runs it.
// Build the exact submitted source under a private OFF configuration.
// This request has no option-selection recursion and cannot reuse the ON cache.
void complete_cmake_option_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.cmake_graph_check||
       !request.cmake_graph_check->option_selection||
       (request.action!=CppRunRequest::Action::compile_run&&
        request.action!=CppRunRequest::Action::execute)||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto& authored=*request.cmake_graph_check;
    const auto off=default_option_request(request,*authored.option_selection,
        authored);
    const auto observed=run_cpp(off,stop);
    result.cmake_option_probe=CppCmakeOptionProbe{
        observed.request_id,observed.revision,observed.project_fingerprint,
        observed.configuration_fingerprint,observed.target_type,
        observed.error_utf8,observed.executable,observed.compilation,
        observed.execution,observed.configuration,observed.cmake_graph};
}
// The student's unmodified submitted program runs first. Only afterward may
// a second private native program observe the real identity and cleanup.
// This never edits an existing draft, curricular asset or saved achievement.
// Independent compiled semantics: one successful generated-move witness and
// two genuinely compiler-rejected copy operations. No undefined-behavior
// execution is used for negative cases and no extra learner input is requested.
void complete_defaulted_move_policy_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.defaulted_move_policy_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=defaulted_move_policy_fingerprint(request);
    if(frame.empty())return;
    const std::array modes{
        curriculum::CppDefaultedMoveProbe::success,
        curriculum::CppDefaultedMoveProbe::rejected_copy_constructor,
        curriculum::CppDefaultedMoveProbe::rejected_copy_assignment};
    CppDefaultedMovePolicyEvidence observed;
    observed.program_sha256=state::sha256(request.source_utf8);
    observed.request_fingerprint=frame;
    for(std::size_t i=0;i<modes.size();++i) {
        if(stop.stop_requested())return;
        const auto driver=curriculum::cpp_defaulted_move_policy_probe_source(
            *request.defaulted_move_policy_probe,request.source_utf8,modes[i]);
        if(!driver)return;
        auto private_request=request;
        private_request.defaulted_move_policy_probe.reset(); // never recurse
        private_request.request_id=(~request.request_id)^(static_cast<std::uint64_t>(i)+1);
        private_request.project_key+="/independent-defaulted-move-v1/"+std::to_string(i);
        private_request.source_utf8=*driver;
        private_request.stdin_utf8.clear();
        const auto proof=run_cpp(private_request,stop);
        if(!proof.error_utf8.empty()||stop.stop_requested())return;
        observed.phases[i]=CppDefaultedMovePhaseEvidence{
            state::sha256(*driver),proof.compilation,proof.execution};
    }
    result.defaulted_move_policy_evidence=std::move(observed);
}

void complete_independent_copy_assignment_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.independent_copy_assignment_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=independent_copy_assignment_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_independent_copy_probe_source(
        *request.independent_copy_assignment_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.independent_copy_assignment_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-copy-assignment-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.independent_copy_assignment_evidence=CppIndependentCopyAssignmentEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_move_assignment_invariant_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.move_assignment_invariant_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=move_assignment_invariant_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_move_invariant_private_driver(
        *request.move_assignment_invariant_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.move_assignment_invariant_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-move-invariants-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.move_assignment_invariant_evidence=CppMoveAssignmentInvariantEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_array_owner_read_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.array_owner_read_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=array_owner_read_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_array_owner_read_private_driver(
        *request.array_owner_read_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.array_owner_read_probe.reset(); // Avoid another private run.
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-array-owner-read-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.array_owner_read_evidence=CppArrayOwnerReadEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_array_copy_move_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.array_copy_move_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=array_copy_move_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_array_copy_move_private_driver(
        *request.array_copy_move_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.array_copy_move_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-array-copy-move-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.array_copy_move_evidence=CppArrayCopyMoveEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_array_return_take_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.array_return_take_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=array_return_take_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_array_return_take_private_driver(
        *request.array_return_take_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.array_return_take_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-array-return-take-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.array_return_take_evidence=CppArrayReturnTakeEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_polymorphic_destruction_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.polymorphic_destruction_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=polymorphic_destruction_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_polymorphic_destruction_private_driver(
        *request.polymorphic_destruction_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.polymorphic_destruction_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-polymorphic-destruction-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.polymorphic_destruction_evidence=CppPolymorphicDestructionEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_virtual_lifetime_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.virtual_lifetime_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=virtual_lifetime_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_virtual_lifetime_dispatch_private_driver(
        *request.virtual_lifetime_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.virtual_lifetime_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-virtual-lifetime-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.virtual_lifetime_evidence=CppVirtualLifetimeEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_derived_preparation_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.derived_preparation_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=derived_preparation_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_derived_preparation_private_driver(
        *request.derived_preparation_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.derived_preparation_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-derived-preparation-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.derived_preparation_evidence=CppDerivedPreparationEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_abstract_interface_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.abstract_interface_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=abstract_interface_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_abstract_interface_private_driver(
        *request.abstract_interface_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.abstract_interface_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-abstract-interface-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.abstract_interface_evidence=CppAbstractInterfaceEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_interface_collection_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.interface_collection_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=interface_collection_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_interface_collection_private_driver(
        *request.interface_collection_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.interface_collection_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-interface-collection-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.interface_collection_evidence=CppInterfaceCollectionEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_polymorphic_clone_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.polymorphic_clone_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=polymorphic_clone_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_polymorphic_clone_private_driver(
        *request.polymorphic_clone_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.polymorphic_clone_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-polymorphic-clone-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.polymorphic_clone_evidence=CppPolymorphicCloneEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_dual_interface_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.dual_interface_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=dual_interface_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_dual_interface_private_driver(
        *request.dual_interface_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.dual_interface_probe.reset();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-dual-interface-v1";
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.dual_interface_evidence=CppDualInterfaceEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_indexed_unique_transfer_probe(const CppRunRequest& request,
        CppRunResult& result,std::stop_token stop) {
    if(!request.indexed_unique_transfer_probe||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=indexed_unique_transfer_fingerprint(request);
    if(frame.empty())return;
    const auto driver=curriculum::cpp_indexed_unique_transfer_probe_source(
        *request.indexed_unique_transfer_probe,request.source_utf8);
    if(!driver)return;
    auto private_request=request;
    private_request.indexed_unique_transfer_probe.reset(); // no recursion
    private_request.source_utf8=*driver;
    private_request.stdin_utf8.clear();
    private_request.request_id=~request.request_id;
    private_request.project_key+="/independent-transfer-v1";
    const auto proof=run_cpp(private_request,stop);
    if(!proof.error_utf8.empty()||stop.stop_requested())return;
    result.indexed_unique_transfer_evidence=CppIndexedUniqueTransferEvidence{
        state::sha256(*driver),state::sha256(request.source_utf8),frame,
        proof.compilation,proof.execution};
}

void complete_named_rule_probe(const CppRunRequest& request,CppRunResult& result,
        std::stop_token stop) {
    if(!request.named_rule_probe||
       request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const bool project=static_cast<bool>(request.cmake);
    if(project?request.project_files.empty():
        (!request.project_files.empty()||!request.source_files.empty()))return;
    const auto source=project?
        curriculum::named_rule_probe_source(*request.named_rule_probe):
        curriculum::named_rule_single_file_probe_source(
            *request.named_rule_probe,request.source_utf8);
    if(!source)return;
    auto second=request;
    second.named_rule_probe.reset(); // recursion and double credit forbidden
    second.capture_cmake_graph=false; // private probe must not grade CMake structure
    second.cmake_graph_check.reset();
    second.rejection_probe.reset();
    second.observation_probe.reset();
    second.assertion_mode.reset();
    second.request_id=~request.request_id;
    second.project_key+="/named-rule-independent-v1";
    if(project) {
        const auto entry=std::ranges::find_if(second.project_files,
            [&](const auto& file) {
                return file.first==request.named_rule_probe->harness_path;
            });
        if(entry==second.project_files.end())return;
        entry->second=*source;
    }else{
        // Preserve the submitted functions, but run them with an independent
        // main instead of trusting editable student reports and expectations.
        second.source_utf8=*source;
        second.stdin_utf8.clear();
    }
    const auto proof=run_cpp(second,stop);
    if(!proof.error_utf8.empty())return;
    result.named_rule_probe=CppNamedRuleProbe{
        state::sha256(*source),
        project?state::sha256(curriculum::cpp_project_frame(
            request.project_files,request.source_files,request.cmake)):
            state::sha256(request.source_utf8),
        proof.compilation,proof.execution};
    if(!named_rule_constant_variations_requested(request)||
       !named_rule_probe_completed(*result.named_rule_probe))return;
    const auto& spec=*request.named_rule_probe->constant_variations;
    for(std::size_t index=0;index<spec.variations.size();++index) {
        if(stop.stop_requested())return;
        const auto phase=named_rule_constant_variation_request(request,index);
        if(!phase)return;
        const auto observed=run_cpp(phase->request,stop);
        if(!observed.error_utf8.empty())return;
        result.named_rule_constant_variation_probes.push_back({index,{
            phase->driver_sha256,phase->program_sha256,
            observed.compilation,observed.execution}});
        if(!named_rule_probe_completed(result.named_rule_constant_variation_probes.back().proof))
            return;
    }
}

// This proof runs only from the already isolated C++ project worker.
// A distinct SHA-owned workspace is used for each compiled mutation; no
// student draft, historical answer or profile state is rewritten here.
void complete_open_battery_probe(const CppRunRequest& request,CppRunResult& result,
        std::stop_token stop) {
    if(!request.open_battery_probe||!request.cmake||
       request.backend!=CppRunRequest::Backend::native_msvc||
       (request.action!=CppRunRequest::Action::compile_run&&
        request.action!=CppRunRequest::Action::execute)||
       !request.stdin_utf8.empty()||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto& spec=*request.open_battery_probe;
    if(!curriculum::open_battery_probe_spec_valid(spec)||
       request.cmake->target!=spec.test_target)return;
    const auto lib=std::ranges::find(request.project_files,spec.library_path,
        &curriculum::CppProjectFiles::value_type::first);
    const auto test=std::ranges::find(request.project_files,spec.test_path,
        &curriculum::CppProjectFiles::value_type::first);
    if(lib==request.project_files.end()||test==request.project_files.end())return;
    const auto comparison=curriculum::open_battery_instrumented_comparison(
        test->second,spec.comparison_name);
    if(!comparison)return;
    CppOpenBatteryEvidence evidence;
    evidence.original_project_fingerprint=fingerprint(request);
    const auto run=[&](curriculum::OpenBatteryMutation mutation,
            std::optional<curriculum::OpenBatteryCall> target,
            std::size_t index)->bool {
        if(stop.stop_requested())return false;
        const auto source=curriculum::open_battery_mutated_library(
            lib->second,spec.function_name,mutation,target);
        if(!source)return false;
        auto private_request=request;
        // Even a public execute needs a fresh build of each private mutation.
        private_request.action=CppRunRequest::Action::compile_run;
        private_request.open_battery_probe.reset(); // no nested probes
        private_request.named_rule_probe.reset();
        private_request.rejection_probe.reset();
        private_request.observation_probe.reset();
        private_request.assertion_mode.reset();
        private_request.cmake_graph_check.reset();
        private_request.capture_cmake_graph=false;
        private_request.stdin_utf8.clear();
        private_request.project_key+="/open-battery-private-v4/"+std::to_string(index);
        private_request.request_id=~(request.request_id+index);
        for(auto& item:private_request.project_files) {
            if(item.first==spec.library_path)item.second=*source;
            else if(item.first==spec.test_path)item.second=*comparison;
        }
        const auto observed=run_cpp(private_request,stop);
        if(!observed.error_utf8.empty()||stop.stop_requested())return false;
        auto trace=curriculum::open_battery_parse_trace(
            observed.execution.stderr_bytes);
        if(!trace)return false;
        curriculum::OpenBatteryRun phase;
        phase.program_frame=evidence.original_project_fingerprint;
        phase.mutation=mutation;
        phase.target=target;
        phase.compiled=observed.compilation.succeeded()&&
            (observed.configuration.status==CppPhaseStatus::not_started||
             observed.configuration.succeeded());
        phase.executed=observed.execution.status==CppPhaseStatus::completed&&
            observed.execution.exit_code.has_value()&&
            *observed.execution.exit_code<=255;
        if(phase.executed)
            phase.exit_code=static_cast<int>(*observed.execution.exit_code);
        phase.calls=std::move(trace->calls);
        phase.case_numbers=std::move(trace->case_numbers);
        phase.stdout_bytes=observed.execution.stdout_bytes;
        phase.stderr_bytes=std::move(trace->visible);
        evidence.runs.push_back(std::move(phase));
        return evidence.runs.back().compiled&&evidence.runs.back().executed;
    };
    if(!run(curriculum::OpenBatteryMutation::baseline,{},0))return;
    const auto baseline=curriculum::open_battery_verify_baseline(
        spec.original_cases,spec.original_case_numbers,evidence.runs.front(),
        evidence.original_project_fingerprint);
    if(!baseline.accepted||!baseline.added)return;
    const auto plan=curriculum::open_battery_mutation_plan(
        spec.original_cases,*baseline.added);
    if(!plan)return;
    // The clean baseline above is phase zero. Preserve separate builds
    // and executions for both observations of every historical case.
    for(std::size_t i=1;i<plan->size();++i)
        if(!run((*plan)[i].mutation,(*plan)[i].target,i))return;
    result.open_battery_evidence=std::move(evidence);
}

// A second, isolated compile/execute exercise proves that the same student
// diagnostic function writes to the caller-supplied ostream, not hard-coded
// std::cerr. Never convert an incomplete probe into an execution achievement.
void complete_rejection_stream_probe(const CppRunRequest& request,CppRunResult& result,std::stop_token stop) {
    if(!request.rejection_probe||request.action!=CppRunRequest::Action::compile_run||
        !request.project_files.empty()||request.cmake||request.diagnostic||
        !result.compilation.succeeded()||result.execution.status!=CppPhaseStatus::completed||
        !result.execution.exit_code||*result.execution.exit_code>255||stop.stop_requested())return;
    auto second=request;
    second.rejection_probe.reset();
    second.request_id=~request.request_id;
    second.source_utf8=curriculum::rejection_stream_probe_source(
        *request.rejection_probe,request.source_utf8);
    second.stdin_utf8.clear();
    const auto proof=run_cpp(second,stop);
    if(!proof.error_utf8.empty())return;
    result.rejection_probe=CppRejectionProbe{
        state::sha256(second.source_utf8),proof.compilation,proof.execution};
}

// Independent normal executions of the *same* source with unseen observations
// catch printed constants, merged duplicate labels, skipped valid rows and
// incorrect counters. Each result is sealed to the original source and input.
// The isolated requests disable both probes to prevent recursion.
void complete_observation_probes(const CppRunRequest& request,CppRunResult& result,std::stop_token stop) {
    if(!request.observation_probe||request.rejection_probe||
        request.action!=CppRunRequest::Action::compile_run||request.cmake||
        !request.project_files.empty()||request.diagnostic||
        !result.compilation.succeeded()||
        result.execution.status!=CppPhaseStatus::completed||
        !result.execution.exit_code||*result.execution.exit_code>255||
        stop.stop_requested())return;
    const auto hash=state::sha256(request.source_utf8);
    for(std::size_t i=0;i<curriculum::observation_probe_inputs.size();++i) {
        if(stop.stop_requested())break;
        auto second=request;
        second.observation_probe.reset();
        second.rejection_probe.reset();
        second.request_id=~(request.request_id+static_cast<std::uint64_t>(i)+1);
        second.stdin_utf8=std::string(curriculum::observation_probe_inputs[i]);
        const auto proof=run_cpp(second,stop);
        if(!proof.error_utf8.empty())break;
        result.observation_probes.push_back(CppObservationProbe{
            hash,second.stdin_utf8,proof.compilation,proof.execution});
        if(!proof.compilation.succeeded()||proof.execution.status!=CppPhaseStatus::completed)break;
    }
}

// A repaired formula must also work for new data without disabling assert
// or fabricating the prepared stdout. These disposable WASI executions retain
// the original source and are sealed to the captured revision and input.
void complete_assertion_probes(const CppRunRequest& request,CppRunResult& result,
        std::stop_token stop) {
    if(request.assertion_mode!=curriculum::AssertionMode::repaired||
       request.backend!=CppRunRequest::Backend::wasi||
       request.action!=CppRunRequest::Action::compile_run||
       !request.project_files.empty()||request.cmake||request.diagnostic||
       !result.compilation.succeeded()||
       result.execution.status!=CppPhaseStatus::completed||
       !result.execution.exit_code||*result.execution.exit_code>255||
       stop.stop_requested())return;
    const auto hash=state::sha256(request.source_utf8);
    for(std::size_t i=0;i<curriculum::repaired_assertion_inputs.size();++i) {
        if(stop.stop_requested())break;
        auto current=request;
        current.assertion_mode.reset();
        current.request_id=~(request.request_id+static_cast<std::uint64_t>(i)+7);
        current.stdin_utf8=std::string(curriculum::repaired_assertion_inputs[i]);
        const auto proof=run_cpp(current,stop);
        if(!proof.error_utf8.empty())break;
        result.assertion_probes.push_back(CppAssertionProbe{
            hash,current.stdin_utf8,proof.compilation,proof.execution});
        if(!proof.compilation.succeeded()||
           proof.execution.status!=CppPhaseStatus::completed)break;
    }
}

namespace {
std::vector<std::wstring> single_file_arguments(const std::filesystem::path& directory) {
    const auto executable=directory/L"program.exe";
    const auto source=directory/L"main.cpp";
    return {
        L"/nologo",L"/EHsc",build::cpp_standard_option,L"/Zc:__cplusplus",L"/permissive-",L"/utf-8",L"/W4",
        L"/diagnostics:column",L"/Fe:"+executable.wstring(),
        L"/Fo:"+(directory/L"program.obj").wstring(),source.wstring(),
        L"/link",L"/INCREMENTAL:NO",L"/MACHINE:X64",L"/SUBSYSTEM:CONSOLE",
        L"/MANIFEST:EMBED",L"/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    };
}

// Only the GUID-owned, already drained run directory may be cleared. Check
// every entry before removal; a reparse point is an error, never a reason to
// traverse another directory or touch a persisted learner draft.
void clear_private_probe_directory(const std::filesystem::path& directory,
        bool keep_execution_inputs=false) {
    state::require_safe_path(directory);
    std::vector<std::filesystem::path> entries;
    const auto collect=[&](auto&& self,const std::filesystem::path& parent)->void {
        for(const auto& entry:std::filesystem::directory_iterator(parent)) {
            state::require_safe_path(entry.path());
            if(keep_execution_inputs&&parent==directory) {
                const auto name=entry.path().filename();
                if(name==L"program.exe"||name==L"stdin.bin"||
                   name==L"compiler-stdin.bin"||name==L"toolchain.txt") {
                    if(!entry.is_regular_file())
                        throw std::runtime_error("A private execution input is not a regular file");
                    continue;
                }
            }
            if(entry.is_directory())self(self,entry.path());
            entries.push_back(entry.path());
        }
    };
    collect(collect,directory);
    for(const auto& entry:entries) {
        state::require_safe_path(entry);
        if(!std::filesystem::remove(entry))
            throw std::runtime_error("Cannot reset the private execution workspace");
    }
}

// Each objective supplies its own instrumented sources and typed observations.
// This helper owns only the physical execution: every phase sees the public
// path/argv/input/environment, and the public filesystem is restored byte for byte.
template<class MakeSource,class Observe>
bool execute_private_single_file_phases(const CppRunRequest& request,const CppRunResult& result,
        const std::filesystem::path& compiler,const std::vector<wchar_t>& environment,
        std::string_view initial_toolchain,std::size_t count,
        MakeSource&& make_source,Observe&& observe,std::stop_token stop) {
    const auto& directory=result.run_directory;
    const auto executable=directory/L"program.exe";
    const auto source=directory/L"main.cpp";
    const auto input=directory/L"stdin.bin";
    const auto compiler_input=directory/L"compiler-stdin.bin";
    state::require_safe_path(executable);
    // Retain every public output in memory, never in phase-labelled files
    // that the next execution could inspect. A program-created file belongs
    // to the public attempt too and must be restored after private work.
    // This bound is private-probe capacity, not a grading decision or a limit
    // imposed on ordinary C++ exercises.
    constexpr std::uintmax_t artifact_limit=128ULL*1024*1024;
    std::uintmax_t retained_bytes=0;
    std::vector<std::filesystem::path> original_directories;
    std::vector<std::pair<std::filesystem::path,std::string>> original_files;
    const auto retain=[&](auto&& self,const std::filesystem::path& parent)->bool {
        for(const auto& entry:std::filesystem::directory_iterator(parent)) {
            state::require_safe_path(entry.path());
            const auto relative=entry.path().lexically_relative(directory);
            if(entry.is_directory()) {
                original_directories.push_back(relative);
                if(!self(self,entry.path()))return false;
            }else if(entry.is_regular_file()) {
                const auto size=entry.file_size();
                if(size>artifact_limit-retained_bytes)return false;
                auto bytes=read_exact(entry.path(),static_cast<std::size_t>(size));
                if(bytes.size()!=size)
                    throw std::runtime_error("A public execution output could not be retained");
                retained_bytes+=size;
                original_files.emplace_back(relative,std::move(bytes));
            }else{
                return false;
            }
        }
        return true;
    };
    if(!retain(retain,directory))return false;
    const auto arguments=single_file_arguments(directory);
    const auto restore=[&] {
        clear_private_probe_directory(directory);
        for(const auto& relative:original_directories) {
            const auto restored=directory/relative;
            std::filesystem::create_directories(restored);
            state::require_safe_path(restored);
        }
        for(const auto& [relative,bytes]:original_files) {
            const auto restored=directory/relative;
            state::require_safe_path(restored);
            write_exact(restored,bytes);
        }
    };
    try {
        for(std::size_t index=0;index<count;++index) {
            if(stop.stop_requested())break;
            const auto instrumented=make_source(index);
            if(!instrumented)break;
            clear_private_probe_directory(directory);
            write_exact(source,*instrumented);
            write_exact(input,request.stdin_utf8);
            write_exact(compiler_input,"");
            write_exact(directory/L"toolchain.txt",initial_toolchain);
            const auto compilation=run_phase(compiler,arguments,directory,compiler_input,
                L"compilation",request.compile_timeout,request.output_limit_bytes,true,
                stop,true,&environment);
            CppPhaseResult execution;
            if(compilation.succeeded()&&!stop.stop_requested()) {
                // No source, object or previous phase output is visible.
                // The executable path, argv, cwd, stdin and environment
                // remain identical to the original public execution.
                clear_private_probe_directory(directory,true);
                execution=run_phase(executable,{},directory,input,L"execution",
                    request.execution_timeout,request.output_limit_bytes,false,
                    stop,false,&environment);
            }
            observe(index,compilation,execution);
        }
    }catch(...) {
        restore();
        throw;
    }
    restore();
    return !stop.stop_requested();
}


// CMake private phases keep the same physical workspace, target and process
// inputs. Snapshots live only in memory; no phase-labelled source, object or
// cache remains observable while either the public or private program runs.
struct CmakeProbeFile {
    std::string bytes;
    std::filesystem::file_time_type modified;
};
struct CmakeProbeTree {
    std::filesystem::file_time_type modified;
    std::map<std::filesystem::path,std::filesystem::file_time_type> directories;
    std::map<std::filesystem::path,CmakeProbeFile> files;
};
CmakeProbeTree retain_cmake_probe_tree(const std::filesystem::path& root) {
    state::require_safe_path(root);
    CmakeProbeTree result;
    result.modified=std::filesystem::last_write_time(root);
    constexpr std::uintmax_t limit=128ULL*1024*1024;
    std::uintmax_t bytes=0;
    std::size_t entries=0;
    const auto visit=[&](auto&& self,const std::filesystem::path& parent)->void {
        for(const auto& entry:std::filesystem::directory_iterator(parent)) {
            state::require_safe_path(entry.path());
            if(++entries>65536)
                throw std::runtime_error("The private CMake snapshot has too many entries");
            const auto relative=entry.path().lexically_relative(root);
            if(entry.is_directory()) {
                result.directories.emplace(relative,entry.last_write_time());
                self(self,entry.path());
            }else if(entry.is_regular_file()) {
                const auto size=entry.file_size();
                if(size>limit-bytes)
                    throw std::runtime_error("The private CMake snapshot exceeds128 MiB");
                auto content=read_exact(entry.path(),static_cast<std::size_t>(size));
                if(content.size()!=size)
                    throw std::runtime_error("Cannot retain the public CMake artifact");
                bytes+=size;
                result.files.emplace(relative,CmakeProbeFile{
                    std::move(content),entry.last_write_time()});
            }else{
                throw std::runtime_error("An unsupported entry prevents a private CMake snapshot");
            }
        }
    };
    visit(visit,root);
    return result;
}
bool cmake_probe_descendant(const std::filesystem::path& path,
        const std::filesystem::path& parent) {
    const auto relative=path.lexically_relative(parent);
    return !relative.empty()&&!relative.is_absolute()&&
        *relative.begin()!=L"..";
}
void clear_cmake_probe_tree(const std::filesystem::path& root,
        const std::vector<std::filesystem::path>& kept={}) {
    state::require_safe_path(root);
    const auto visit=[&](auto&& self,const std::filesystem::path& parent)->void {
        for(const auto& entry:std::filesystem::directory_iterator(parent)) {
            state::require_safe_path(entry.path());
            const auto relative=entry.path().lexically_relative(root);
            if(std::ranges::find(kept,relative)!=kept.end()) {
                if(!entry.is_regular_file())
                    throw std::runtime_error("A CMake execution input is not a regular file");
                continue;
            }
            const bool ancestor=std::ranges::any_of(kept,
                [&](const auto& path){return cmake_probe_descendant(path,relative);});
            if(entry.is_directory())self(self,entry.path());
            if(!ancestor&&!std::filesystem::remove(entry.path()))
                throw std::runtime_error("Cannot reset the private CMake execution workspace");
        }
    };
    visit(visit,root);
}
void restore_cmake_probe_tree(const std::filesystem::path& root,const CmakeProbeTree& tree) {
    clear_cmake_probe_tree(root);
    for(const auto& [relative,unused]:tree.directories) {
        const auto path=root/relative;
        state::require_safe_path(path);
        std::filesystem::create_directories(path);
        state::require_safe_path(path);
    }
    for(const auto& [relative,file]:tree.files) {
        const auto path=root/relative;
        state::require_safe_path(path);
        std::filesystem::create_directories(path.parent_path());
        state::require_safe_path(path);
        write_exact(path,file.bytes);
        std::filesystem::last_write_time(path,file.modified);
    }
    // Ninja's existing state is restored with the timestamps that belonged to
    // its files, rather than pairing old bytes with new write times.
    for(auto it=tree.directories.rbegin();it!=tree.directories.rend();++it)
        std::filesystem::last_write_time(root/it->first,it->second);
    std::filesystem::last_write_time(root,tree.modified);
}
void restore_cmake_public_tree(const std::filesystem::path& root,
        const CmakeProbeTree& hidden,const CmakeProbeTree& visible_before) {
    state::require_safe_path(root);
    const auto root_time=std::filesystem::last_write_time(root);
    std::vector<std::pair<std::filesystem::path,std::filesystem::file_time_type>> restored;
    const auto safe=[&](const std::filesystem::path& path) {
        try {state::require_safe_path(path);return true;}
        catch(const std::exception&) {return false;}
    };
    // Restore only entries hidden before launching the public program. Existing
    // public files, directories, replacements and deletions take precedence.
    // This needs no snapshot of program-created data, which may exceed the
    // private evidence capacity and must still be preserved.
    for(const auto& [relative,modified]:hidden.directories) {
        const auto path=root/relative;
        if(!safe(path))continue;
        if(std::filesystem::is_directory(path)) {
            const auto actual=std::filesystem::last_write_time(path);
            const auto before=visible_before.directories.find(relative);
            restored.emplace_back(path,before!=visible_before.directories.end()&&
                actual==before->second?modified:actual);
            continue;
        }
        if(std::filesystem::exists(path))continue;
        if(visible_before.directories.contains(relative))continue;
        const auto parent=path.parent_path();
        if(!safe(parent)||!std::filesystem::is_directory(parent))continue;
        std::filesystem::create_directory(path);
        state::require_safe_path(path);
        restored.emplace_back(path,modified);
    }
    for(const auto& [relative,file]:hidden.files) {
        if(visible_before.files.contains(relative))continue;
        const auto path=root/relative;
        if(!safe(path)||std::filesystem::exists(path))continue;
        const auto parent=path.parent_path();
        if(!safe(parent)||!std::filesystem::is_directory(parent))continue;
        write_exact(path,file.bytes);
        std::filesystem::last_write_time(path,file.modified);
    }
    for(auto it=restored.rbegin();it!=restored.rend();++it)
        std::filesystem::last_write_time(it->first,it->second);
    std::filesystem::last_write_time(root,root_time==visible_before.modified?
        hidden.modified:root_time);
}
void execute_hidden_cmake_phase(const CppRunRequest& request,CppRunResult& observed,
        const std::filesystem::path& workspace,const std::vector<wchar_t>& environment,
        CmakeProbeTree& build_tree,CmakeProbeTree& run_tree,bool preserve_outputs,
        std::stop_token stop) {
    const auto& directory=observed.run_directory;
    const auto artifact=contained(workspace,observed.artifact).lexically_relative(workspace);
    CmakeProbeTree visible_build,visible_run;
    bool launched=false;
    const auto restore=[&] {
        if(preserve_outputs&&launched) {
            restore_cmake_public_tree(workspace,build_tree,visible_build);
            restore_cmake_public_tree(directory,run_tree,visible_run);
        }else{
            restore_cmake_probe_tree(workspace,build_tree);
            restore_cmake_probe_tree(directory,run_tree);
        }
    };
    try {
        clear_cmake_probe_tree(workspace,{artifact});
        clear_cmake_probe_tree(directory,{L"stdin.bin",L"compiler-stdin.bin",L"toolchain.txt"});
        visible_build=retain_cmake_probe_tree(workspace);
        visible_run=retain_cmake_probe_tree(directory);
        launched=true;
        observed.execution=run_phase(observed.artifact,{},directory,directory/L"stdin.bin",
            L"execution",request.execution_timeout,request.output_limit_bytes,false,stop,
            false,&environment);
    }catch(...) {
        restore();
        throw;
    }
    restore();
    if(preserve_outputs) {
        // If public output exceeds the evidence cap or introduces an unsafe
        // entry, this read can fail without clearing any of those public files.
        build_tree=retain_cmake_probe_tree(workspace);
        run_tree=retain_cmake_probe_tree(directory);
    }
}


std::optional<std::string> integer_battery_driver_path(const CppRunRequest& request,
        const std::filesystem::path& workspace,bool client) {
    const auto graph=snapshot_cmake_graph(workspace,request,
        fingerprint(request),fingerprint(request,true));
    const auto target=std::ranges::find_if(graph.targets,
        [&](const auto& item){return item.name==request.cmake->target;});
    if(target==graph.targets.end()||target->type!="EXECUTABLE")return {};
    std::optional<std::string> driver;
    for(const auto& path:target->compiled_sources) {
        const auto absolute=contained(workspace/L"p",project_path(path));
        const auto relative=utf8(absolute.lexically_relative(workspace/L"p").generic_wstring());
        const auto source=std::ranges::find_if(request.project_files,
            [&](const auto& item){return item.first==relative;});
        if(source==request.project_files.end()||
           !curriculum::integer_battery_driver_has_main(source->second))continue;
        if(driver||(!client&&relative!=request.integer_battery_probe->test_path))return {};
        driver=relative;
    }
    return driver;
}

curriculum::NamedRuleBatteryRun named_rule_battery_observation(
        const curriculum::NamedRuleBatteryPhase& phase,std::string_view frame,
        const CppPhaseResult& compilation,const CppPhaseResult& execution) {
    curriculum::NamedRuleBatteryRun run;
    run.program_frame=frame;
    run.phase=phase;
    run.compiled=compilation.succeeded();
    run.executed=execution.status==CppPhaseStatus::completed&&execution.exit_code.has_value();
    run.exit_code=execution.exit_code.value_or(0);
    run.stdout_bytes=execution.stdout_bytes;
    run.stderr_bytes=execution.stderr_bytes;
    if(const auto trace=curriculum::named_rule_battery_parse_trace(execution.stderr_bytes)) {
        run.calls=trace->calls;
        run.completed_normally=run.executed&&trace->completed_normally;
        run.stderr_bytes=trace->visible;
    }
    return run;
}

// Preserve and execute the submitted checker. Only the reviewed functions are
// instrumented, and one declared call receives a changed result per phase.
// The existing owned-workspace helpers hide the phase sources while running
// and restore the public build, logs and program-created files afterwards.
void run_named_rule_battery_cmake(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& input,const std::filesystem::path& compiler_input,
        std::string_view initial_toolchain,std::stop_token stop) {
    validate_request_project(request);
    const auto workspace=cmake_workspace(request);
    const auto shared_lock=project_mutex(workspace);
    std::unique_lock<std::timed_mutex> lock(*shared_lock,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10)))if(stop.stop_requested()) {
        result.compilation.status=CppPhaseStatus::cancelled;
        return;
    }
    const bool supported=named_rule_battery_context(request);
    const auto environment=compiler_environment();
    run_cmake_unlocked(request,result,input,compiler_input,stop,&environment,supported);
    if(!supported||!result.compilation.succeeded()||result.artifact.empty()||
       result.target_type!="EXECUTABLE"||stop.stop_requested())return;
    const auto& probe=*request.named_rule_probe;
    const auto& spec=*probe.battery_check;
    const auto library=std::ranges::find_if(request.project_files,
        [&](const auto& file){return file.first==spec.source_path;});
    const auto driver=std::ranges::find_if(request.project_files,
        [&](const auto& file){return file.first==probe.harness_path;});
    const auto instrumented_driver=driver!=request.project_files.end()
        ?curriculum::named_rule_battery_instrument_driver(driver->second,spec)
        :std::optional<std::string>{};
    auto build_tree=retain_cmake_probe_tree(workspace);
    auto run_tree=retain_cmake_probe_tree(result.run_directory);
    const auto original_build=build_tree;
    execute_hidden_cmake_phase(request,result,workspace,environment,
        build_tree,run_tree,true,stop);
    if(!result.execution.succeeded()||stop.stop_requested()||
       library==request.project_files.end()||!instrumented_driver)return;
    const auto plan=curriculum::named_rule_battery_plan(spec);
    if(!plan)return;
    CppNamedRuleBatteryEvidence evidence;
    evidence.original_program_fingerprint=named_rule_battery_fingerprint(request);
    const auto restore_public=[&] {
        restore_cmake_probe_tree(workspace,build_tree);
        restore_cmake_probe_tree(result.run_directory,run_tree);
    };
    try {
        restore_cmake_probe_tree(workspace,original_build);
        for(const auto& phase:*plan) {
            if(stop.stop_requested())break;
            const auto instrumented=curriculum::named_rule_battery_instrument_source(
                library->second,spec,phase);
            if(!instrumented)break;
            auto private_request=request;
            private_request.named_rule_probe.reset();
            for(auto& file:private_request.project_files)
                if(file.first==spec.source_path)file.second=*instrumented;
                else if(file.first==probe.harness_path)file.second=*instrumented_driver;
            if(cmake_workspace(private_request)!=workspace)
                throw std::runtime_error("A named-rule probe changed its CMake workspace identity");
            clear_cmake_probe_tree(result.run_directory);
            write_exact(input,request.stdin_utf8);
            write_exact(compiler_input,"");
            write_exact(result.run_directory/L"toolchain.txt",initial_toolchain);
            CppRunResult observed;
            observed.request_id=request.request_id;
            observed.revision=request.revision;
            observed.run_directory=result.run_directory;
            run_cmake_unlocked(private_request,observed,input,compiler_input,stop,
                &environment,true);
            const bool bound=observed.project_fingerprint==fingerprint(private_request)&&
                observed.configuration_fingerprint==fingerprint(private_request,true)&&
                observed.artifact==result.artifact&&observed.target_type=="EXECUTABLE";
            if(bound&&observed.compilation.succeeded()&&!stop.stop_requested()) {
                auto private_build=retain_cmake_probe_tree(workspace);
                auto private_run=retain_cmake_probe_tree(result.run_directory);
                execute_hidden_cmake_phase(private_request,observed,workspace,environment,
                    private_build,private_run,false,stop);
            }
            auto run=named_rule_battery_observation(phase,
                evidence.original_program_fingerprint,observed.compilation,observed.execution);
            run.compiled=run.compiled&&bound&&
                (observed.configuration.status==CppPhaseStatus::not_started||
                 observed.configuration.succeeded());
            evidence.runs.push_back(std::move(run));
            if(!evidence.runs.back().compiled||!evidence.runs.back().executed)break;
        }
    }catch(...) {
        restore_public();
        throw;
    }
    restore_public();
    result.named_rule_battery_evidence=std::move(evidence);
}

void complete_named_rule_battery_single(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& compiler,const std::vector<wchar_t>& environment,
        std::string_view initial_toolchain,std::stop_token stop) {
    if(!named_rule_battery_context(request)||request.cmake||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto& spec=*request.named_rule_probe->battery_check;
    const auto plan=curriculum::named_rule_battery_plan(spec);
    if(!plan)return;
    CppNamedRuleBatteryEvidence evidence;
    evidence.original_program_fingerprint=named_rule_battery_fingerprint(request);
    const auto make_source=[&](std::size_t index)->std::optional<std::string> {
        const auto instrumented=curriculum::named_rule_battery_instrument_source(
            request.source_utf8,spec,(*plan)[index]);
        return instrumented?curriculum::named_rule_battery_instrument_driver(*instrumented,spec)
            :std::optional<std::string>{};
    };
    const auto observe=[&](std::size_t index,const CppPhaseResult& compilation,
                          const CppPhaseResult& execution) {
        evidence.runs.push_back(named_rule_battery_observation((*plan)[index],
            evidence.original_program_fingerprint,compilation,execution));
    };
    if(execute_private_single_file_phases(request,result,compiler,environment,initial_toolchain,
            plan->size(),make_source,observe,stop))
        result.named_rule_battery_evidence=std::move(evidence);
}

void run_integer_battery_cmake(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& input,const std::filesystem::path& compiler_input,
        std::string_view initial_toolchain,std::stop_token stop) {
    validate_request_project(request);
    const auto workspace=cmake_workspace(request);
    const auto shared_lock=project_mutex(workspace);
    std::unique_lock<std::timed_mutex> lock(*shared_lock,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10)))if(stop.stop_requested()) {
        auto& phase=request.action==CppRunRequest::Action::execute?result.execution:
            request.action==CppRunRequest::Action::build?result.compilation:result.configuration;
        phase.status=CppPhaseStatus::cancelled;
        return;
    }
    const auto& spec=*request.integer_battery_probe;
    const bool client=spec.prerequisite&&request.cmake->target==spec.prerequisite->target;
    const bool supported=integer_battery_context(request)&&
        (request.cmake->target==spec.test_target||client);
    const bool execute=supported&&(request.action==CppRunRequest::Action::compile_run||
        request.action==CppRunRequest::Action::execute);
    const auto environment=compiler_environment();
    run_cmake_unlocked(request,result,input,compiler_input,stop,&environment,execute);
    if(!execute||!result.compilation.succeeded()||result.artifact.empty()||
       result.target_type!="EXECUTABLE"||stop.stop_requested())return;

    // Resolve the trusted target before the public program can write files.
    const auto library=std::ranges::find_if(request.project_files,
        [&](const auto& file){return file.first==spec.library_path;});
    const auto driver_path=integer_battery_driver_path(request,workspace,client);
    const auto driver=driver_path?std::ranges::find_if(request.project_files,
        [&](const auto& file){return file.first==*driver_path;}):request.project_files.end();
    const auto instrumented_driver=driver!=request.project_files.end()
        ?curriculum::integer_battery_instrument_driver(driver->second,spec)
        :std::optional<std::string>{};
    auto build_tree=retain_cmake_probe_tree(workspace);
    auto run_tree=retain_cmake_probe_tree(result.run_directory);
    const auto original_build=build_tree;
    execute_hidden_cmake_phase(request,result,workspace,environment,
        build_tree,run_tree,true,stop);
    if(result.execution.status!=CppPhaseStatus::completed||!result.execution.exit_code||
       stop.stop_requested())return;

    if(library==request.project_files.end()||!instrumented_driver)return;
    const auto plan=curriculum::integer_battery_plan(spec);
    if(!plan)return;
    const std::vector<curriculum::IntegerBatteryPhase> phases=client
        ?std::vector<curriculum::IntegerBatteryPhase>{
            {curriculum::IntegerBatteryMutation::published,{}}}:*plan;
    CppIntegerBatteryEvidence evidence;
    evidence.original_program_fingerprint=integer_battery_fingerprint(request);
    const auto restore_public=[&] {
        restore_cmake_probe_tree(workspace,build_tree);
        restore_cmake_probe_tree(result.run_directory,run_tree);
    };
    try {
        restore_cmake_probe_tree(workspace,original_build);
        for(const auto& phase:phases) {
            if(stop.stop_requested())break;
            const auto instrumented=curriculum::integer_battery_instrument(
                library->second,spec,phase);
            if(!instrumented)break;
            auto private_request=request;
            private_request.action=CppRunRequest::Action::compile_run;
            for(auto& file:private_request.project_files)
                if(file.first==spec.library_path)file.second=*instrumented;
                else if(file.first==*driver_path)file.second=*instrumented_driver;
            if(cmake_workspace(private_request)!=workspace)
                throw std::runtime_error("An integer probe changed the CMake workspace identity");
            clear_cmake_probe_tree(result.run_directory);
            write_exact(input,request.stdin_utf8);
            write_exact(compiler_input,"");
            write_exact(result.run_directory/L"toolchain.txt",initial_toolchain);
            CppRunResult observed;
            observed.request_id=request.request_id;
            observed.revision=request.revision;
            observed.run_directory=result.run_directory;
            run_cmake_unlocked(private_request,observed,input,compiler_input,stop,
                &environment,true);
            if(observed.compilation.succeeded()&&observed.artifact==result.artifact&&
               observed.target_type=="EXECUTABLE"&&!stop.stop_requested()) {
                auto private_build=retain_cmake_probe_tree(workspace);
                auto private_run=retain_cmake_probe_tree(result.run_directory);
                execute_hidden_cmake_phase(private_request,observed,workspace,environment,
                    private_build,private_run,false,stop);
            }
            curriculum::IntegerBatteryRun run;
            run.program_frame=evidence.original_program_fingerprint;
            run.compiled=observed.compilation.succeeded()&&
                (observed.configuration.status==CppPhaseStatus::not_started||
                 observed.configuration.succeeded());
            run.executed=observed.execution.status==CppPhaseStatus::completed&&
                observed.execution.exit_code.has_value();
            run.exit_code=observed.execution.exit_code.value_or(0);
            run.phase=phase;
            run.stdout_bytes=observed.execution.stdout_bytes;
            run.stderr_bytes=observed.execution.stderr_bytes;
            if(const auto trace=curriculum::integer_battery_parse_trace(run.stderr_bytes)) {
                run.calls=trace->calls;
                run.completed_normally=trace->completed_normally;
                run.stderr_bytes=trace->visible;
            }
            evidence.runs.push_back(std::move(run));
            if(!evidence.runs.back().compiled||!evidence.runs.back().executed)break;
        }
    }catch(...) {
        restore_public();
        throw;
    }
    restore_public();
    result.integer_battery_evidence=std::move(evidence);
}

// The whole construction is one captured request. Reuse its CMake configure
// and rebuild only changed translation units while retaining the public
// artifact, logs, files and timestamps for restoration before returning.
void run_adjustment_project_cmake(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& input,const std::filesystem::path& compiler_input,
        std::string_view initial_toolchain,std::stop_token stop) {
    validate_request_project(request);
    const auto workspace=cmake_workspace(request);
    const auto shared_lock=project_mutex(workspace);
    std::unique_lock<std::timed_mutex> lock(*shared_lock,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10)))if(stop.stop_requested()) {
        auto& phase=request.action==CppRunRequest::Action::execute?result.execution:
            request.action==CppRunRequest::Action::build?result.compilation:result.configuration;
        phase.status=CppPhaseStatus::cancelled;
        return;
    }
    const bool execute=adjustment_project_context(request)&&
        (request.action==CppRunRequest::Action::compile_run||
         request.action==CppRunRequest::Action::execute);
    const auto environment=compiler_environment();
    run_cmake_unlocked(request,result,input,compiler_input,stop,&environment,execute);
    if(!execute||!result.compilation.succeeded()||result.artifact.empty()||
       result.target_type!="EXECUTABLE"||stop.stop_requested())return;

    const auto& spec=*request.adjustment_project_probe;
    const auto source_at=[&](std::string_view path) {
        return std::ranges::find_if(request.project_files,
            [&](const auto& file){return file.first==path;});
    };
    const auto library=source_at(spec.library_path),header=source_at(spec.header_path);
    const auto client=source_at(spec.client_path),battery=source_at(spec.test_path);
    const auto component=curriculum::adjustment_project_component_source(spec);
    auto build_tree=retain_cmake_probe_tree(workspace);
    auto run_tree=retain_cmake_probe_tree(result.run_directory);
    const auto original_build=build_tree;
    execute_hidden_cmake_phase(request,result,workspace,environment,
        build_tree,run_tree,true,stop);
    if(!result.execution.succeeded()||stop.stop_requested()||!component||
       library==request.project_files.end()||header==request.project_files.end()||
       client==request.project_files.end()||battery==request.project_files.end()||
       !curriculum::adjustment_project_header_valid(header->second,spec)||
       !evaluate_cmake_graph(request,result,*request.cmake_graph_check).correct)return;

    using Part=curriculum::AdjustmentProjectPart;
    using Mutation=curriculum::OpenBatteryMutation;
    using Phase=curriculum::AdjustmentProjectPhase;
    const auto inputs=curriculum::adjustment_project_client_inputs(spec);
    std::vector<Phase> phases{{Part::component,Mutation::baseline,{},0}};
    for(std::size_t i=0;i<inputs.size();++i)
        phases.push_back({Part::client,Mutation::baseline,{},i});
    phases.push_back({Part::battery,Mutation::baseline,{},0});
    phases.push_back({Part::battery,Mutation::baseline,{},0,true});
    const auto prefix_count=phases.size();
    CppAdjustmentProjectEvidence evidence;
    evidence.original_program_fingerprint=adjustment_project_fingerprint(request);
    const auto restore_public=[&] {
        restore_cmake_probe_tree(workspace,build_tree);
        restore_cmake_probe_tree(result.run_directory,run_tree);
    };
    try {
        restore_cmake_probe_tree(workspace,original_build);
        for(std::size_t index=0;index<phases.size();++index) {
            if(stop.stop_requested())break;
            // Copy: discovering the battery may replace/extend this vector.
            const auto phase=phases[index];
            auto private_request=request;
            private_request.action=CppRunRequest::Action::compile_run;
            private_request.adjustment_project_probe.reset();
            private_request.capture_cmake_graph=false;
            private_request.cmake_graph_check.reset();
            private_request.cmake->target=phase.part==Part::client?
                spec.client_target:spec.test_target;
            private_request.stdin_utf8=phase.part==Part::client?
                inputs.at(phase.sample_index):std::string{};
            const auto instrumented=phase.part==Part::component?
                std::optional<std::string>{library->second}:
                curriculum::adjustment_project_instrumented_library(library->second,spec,phase);
            if(!instrumented)break;
            for(auto& file:private_request.project_files) {
                if(file.first==spec.library_path)file.second=*instrumented;
                else if(file.first==spec.test_path) {
                    if(phase.part==Part::component)file.second=*component;
                    else if(phase.assertions_disabled)
                        file.second="#undef NDEBUG\n#define NDEBUG 1\n"+file.second;
                }
            }
            if(cmake_workspace(private_request)!=workspace)
                throw std::runtime_error("An adjustment probe changed the CMake workspace identity");
            clear_cmake_probe_tree(result.run_directory);
            write_exact(input,private_request.stdin_utf8);
            write_exact(compiler_input,"");
            write_exact(result.run_directory/L"toolchain.txt",initial_toolchain);
            CppRunResult observed;
            observed.request_id=request.request_id;
            observed.revision=request.revision;
            observed.run_directory=result.run_directory;
            run_cmake_unlocked(private_request,observed,input,compiler_input,stop,
                &environment,true);
            const bool bound=observed.project_fingerprint==fingerprint(private_request)&&
                observed.configuration_fingerprint==fingerprint(private_request,true)&&
                observed.executable==private_request.cmake->target&&
                observed.target_type=="EXECUTABLE"&&!observed.artifact.empty();
            if(bound&&observed.compilation.succeeded()&&!stop.stop_requested()) {
                auto private_build=retain_cmake_probe_tree(workspace);
                auto private_run=retain_cmake_probe_tree(result.run_directory);
                execute_hidden_cmake_phase(private_request,observed,workspace,environment,
                    private_build,private_run,false,stop);
            }
            curriculum::AdjustmentProjectRun run;
            run.program_frame=evidence.original_program_fingerprint;
            run.compiled=bound&&observed.compilation.succeeded()&&
                (observed.configuration.status==CppPhaseStatus::not_started||
                 observed.configuration.succeeded());
            run.executed=observed.execution.status==CppPhaseStatus::completed&&
                observed.execution.exit_code&&*observed.execution.exit_code<=255;
            if(run.executed)run.exit_code=static_cast<int>(*observed.execution.exit_code);
            run.phase=phase;
            run.standard_input=private_request.stdin_utf8;
            run.stdout_bytes=observed.execution.stdout_bytes;
            run.stderr_bytes=observed.execution.stderr_bytes;
            if(const auto calls=curriculum::adjustment_project_parse_observations(run.stderr_bytes)) {
                run.observations=*calls;
                run.stderr_bytes=curriculum::adjustment_project_student_stderr(run.stderr_bytes);
            }else{
                run.executed=false; // an incomplete trace cannot attest a call
            }
            evidence.runs.push_back(std::move(run));
            if(!evidence.runs.back().compiled||!evidence.runs.back().executed)break;
            if(index+1==prefix_count) {
                // Mutation targets come from real calls, not reference rows.
                const auto complete=curriculum::adjustment_project_plan(
                    spec,evidence.runs.back().observations);
                if(!complete||complete->size()<prefix_count||
                   !std::equal(phases.begin(),phases.end(),complete->begin()))break;
                phases=*complete;
            }
        }
    }catch(...) {
        restore_public();
        throw;
    }
    restore_public();
    result.adjustment_project_evidence=std::move(evidence);
}

void complete_boolean_battery_probe(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& compiler,const std::vector<wchar_t>& environment,
        std::string_view initial_toolchain,std::stop_token stop) {
    if(!request.boolean_battery_probe||request.reduction_probe||request.cmake||
       !request.project_files.empty()||!request.source_files.empty()||
       request.diagnostic||request.open_battery_probe||request.named_rule_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.cmake_graph_check||request.capture_cmake_graph||request.adjustment_project_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||
       !result.compilation.succeeded()||!result.error_utf8.empty()||
       result.execution.status!=CppPhaseStatus::completed||!result.execution.exit_code||
       stop.stop_requested())return;
    const auto& spec=*request.boolean_battery_probe;
    if(!curriculum::boolean_battery_preserves_function(request.source_utf8,spec))return;
    const auto plan=curriculum::boolean_battery_mutation_plan(spec);
    if(!plan)return;
    CppBooleanBatteryEvidence evidence;
    evidence.original_program_fingerprint=boolean_battery_fingerprint(request);
    const auto make_source=[&](std::size_t index) {
        return curriculum::boolean_battery_instrument(request.source_utf8,spec,(*plan)[index]);
    };
    const auto observe=[&](std::size_t index,const CppPhaseResult& compilation,
                          const CppPhaseResult& execution) {
        const auto& required=(*plan)[index];
        curriculum::BooleanBatteryRun observation;
        observation.program_frame=evidence.original_program_fingerprint;
        observation.mutation=required.mutation;
        observation.target=required.target;
        observation.compiled=compilation.succeeded();
        observation.executed=execution.status==CppPhaseStatus::completed&&
            execution.exit_code.has_value();
        if(execution.exit_code)observation.exit_code=*execution.exit_code;
        observation.stdout_bytes=execution.stdout_bytes;
        const auto trace=curriculum::boolean_battery_parse_trace(execution.stderr_bytes);
        if(trace) {
            observation.calls=trace->calls;
            observation.completed_normally=observation.executed&&trace->completed_normally;
            observation.stderr_bytes=trace->visible;
        }else{
            observation.stderr_bytes=execution.stderr_bytes;
        }
        evidence.runs.push_back(std::move(observation));
    };
    if(execute_private_single_file_phases(request,result,compiler,environment,initial_toolchain,
            plan->size(),make_source,observe,stop))
        result.boolean_battery_evidence=std::move(evidence);
}

void complete_local_int_check(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& compiler,const std::vector<wchar_t>& environment,
        std::string_view initial_toolchain,std::stop_token stop) {
    if(!request.local_int_check||request.backend!=CppRunRequest::Backend::native_msvc||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=local_int_fingerprint(request);
    if(frame.empty())return;
    const auto& probes=request.local_int_check->probes;
    std::vector<CppLocalIntEvidence> evidence;
    evidence.reserve(probes.size());
    const auto make_source=[&](std::size_t index) {
        return curriculum::cpp_local_int_override(request.source_utf8,probes.at(index));
    };
    const auto observe=[&](std::size_t index,const CppPhaseResult& compilation,
                          const CppPhaseResult& execution) {
        const auto source=make_source(index);
        evidence.push_back(CppLocalIntEvidence{
            source?state::sha256(*source):std::string{},compilation,execution});
    };
    if(execute_private_single_file_phases(request,result,compiler,environment,
            initial_toolchain,probes.size(),make_source,observe,stop))
        result.local_int_evidence=std::move(evidence);
}

void complete_local_string_check(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& compiler,const std::vector<wchar_t>& environment,
        std::string_view initial_toolchain,std::stop_token stop) {
    if(!request.local_string_check||
       request.backend!=CppRunRequest::Backend::native_msvc||
       !result.compilation.succeeded()||!result.execution.succeeded()||
       !result.error_utf8.empty()||stop.stop_requested())return;
    const auto frame=local_string_fingerprint(request);
    if(frame.empty())return;
    const auto& probes=request.local_string_check->probes;
    std::vector<CppLocalStringEvidence> evidence;
    evidence.reserve(probes.size());
    const auto make_source=[&](std::size_t index) {
        return curriculum::cpp_local_string_override(request.source_utf8,probes.at(index));
    };
    const auto observe=[&](std::size_t index,const CppPhaseResult& compilation,
                          const CppPhaseResult& execution) {
        const auto instrumented=make_source(index);
        evidence.push_back(CppLocalStringEvidence{
            instrumented?state::sha256(*instrumented):std::string{},
            compilation,execution});
    };
    if(execute_private_single_file_phases(request,result,compiler,environment,
            initial_toolchain,probes.size(),make_source,observe,stop))
        result.local_string_evidence=std::move(evidence);
}

void complete_reduction_probe(const CppRunRequest& request,CppRunResult& result,
        const std::filesystem::path& compiler,const std::vector<wchar_t>& environment,
        std::string_view initial_toolchain,std::stop_token stop) {
    if(!request.reduction_probe||request.boolean_battery_probe||request.cmake||
       !request.project_files.empty()||!request.source_files.empty()||
       request.diagnostic||request.open_battery_probe||request.named_rule_probe||
       request.rejection_probe||request.observation_probe||request.assertion_mode||
       request.cmake_graph_check||request.capture_cmake_graph||request.adjustment_project_probe||
       request.backend!=CppRunRequest::Backend::native_msvc||
       request.action!=CppRunRequest::Action::compile_run||
       !result.compilation.succeeded()||!result.error_utf8.empty()||
       result.execution.status!=CppPhaseStatus::completed||!result.execution.exit_code||
       stop.stop_requested())return;
    const auto& spec=*request.reduction_probe;
    const auto plan=curriculum::reduction_plan(spec);
    if(!plan)return;
    CppReductionEvidence evidence;
    evidence.original_program_fingerprint=reduction_fingerprint(request);
    const auto make_source=[&](std::size_t index) {
        // Source reduction and direct-header checks decide credit separately.
        // An unchanged initial driver can still provide a real normal witness.
        return curriculum::reduction_instrument(request.source_utf8,spec,(*plan)[index]);
    };
    const auto observe=[&](std::size_t index,const CppPhaseResult& compilation,
                          const CppPhaseResult& execution) {
        curriculum::ReductionRun observation;
        observation.program_frame=evidence.original_program_fingerprint;
        observation.phase=(*plan)[index];
        observation.compiled=compilation.succeeded();
        observation.executed=execution.status==CppPhaseStatus::completed&&
            execution.exit_code.has_value();
        if(execution.exit_code)observation.exit_code=*execution.exit_code;
        observation.stdout_bytes=execution.stdout_bytes;
        const auto trace=curriculum::reduction_parse_trace(execution.stderr_bytes);
        if(trace) {
            observation.calls=trace->calls;
            observation.completed_normally=observation.executed&&trace->completed_normally;
            observation.stderr_bytes=trace->visible;
        }else{
            observation.stderr_bytes=execution.stderr_bytes;
        }
        evidence.runs.push_back(std::move(observation));
    };
    if(execute_private_single_file_phases(request,result,compiler,environment,initial_toolchain,
            plan->size(),make_source,observe,stop))
        result.reduction_evidence=std::move(evidence);
}
} // namespace

std::string cpp_local_int_request_fingerprint(const CppRunRequest& request) {
    return local_int_fingerprint(request);
}
std::string cpp_local_string_request_fingerprint(const CppRunRequest& request) {
    return local_string_fingerprint(request);
}

std::string cpp_defaulted_move_policy_request_fingerprint(
        const CppRunRequest& request) {
    return defaulted_move_policy_fingerprint(request);
}

std::string cpp_independent_copy_assignment_request_fingerprint(
        const CppRunRequest& request) {
    return independent_copy_assignment_fingerprint(request);
}

std::string cpp_move_assignment_invariant_request_fingerprint(
        const CppRunRequest& request) {
    return move_assignment_invariant_fingerprint(request);
}

std::string cpp_array_owner_read_request_fingerprint(
        const CppRunRequest& request) {
    return array_owner_read_fingerprint(request);
}

std::string cpp_array_copy_move_request_fingerprint(
        const CppRunRequest& request) {
    return array_copy_move_fingerprint(request);
}

std::string cpp_array_return_take_request_fingerprint(
        const CppRunRequest& request) {
    return array_return_take_fingerprint(request);
}
std::string cpp_polymorphic_destruction_request_fingerprint(
        const CppRunRequest& request) {
    return polymorphic_destruction_fingerprint(request);
}
std::string cpp_virtual_lifetime_request_fingerprint(
        const CppRunRequest& request) {
    return virtual_lifetime_fingerprint(request);
}
std::string cpp_derived_preparation_request_fingerprint(
        const CppRunRequest& request) {
    return derived_preparation_fingerprint(request);
}
std::string cpp_abstract_interface_request_fingerprint(
        const CppRunRequest& request) {
    return abstract_interface_fingerprint(request);
}

std::string cpp_interface_collection_request_fingerprint(
        const CppRunRequest& request) {
    return interface_collection_fingerprint(request);
}

std::string cpp_polymorphic_clone_request_fingerprint(
        const CppRunRequest& request) {
    return polymorphic_clone_fingerprint(request);
}

std::string cpp_dual_interface_request_fingerprint(
        const CppRunRequest& request) {
    return dual_interface_fingerprint(request);
}

std::string cpp_indexed_unique_transfer_request_fingerprint(
        const CppRunRequest& request) {
    return indexed_unique_transfer_fingerprint(request);
}

CppRunResult run_cpp(const CppRunRequest& request, std::stop_token stop) {
    CppRunResult result;
    result.request_id = request.request_id;
    result.revision = request.revision;
    if(request.diagnostic)result.diagnostic.emplace();
    try {
        if(named_rule_battery_requested(request))
            result.named_rule_battery_request_fingerprint=named_rule_battery_fingerprint(request);
        if(named_rule_constant_variations_requested(request))
            result.named_rule_constant_variation_request_fingerprint=
                named_rule_constant_variation_fingerprint(request);
        if(request.local_int_check)
            result.local_int_request_fingerprint=local_int_fingerprint(request);
        if(request.local_string_check)
            result.local_string_request_fingerprint=local_string_fingerprint(request);
        if(request.boolean_battery_probe)
            result.boolean_battery_request_fingerprint=boolean_battery_fingerprint(request);
        if(request.reduction_probe)
            result.reduction_request_fingerprint=reduction_fingerprint(request);
        if(request.integer_battery_probe)
            result.integer_battery_request_fingerprint=integer_battery_fingerprint(request);
        if(request.adjustment_project_probe)
            result.adjustment_project_request_fingerprint=adjustment_project_fingerprint(request);
        if(request.cmake_graph_check&&request.cmake_graph_check->initial_observation)
            result.cmake_initial_observation_request_fingerprint=
                cmake_initial_observation_fingerprint(request);
        ensure_winrt_apartment();
        if (stop.stop_requested()) {
            (request.cmake&&request.action==CppRunRequest::Action::configure?result.configuration:request.cmake&&request.action==CppRunRequest::Action::execute?result.execution:result.compilation).status = CppPhaseStatus::cancelled;
            return result;
        }
        if (request.compile_timeout.count() <= 0 || request.execution_timeout.count() <= 0 ||
                request.output_limit_bytes == 0 || request.output_limit_bytes > 16 * 1024 * 1024 ||
                (request.project_files.empty()&&request.source_utf8.size() > 4 * 1024 * 1024) || request.stdin_utf8.size() > 4 * 1024 * 1024)
            throw std::invalid_argument("Invalid educational execution limits");
        if(!request.project_files.empty())validate_request_project(request);
        else if(request.cmake||request.action!=CppRunRequest::Action::compile_run)throw std::invalid_argument("Project phase actions require projectFiles and CMake");
        if(request.diagnostic) {
            if(!request.stdin_utf8.empty()||request.cmake||request.diagnostic->control_source_utf8.size()>4*1024*1024)
                throw std::invalid_argument("A diagnostic objective cannot use runtime input or CMake");
            if(request.diagnostic->project) {
                if(request.project_files.empty())throw std::invalid_argument("A project diagnostic requires its captured files");
                curriculum::validate_cpp_project(request.diagnostic->project->control_files,request.diagnostic->project->control_sources);
            }else if(!request.project_files.empty()||!request.source_files.empty())
                throw std::invalid_argument("A single-file diagnostic cannot use project files");
        }
        result.run_directory = create_run_directory(request);
        result.diagnostic_source_directory = result.run_directory;
        const auto source = result.run_directory / L"main.cpp";
        const auto input = result.run_directory / L"stdin.bin";
        const auto compiler_input = result.run_directory / L"compiler-stdin.bin";
        const auto executable = result.run_directory / L"program.exe";
        const std::filesystem::path vc{build::educational_vc_root};
        const auto compiler = vc / L"bin/Hostx64/x64/cl.exe";
        write_exact(input, request.stdin_utf8);
        write_exact(compiler_input, "");
        if(request.backend==CppRunRequest::Backend::wasi) {
            run_wasi_single(request,result,source,input,compiler_input,stop);
            complete_named_rule_probe(request,result,stop);
            complete_rejection_stream_probe(request,result,stop);
            complete_observation_probes(request,result,stop);
            complete_assertion_probes(request,result,stop);
            return result;
        }
        if (!std::filesystem::is_regular_file(compiler))
            throw std::runtime_error("Pinned educational MSVC compiler is unavailable");
        const auto initial_toolchain=std::string("MSVC=14.51.36231\nSDK=")+
            utf8(build::educational_sdk_version)+"\ncompiler="+utf8(compiler.wstring())+"\n";
        write_exact(result.run_directory/L"toolchain.txt",initial_toolchain);
        if(request.diagnostic){
            if(request.diagnostic->project)run_project_diagnostic(request,result,compiler,compiler_input,stop);
            else run_diagnostic(request,result,compiler,compiler_input,stop);
            return result;
        }
        if(request.cmake){
            if(named_rule_battery_requested(request)) {
                run_named_rule_battery_cmake(request,result,input,compiler_input,
                    initial_toolchain,stop);
                complete_named_rule_probe(request,result,stop);
                return result;
            }
            if(request.adjustment_project_probe) {
                run_adjustment_project_cmake(request,result,input,compiler_input,
                    initial_toolchain,stop);
                return result;
            }
            if(request.integer_battery_probe) {
                run_integer_battery_cmake(request,result,input,compiler_input,
                    initial_toolchain,stop);
                return result;
            }
            run_cmake(request,result,input,compiler_input,stop);
            complete_cmake_option_probe(request,result,stop);
            complete_named_rule_probe(request,result,stop);
            complete_open_battery_probe(request,result,stop);
            return result;
        }
        if(!request.project_files.empty()){run_multifile(request,result,input,compiler_input,stop);return result;}
        write_exact(source, request.source_utf8);
        const auto arguments=single_file_arguments(result.run_directory);
        const bool has_private_probe=request.local_int_check||request.local_string_check||request.boolean_battery_probe||request.reduction_probe||
            named_rule_battery_requested(request)||request.indexed_unique_transfer_probe||
            request.defaulted_move_policy_probe||request.independent_copy_assignment_probe||
            request.move_assignment_invariant_probe||request.array_owner_read_probe||
            request.array_copy_move_probe||request.array_return_take_probe||
            request.polymorphic_destruction_probe||
            request.virtual_lifetime_probe||request.derived_preparation_probe||
            request.abstract_interface_probe||request.interface_collection_probe||
            request.polymorphic_clone_probe||request.dual_interface_probe;
        const auto private_environment=has_private_probe
            ?compiler_environment():std::vector<wchar_t>{};
        const auto fixed_environment=has_private_probe?&private_environment:nullptr;
        // Localized MSVC diagnostics and Unicode paths need the same private
        // UTF-8 tool console as CMake. Program execution keeps raw binary I/O.
        result.compilation = run_phase(compiler, arguments, result.run_directory,
            compiler_input, L"compilation", request.compile_timeout,
            request.output_limit_bytes, true, stop, true,fixed_environment);
        if (result.compilation.succeeded()) {
            result.artifact=executable;result.target_type="EXECUTABLE";
            if(has_private_probe) {
                // Match the private executions: no phase source/object file
                // remains visible to the public program while it runs.
                clear_private_probe_directory(result.run_directory,true);
            }
            result.execution = run_phase(executable, {}, result.run_directory, input,
                L"execution", request.execution_timeout, request.output_limit_bytes, false, stop,
                false,fixed_environment);
            if(has_private_probe) {
                write_exact(source,request.source_utf8);
                write_exact(result.run_directory/L"compilation-stdout.bin",result.compilation.stdout_bytes);
                write_exact(result.run_directory/L"compilation-stderr.bin",result.compilation.stderr_bytes);
            }
        }
        complete_named_rule_probe(request,result,stop);
        complete_indexed_unique_transfer_probe(request,result,stop);
        complete_defaulted_move_policy_probe(request,result,stop);
        complete_independent_copy_assignment_probe(request,result,stop);
        complete_move_assignment_invariant_probe(request,result,stop);
        complete_array_owner_read_probe(request,result,stop);
        complete_array_copy_move_probe(request,result,stop);
        complete_array_return_take_probe(request,result,stop);
        complete_polymorphic_destruction_probe(request,result,stop);
        complete_virtual_lifetime_probe(request,result,stop);
        complete_derived_preparation_probe(request,result,stop);
        complete_abstract_interface_probe(request,result,stop);
        complete_interface_collection_probe(request,result,stop);
        complete_polymorphic_clone_probe(request,result,stop);
        complete_dual_interface_probe(request,result,stop);
        complete_named_rule_battery_single(request,result,compiler,private_environment,
            initial_toolchain,stop);
        complete_rejection_stream_probe(request,result,stop);
        complete_observation_probes(request,result,stop);
        complete_boolean_battery_probe(request,result,compiler,private_environment,initial_toolchain,stop);
        complete_reduction_probe(request,result,compiler,private_environment,initial_toolchain,stop);
        complete_local_int_check(request,result,compiler,private_environment,initial_toolchain,stop);
        complete_local_string_check(request,result,compiler,private_environment,initial_toolchain,stop);
    } catch (const std::exception& exception) {
        result.error_utf8 = exception.what();
        auto& phase = request.diagnostic?result.compilation:request.cmake&&request.action==CppRunRequest::Action::configure?result.configuration:
            request.cmake&&request.action==CppRunRequest::Action::execute?result.execution:result.compilation.succeeded() ? result.execution : result.compilation;
        phase.status = CppPhaseStatus::launch_failed;
    } catch (const winrt::hresult_error& error) {
        result.error_utf8=winrt::to_string(error.message());auto& phase=request.cmake&&request.action==CppRunRequest::Action::configure?result.configuration:request.cmake&&request.action==CppRunRequest::Action::execute?result.execution:result.compilation;
        phase.status = CppPhaseStatus::launch_failed;
    }
    return result;
}

} // namespace lgaed::windows


