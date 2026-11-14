#include "platform/windows/cpp_runner.hpp"
#include "core/lesson.hpp"
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
    std::stop_token stop,bool utf8_console=false) {
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
    auto environment = compiler_environment();
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
            if(target.HasKey(L"compileGroups"))
                for(const auto& group_value:target.GetNamedArray(L"compileGroups")) {
                    const auto group=group_value.GetObject();
                    if(json_text(group,L"language")!="CXX"||
                       !group.HasKey(L"includes"))continue;
                    for(const auto& item_include:group.GetNamedArray(L"includes")) {
                        const auto path=json_text(item_include.GetObject(),L"path");
                        if(!path.empty()&&std::ranges::find(
                                fact.include_directories,path)==
                                fact.include_directories.end())
                            fact.include_directories.push_back(path);
                    }
                }
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
         std::ranges::any_of(request.cmake_graph_check->targets,
            [](const auto& target) {
                return target.require_explicit_cxx20||
                    target.require_explicit_extensions_off||
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
    const auto target_for=[&](std::string_view name)->CppCmakeTargetEvidence* {
        const auto it=std::ranges::find_if(observed,
            [&](const auto& item){return item.name==name;});
        return it==observed.end()?nullptr:&*it;
    };
    const auto apply_property=[&](std::string_view name,
            std::string_view key,std::string_view value) {
        auto* target=target_for(name);
        if(!target)return;
        if(equals(key,"CXX_EXTENSIONS"))
            target->explicit_extensions_off=off(value);
        else if(equals(key,"CXX_STANDARD"))
            target->explicit_cxx20=value=="20";
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
            const bool authored=std::ranges::any_of(request.project_files,
                [&](const auto& file) {
                    if(!authored_cmake_path(file.first))return false;
                    const auto expected=(source/project_path(file.first))
                        .lexically_normal().generic_wstring();
                    const auto observed_path=origin.generic_wstring();
                    const CaseInsensitive cmp;
                    return !cmp(expected,observed_path)&&
                        !cmp(observed_path,expected);
                });
            if(!authored)continue;
            const auto command=json_text(event,L"cmd");
            std::vector<std::string> args;
            for(const auto& value:event.GetNamedArray(L"args")) {
                const auto expanded=winrt::to_string(value.GetString());
                std::size_t begin=0;
                for(;;) {
                    const auto end=expanded.find(';',begin);
                    args.push_back(expanded.substr(begin,end==std::string::npos?
                        end:end-begin));
                    if(end==std::string::npos)break;
                    begin=end+1;
                }
            }
            if(args.empty())continue;
            if(equals(command,"option")&&args.size()>=2) {
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
            }else if(equals(command,"target_compile_features")&&args.size()>2) {
                auto* target=target_for(args.front());
                if(!target)continue;
                bool applies_to_target=false;
                for(std::size_t i=1;i<args.size();++i) {
                    if(equals(args[i],"PRIVATE")||equals(args[i],"PUBLIC"))
                        applies_to_target=true;
                    else if(equals(args[i],"INTERFACE"))
                        applies_to_target=false;
                    else if(applies_to_target&&equals(args[i],"cxx_std_20"))
                        target->explicit_cxx20=true;
                }
            }else if(equals(command,"set_target_properties")&&args.size()>=4) {
                const auto separator=std::ranges::find_if(args,
                    [&](const auto& arg){return equals(arg,"PROPERTIES");});
                if(separator==args.end())continue;
                const auto pos=static_cast<std::size_t>(separator-args.begin());
                if(pos==0)continue;
                for(auto i=pos+1;i+1<args.size();i+=2)
                    for(std::size_t t=0;t<pos;++t)
                        apply_property(args[t],args[i],args[i+1]);
            }else if(equals(command,"set_property")&&args.size()>=5&&
                     equals(args.front(),"TARGET")) {
                const auto separator=std::ranges::find_if(args,
                    [&](const auto& arg){return equals(arg,"PROPERTY");});
                if(separator==args.end())continue;
                const auto pos=static_cast<std::size_t>(separator-args.begin());
                if(pos<2||pos+3!=args.size())continue;
                bool append=false;
                for(std::size_t i=1;i<pos;++i)
                    append=append||equals(args[i],"APPEND")||
                        equals(args[i],"APPEND_STRING");
                if(append)continue;
                for(std::size_t i=1;i<pos;++i)
                    apply_property(args[i],args[pos+1],args[pos+2]);
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
    }
    graph.authored_option_defaults=std::move(observed_options);
}
bool executable64(const std::filesystem::path& path){state::require_safe_path(path);DWORD kind=0;return std::filesystem::is_regular_file(path)&&GetBinaryTypeW(path.c_str(),&kind)&&kind==SCS_64BIT_BINARY;}
CppProjectAvailability inspect_unlocked(const CppRunRequest& request){CppProjectAvailability result;result.project_fingerprint=fingerprint(request);result.configuration_fingerprint=fingerprint(request,true);result.workspace=cmake_workspace(request);
    if(!verify_workspace_identity(request,false))return result;const auto path=result.workspace/L"state.json";if(!std::filesystem::is_regular_file(path))return result;
    try{const auto saved=read_json(path);result.configured=json_text(saved,L"configuration")==result.configuration_fingerprint&&std::filesystem::is_regular_file(result.workspace/L"build.ninja");
        if(!result.configured)return result;const auto target=cmake_target(result.workspace,*request.cmake);result.target_type=target.type;
        if(json_text(saved,L"built")!=result.project_fingerprint||result.target_type!="EXECUTABLE")return result;
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
    materialize(request.project_files,source);std::filesystem::create_directory(objects);const std::filesystem::path compiler=std::filesystem::path(build::educational_vc_root)/L"bin/Hostx64/x64/cl.exe";std::vector<std::wstring> link{L"/nologo",L"/Fe:"+executable.wstring()};
    const auto start=std::chrono::steady_clock::now();const auto remaining=[&]{return request.compile_timeout-std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start);};
    std::size_t sequence=0;for(const auto& file:request.source_files){if(remaining().count()<=0){result.compilation.status=CppPhaseStatus::timed_out;return;}const auto object=objects/(std::to_wstring(sequence)+L".obj");const std::vector<std::wstring> args{L"/nologo",L"/c",L"/EHsc",build::cpp_standard_option,L"/Zc:__cplusplus",L"/permissive-",L"/utf-8",L"/W4",L"/diagnostics:column",L"/I"+source.wstring(),L"/Fo:"+object.wstring(),(source/project_path(file)).wstring()};
        merge_phase(result.compilation,run_phase(compiler,args,result.run_directory,compiler_input,L"compile-"+std::to_wstring(sequence++),remaining(),request.output_limit_bytes,true,stop,true),request.output_limit_bytes);if(!result.compilation.succeeded())return;link.push_back(object.wstring());}
    link.insert(link.end(),{L"/link",L"/INCREMENTAL:NO",L"/MACHINE:X64",L"/SUBSYSTEM:CONSOLE",L"/MANIFEST:EMBED",L"/MANIFESTUAC:level='asInvoker' uiAccess='false'"});
    if(remaining().count()<=0){result.compilation.status=CppPhaseStatus::timed_out;return;}merge_phase(result.compilation,run_phase(compiler,link,result.run_directory,compiler_input,L"link",remaining(),request.output_limit_bytes,true,stop,true),request.output_limit_bytes);
    if(result.compilation.succeeded()){result.artifact=executable;result.target_type="EXECUTABLE";result.execution=run_phase(executable,{},result.run_directory,input,L"execution",request.execution_timeout,request.output_limit_bytes,false,stop);}
}
void run_cmake(const CppRunRequest& request,CppRunResult& result,const std::filesystem::path& input,const std::filesystem::path& compiler_input,std::stop_token stop){
    validate_request_project(request);const auto workspace=cmake_workspace(request);const auto shared_lock=project_mutex(workspace);std::unique_lock<std::timed_mutex> lock(*shared_lock,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10)))if(stop.stop_requested()){auto& phase=request.action==CppRunRequest::Action::execute?result.execution:request.action==CppRunRequest::Action::build?result.compilation:result.configuration;phase.status=CppPhaseStatus::cancelled;return;}
    result.project_fingerprint=fingerprint(request);result.configuration_fingerprint=fingerprint(request,true);const auto& build_directory=workspace;const auto source=workspace/L"p",state_file=workspace/L"state.json";
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
        std::vector<std::wstring> arguments{L"--fresh",L"-S",source.wstring(),L"-B",build_directory.wstring(),L"-G",L"Ninja",L"-DCMAKE_MAKE_PROGRAM="+ninja.wstring(),L"-DCMAKE_CXX_COMPILER="+compiler.wstring(),L"-DCMAKE_C_COMPILER="+compiler.wstring(),L"-DCMAKE_RC_COMPILER="+std::wstring(build::educational_resource_compiler),L"-DCMAKE_BUILD_TYPE="+std::wstring(winrt::to_hstring(request.cmake->build_type)),L"-DCMAKE_CXX_STANDARD="+std::to_wstring(build::cpp_standard),L"-DCMAKE_CXX_STANDARD_REQUIRED=ON",L"-DCMAKE_CXX_EXTENSIONS=OFF",L"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",L"-DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX="+language_rules.wstring()};
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
        result.configuration=run_phase(cmake,arguments,result.run_directory,compiler_input,L"configuration",request.compile_timeout,request.output_limit_bytes,true,stop,true);
        if(!result.configuration.succeeded())return;if(!std::filesystem::is_regular_file(build_directory/L"build.ninja"))throw std::runtime_error("CMake did not generate real Ninja rules");
        const auto target=cmake_target(workspace,*request.cmake);result.target_type=target.type;saved.Insert(L"configuration",JsonValue::CreateStringValue(winrt::to_hstring(result.configuration_fingerprint)));save_json(state_file,saved);availability=inspect_unlocked(request);
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
        if(saved.HasKey(L"artifact")&&saved.HasKey(L"artifactSha256")){const auto old=contained(build_directory,project_path(json_text(saved,L"artifact")));const auto current_target=cmake_target(workspace,*request.cmake);
            // An unchanged Ninja build cannot certify a tampered old binary.
            // Remove only that proven changed generated artifact, owned by the
            // selected File API target, so Ninja must produce it again.
            if(std::ranges::find(current_target.artifacts,old)!=current_target.artifacts.end()&&std::filesystem::is_regular_file(old)&&state::sha256(state::read_bytes(old,128*1024*1024))!=json_text(saved,L"artifactSha256"))std::filesystem::remove(old);}
        for(const auto key:{L"built",L"artifact",L"artifactSha256"})if(saved.HasKey(key))saved.Remove(key);save_json(state_file,saved);
        result.compilation=run_phase(cmake,{L"--build",build_directory.wstring(),L"--target",std::wstring(winrt::to_hstring(request.cmake->target)),L"--parallel",L"2"},result.run_directory,compiler_input,L"build",request.compile_timeout,request.output_limit_bytes,true,stop,true);
        if(!result.compilation.succeeded())return;const auto target=cmake_target(workspace,*request.cmake);result.target_type=target.type;
        if(target.type=="EXECUTABLE"){for(const auto& artifact:target.artifacts)if(executable64(artifact)){result.artifact=artifact;break;}if(result.artifact.empty())throw std::runtime_error("The selected CMake executable did not produce a native x64 artifact inside its build directory");
            saved.Insert(L"built",JsonValue::CreateStringValue(winrt::to_hstring(result.project_fingerprint)));saved.Insert(L"artifact",JsonValue::CreateStringValue(result.artifact.lexically_relative(build_directory).generic_wstring()));saved.Insert(L"artifactSha256",JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(state::read_bytes(result.artifact,128*1024*1024)))));save_json(state_file,saved);}
        availability=inspect_unlocked(request);
    }
    if(action==CppRunRequest::Action::build)return;
    if(!availability.built)throw std::runtime_error("Build the executable of the current project revision before running it");
    result.compilation.status=CppPhaseStatus::completed;result.compilation.exit_code=0;result.artifact=availability.artifact;result.target_type=availability.target_type;result.executable=request.cmake->target;
    result.execution=run_phase(result.artifact,{},result.run_directory,input,L"execution",request.execution_timeout,request.output_limit_bytes,false,stop);
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
std::vector<CppCompilerDiagnostic> compilation_errors(std::string_view stream,const std::filesystem::path& directory) {
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
            const bool primary=(line[offset]=='C'&&number<4000)||(line[offset]=='D'&&number<9000)||prefix==3;
            if(primary) {
                CppCompilerDiagnostic diagnostic;diagnostic.code=std::string(line.substr(offset,prefix+4));
                const auto header=line.substr(0,offset);const auto close=header.rfind(')');
                const auto open=close==header.npos?header.npos:header.rfind('(',close);
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
       request.action!=CppRunRequest::Action::compile_run||
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
        if(!expected||!actual||*expected!=*actual)
            return fail("La versión mínima de CMake declarada en el proyecto no coincide.");
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
    for(const auto& [path,digest]:required.source_integrity) {
        const auto found=std::ranges::find_if(request.project_files,
            [&](const auto& file){return file.first==path;});
        if(found==request.project_files.end()||
           state::sha256(found->second)!=digest)
            return fail("Se modificó un archivo original fuera de la reparación autorizada: "+path);
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
curriculum::FreshExecution capture_cpp_execution(const CppRunRequest& request,const CppRunResult& result) {
    curriculum::FreshExecution fresh;
    if(request.request_id!=result.request_id||request.revision!=result.revision||request.diagnostic
        ||(request.action!=CppRunRequest::Action::compile_run&&request.action!=CppRunRequest::Action::execute)
        ||!result.error_utf8.empty()
        ||(request.cmake&&result.executable!=std::optional<std::string>{request.cmake->target}))return fresh;
    fresh.source_revision=result.revision;
    fresh.compilation_succeeded=result.compilation.succeeded()
        &&(result.configuration.status==CppPhaseStatus::not_started||result.configuration.succeeded());
    fresh.execution_succeeded=result.execution.succeeded();
    fresh.execution_completed=result.execution.status==CppPhaseStatus::completed
        &&result.execution.exit_code&&*result.execution.exit_code<=255;
    if(result.execution.exit_code&&*result.execution.exit_code<=255)fresh.exit_code=static_cast<int>(*result.execution.exit_code);
    fresh.standard_input=request.stdin_utf8;fresh.standard_output=result.execution.stdout_bytes;
    fresh.standard_error=result.execution.stderr_bytes;fresh.executable=result.executable;
    if(request.cmake_graph_check) {
        const auto& required=*request.cmake_graph_check;
        fresh.cmake_graph_correct=evaluate_cmake_graph(
            request,result,required).correct;
        if(fresh.cmake_graph_correct&&required.option_selection)
            fresh.cmake_graph_correct=verified_default_option_probe(
                request,result,required);
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
                probe.compilation.succeeded()&&probe.execution.succeeded()&&
                probe.execution.stdout_bytes.empty()&&
                // A student function may exit(0) before all comparisons
                // run. Only the generated main emits this final witness.
                probe.execution.stderr_bytes==
                    curriculum::named_rule_probe_completion_marker;
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
       request.action!=CppRunRequest::Action::compile_run||
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

CppRunResult run_cpp(const CppRunRequest& request, std::stop_token stop) {
    CppRunResult result;
    result.request_id = request.request_id;
    result.revision = request.revision;
    if(request.diagnostic)result.diagnostic.emplace();
    try {
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
        write_exact(result.run_directory / L"toolchain.txt", "MSVC=14.51.36231\nSDK=" +
            utf8(build::educational_sdk_version) + "\ncompiler=" + utf8(compiler.wstring()) + "\n");
        if(request.diagnostic){
            if(request.diagnostic->project)run_project_diagnostic(request,result,compiler,compiler_input,stop);
            else run_diagnostic(request,result,compiler,compiler_input,stop);
            return result;
        }
        if(request.cmake){
            run_cmake(request,result,input,compiler_input,stop);
            complete_cmake_option_probe(request,result,stop);
            complete_named_rule_probe(request,result,stop);
            return result;
        }
        if(!request.project_files.empty()){run_multifile(request,result,input,compiler_input,stop);return result;}
        write_exact(source, request.source_utf8);
        const std::vector<std::wstring> arguments{
            L"/nologo", L"/EHsc", build::cpp_standard_option, L"/Zc:__cplusplus", L"/permissive-", L"/utf-8", L"/W4",
            L"/diagnostics:column", L"/Fe:" + executable.wstring(),
            L"/Fo:" + (result.run_directory / L"program.obj").wstring(), source.wstring(),
            L"/link", L"/INCREMENTAL:NO", L"/MACHINE:X64", L"/SUBSYSTEM:CONSOLE",
            L"/MANIFEST:EMBED", L"/MANIFESTUAC:level='asInvoker' uiAccess='false'"};
        // Localized MSVC diagnostics and Unicode paths need the same private
        // UTF-8 tool console as CMake. Program execution keeps raw binary I/O.
        result.compilation = run_phase(compiler, arguments, result.run_directory,
            compiler_input, L"compilation", request.compile_timeout,
            request.output_limit_bytes, true, stop, true);
        if (result.compilation.succeeded()) {
            result.artifact=executable;result.target_type="EXECUTABLE";
            result.execution = run_phase(executable, {}, result.run_directory, input,
                L"execution", request.execution_timeout, request.output_limit_bytes, false, stop);
        }
        complete_named_rule_probe(request,result,stop);
        complete_rejection_stream_probe(request,result,stop);
        complete_observation_probes(request,result,stop);
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
