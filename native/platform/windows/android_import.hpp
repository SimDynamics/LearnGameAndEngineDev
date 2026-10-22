#pragma once

#include "core/lesson_state.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace lgaed::state {
struct ImportFile {
    std::filesystem::path relative_path;
    std::uintmax_t bytes = 0;
    std::string sha256;
};
struct AndroidImportPreview {
    std::filesystem::path source;
    StateData data;
    std::vector<ImportFile> files;
    std::size_t preference_count = 0, project_revisions = 0;
    std::vector<std::string> notes;
};
// Select the already-exported app data folder containing shared_prefs/ and optional files/.
// No connection to a phone, extraction, change to the source or silent merge is performed.
[[nodiscard]] AndroidImportPreview inspect_android_export(const std::filesystem::path&);
// An untouched profile or one containing only native catalog/UI navigation may
// accept import; real progress, drafts and unknown domains never qualify.
[[nodiscard]] bool can_import_android_profile(const StateData&);
// The narrowly allowed native UI preferences survive the verified transaction.
void commit_android_import(const AndroidImportPreview&, const std::filesystem::path& profile_root);
} // namespace lgaed::state
