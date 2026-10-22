#pragma once
#include "core/lesson_state.hpp"
#include <windows.h>
#include <functional>
#include <string>

namespace lgaed::presentation {
using PreferencesMutation = std::function<void(state::StateData&)>;
struct SettingsAccess {
    std::function<state::StateData()> read;
    std::function<void(const PreferencesMutation&)> update;
    std::function<void()> changed, close, reset_scope;
    std::string title, subtitle;
};
// Normal application page, not a second copy of the lesson or its data.
[[nodiscard]] HWND create_settings_view(HWND parent,SettingsAccess);
}
