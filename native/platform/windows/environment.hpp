#pragma once

#include "core/profile.hpp"
#include <memory>

namespace lgaed {

struct WindowsEnvironment {
    LaunchOptions options;
    ProfilePaths paths;
    bool elevated = false;
};

[[nodiscard]] WindowsEnvironment resolve_windows_environment(const LaunchOptions& options);
void initialize_profile_directories(const ProfilePaths& paths);

// Keep this lease alive until the application exits. Normal profiles are
// validated physically, locked and migrated before any StateStore is loaded.
// Explicit QA launches never inspect or migrate a personal profile.
[[nodiscard]] std::shared_ptr<void> prepare_windows_profile(const WindowsEnvironment&);

// Diagnostic records are restricted to explicit, isolated QA launches.
void write_qa_window_record(const WindowsEnvironment& environment,
    unsigned long process_id, unsigned int dpi, bool smoke_test);

} // namespace lgaed
