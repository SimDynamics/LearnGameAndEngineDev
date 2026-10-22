#pragma once
#include "core/lesson_state.hpp"
#include <span>

namespace lgaed::appearance {
// These are the original Android preference domains, including course IDs and
// opt-out flags. Presentation never derives them from a translated title.
[[nodiscard]] int global_text_scale(const state::StateData&);
[[nodiscard]] int course_text_scale(const state::StateData&,std::string_view course);
[[nodiscard]] bool inherits_text_scale(const state::StateData&,std::string_view course);
[[nodiscard]] int text_scale(const state::StateData&,std::string_view course);
void save_global_text_scale(state::StateData&,int percentage);
void save_course_text_scale(state::StateData&,std::string_view course,bool inherit,int percentage);
[[nodiscard]] bool global_code_wrap(const state::StateData&);
[[nodiscard]] bool code_wrap(const state::StateData&,std::string_view course);
[[nodiscard]] bool allows_global_wrap(const state::StateData&,std::string_view course);
void save_course_wrap(state::StateData&,std::string_view course,bool wrap);
void save_wrap_overwrite(state::StateData&,std::string_view course,bool allowed);
void save_global_wrap(state::StateData&,bool wrap,std::span<const std::string> courses);
void reset_wrap_exceptions(state::StateData&,std::span<const std::string> courses);
}
