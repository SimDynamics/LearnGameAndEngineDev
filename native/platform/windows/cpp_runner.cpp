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
    const bool configure=action==CppRunRequest::Action::configure||(action==CppRunRequest::Action::compile_run&&!availability.configured);
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
        result.configuration=run_phase(cmake,arguments,result.run_directory,compiler_input,L"configuration",request.compile_timeout,request.output_limit_bytes,true,stop,true);
        if(!result.configuration.succeeded())return;if(!std::filesystem::is_regular_file(build_directory/L"build.ninja"))throw std::runtime_error("CMake did not generate real Ninja rules");
        const auto target=cmake_target(workspace,*request.cmake);result.target_type=target.type;saved.Insert(L"configuration",JsonValue::CreateStringValue(winrt::to_hstring(result.configuration_fingerprint)));save_json(state_file,saved);availability=inspect_unlocked(request);
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

} // namespace

bool expected_compile_diagnostic(const CppRunRequest& request,const CppRunResult& result) try {
    if(!request.diagnostic||!result.diagnostic||!request.project_files.empty()||!request.source_files.empty()||!request.stdin_utf8.empty()||request.cmake||
        request.action!=CppRunRequest::Action::compile_run||request.request_id!=result.request_id||request.revision!=result.revision||
        !result.error_utf8.empty()||result.run_directory.empty()||!result.artifact.empty()||result.executable||!result.target_type.empty())return false;
    const auto unused=[](const CppPhaseResult& phase){return phase.status==CppPhaseStatus::not_started&&!phase.exit_code&&phase.stdout_bytes.empty()&&phase.stderr_bytes.empty();};
    if(!unused(result.execution)||!unused(result.configuration))return false;
    const auto& objective=*request.diagnostic;const auto& evidence=*result.diagnostic;
    if(objective.expected_error_code.size()!=5||objective.expected_error_code.front()!='C'||
        diagnostic_number(std::string_view(objective.expected_error_code).substr(1))<2000||
        diagnostic_number(std::string_view(objective.expected_error_code).substr(1))>=4000||
        !objective.first_line||objective.last_line<objective.first_line||!evidence.control.succeeded()||
        evidence.source_sha256!=state::sha256(request.source_utf8)||evidence.control_sha256!=state::sha256(objective.control_source_utf8)||
        result.compilation.status!=CppPhaseStatus::completed||!result.compilation.exit_code||
        !*result.compilation.exit_code||*result.compilation.exit_code>255)return false;
    if(!compilation_errors(evidence.control,result.run_directory/L"control").empty())return false;
    const auto errors=compilation_errors(result.compilation,result.run_directory/L"candidate");
    if(errors.empty()||errors!=evidence.errors)return false;
    const auto source=result.run_directory/L"candidate/main.cpp";
    return std::ranges::all_of(errors,[&](const auto& item){return item.code==objective.expected_error_code&&item.column&&
        item.line>=objective.first_line&&item.line<=objective.last_line&&same_diagnostic_source(item.source,source);});
}catch(const std::exception&){return false;}catch(const winrt::hresult_error&){return false;}

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
        if(request.diagnostic&&(!request.project_files.empty()||!request.source_files.empty()||!request.stdin_utf8.empty()||
                request.cmake||request.diagnostic->control_source_utf8.size()>4*1024*1024))
            throw std::invalid_argument("A diagnostic objective requires one bounded translation unit without standard input");
        result.run_directory = create_run_directory(request);
        const auto source = result.run_directory / L"main.cpp";
        const auto input = result.run_directory / L"stdin.bin";
        const auto compiler_input = result.run_directory / L"compiler-stdin.bin";
        const auto executable = result.run_directory / L"program.exe";
        const std::filesystem::path vc{build::educational_vc_root};
        const auto compiler = vc / L"bin/Hostx64/x64/cl.exe";
        if (!std::filesystem::is_regular_file(compiler))
            throw std::runtime_error("Pinned educational MSVC compiler is unavailable");
        write_exact(input, request.stdin_utf8);
        write_exact(compiler_input, "");
        write_exact(result.run_directory / L"toolchain.txt", "MSVC=14.51.36231\nSDK=" +
            utf8(build::educational_sdk_version) + "\ncompiler=" + utf8(compiler.wstring()) + "\n");
        if(request.cmake){run_cmake(request,result,input,compiler_input,stop);return result;}
        if(!request.project_files.empty()){run_multifile(request,result,input,compiler_input,stop);return result;}
        if(request.diagnostic){run_diagnostic(request,result,compiler,compiler_input,stop);return result;}
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
