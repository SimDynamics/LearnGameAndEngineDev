#pragma once
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace lgaed::presentation {
// Layouts the compact floating controls inside client coordinates. A manually
// dragged position is retained and only clamped when the available area shrinks.
// This changes geometry only; it never updates playback or narration history.
void layout_lesson_reader_bar(HWND bar,const RECT& available);
struct LessonReaderSegmentSnapshot {
    std::string region,source,spoken;
    HWND target = nullptr;
    std::size_t block = 0,start = 0,end = 0;
    double duration_ms = 0;
    bool available = false,feedback = false,correct = false;
    // Contiguous playback of this exact recording, independent of a global
    // boundary that can still be waiting for earlier feedback after a seek.
    bool feedback_fully_played = false;
    std::vector<windows::NarrationTiming> timings;
    std::filesystem::path recording;
};
struct LessonReaderSnapshot {
    bool playing = false,allocated = false,planned = false,preparing = false,finished = false,waiting_gate = false;
    bool preparation_suspended = false;
    std::size_t index = 0;
    std::int64_t offset = 0,heard_through = 0;
    double rate = 1,playback_rate = 0;
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
    // Header action: hide pauses/releases through the existing pause policy;
    // showing controls never starts or restarts playback.
    void set_controls_visible(bool);
    void toggle_controls();
    [[nodiscard]] bool controls_visible() const;
    void settings_changed();
    // Temporary options navigation only: stop/cancellation callbacks and joins
    // run off the UI thread. Keeps the plan/bookmark/cache and never autoplays;
    // an in-flight host operation still acknowledges cancellation normally.
    void suspend_preparation(bool suspended);
    void seek_seconds(int);
    void restart();
    // Called before dialog dispatch so focused RichEdit controls cannot swallow
    // reader keys. F9/F10 cycle speed in reverse/forward order, including repeats;
    // the speed button uses that same forward cycle. Other actions fire once.
    // Left/Up/Down/Right alias F5/F6/F7/F8 only outside text/interactive controls.
    [[nodiscard]] bool dispatch_shortcut(const MSG&);
    [[nodiscard]] HWND bar()const;
    // Observes UI-thread state only. Does not tick media, advance reading or save.
    [[nodiscard]] LessonReaderSnapshot snapshot()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
