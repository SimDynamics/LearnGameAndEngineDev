#pragma once

#include "platform/windows/narration_worker.hpp"
#include "platform/windows/narration_storage.hpp"

namespace lgaed::windows {
struct SystemVoice { std::string id,name,language; };
[[nodiscard]] std::vector<SystemVoice> system_voices();
[[nodiscard]] bool system_voice_matches(std::string_view voice_language,std::string_view requested_language);
// A separate system-voice cache. SAPI produces the actual WAV and word events;
// this adapter never substitutes Kokoro or approximates word durations. New
// recordings share PCM by spoken text/installed voice, then project the real
// spoken boundaries to this call's source. Exact v1 entries remain read-only.
[[nodiscard]] NarrationAudio synthesize_system_speech(const NarrationSegment&,std::string_view language,
    std::string_view voice_id,const std::filesystem::path& output_directory,std::stop_token = {});
// Advance-generation misses share one configured ceiling across SAPI voices.
// Cache hits remain usable with a zero/full budget; foreground API above keeps
// its existing behavior. Every miss also preserves the free-space reserve.
[[nodiscard]] NarrationAudio synthesize_system_speech_with_budget(const NarrationSegment&,std::string_view language,
    std::string_view voice_id,const std::filesystem::path& output_directory,std::stop_token,std::uint64_t budget_bytes);
[[nodiscard]] NarrationStorageSnapshot system_speech_storage_snapshot(const std::filesystem::path& output_directory,
    std::uint64_t budget_bytes=std::numeric_limits<std::uint64_t>::max());
} // namespace lgaed::windows
