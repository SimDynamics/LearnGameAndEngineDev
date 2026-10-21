#pragma once

#include "core/profile.hpp"

namespace lgaed {

struct WindowsEnvironment {
    LaunchOptions options;
    ProfilePaths paths;
    bool elevated = false;
};

[[nodiscard]] WindowsEnvironment resolve_windows_environment(const LaunchOptions& options);
void initialize_profile_directories(const ProfilePaths& paths);

// Diagnostic records are restricted to explicit, isolated QA launches.
void write_qa_window_record(const WindowsEnvironment& environment,
    unsigned long process_id, unsigned int dpi, bool smoke_test);

} // namespace lgaed
