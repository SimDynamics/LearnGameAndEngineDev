#include "core/narration_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace lgaed::narration {
History read_history(const state::StateData& data){
    History history;
    for(int i=0;i<3;++i){const auto suffix=std::to_string(i);const auto key=state::string_value(data,resume_domain,"key_"+suffix);
        if(key.empty()||std::ranges::any_of(history.entries,[&](const auto& entry){return entry.key==key;}))continue;
        const auto finish=state::find(data,resume_domain,"finished_"+suffix);
        history.entries.push_back({key,state::string_value(data,resume_domain,"stable_"+suffix),{
            state::integer_value(data,resume_domain,"segment_"+suffix),state::integer_value(data,resume_domain,"position_"+suffix),
            std::max<std::int64_t>(0,state::integer_value(data,resume_domain,"heard_"+suffix)),finish&&std::holds_alternative<bool>(*finish)&&std::get<bool>(*finish)}});
    }
    history.active_key=state::string_value(data,resume_domain,"active_key");history.active_owner=state::string_value(data,resume_domain,"active_owner");return history;
}
void write_history(state::StateData& data,const History& history){
    if(history.entries.size()>3)throw std::invalid_argument("Narration history exceeds three pages");
    auto& prefs=data.preferences[resume_domain];prefs["active_key"]=history.active_key;prefs["active_owner"]=history.active_owner;
    for(std::size_t i=0;i<3;++i){const auto suffix=std::to_string(i);
        if(i>=history.entries.size()){for(const auto prefix:{"key_","segment_","position_","finished_","heard_","stable_"})prefs.erase(prefix+suffix);continue;}
        const auto& entry=history.entries[i];prefs["key_"+suffix]=entry.key;prefs["segment_"+suffix]=entry.position.segment;
        prefs["position_"+suffix]=entry.position.milliseconds;prefs["finished_"+suffix]=entry.position.finished;prefs["heard_"+suffix]=entry.position.heard_through;
        if(entry.stable.empty())prefs.erase("stable_"+suffix);else prefs["stable_"+suffix]=entry.stable;
    }
}
std::optional<Position> restore(const History& history,std::string_view key,std::size_t segments){
    const auto found=std::ranges::find(history.entries,key,&Bookmark::key);if(found==history.entries.end())return {};
    const auto& value=found->position;if(value.segment<0||value.milliseconds<0||static_cast<std::size_t>(value.segment)>=segments
        ||(value.finished&&static_cast<std::size_t>(value.segment)+1!=segments))return {};return value;
}
History begin(History history,std::string key,std::string owner,Position value,std::string stable){
    if(key.empty()||owner.empty()||value.segment<0||value.milliseconds<0)throw std::invalid_argument("Invalid narration owner or position");
    std::erase_if(history.entries,[&](const auto& entry){return entry.key==key;});value.finished=false;
    history.entries.insert(history.entries.begin(),{key,std::move(stable),value});if(history.entries.size()>3)history.entries.resize(3);
    history.active_key=std::move(key);history.active_owner=std::move(owner);return history;
}
History checkpoint(History history,std::string_view key,std::string_view owner,Position value){
    if(owner.empty()||history.active_key!=key||history.active_owner!=owner||value.segment<0||value.milliseconds<0)return history;
    for(auto& entry:history.entries)if(entry.key==key)entry.position=value;
    if(value.finished){history.active_key.clear();history.active_owner.clear();}return history;
}
Position seek(Position value,std::int64_t segment,std::int64_t milliseconds){value.segment=std::max<std::int64_t>(0,segment);value.milliseconds=std::max<std::int64_t>(0,milliseconds);value.finished=false;return value;}
}
