#pragma once

#include "core/lesson.hpp"
#include "core/compiler_flow.hpp"
#include "presentation/windows/lesson_view.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

namespace lgaed::presentation::reading_gate {
// Narration shares the mandatory curricular-editor policy with navigation.
// An editor whose evaluator is still missing cannot release the next passage.
inline bool compiler_objective(const curriculum::Block& block) {
    return curriculum::compiler_requires_completion(block);
}
struct FeedbackPriority { std::size_t block=0,allowed_through=0; };
enum class BlockerReason { answer, correct_feedback };
struct PrefixBlocker { std::size_t index=0,block=0;BlockerReason reason=BlockerReason::answer; };
struct NoFeedbackAdmission {
    template<class Part>bool operator()(const Part&)const{return false;}
};

inline bool same_script_region(const LessonReadingRegion& planned,const LessonReadingRegion& live) {
    return planned.key==live.key&&planned.source_text==live.source_text&&
        planned.reading_context==live.reading_context&&planned.math_mode==live.math_mode&&planned.text_semantics==live.text_semantics;
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
// exercise group (or declared that group finished). No history is rewritten.
template<std::ranges::random_access_range Parts,class Projection=std::identity>
std::optional<std::size_t> exercise_rewind_target(const Parts& parts,std::size_t current,bool finished,Projection region={}) {
    const auto count=std::ranges::size(parts);
    for(std::size_t first=0;first<count;) {
        auto last=first;
        while(last+1<count&&std::invoke(region,parts[last+1]).block_index==std::invoke(region,parts[first]).block_index)++last;
        bool pending=false;
        for(auto n=first;n<=last;++n) {
            const auto& item=std::invoke(region,parts[n]);
            pending=pending||(item.is_exercise&&!item.solved);
        }
        if(pending&&(current>last||(finished&&current>=first)))return first;
        first=last+1;
    }
    return {};
}

// Existing clients retain their API; the mandatory policy now covers every
// puzzle, including ordinary and mathematical activities on the same page.
template<std::ranges::random_access_range Parts,class Projection=std::identity>
std::optional<std::size_t> compiler_rewind_target(const Parts& parts,std::size_t current,bool finished,Projection region={}) {
    return exercise_rewind_target(parts,current,finished,region);
}

// A later explicit answer may abandon the particular earlier explanation
// the learner actually began to hear. This is navigation permission only;
// neither an unplayed restored bookmark nor a seek offset proves hearing.
template<std::ranges::random_access_range Parts,class Projection=std::identity>
std::optional<std::size_t> observed_feedback_departure(const Parts& parts,std::size_t at,
        bool heard_recording,Projection region={}) {
    if(!heard_recording||at>=std::ranges::size(parts))return {};
    const auto& current=std::invoke(region,parts[at]);
    if(!current.is_exercise||!current.feedback||!current.correct||!current.solved)return {};
    for(const auto& part:parts) {
        const auto& item=std::invoke(region,part);
        if(item.block_index!=current.block_index)continue;
        if((item.is_exercise&&!item.solved)||(item.feedback&&(!item.correct||!item.solved)))return {};
    }
    return current.block_index;
}

template<std::ranges::random_access_range Parts,class Projection=std::identity,class Admission=NoFeedbackAdmission>
std::optional<PrefixBlocker> prefix_blocker(const Parts& parts,std::size_t destination,std::int64_t heard_through,
        std::optional<FeedbackPriority> priority={},bool manual_forward=false,Projection region={},Admission admitted={},
        std::optional<std::size_t> playback_at={}) {
    const auto count=std::ranges::size(parts);
    std::optional<std::size_t> feedback_start;
    if(priority)for(std::size_t n=0;n<count;++n) {
        const auto& item=std::invoke(region,parts[n]);
        if(item.block_index==priority->block&&item.feedback&&item.correct&&item.solved){feedback_start=n;break;}
    }
    const bool within_feedback=feedback_start&&destination<count&&std::invoke(region,parts[destination]).feedback&&
        std::invoke(region,parts[destination]).block_index==priority->block;
    // A complete later explanation does not itself authorize earlier
    // unheard feedback. Only its own hearing or source-bound navigation
    // admission can release each older correct-feedback sentence.
    for(std::size_t first=0;first<destination&&first<count;) {
        auto last=first;
        while(last+1<count&&std::invoke(region,parts[last+1]).block_index==std::invoke(region,parts[first]).block_index)++last;
        // Segments inside one puzzle do not form additional independent gates.
        if(last>=destination)break;
        // A fresh answer may enter its own feedback across an earlier gate.
        if(!(within_feedback&&last<*feedback_start))for(auto n=first;n<=last;++n){
            const auto& item=std::invoke(region,parts[n]);
            if(item.is_exercise&&!item.solved)return PrefixBlocker{first,item.block_index,BlockerReason::answer};
            if(!item.is_exercise||!item.feedback||!item.correct)continue;
            const bool own_feedback=feedback_start&&item.block_index==priority->block;
            const bool ending_here=playback_at&&*playback_at==last&&!manual_forward;
            const auto permitted=own_feedback&&!ending_here?static_cast<std::int64_t>(priority->allowed_through):heard_through;
            if(permitted<=static_cast<std::int64_t>(n)&&!(own_feedback&&manual_forward)&&!std::invoke(admitted,parts[n]))
                return PrefixBlocker{n,item.block_index,BlockerReason::correct_feedback};
        }
        first=last+1;
    }
    return {};
}
template<std::ranges::random_access_range Parts,class Projection=std::identity>
bool prefix_admitted(const Parts& parts,std::size_t destination,std::int64_t heard_through,
        std::optional<FeedbackPriority> priority={},bool manual_forward=false,Projection region={}) {
    return !prefix_blocker(parts,destination,heard_through,priority,manual_forward,std::move(region));
}
} // namespace lgaed::presentation::reading_gate
