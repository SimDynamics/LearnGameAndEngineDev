#pragma once
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace lgaed::windows {
struct SpeechRange {std::size_t start=0,end=0;};
struct NarrationSegment {
    std::string text,prepared;std::size_t start=0,end=0;
    // Paired offsets use UTF-16, as on Android. Displayed ranges address text;
    // spoken ranges address the annotation-free spoken text actually supplied
    // to a native system voice. A source token can own several spoken words.
    std::string spoken;
    std::vector<SpeechRange> spoken_ranges,displayed_ranges;
};
struct NarrationTiming {std::size_t start=0,end=0;double start_ms=0,end_ms=0;};
struct NarrationAudio {
    std::string key,source,spoken,provider;
    std::filesystem::path wav;
    std::vector<NarrationTiming> timings;
    double duration_ms=0;bool cache_hit=false;
};
// Only inference and the shared linguistic pipeline live in the host worker.
// Playback, state ownership and user interaction remain native Win32.
class NarrationWorker {
public:
    explicit NarrationWorker(std::filesystem::path runtime);
    ~NarrationWorker();
    NarrationWorker(const NarrationWorker&)=delete;NarrationWorker& operator=(const NarrationWorker&)=delete;
    [[nodiscard]] std::vector<NarrationSegment> plan(std::string_view text,std::string_view language,std::stop_token={});
    [[nodiscard]] NarrationAudio prepare(const NarrationSegment&,std::string_view language,std::string_view voice,
        const std::filesystem::path& output,unsigned threads,std::stop_token={});
    [[nodiscard]] std::string identity(std::string_view page,std::string_view language,std::string_view voice,
        const std::vector<NarrationSegment>&,std::stop_token={},std::string_view engine="KOKORO");
    // Raw curricular JSON travels as a string: WinRT's floating-point JSON
    // representation must not round exact decimal/integer answer contracts.
    [[nodiscard]] std::string describe_math(std::string_view raw_block,std::string_view language,std::stop_token={});
    [[nodiscard]] std::string validate_math(std::string_view raw_block,std::string_view raw_responses,
        std::string_view language,std::stop_token={});
    [[nodiscard]] std::string math_editor(std::string_view request_json,std::stop_token={});
    [[nodiscard]] std::string math_keyboard(std::string_view request_json,std::stop_token={});
    [[nodiscard]] std::string math_calculate(std::string_view request_json,std::stop_token={});
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
