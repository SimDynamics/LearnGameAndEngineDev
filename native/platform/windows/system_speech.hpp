#pragma once

#include "platform/windows/narration_worker.hpp"

namespace lgaed::windows {
struct SystemVoice { std::string id,name,language; };
[[nodiscard]] std::vector<SystemVoice> system_voices();
[[nodiscard]] bool system_voice_matches(std::string_view voice_language,std::string_view requested_language);
// A separate system-voice cache. SAPI produces the actual WAV and word events;
// this adapter never substitutes Kokoro or approximates word durations.
[[nodiscard]] NarrationAudio synthesize_system_speech(const NarrationSegment&,std::string_view language,
    std::string_view voice_id,const std::filesystem::path& output_directory,std::stop_token = {});
} // namespace lgaed::windows
