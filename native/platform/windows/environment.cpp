#include "platform/windows/environment.hpp"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace lgaed {
namespace {

struct TaskMemoryDeleter {
    void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
};

struct HandleCloser {
    void operator()(void* value) const noexcept {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

std::string utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
            "Cannot encode Windows path as UTF-8");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
            "Cannot encode Windows path as UTF-8");
    }
    return result;
}

std::wstring executable_path() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                "Cannot inspect executable path");
        }
        if (length < buffer.size()) {
            return {buffer.data(), length};
        }
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

WindowsEnvironment resolve_windows_environment(const LaunchOptions& options) {
    PWSTR allocated_path = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &allocated_path);
    const std::unique_ptr<wchar_t, TaskMemoryDeleter> path(allocated_path);
    if (FAILED(result) || !path) {
        throw std::system_error(static_cast<int>(result), std::system_category(),
            "Cannot resolve the current user's LocalAppData folder");
    }
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
            "Cannot inspect process permissions");
    }
    const std::unique_ptr<void, HandleCloser> token(raw_token);
    TOKEN_ELEVATION elevation{};
    DWORD length = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &length)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
            "Cannot inspect process elevation");
    }
    return {options, make_profile_paths(path.get(), options.profile,
        options.qa_instance.value_or(L"")), elevation.TokenIsElevated != 0};
}

void initialize_profile_directories(const ProfilePaths& paths) {
    for (const auto* directory : {&paths.progress, &paths.drafts, &paths.preferences,
             &paths.logs, &paths.projects, &paths.cache}) {
        std::filesystem::create_directories(*directory);
    }
}

void write_qa_window_record(const WindowsEnvironment& environment,
    unsigned long process_id, unsigned int dpi, bool smoke_test) {
    if (!environment.options.qa_instance) {
        return;
    }
    const auto filename = smoke_test ? std::wstring(L"smoke-result.txt")
        : L"instance-" + std::to_wstring(process_id) + L".txt";
    std::ofstream record(environment.paths.logs / filename, std::ios::binary | std::ios::trunc);
    record.exceptions(std::ios::badbit | std::ios::failbit);
    record << "status=window-painted\n"
        << "profile=" << utf8(profile_name(environment.options.profile)) << '\n'
        << "qa_instance=" << utf8(*environment.options.qa_instance) << '\n'
        << "pid=" << process_id << '\n'
        << "data_root=" << utf8(environment.paths.root.native()) << '\n'
        << "exe_path=" << utf8(executable_path()) << '\n'
        << "progress=" << utf8(environment.paths.progress.native()) << '\n'
        << "drafts=" << utf8(environment.paths.drafts.native()) << '\n'
        << "preferences=" << utf8(environment.paths.preferences.native()) << '\n'
        << "logs=" << utf8(environment.paths.logs.native()) << '\n'
        << "projects=" << utf8(environment.paths.projects.native()) << '\n'
        << "cache=" << utf8(environment.paths.cache.native()) << '\n'
        << "window_painted=1\n"
        << "pointer_bits=" << sizeof(void*) * 8 << '\n'
        << "architecture=x64\n"
        << "elevated=" << (environment.elevated ? 1 : 0) << '\n'
        << "dpi=" << dpi << '\n'
        << "named_resources=none\n";
    record.close();
}

} // namespace lgaed
