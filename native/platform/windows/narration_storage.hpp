#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>

namespace lgaed::windows {
// Same non-evicting audio ceiling as Android. Models and timing sidecars are
// not recordings; lowering the ceiling never removes an existing recording.
inline constexpr std::uint64_t narration_storage_reserve = 1024ull * 1024 * 1024;
struct NarrationStorageSnapshot {
    std::uint64_t stored_bytes=0,available_bytes=0,maximum_bytes=0,configured_bytes=0,effective_bytes=0;
};
inline NarrationStorageSnapshot narration_storage_snapshot(const std::filesystem::path& cache,
        std::uint64_t limit=std::numeric_limits<std::uint64_t>::max()) {
    NarrationStorageSnapshot result;
    std::error_code error;
    if(std::filesystem::is_directory(cache,error))for(const auto& entry:std::filesystem::directory_iterator(cache,error)) {
        if(error)break;
        const auto extension=entry.path().extension();
        if((extension==L".wav"||extension==L".ogg")&&entry.is_regular_file(error)) {
            const auto size=entry.file_size(error);
            if(!error)result.stored_bytes+=std::min<std::uint64_t>(size,std::numeric_limits<std::uint64_t>::max()-result.stored_bytes);
        }
    }
    auto volume=cache;
    while(!volume.empty()&&!std::filesystem::exists(volume,error))volume=volume.parent_path();
    const auto space=std::filesystem::space(volume,error);
    if(!error)result.available_bytes=space.available;
    const auto sum=result.stored_bytes+std::min(result.available_bytes,std::numeric_limits<std::uint64_t>::max()-result.stored_bytes);
    result.maximum_bytes=sum>narration_storage_reserve?sum-narration_storage_reserve:0;
    result.configured_bytes=limit==std::numeric_limits<std::uint64_t>::max()?result.maximum_bytes:limit;
    result.effective_bytes=std::min(result.configured_bytes,result.maximum_bytes);
    return result;
}
}
