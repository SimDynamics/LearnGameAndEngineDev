#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lgaed {

enum class ProfileId { Work, Codex };

[[nodiscard]] std::optional<ProfileId> parse_profile(std::wstring_view value) noexcept;
[[nodiscard]] std::wstring_view profile_name(ProfileId profile) noexcept;
[[nodiscard]] bool is_valid_qa_instance(std::wstring_view value) noexcept;

struct LaunchOptions {
    ProfileId profile;
    std::optional<std::wstring> qa_instance;
    bool smoke_test = false;
};

// Arguments exclude the executable name. Unknown or repeated switches fail closed.
[[nodiscard]] LaunchOptions parse_launch_options(
    std::span<const std::wstring_view> arguments, ProfileId default_profile);

struct ProfilePaths {
    std::filesystem::path root;
    std::filesystem::path progress;
    std::filesystem::path drafts;
    std::filesystem::path preferences;
    std::filesystem::path logs;
    std::filesystem::path projects;
    std::filesystem::path cache;
};

// Pure path construction: this function never creates or changes files.
[[nodiscard]] ProfilePaths make_profile_paths(
    const std::filesystem::path& local_app_data,
    ProfileId profile,
    std::wstring_view qa_instance = {});

} // namespace lgaed
