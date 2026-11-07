#pragma once

#include "core/lesson_state.hpp"
#include <cstddef>
#include <optional>
#include <vector>

namespace lgaed::curriculum { struct Lesson; }
namespace lgaed::state {
// Arrival is navigation bookkeeping, never an answer, completion or listening
// certificate. Mapped lessons reuse their existing stable coverage and pin.
struct PageReachability {
    std::vector<bool> reached;
    std::size_t first_unreached=0;
};
[[nodiscard]] PageReachability page_reachability(const StateData&,const curriculum::Lesson&);
[[nodiscard]] bool can_reach_page(const StateData&,const curriculum::Lesson&,std::size_t page);
[[nodiscard]] std::size_t clamp_reachable_page(const StateData&,const curriculum::Lesson&,std::size_t requested);
[[nodiscard]] bool all_pages_reached(const StateData&,const curriculum::Lesson&);
// Call only after the page is admitted. Does not save a current bookmark or
// create achievements, and must not dirty an otherwise untouched UI profile.
bool mark_page_reached(StateData&,const curriculum::Lesson&,std::size_t page);
// First map adoption projects this historical positional prefix through origin;
// the original bookmark and legacy frontier remain intact.
[[nodiscard]] std::optional<std::size_t> legacy_page_frontier(const StateData&,const curriculum::Lesson&,
    bool include_saved_bookmark=true);
}
