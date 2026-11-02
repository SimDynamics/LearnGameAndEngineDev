#pragma once
#include "core/lesson_state.hpp"
#include "platform/windows/narration_storage.hpp"
#include <windows.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace lgaed::presentation {
using PreferencesMutation = std::function<void(state::StateData&)>;
enum class SettingsScope {global,course,unit,lesson,audio};
struct SettingsResetText {std::string title,confirmation,success;};
[[nodiscard]] SettingsResetText settings_reset_text(SettingsScope,std::string_view locale,std::string_view route={});
struct SettingsAccess {
    std::function<state::StateData()> read;
    std::function<void(const PreferencesMutation&)> update;
    std::function<void()> changed, close, reset_scope;
    std::function<void(std::string_view)> reset_route;
    std::function<void(std::string_view)> open_audio;
    std::function<void()> open_licenses;
    std::function<void(std::string_view)> unsaved_discarded;
    std::function<windows::NarrationStorageSnapshot()> storage_snapshot;
    std::vector<std::pair<std::string,std::string>> audio_courses;
    std::vector<std::string> programming_course_ids;
    std::vector<std::string> math_course_ids;
    std::string title, subtitle;
    std::string eyebrow,course_id,reset_label,route_id;
    SettingsScope scope=SettingsScope::global;
    bool programming=false;
    // The lesson supplies only pages allowed by its current navigation gates.
    std::vector<std::pair<std::size_t,std::string>> lesson_pages;
    std::size_t current_page=0;
    std::function<void(std::size_t)> navigate_page;
    std::function<void()> page_top, page_bottom, import_state, finish;
    bool finish_enabled=false;
    std::function<std::string()> generation_status;
};
// Normal application page, not a second copy of the lesson or its data.
[[nodiscard]] HWND create_settings_view(HWND parent,SettingsAccess);
// Compatibility entry point: returns the same internal child page, sized to
// its parent's client area. It never creates a separate application window.
[[nodiscard]] HWND create_settings_window(HWND owner,SettingsAccess);
void refresh_settings_language(HWND settings_window);
// The same non-modal, route-specific reserve detail strip as the lesson header.
void toggle_attempt_details(HWND lesson,std::function<state::StateData()> read,std::string_view route);
// Header shortcut to the same global UI language preference, including Auto.
[[nodiscard]] std::optional<std::string> choose_interface_language(HWND owner,const state::StateData&);
// Transient informational message: does not take focus or block navigation.
void show_settings_notice(HWND owner,std::string_view text);
}
