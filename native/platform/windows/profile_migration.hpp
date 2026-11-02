#pragma once

#include <filesystem>
#include <string_view>

namespace lgaed::windows {

enum class ProfileMigrationResult { no_legacy_state, destination_preserved, migrated };

inline constexpr std::wstring_view profile_migration_marker_name =
    L".local-to-roaming-migration-v1.bin";

// The caller holds legacy/state/writer.lock exclusively for this call and the
// application's lifetime, and excludes other sessions using the destination.
// Copies only state/active, retaining its original bytes and historical files.
// No existing active directory is replaced or merged. Failed pending copies are
// retained; neither source data nor a previously published destination is erased.
[[nodiscard]] ProfileMigrationResult migrate_profile_state(
    const std::filesystem::path& legacy_profile_root,
    const std::filesystem::path& new_profile_root);

} // namespace lgaed::windows
