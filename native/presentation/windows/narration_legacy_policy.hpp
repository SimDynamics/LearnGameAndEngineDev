#pragma once

#include "core/narration_state.hpp"
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace lgaed::presentation::narration_legacy {
inline constexpr std::string_view previous_output_prefix="previous_output_data/";
inline constexpr std::string_view b42_output_prefix="previous_output_b42/";
struct Passage {
    std::string_view owner,source,context,math_mode,prepared;
    std::size_t start=0,end=0;
};
// Private historical plans can authenticate the same visible passage, never
// listening to its new pronunciation. Range/source/owner equality is mandatory.
inline bool same_previous_passage(const Passage& before,const Passage& after) {
    auto context=before.context;
    const bool original=context.starts_with(previous_output_prefix);
    const bool b42=context.starts_with(b42_output_prefix);
    const bool previous_output=original||b42;
    if(original)context.remove_prefix(previous_output_prefix.size());
    else if(b42)context.remove_prefix(b42_output_prefix.size());
    const bool previous_cpp=(context=="auto"||context=="cpp_previous_parentheses")
        &&after.context=="cpp"&&before.math_mode=="auto"&&after.math_mode=="auto";
    const bool same_context=previous_output&&context==after.context;
    return (same_context||previous_cpp)&&before.math_mode==after.math_mode
        &&before.owner==after.owner&&before.source==after.source
        &&before.start==after.start&&before.end==after.end&&before.prepared!=after.prepared;
}
// The caller authenticates a historical page key and maps its exact source
// passage first. Rename only that existing bounded slot, preserving recency,
// unrelated pauses and ownership; never begin a fourth parallel bookmark.
inline bool rekey_authenticated_bookmark(narration::History& history,std::string_view previous,
        std::string_view current,const narration::Position& position,std::string_view page) {
    if(previous.empty()||current.empty()||previous==current||
        std::ranges::any_of(history.entries,[&](const auto& entry){return entry.key==current;}))return false;
    const auto found=std::ranges::find(history.entries,previous,&narration::Bookmark::key);
    if(found==history.entries.end())return false;
    found->key=current;found->position=position;found->page=page;
    if(history.active_key==previous)history.active_key=current;
    return true;
}
} // namespace lgaed::presentation::narration_legacy
