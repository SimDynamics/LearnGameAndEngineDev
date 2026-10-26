#pragma once

#include "platform/windows/environment.hpp"
#include <windows.h>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace lgaed::curriculum { struct Lesson; }
namespace lgaed::state { struct StateData; }

namespace lgaed::presentation {

struct LessonPageState {
    std::size_t index = 0, count = 0;
    bool can_previous = false, can_next = false;
    std::string lesson_id, page_id, locale;
    std::string narration_scope,narration_page_identity,narration_language;
    std::string lesson_title;
};
struct LessonReadingRegion {
    std::string key, source_text;
    HWND target = nullptr;
    std::size_t block_index = std::numeric_limits<std::size_t>::max();
    bool feedback = false, correct = false, is_exercise = false, solved = false;
    // Spoken Android heading, anchored to its compact visual language caption.
    // It has no invented word map onto that differently written caption.
    bool passive_label = false;
    // Stable component identity survives removal of preceding visual controls.
    std::string anchor, retired_successor;
    bool required_compiler = false;
};
struct LessonReadingArchive {
    std::string page_identity;
    std::vector<LessonReadingRegion> regions;
};
struct LessonViewHooks {
    std::string lesson_label = "Lección";
    std::function<void(HWND)> back_to_catalog, content_changed, before_page_change, before_destroy;
    std::function<void(HWND,std::size_t,bool)> feedback_changed;
    std::function<void(HWND)> reader_requested, language_requested, attempts_requested, settings_requested;
    std::function<void(HWND)> lesson_completed;
    std::function<std::shared_ptr<void>(std::stop_token)> priority_work;
    std::function<std::string(std::string_view,std::string_view,std::string_view,std::stop_token)> validate_math;
    std::function<std::string(std::string_view)> math_editor_request;
    std::function<void(HWND,HWND)> calculator_requested;
};
// Page actions share exactly the gates used by gestures and the native Pages
// menu. These messages carry no fake/hidden navigation controls.
inline constexpr UINT lesson_page_query_message = WM_APP + 180;
inline constexpr UINT lesson_page_navigate_message = WM_APP + 181;
inline constexpr UINT lesson_reading_regions_message = WM_APP + 182;
inline constexpr UINT lesson_view_hooks_message = WM_APP + 183;
inline constexpr UINT lesson_reader_bar_message = WM_APP + 184;
inline constexpr UINT lesson_state_read_message = WM_APP + 185;
inline constexpr UINT lesson_preferences_update_message = WM_APP + 186;
[[nodiscard]] LessonPageState lesson_page_state(HWND window);
[[nodiscard]] bool navigate_lesson_view(HWND window,std::size_t page);
// Back first dismisses the current transient surface, then uses the existing
// catalog callback. It never changes page/reading history on its own.
[[nodiscard]] bool navigate_lesson_back(HWND window);
// Run before IsDialogMessage. Each physical Enter checks or advances once;
// multiline editors keep their normal newline behavior.
[[nodiscard]] bool dispatch_lesson_enter(MSG& message);
[[nodiscard]] std::vector<LessonReadingRegion> lesson_reading_regions(HWND window);
// Reconstructs only the previous Windows script for exact bookmark migration.
// These regions are never used for playback or visual shading.
[[nodiscard]] std::vector<LessonReadingRegion> legacy_lesson_reading_regions(HWND window);
// Exact prior script before Android code-heading narration was restored.
[[nodiscard]] std::vector<LessonReadingRegion> pre_caption_lesson_reading_regions(HWND window);
// Reconstructs an archived pre-retirement script for bookmark migration only.
[[nodiscard]] std::vector<LessonReadingRegion> retired_lesson_reading_regions(HWND window);
// Each retirement batch retains its exact prior page identity and script.
// Archived text is used only to prove a stored bookmark, never for playback.
[[nodiscard]] std::vector<LessonReadingArchive> retired_lesson_reading_archives(HWND window);
void set_lesson_view_hooks(HWND window,LessonViewHooks hooks);
// The caller owns the reader bar. It must be a child of the lesson view; the
// bar overlays the viewport, as on Android; it never reserves a footer.
void attach_lesson_reader_bar(HWND window,HWND reader_bar,int logical_height);
void set_lesson_attempts_state(HWND window,int available,int maximum,double progress,bool premium);
[[nodiscard]] state::StateData read_lesson_state(HWND window);
void update_lesson_preferences(HWND window,const std::function<void(state::StateData&)>& update);

// A native child window owns the lesson, controls and its cancellable execution worker.
// Content is read directly from the existing JSON; no second curriculum is generated.
[[nodiscard]] HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment,
                                     const std::filesystem::path& content_root);
// A loaded model can also be supplied by a catalog or another trusted content adapter.
[[nodiscard]] HWND create_lesson_view(HWND parent, const WindowsEnvironment& environment,
                                     curriculum::Lesson lesson,LessonViewHooks hooks = {});
[[nodiscard]] bool flush_lesson_view(HWND window);

} // namespace lgaed::presentation
