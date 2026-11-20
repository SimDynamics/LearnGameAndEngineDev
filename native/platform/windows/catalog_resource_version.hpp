#pragma once

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace lgaed::windows {

// The Windows file-change clock is independent of LastWriteTime. A resource
// update that restores the visible modification date and preserves length
// must still invalidate cached course-edition identity and lesson metadata.
// The stamp supplements full descriptor and state-layout authentication.
[[nodiscard]] inline std::int64_t catalog_resource_change_time(
        const std::filesystem::path& asset) {
    const auto handle = CreateFileW(asset.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("No se puede consultar el cambio del recurso curricular.");
    FILE_BASIC_INFO basic{};
    const auto queried = GetFileInformationByHandleEx(
        handle, FileBasicInfo, &basic, sizeof(basic));
    CloseHandle(handle);
    if(!queried)
        throw std::runtime_error("No se pudo verificar la fecha de cambio del recurso curricular.");
    return basic.ChangeTime.QuadPart;
}

} // namespace lgaed::windows
