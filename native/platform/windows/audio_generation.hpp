#pragma once
#include "core/audio_schedule.hpp"
#include "platform/windows/narration_worker.hpp"
#include "core/lesson_state.hpp"
#include <map>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>

namespace lgaed::windows {
struct AudioGenerationSelection {
    curriculum::CatalogLocation location;
    std::optional<std::size_t> page;
    std::string language="es",ui_language="es",engine="KOKORO";
    std::map<std::string,std::string> voices;
    std::set<std::string> paused_courses;
    bool enabled=true;
    std::uint64_t maximum_audio_bytes=std::numeric_limits<std::uint64_t>::max();
    bool operator==(const AudioGenerationSelection&)const=default;
};
[[nodiscard]] AudioGenerationSelection audio_generation_selection(const state::StateData&,
    curriculum::CatalogLocation,std::optional<std::size_t> = {});
struct AudioGenerationStatus {
    std::string page,voice,error;std::size_t completed=0;bool running=false;
    std::string engine,language;
    std::size_t total=0;
    // finished covers the eligible inventory for the current language/voices;
    // explicitly paused courses remain paused. waiting is an actual event wait,
    // not a running synthesis. scans permits diagnostics without exposing text.
    bool finished=false,waiting=false;
    std::uint64_t scans=0;
};
// One app-owned cancellable producer. It survives catalog/lesson navigation and
// uses the existing worker/cache; it never owns playback or reading bookmarks.
class AudioGeneration {
public:
    AudioGeneration(std::shared_ptr<NarrationWorker>,std::filesystem::path content,
        std::filesystem::path cache,std::filesystem::path profile);
    ~AudioGeneration();
    void update(AudioGenerationSelection);
    [[nodiscard]] AudioGenerationStatus status()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
