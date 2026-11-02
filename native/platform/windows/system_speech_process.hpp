#pragma once
#include "platform/windows/narration_worker.hpp"
#include <memory>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace lgaed::windows {
struct SystemSpeechProcessResult {
    NarrationAudio audio;
    unsigned processors=0;
    unsigned long process_id=0;
};
// A private, one-request native SAPI child. Its job/process hard affinity is
// bounded before it runs. Neither the application's affinity nor a shared
// Windows speech service is modified. Finished recordings use the same SAPI
// cache as foreground playback.
class SystemSpeechProcess {
public:
    explicit SystemSpeechProcess(std::filesystem::path host={});
    ~SystemSpeechProcess();
    SystemSpeechProcess(const SystemSpeechProcess&)=delete;
    SystemSpeechProcess& operator=(const SystemSpeechProcess&)=delete;
    [[nodiscard]] SystemSpeechProcessResult prepare(const NarrationSegment&,std::string_view language,
        std::string_view voice,const std::filesystem::path& cache,std::stop_token={},
        std::uint64_t maximum_audio_bytes=std::numeric_limits<std::uint64_t>::max());
    // Terminal, non-waiting cancellation of this object's private child only.
    void shutdown() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Entry point used only by the separately installed native helper executable.
int run_system_speech_host(std::span<const std::wstring_view>) noexcept;
}
