#include "core/profile.hpp"

#include <algorithm>
#include <stdexcept>

namespace lgaed {
namespace {

bool is_reserved_device_name(std::wstring_view value) noexcept {
    // QA names are ASCII, so locale-dependent case conversion is unnecessary.
    const auto ascii_upper = [](wchar_t character) {
        return character >= L'a' && character <= L'z'
            ? static_cast<wchar_t>(character - L'a' + L'A') : character;
    };
    const auto matches = [value, ascii_upper](std::wstring_view expected) {
        if (value.size() != expected.size()) {
            return false;
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (ascii_upper(value[i]) != expected[i]) {
                return false;
            }
        }
        return true;
    };
    if (matches(L"CON") || matches(L"PRN") || matches(L"AUX") || matches(L"NUL")) {
        return true;
    }
    if (value.size() == 4 && value[3] >= L'1' && value[3] <= L'9') {
        return (ascii_upper(value[0]) == L'C' && ascii_upper(value[1]) == L'O'
                && ascii_upper(value[2]) == L'M')
            || (ascii_upper(value[0]) == L'L' && ascii_upper(value[1]) == L'P'
                && ascii_upper(value[2]) == L'T');
    }
    return false;
}

} // namespace

std::optional<ProfileId> parse_profile(std::wstring_view value) noexcept {
    if (value == L"Work") {
        return ProfileId::Work;
    }
    if (value == L"Codex") {
        return ProfileId::Codex;
    }
    return std::nullopt;
}

std::wstring_view profile_name(ProfileId profile) noexcept {
    return profile == ProfileId::Work ? L"Work" : L"Codex";
}

bool is_valid_qa_instance(std::wstring_view value) noexcept {
    if (value.empty() || value.size() > 64 || is_reserved_device_name(value)) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'a' && character <= L'z')
            || (character >= L'A' && character <= L'Z')
            || (character >= L'0' && character <= L'9')
            || character == L'_' || character == L'-';
    });
}

LaunchOptions parse_launch_options(
    std::span<const std::wstring_view> arguments, ProfileId default_profile) {
    LaunchOptions options{default_profile, std::nullopt, false};
    bool profile_seen = false;
    bool qa_seen = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto argument = arguments[i];
        if (argument == L"--profile") {
            if (profile_seen || ++i == arguments.size()) {
                throw std::invalid_argument("--profile requires one Work or Codex value");
            }
            const auto profile = parse_profile(arguments[i]);
            if (!profile) {
                throw std::invalid_argument("Profile must be Work or Codex");
            }
            options.profile = *profile;
            profile_seen = true;
        } else if (argument == L"--qa-instance") {
            if (qa_seen || ++i == arguments.size() || !is_valid_qa_instance(arguments[i])) {
                throw std::invalid_argument("--qa-instance requires a safe unique ASCII token");
            }
            options.qa_instance = std::wstring(arguments[i]);
            qa_seen = true;
        } else if (argument == L"--smoke-test") {
            if (options.smoke_test) {
                throw std::invalid_argument("--smoke-test may only be specified once");
            }
            options.smoke_test = true;
        } else {
            throw std::invalid_argument("Unknown launch argument");
        }
    }
    if (options.smoke_test && !options.qa_instance) {
        throw std::invalid_argument("--smoke-test requires an isolated --qa-instance");
    }
    return options;
}

ProfilePaths make_profile_paths(const std::filesystem::path& local_app_data,
    ProfileId profile, std::wstring_view qa_instance) {
    if (local_app_data.empty() || !local_app_data.is_absolute()) {
        throw std::invalid_argument("LocalAppData must be an absolute path");
    }
    auto root = local_app_data / L"LearnGameAndEngineDev" / profile_name(profile);
    if (!qa_instance.empty()) {
        if (!is_valid_qa_instance(qa_instance)) {
            throw std::invalid_argument("Invalid QA instance path component");
        }
        root /= L"qa";
        root /= qa_instance;
    }
    return {root, root / L"progress", root / L"drafts", root / L"preferences",
        root / L"logs", root / L"projects", root / L"cache"};
}

ProfilePaths make_profile_paths(const std::filesystem::path& local_app_data,
    const std::filesystem::path& roaming_app_data, ProfileId profile,
    std::wstring_view qa_instance) {
    auto paths = make_profile_paths(local_app_data, profile, qa_instance);
    if (roaming_app_data.empty() || !roaming_app_data.is_absolute())
        throw std::invalid_argument("RoamingAppData must be an absolute path");
    if (!qa_instance.empty()) return paths;
    paths.root = roaming_app_data / L"LearnGameAndEngineDev" / profile_name(profile);
    paths.progress = paths.root / L"progress";
    paths.drafts = paths.root / L"drafts";
    paths.preferences = paths.root / L"preferences";
    return paths;
}

} // namespace lgaed
