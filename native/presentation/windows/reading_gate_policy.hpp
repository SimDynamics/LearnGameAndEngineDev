#pragma once

#include "core/lesson.hpp"
#include "presentation/windows/lesson_view.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>

namespace lgaed::presentation::reading_gate {
// Narration stops at every authored compiler objective. Whether the whole
// lesson requires compiler completion for navigation is a separate policy.
inline bool compiler_objective(const curriculum::Block& block) {
    return curriculum::has_compiler_objective(block);
}
struct FeedbackPriority { std::size_t block=0,allowed_through=0; };

inline bool same_script_region(const LessonReadingRegion& planned,const LessonReadingRegion& live) {
    return planned.key==live.key&&planned.source_text==live.source_text&&
        planned.reading_context==live.reading_context&&planned.math_mode==live.math_mode;
}
// A plan may finish after live completion metadata changed without changing
// its spoken script. Validate the complete mapping before copying anything;
// stale/missing/ambiguous script identity asks the caller to plan again.
template<std::ranges::forward_range Parts,class Projection=std::identity>
bool reconcile(Parts& parts,std::span<const LessonReadingRegion> live,Projection region={}) {
    const auto find=[&](const LessonReadingRegion& planned)->const LessonReadingRegion* {
        const LessonReadingRegion* found=nullptr;
        for(const auto& current:live)if(current.key==planned.key) {
            if(found||!same_script_region(planned,current))return nullptr;
            found=&current;
        }
        return found;
    };
    for(auto& part:parts)if(!find(std::invoke(region,part)))return false;
    for(auto& part:parts)std::invoke(region,part)=*find(std::invoke(region,part));
    return true;
}

// Returns a position only when the current bookmark has crossed a pending
// compiler group (or declared that group finished). No history is rewritten.
template<std::ranges::random_access_range Parts,class Projection=std::identity>
std::optional<std::size_t> compiler_rewind_target(const Parts& parts,std::size_t current,bool finished,Projection region={}) {
    const auto count=std::ranges::size(parts);
    for(std::size_t first=0;first<count;) {
        auto last=first;
        while(last+1<count&&std::invoke(region,parts[last+1]).block_index==std::invoke(region,parts[first]).block_index)++last;
        bool pending=false;
        for(auto n=first;n<=last;++n) {
            const auto& item=std::invoke(region,parts[n]);
            pending=pending||(item.required_compiler&&!item.solved);
        }
        if(pending&&(current>last||(finished&&current>=first)))return first;
        first=last+1;
    }
    return {};
}

template<std::ranges::random_access_range Parts,class Projection=std::identity>
bool prefix_admitted(const Parts& parts,std::size_t destination,std::int64_t heard_through,
        std::optional<FeedbackPriority> priority={},bool manual_forward=false,Projection region={}) {
    const auto count=std::ranges::size(parts);
    std::optional<std::size_t> feedback_start;
    if(priority)for(std::size_t n=0;n<count;++n) {
        const auto& item=std::invoke(region,parts[n]);
        if(item.block_index==priority->block&&item.feedback&&item.correct&&item.solved){feedback_start=n;break;}
    }
    const bool within_feedback=feedback_start&&destination<count&&std::invoke(region,parts[destination]).feedback&&
        std::invoke(region,parts[destination]).block_index==priority->block;
    for(std::size_t n=0;n<destination&&n<count;++n) {
        const auto& item=std::invoke(region,parts[n]);
        if(!item.is_exercise)continue;
        // A fresh answer owns its feedback, not completion of another compiler.
        // It may enter that feedback but cannot leave it across a pending run.
        if(feedback_start&&n<*feedback_start&&(!item.required_compiler||within_feedback))continue;
        if(n+1<count&&std::invoke(region,parts[n+1]).block_index==item.block_index)continue;
        if(!item.solved)return false;
        const auto permitted=feedback_start?static_cast<std::int64_t>(priority->allowed_through):heard_through;
        if(item.feedback&&item.correct&&permitted<=static_cast<std::int64_t>(n)&&!(feedback_start&&manual_forward))return false;
    }
    return true;
}
} // namespace lgaed::presentation::reading_gate
