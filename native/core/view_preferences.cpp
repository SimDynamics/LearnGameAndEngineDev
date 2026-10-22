#include "core/view_preferences.hpp"
#include <algorithm>
#include <stdexcept>

namespace lgaed::appearance {
namespace {
constexpr const char* text="lesson_text_size_settings";
constexpr const char* wrap="code_wrap_settings";
bool boolean(const state::StateData& data,const char* domain,std::string_view key,bool fallback){
    const auto value=state::find(data,domain,key);if(!value)return fallback;
    if(const auto result=std::get_if<bool>(value))return *result;
    throw std::invalid_argument("A stored view preference is not a boolean");
}
int scale(const state::StateData& data,std::string_view key,int fallback){
    const auto value=state::find(data,text,key);if(!value)return fallback;
    if(const auto result=std::get_if<std::int64_t>(value))return static_cast<int>(std::clamp<std::int64_t>(*result,75,150));
    throw std::invalid_argument("A stored text scale is not an integer");
}
std::string key(std::string_view prefix,std::string_view course){if(course.empty())throw std::invalid_argument("Course appearance needs a course identifier");return std::string(prefix)+std::string(course);}
}
int global_text_scale(const state::StateData& data){return scale(data,"global_scale",100);}
int course_text_scale(const state::StateData& data,std::string_view course){return scale(data,key("course_scale_",course),global_text_scale(data));}
bool inherits_text_scale(const state::StateData& data,std::string_view course){return course.empty()||boolean(data,text,key("course_use_global_",course),true);}
int text_scale(const state::StateData& data,std::string_view course){return inherits_text_scale(data,course)?global_text_scale(data):course_text_scale(data,course);}
void save_global_text_scale(state::StateData& data,int percentage){data.preferences[text]["global_scale"]=static_cast<std::int64_t>(std::clamp(percentage,75,150));}
void save_course_text_scale(state::StateData& data,std::string_view course,bool inherit,int percentage){auto& preferences=data.preferences[text];
    preferences[key("course_use_global_",course)]=inherit;
    preferences[key("course_scale_",course)]=static_cast<std::int64_t>(std::clamp(percentage,75,150));}
bool global_code_wrap(const state::StateData& data){return state::find(data,wrap,"global_wrap")?boolean(data,wrap,"global_wrap",false):boolean(data,"native_windows","code_wrap",false);}
bool code_wrap(const state::StateData& data,std::string_view course){return course.empty()?global_code_wrap(data):boolean(data,wrap,key("course_wrap_",course),global_code_wrap(data));}
bool allows_global_wrap(const state::StateData& data,std::string_view course){return boolean(data,wrap,key("course_allow_global_overwrite_",course),true);}
void save_course_wrap(state::StateData& data,std::string_view course,bool enabled){data.preferences[wrap][key("course_wrap_",course)]=enabled;}
void save_wrap_overwrite(state::StateData& data,std::string_view course,bool allowed){data.preferences[wrap][key("course_allow_global_overwrite_",course)]=allowed;}
void save_global_wrap(state::StateData& data,bool enabled,std::span<const std::string> courses){data.preferences[wrap]["global_wrap"]=enabled;
    for(const auto& course:courses)if(allows_global_wrap(data,course))save_course_wrap(data,course,enabled);}
void reset_wrap_exceptions(state::StateData& data,std::span<const std::string> courses){const auto enabled=global_code_wrap(data);
    for(const auto& course:courses){save_wrap_overwrite(data,course,true);save_course_wrap(data,course,enabled);}}
}
