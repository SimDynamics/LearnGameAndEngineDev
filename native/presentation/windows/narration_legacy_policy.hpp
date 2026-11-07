#pragma once

#include "core/narration_state.hpp"
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace lgaed::presentation::narration_legacy {
inline constexpr std::string_view previous_output_prefix="previous_output_data/";
inline constexpr std::string_view b42_output_prefix="previous_output_b42/";
inline constexpr std::string_view semantic_prefix="previous_text_semantics/";
inline constexpr std::string_view component_mode_prefix="previous_component_math_mode/";
struct Passage {
    std::string_view owner,source,context,math_mode,prepared;
    std::size_t start=0,end=0;
};
// The entire historical key is authenticated after this source-owner check.
// Only a current, earned compiler guide may replace its old authored body;
// its changed recording is revisited through the existing revision boundary.
template<class Region>
bool matches_saved_semantic_region(const narration::SemanticRegionProof& proof,const Region& live){
    return (live.anchor.empty()?live.key:live.anchor)==proof.owner&&live.reading_context==proof.context&&
        (live.math_mode==proof.math_mode||live.previous_math_mode==proof.math_mode)&&
        (live.source_text==proof.source||(live.compiler_guidance&&live.feedback&&live.correct&&live.required_compiler));
}
// Private historical plans can authenticate the same visible passage, never
// listening to its new pronunciation. Range/source/owner equality is mandatory.
inline bool same_previous_passage(const Passage& before,const Passage& after) {
    auto context=before.context;
    const bool previous_component_mode=context.starts_with(component_mode_prefix);
    if(previous_component_mode)context.remove_prefix(component_mode_prefix.size());
    const bool previous_semantics=context.starts_with(semantic_prefix);
    if(previous_semantics)context.remove_prefix(semantic_prefix.size());
    const bool original=context.starts_with(previous_output_prefix);
    const bool b42=context.starts_with(b42_output_prefix);
    const bool previous_output=original||b42;
    if(original)context.remove_prefix(previous_output_prefix.size());
    else if(b42)context.remove_prefix(b42_output_prefix.size());
    const bool previous_cpp=(context=="auto"||context=="cpp_previous_parentheses")
        &&after.context=="cpp"&&before.math_mode=="auto"&&after.math_mode=="auto";
    const bool same_context=(previous_output||previous_semantics||previous_component_mode)&&context==after.context;
    return (same_context||previous_cpp)&&(before.math_mode==after.math_mode||previous_component_mode)
        &&before.owner==after.owner&&before.source==after.source
        &&before.start==after.start&&before.end==after.end&&before.prepared!=after.prepared;
}
inline narration::Position changed_speech_position(narration::Position value,bool same_recording) {
    if(!same_recording){value.milliseconds=0;value.finished=false;value.heard_prefix.reset();}
    return value;
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
    found->semantic_plan.reset();
    if(history.active_key==previous)history.active_key=current;
    return true;
}
} // namespace lgaed::presentation::narration_legacy
