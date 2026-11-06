#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace lgaed::curriculum {
enum class EntryCompletion { complete, pending_math, incomplete };
struct EntryRequirement {std::size_t page=0;EntryCompletion completion=EntryCompletion::incomplete;};
struct LessonEntryDecision {std::size_t page=0;bool pending_math=false;};

// Current-page exercises remain reachable. Every earlier exercise must be
// complete; unresolved stored math is neither an answer nor a failed attempt.
inline LessonEntryDecision lesson_entry_decision(std::size_t requested,std::size_t page_count,
        std::span<const EntryRequirement> requirements) {
    if(!page_count)throw std::invalid_argument("A lesson entry requires at least one page");
    auto page=std::min(requested,page_count-1);
    for(const auto& item:requirements){
        if(item.page>=page_count)throw std::invalid_argument("A lesson entry requirement has no page");
        if(item.page<page&&item.completion==EntryCompletion::incomplete)page=item.page;
    }
    const bool pending=std::ranges::any_of(requirements,[&](const auto& item){
        return item.page<page&&item.completion==EntryCompletion::pending_math;
    });
    return {page,pending};
}

// An asynchronous restore may authenticate answers after navigation/import,
// but only the still-current intent may choose the initial visible page.
class DeferredLessonEntry {
public:
    void defer(std::size_t page) {++generation_;requested_=page;}
    void invalidate() noexcept {++generation_;}
    void cancel() noexcept {++generation_;requested_.reset();}
    [[nodiscard]] bool waiting() const noexcept {return requested_.has_value();}
    [[nodiscard]] std::uint64_t generation() const noexcept {return generation_;}
    [[nodiscard]] std::optional<std::size_t> requested(std::uint64_t generation) const noexcept {
        return generation==generation_?requested_:std::nullopt;
    }
private:
    std::uint64_t generation_=0;
    std::optional<std::size_t> requested_;
};
}
