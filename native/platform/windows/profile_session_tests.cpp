#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<class Function>
bool rejected(Function&& action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit Handle(HANDLE handle) : value(handle) {}
};

void write_new_file(const std::filesystem::path& path, std::string_view contents) {
    state::require_safe_path(path);
    Handle file(CreateFileW(state::native_io_path(path).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    check(file.value != INVALID_HANDLE_VALUE, "Cannot create a new test-owned file");
    state::require_unredirected_handle(file.value, path);
    DWORD written = 0;
    check(contents.size() <= MAXDWORD && WriteFile(file.value, contents.data(),
        static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size() && FlushFileBuffers(file.value),
        "Cannot write the isolated fixture");
}

struct Entry {
    std::filesystem::path relative;
    bool directory;
    std::vector<std::uint8_t> bytes;
    std::filesystem::file_time_type modified;
    bool operator==(const Entry&) const = default;
};

std::vector<Entry> snapshot(const std::filesystem::path& root) {
    std::vector<Entry> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const bool directory = entry.is_directory();
        check(directory || entry.is_regular_file(), "Unexpected fixture entry type");
        // directory_entry may retain FindFirstFile enumeration metadata from
        // creation. Query the path afresh so snapshots compare current values,
        // including strict file mtimes, rather than two cached directory views.
        result.push_back({entry.path().lexically_relative(root), directory,
            directory ? std::vector<std::uint8_t>{} : state::read_bytes(entry.path()),
            std::filesystem::last_write_time(entry.path())});
    }
    std::sort(result.begin(), result.end(), [](const Entry& left, const Entry& right) {
        return left.relative < right.relative;
    });
    return result;
}

void describe_entry(const char* label, const Entry* entry) {
    std::cerr << "  " << label << ": ";
    if (!entry) {
        std::cerr << "missing\n";
        return;
    }
    std::cerr << (entry->directory ? "directory" : "file")
        << " bytes=" << entry->bytes.size()
        << " modifiedTicks=" << entry->modified.time_since_epoch().count();
    if (!entry->directory) std::cerr << " sha256=" << state::sha256(entry->bytes);
    std::cerr << '\n';
}

void require_same_snapshot(const std::vector<Entry>& before, const std::vector<Entry>& after,
    const char* phase) {
    if (before == after) return;
    std::cerr << "Snapshot mismatch during " << phase << ": beforeEntries=" << before.size()
        << " afterEntries=" << after.size() << '\n';
    std::size_t left = 0, right = 0;
    while (left < before.size() || right < after.size()) {
        const Entry* previous = left < before.size() ? &before[left] : nullptr;
        const Entry* current = right < after.size() ? &after[right] : nullptr;
        if (previous && (!current || previous->relative < current->relative)) {
            current = nullptr;
            ++left;
        } else if (current && (!previous || current->relative < previous->relative)) {
            previous = nullptr;
            ++right;
        } else {
            ++left;
            ++right;
            if (*previous == *current) continue;
        }
        std::cerr << "  path=" << (previous ? previous : current)->relative.generic_string() << '\n';
        describe_entry("before", previous);
        describe_entry("after", current);
    }
    throw std::runtime_error(std::string(phase)
        + ": QA preparation or the isolated fixture changed; see per-path diagnostics");
}

void physical_handle_guard(const std::filesystem::path& base) {
    const auto directory = base / L"physical-guard";
    check(std::filesystem::create_directory(directory), "Guard fixture directory already exists");
    Handle folder(CreateFileW(state::native_io_path(directory).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    check(folder.value != INVALID_HANDLE_VALUE, "Cannot open the isolated directory handle");
    state::require_unredirected_handle(folder.value, directory);
    check(rejected([&] { state::require_unredirected_handle(folder.value, base / L"different-directory"); }),
        "A directory handle was accepted for a different requested destination");

    // Non-ASCII names exercise the wide Windows API without depending on the
    // compiler's source encoding or the console's current code page.
    const auto file_path = directory / L"progreso-\u00f1-\u6f22.pending";
    const auto other_path = directory / L"other-profile.pending";
    Handle file(CreateFileW(state::native_io_path(file_path).c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    check(file.value != INVALID_HANDLE_VALUE, "Cannot create the isolated pending file");
    check(rejected([&] { state::require_unredirected_handle(file.value, other_path); }),
        "A file handle was accepted for a different requested destination");
    LARGE_INTEGER size{};
    check(GetFileSizeEx(file.value, &size) && size.QuadPart == 0
        && !std::filesystem::exists(other_path),
        "Rejecting a redirected destination wrote payload or created the requested alias");

    state::require_unredirected_handle(file.value, file_path);
    state::require_unredirected_handle(file.value, state::native_io_path(file_path));
    constexpr char payload[] = "Isolated progress fixture; no personal data.\n";
    DWORD written = 0;
    check(WriteFile(file.value, payload, static_cast<DWORD>(sizeof(payload) - 1), &written, nullptr)
        && written == sizeof(payload) - 1 && FlushFileBuffers(file.value),
        "The accepted caller-owned handle is no longer usable");
    const auto before = state::read_bytes(file_path);
    const auto modified = std::filesystem::last_write_time(file_path);
    check(rejected([&] { state::require_unredirected_handle(file.value, other_path); }),
        "A populated file handle bypassed the destination guard");
    check(state::read_bytes(file_path) == before
        && std::filesystem::last_write_time(file_path) == modified,
        "The destination guard changed an existing file");

    check(rejected([&] { state::require_unredirected_handle(nullptr, file_path); }),
        "A null handle was accepted as a physical file");
    check(rejected([&] { state::require_unredirected_handle(INVALID_HANDLE_VALUE, file_path); }),
        "An invalid handle was accepted as a physical file");
    std::cout << "PASS physical directory/file handles, Unicode and extended paths, destination mismatch, invalid handles and preserved bytes\n";
}

void qa_never_migrates(const std::filesystem::path& base) {
    const auto directory = base / L"qa-does-not-migrate";
    const auto local = directory / L"local";
    const auto roaming = directory / L"roaming";
    check(std::filesystem::create_directory(directory), "QA fixture directory already exists");
    for (const auto profile : {ProfileId::Work, ProfileId::Codex}) {
        const auto legacy = make_profile_paths(local, profile).root;
        const auto personal = roaming / L"LearnGameAndEngineDev" / profile_name(profile);
        check(std::filesystem::create_directories(legacy / L"state" / L"active"),
            "Legacy fixture directory already exists");
        check(std::filesystem::create_directories(personal), "Roaming fixture directory already exists");
        // Deliberately not a valid StateStore payload: QA must not inspect,
        // repair or import any personal state, including malformed state.
        write_new_file(legacy / L"state" / L"active" / L"state.bin", "Isolated malformed legacy sentinel\n");
        write_new_file(personal / L"keep.bin", "Isolated existing roaming sentinel\n");
    }
    const auto before = snapshot(directory);
    // Establish that fixture creation/readback has settled before attributing
    // any difference to QA. Preserve strict directory and file timestamps.
    require_same_snapshot(before, snapshot(directory), "fixture baseline before QA");
    for (const auto profile : {ProfileId::Work, ProfileId::Codex}) {
        WindowsEnvironment environment{
            {profile, std::wstring(L"profile-session-fixture"), false},
            make_profile_paths(local, roaming, profile, L"profile-session-fixture"), false};
        check(!std::filesystem::exists(environment.paths.root), "The QA profile was unexpectedly prepared");
        check(!prepare_windows_profile(environment), "QA acquired a personal-profile session lease");
        require_same_snapshot(before, snapshot(directory),
            profile == ProfileId::Work ? "Work QA preparation" : "Codex QA preparation");
        environment.options.smoke_test = true;
        check(!prepare_windows_profile(environment), "A QA smoke launch acquired a personal-profile session lease");
        require_same_snapshot(before, snapshot(directory),
            profile == ProfileId::Work ? "Work QA smoke preparation" : "Codex QA smoke preparation");
        check(!std::filesystem::exists(environment.paths.root), "QA preparation created or migrated a profile");
    }
    require_same_snapshot(before, snapshot(directory), "final QA preservation");
    std::cout << "PASS QA Work/Codex and smoke preparation bypass personal profiles without leases, migration, directories or changed bytes\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Profile-session tests need one existing absolute fixture-output directory");
        const auto output = std::filesystem::path(argv[1]).lexically_normal();
        check(output.is_absolute() && output != output.root_path() && std::filesystem::is_directory(output),
            "The fixture-output directory must be absolute, existing and below the volume root");
        state::require_safe_path(output);
        const auto base = output / (L"profile-session-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(base), "Tests require a new isolated fixture directory");
        // Keep every fixture for diagnosis. Never remove or overwrite another
        // run, a real profile, an existing state file or a user folder.
        physical_handle_guard(base);
        qa_never_migrates(base);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL profile session: " << error.what() << '\n';
        return 1;
    }
}
