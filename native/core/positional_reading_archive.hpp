#pragma once

#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lgaed::curriculum {
// A loader-authenticated original order, resolved to today's displayed block
// indices. Completion comes from the original compiler's actual native receipt.
struct PositionalReadingBlock {
    std::size_t current_index;
    bool compiler = false;
    bool completed = false;
};

// The caller has already restored each source field and compiler feedback.
// Preserve current anchors/targets while reproducing the old page's order and
// same-page compiler barrier. This creates only a private narration archive;
// it cannot change current controls, permissions, receipts or listening credit.
template<class Region>
std::vector<Region> positional_reading_regions(const std::vector<Region>& current,
        const std::vector<PositionalReadingBlock>& original) {
    constexpr auto title = std::numeric_limits<std::size_t>::max();
    if(original.empty())throw std::invalid_argument("A positional reading archive needs its complete original owners");
    std::map<std::size_t,std::size_t> counts;
    for(const auto& block:original)
        if(block.current_index==title||!counts.emplace(block.current_index,0).second)
            throw std::invalid_argument("A positional reading archive has duplicated or invalid owners");
    for(const auto& region:current) {
        if(region.block_index==title)continue;
        const auto found=counts.find(region.block_index);
        if(found==counts.end())throw std::invalid_argument("A positional reading region belongs to another owner");
        ++found->second;
    }
    for(const auto& [index,count]:counts) {
        (void)index;
        if(!count)throw std::invalid_argument("A positional reading archive is missing an original owner");
    }
    std::vector<Region> result;
    for(const auto& region:current)if(region.block_index==title)result.push_back(region);
    for(const auto& block:original) {
        for(auto region:current)if(region.block_index==block.current_index) {
            region.required_compiler=block.compiler;
            if(block.compiler)region.solved=block.completed;
            result.push_back(std::move(region));
        }
        if(block.compiler&&!block.completed)break;
    }
    return result;
}
} // namespace lgaed::curriculum
