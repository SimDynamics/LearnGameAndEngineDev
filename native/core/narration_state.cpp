#include "core/narration_state.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace lgaed::narration {
namespace {
bool valid_prefix(const HeardPrefix& value) noexcept {
    return value.fingerprint.size()==64&&std::ranges::all_of(value.fingerprint,[](char c){
        return(c>='0'&&c<='9')||(c>='a'&&c<='f');})&&value.microseconds>=0
        &&value.duration_microseconds>0&&value.microseconds<=value.duration_microseconds;
}
std::optional<HeardPrefix> read_prefix(const state::StateData& data,const std::string& suffix){
    const auto* raw=state::find(data,resume_domain,"heard_prefix_v1_"+suffix);
    const auto* values=raw?std::get_if<std::vector<std::string>>(raw):nullptr;
    if(!values||values->size()!=3)return {};
    HeardPrefix result;result.fingerprint=values->at(0);
    const auto number=[](const std::string& text,std::int64_t& value){
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&text==std::to_string(value);
    };
    if(!number(values->at(1),result.microseconds)||!number(values->at(2),result.duration_microseconds)
        ||!valid_prefix(result))return {};
    return result;
}
}
bool heard_prefix_matches(const Position& position,std::string_view fingerprint,
        std::int64_t duration_microseconds) noexcept {
    return position.heard_prefix&&valid_prefix(*position.heard_prefix)
        &&position.heard_prefix->fingerprint==fingerprint
        &&position.heard_prefix->duration_microseconds==duration_microseconds;
}
History read_history(const state::StateData& data){
    History history;
    for(int i=0;i<3;++i){const auto suffix=std::to_string(i);const auto key=state::string_value(data,resume_domain,"key_"+suffix);
        if(key.empty()||std::ranges::any_of(history.entries,[&](const auto& entry){return entry.key==key;}))continue;
        const auto finish=state::find(data,resume_domain,"finished_"+suffix);
        history.entries.push_back({key,state::string_value(data,resume_domain,"stable_"+suffix),{
            state::integer_value(data,resume_domain,"segment_"+suffix),state::integer_value(data,resume_domain,"position_"+suffix),
            std::max<std::int64_t>(0,state::integer_value(data,resume_domain,"heard_"+suffix)),finish&&std::holds_alternative<bool>(*finish)&&std::get<bool>(*finish)},
            state::string_value(data,resume_domain,"page_"+suffix)});
        history.entries.back().position.heard_prefix=read_prefix(data,suffix);
    }
    history.active_key=state::string_value(data,resume_domain,"active_key");history.active_owner=state::string_value(data,resume_domain,"active_owner");return history;
}
void write_history(state::StateData& data,const History& history){
    if(history.entries.size()>3)throw std::invalid_argument("Narration history exceeds three pages");
    auto& prefs=data.preferences[resume_domain];prefs["active_key"]=history.active_key;prefs["active_owner"]=history.active_owner;
    for(std::size_t i=0;i<3;++i){const auto suffix=std::to_string(i);
        if(i>=history.entries.size()){for(const auto prefix:{"key_","segment_","position_","finished_","heard_","stable_","page_","heard_prefix_v1_"})prefs.erase(prefix+suffix);continue;}
        const auto& entry=history.entries[i];prefs["key_"+suffix]=entry.key;prefs["segment_"+suffix]=entry.position.segment;
        prefs["position_"+suffix]=entry.position.milliseconds;prefs["finished_"+suffix]=entry.position.finished;prefs["heard_"+suffix]=entry.position.heard_through;
        if(entry.stable.empty())prefs.erase("stable_"+suffix);else prefs["stable_"+suffix]=entry.stable;
        if(entry.page.empty())prefs.erase("page_"+suffix);else prefs["page_"+suffix]=entry.page;
        if(entry.position.heard_prefix&&valid_prefix(*entry.position.heard_prefix)){
            const auto& prefix=*entry.position.heard_prefix;
            prefs["heard_prefix_v1_"+suffix]=std::vector<std::string>{prefix.fingerprint,
                std::to_string(prefix.microseconds),std::to_string(prefix.duration_microseconds)};
        }else prefs.erase("heard_prefix_v1_"+suffix);
    }
}
std::optional<Position> restore(const History& history,std::string_view key,std::size_t segments){
    const auto found=std::ranges::find(history.entries,key,&Bookmark::key);if(found==history.entries.end())return {};
    auto value=found->position;if(value.segment<0||value.milliseconds<0||static_cast<std::size_t>(value.segment)>=segments
        ||(value.finished&&static_cast<std::size_t>(value.segment)+1!=segments))return {};
    if(value.heard_prefix&&!valid_prefix(*value.heard_prefix))value.heard_prefix.reset();return value;
}
History begin(History history,std::string key,std::string owner,Position value,std::string stable,std::string page){
    if(key.empty()||owner.empty()||value.segment<0||value.milliseconds<0)throw std::invalid_argument("Invalid narration owner or position");
    if(const auto known=std::ranges::find(history.entries,key,&Bookmark::key);known!=history.entries.end()){
        if(stable.empty())stable=known->stable;
        if(page.empty())page=known->page;
    }else value.heard_prefix.reset();
    // Page identity is additive provenance in this same bounded bookmark.
    // A voice/provider change replaces its page slot without inheriting the old
    // recording's offset. Old entries without proven provenance are not guessed.
    std::erase_if(history.entries,[&](const auto& entry){return entry.key==key||(!page.empty()&&entry.page==page);});value.finished=false;
    history.entries.insert(history.entries.begin(),{key,std::move(stable),value,std::move(page)});if(history.entries.size()>3)history.entries.resize(3);
    history.active_key=std::move(key);history.active_owner=std::move(owner);return history;
}
History checkpoint(History history,std::string_view key,std::string_view owner,Position value){
    if(owner.empty()||history.active_key!=key||history.active_owner!=owner||value.segment<0||value.milliseconds<0)return history;
    for(auto& entry:history.entries)if(entry.key==key)entry.position=value;
    if(value.finished){history.active_key.clear();history.active_owner.clear();}return history;
}
Position seek(Position value,std::int64_t segment,std::int64_t milliseconds){
    segment=std::max<std::int64_t>(0,segment);if(segment!=value.segment)value.heard_prefix.reset();
    value.segment=segment;value.milliseconds=std::max<std::int64_t>(0,milliseconds);value.finished=false;return value;
}
}
