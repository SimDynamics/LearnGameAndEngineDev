#pragma once
#include "core/lesson_state.hpp"
#include <windows.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::presentation {
using PreferencesMutation = std::function<void(state::StateData&)>;
enum class SettingsScope {global,course,unit,lesson,audio};
struct SettingsAccess {
    std::function<state::StateData()> read;
    std::function<void(const PreferencesMutation&)> update;
    std::function<void()> changed, close, reset_scope;
    std::function<void(std::string_view)> reset_route;
    std::vector<std::string> programming_course_ids;
    std::vector<std::string> math_course_ids;
    std::string title, subtitle;
    std::string eyebrow,course_id,reset_label,route_id;
    SettingsScope scope=SettingsScope::global;
    bool programming=false;
};
// Normal application page, not a second copy of the lesson or its data.
[[nodiscard]] HWND create_settings_view(HWND parent,SettingsAccess);
// An owned, resizable settings window. The lesson remains alive in its owner;
// closing settings never recreates editors, playback or the student's page.
[[nodiscard]] HWND create_settings_window(HWND owner,SettingsAccess);
void refresh_settings_language(HWND settings_window);
// The same non-modal, route-specific reserve detail strip as the lesson header.
void toggle_attempt_details(HWND lesson,std::function<state::StateData()> read,std::string_view route);
// Header shortcut to the same global UI language preference, including Auto.
[[nodiscard]] std::optional<std::string> choose_interface_language(HWND owner,const state::StateData&);
}
