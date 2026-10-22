#pragma once
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include <windows.h>
#include <functional>
#include <memory>

namespace lgaed::presentation {
struct LessonReaderSegmentSnapshot {
    std::string region,source,spoken;
    HWND target = nullptr;
    std::size_t block = 0,start = 0,end = 0;
    double duration_ms = 0;
    bool available = false,feedback = false,correct = false;
    std::vector<windows::NarrationTiming> timings;
};
struct LessonReaderSnapshot {
    bool playing = false,allocated = false,planned = false,preparing = false,finished = false,waiting_gate = false;
    std::size_t index = 0;
    std::int64_t offset = 0,heard_through = 0;
    std::string error,key;
    std::vector<LessonReaderSegmentSnapshot> segments;
};
class LessonReader {
public:
    LessonReader(HWND lesson,const WindowsEnvironment&,std::shared_ptr<windows::NarrationWorker>,std::function<void()> settings);
    ~LessonReader();
    LessonReader(const LessonReader&)=delete;LessonReader& operator=(const LessonReader&)=delete;
    void content_changed();
    void feedback(std::size_t block,bool correct);
    void toggle();
    void pause();
    void settings_changed();
    void seek_seconds(int);
    void restart();
    [[nodiscard]] HWND bar()const;
    // Observes UI-thread state only. Does not tick media, advance reading or save.
    [[nodiscard]] LessonReaderSnapshot snapshot()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
