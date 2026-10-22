#pragma once
#include "core/lesson_state.hpp"
#include <windows.h>
#include <functional>
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
}
