#pragma once

#include "core/lesson.hpp"

#include <functional>
#include <map>

namespace lgaed::curriculum {
using CompilerCompletionSnapshot = std::map<std::string,bool>;

// Dependencies are validated in display order. Evaluate each compiler once:
// a downstream historical achievement never grants its missing prerequisites.
// The caller authenticates its platform receipts without inventing UI state.
template<class Completed>
CompilerCompletionSnapshot compiler_completion_snapshot(const Lesson& lesson,Completed&& completed) {
    std::map<std::string,const std::vector<std::string>*> requirements;
    for(const auto& relation:lesson.activity_after)requirements.emplace(relation.activity,&relation.prerequisites);
    CompilerCompletionSnapshot result;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks) {
        if(block.type!=BlockType::cpp_compiler)continue;
        bool available=true;
        if(const auto relation=requirements.find(block.stable_id);relation!=requirements.end())
            for(const auto& identity:*relation->second) {
                const auto prior=result.find(identity);
                if(prior==result.end()||!prior->second){available=false;break;}
            }
        result[block.stable_id]=available&&has_compiler_objective(block)&&std::invoke(completed,block);
    }
    return result;
}

inline bool compiler_activity_available(const Lesson& lesson,const Block& activity,
        const CompilerCompletionSnapshot& proofs) {
    for(const auto& position:activity_prerequisites(lesson,activity)) {
        const auto& prerequisite=lesson.pages[position.page].blocks[position.block];
        const auto found=proofs.find(prerequisite.stable_id);
        if(found==proofs.end()||!found->second)return false;
    }
    return true;
}

// Global same-page barriers and explicit referenceAfter owners are distinct.
// The predicate retains the platform's established global-barrier policy;
// reference owners additionally require their complete dependency chain.
template<class Completed>
bool compiler_reference_available(const Lesson& lesson,std::size_t page,const Block& reference,
        const CompilerCompletionSnapshot& proofs,Completed&& completed) {
    if(lesson.compiler_puzzles_required)for(const auto& prior:lesson.pages.at(page).blocks) {
        if(&prior==&reference)break;
        if(has_compiler_objective(prior)&&!std::invoke(completed,prior))return false;
    }
    const auto dependency=reference_activity(lesson,reference);
    if(!dependency)return true;
    const auto& owner=lesson.pages[dependency->page].blocks[dependency->block];
    const auto found=proofs.find(owner.stable_id);
    return found!=proofs.end()&&found->second;
}
} // namespace lgaed::curriculum
