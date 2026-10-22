#pragma once
#include "core/lesson_state.hpp"

namespace lgaed::curriculum {
struct LearningFlow {
    bool between_lessons=true,between_units=false,between_courses=false,swipe_finish=false;
    bool operator==(const LearningFlow&)const=default;
};
[[nodiscard]] bool has_course_flow(const state::StateData&,std::string_view course);
[[nodiscard]] LearningFlow learning_flow(const state::StateData&,std::string_view course={});
void save_global_flow(state::StateData&,LearningFlow);
void save_course_flow(state::StateData&,std::string_view course,LearningFlow);
void clear_course_flow(state::StateData&,std::string_view course);
}
