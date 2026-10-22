#include "core/learning_flow.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace lgaed::curriculum {
namespace {
constexpr const char* domain="learning_flow_settings";
bool value(const state::StateData& data,std::string_view group,std::string_view key,bool fallback){
    const auto stored=state::find(data,group,key);if(!stored)return fallback;
    if(const auto result=std::get_if<bool>(stored))return *result;
    throw std::runtime_error("La configuración de navegación guardada no contiene un valor lógico.");
}
std::string prefix(std::string_view course){if(course.empty())throw std::invalid_argument("Course settings need a course identifier");return "course_override_"+std::string(course)+'_';}
constexpr std::array<const char*,4> names{"between_lessons","between_units","between_courses","swipe_finish"};
std::array<bool,4> fields(LearningFlow v){return {v.between_lessons,v.between_units,v.between_courses,v.swipe_finish};}
}
bool has_course_flow(const state::StateData& data,std::string_view course){return !course.empty()&&value(data,domain,prefix(course)+"enabled",false);}
LearningFlow learning_flow(const state::StateData& data,std::string_view course){
    // Android's original preference domain and defaults are authoritative.
    // Only the early native prototype's explicit choices need a fallback.
    std::array<bool,4> defaults{true,false,false,false},result{};
    constexpr std::array<const char*,4> old{"automatic_between_lessons","automatic_between_units","automatic_between_courses","swipe_finish"};
    for(std::size_t i=0;i<names.size();++i){
        const auto key="global_"+std::string(names[i]);
        result[i]=state::find(data,domain,key)?value(data,domain,key,defaults[i]):value(data,"native_windows",old[i],defaults[i]);
    }
    if(has_course_flow(data,course))for(std::size_t i=0;i<names.size();++i)result[i]=value(data,domain,prefix(course)+names[i],result[i]);
    return {result[0],result[1],result[2],result[3]};
}
void clear_course_flow(state::StateData& data,std::string_view course){
    auto it=data.preferences.find(domain);if(it==data.preferences.end())return;
    const auto base=prefix(course);it->second.erase(base+"enabled");for(const auto name:names)it->second.erase(base+name);
}
void save_course_flow(state::StateData& data,std::string_view course,LearningFlow setting){
    const auto base=prefix(course);auto& preferences=data.preferences[domain];preferences[base+"enabled"]=true;
    const auto values=fields(setting);for(std::size_t i=0;i<names.size();++i)preferences[base+names[i]]=values[i];
}
void save_global_flow(state::StateData& data,LearningFlow setting){
    auto& preferences=data.preferences[domain];const auto values=fields(setting);
    for(std::size_t i=0;i<names.size();++i)preferences["global_"+std::string(names[i])]=values[i];
    // Same explicit action as Android: saving global navigation restores all
    // courses to inheritance, while all student/progress data stays intact.
    std::erase_if(preferences,[](const auto& entry){return entry.first.starts_with("course_override_");});
}
}
