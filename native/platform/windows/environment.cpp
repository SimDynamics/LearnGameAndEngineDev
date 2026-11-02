#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/profile_migration.hpp"

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

std::filesystem::path known_folder(REFKNOWNFOLDERID id, DWORD flags) {
    PWSTR allocated_path = nullptr;
    const auto result = SHGetKnownFolderPath(id, flags, nullptr, &allocated_path);
    const std::unique_ptr<wchar_t, TaskMemoryDeleter> path(allocated_path);
    if (FAILED(result) || !path)
        throw std::runtime_error("No se pudo localizar AppData del usuario; no se ha elegido una carpeta alternativa.");
    return path.get();
}

// Known-folder flags control the returned name, not every subsequent file I/O
// performed by a packaged caller. Verify the opened directory as well.
void require_physical_directory(const std::filesystem::path& directory) {
    state::require_safe_path(directory);
    std::filesystem::create_directories(state::native_io_path(directory));
    const std::unique_ptr<void, HandleCloser> handle(CreateFileW(
        state::native_io_path(directory).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("No se pudo comprobar la carpeta de progreso.");
    state::require_unredirected_handle(handle.get(), directory);
}

bool personal_window_running(ProfileId profile) {
    struct Search { std::wstring title; bool found = false; } search{
        L"LearnGameAndEngineDev \u2014 " + std::wstring(profile_name(profile))};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& query = *reinterpret_cast<Search*>(parameter);
        wchar_t name[100]{};
        if (!GetClassNameW(window, name, 100) || std::wstring_view(name) != L"LearnGameAndEngineDev.NativeShell") return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;
        wchar_t title[256]{}; GetWindowTextW(window, title, 256);
        if (query.title == title) { query.found = true; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

struct ProfileLease {
    std::unique_ptr<void, HandleCloser> session, legacy_writer;
};

std::unique_ptr<void, HandleCloser> exclusive_file(const std::filesystem::path& path) {
    state::require_safe_path(path);
    std::unique_ptr<void, HandleCloser> handle(CreateFileW(state::native_io_path(path).c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Otra copia esta utilizando el progreso. Cierrala y vuelve a abrir la aplicacion.");
    state::require_unredirected_handle(handle.get(), path);
    return handle;
}

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
    const auto local = known_folder(FOLDERID_LocalAppData, options.qa_instance ? 0 : KF_FLAG_NO_PACKAGE_REDIRECTION);
    const auto roaming = known_folder(FOLDERID_RoamingAppData, KF_FLAG_NO_PACKAGE_REDIRECTION);
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
    return {options, make_profile_paths(local, roaming, options.profile,
        options.qa_instance.value_or(L"")), elevation.TokenIsElevated != 0};
}

std::shared_ptr<void> prepare_windows_profile(const WindowsEnvironment& environment) {
    if (environment.options.qa_instance) return {};
    if (personal_window_running(environment.options.profile))
        throw std::runtime_error("Ya hay otra copia de este perfil abierta. Cierrala antes de abrir la nueva version para conservar todo el progreso.");
    const auto& root = environment.paths.root;
    const auto legacy = environment.paths.cache.parent_path();
    require_physical_directory(root);
    auto lease = std::make_shared<ProfileLease>();
    lease->session = exclusive_file(root / L"profile-session.lock");
    // Old binaries do not know about Roaming or the session lease. Holding
    // their existing writer lock prevents them from saving a divergent copy.
    require_physical_directory(legacy / L"state");
    lease->legacy_writer = exclusive_file(legacy / L"state" / L"writer.lock");
    (void)windows::migrate_profile_state(legacy, root);
    return lease;
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
