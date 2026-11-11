#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace lgaed::curriculum {

// Reconstruct a code-order answer without trimming any source fragment.
// Even an empty first card contributes its physical line separator. Visual
// optional separators are never part of the stored order passed here.
[[nodiscard]] inline std::string ordered_source_text(
        std::span<const std::string> fragments,
        std::span<const std::size_t> order) {
    std::string output;
    for(std::size_t i=0;i<order.size();++i) {
        if(order[i]>=fragments.size())
            throw std::out_of_range("Invalid source fragment in code-order copy");
        // A fragment may already contain its physical terminal newline.
        // An authored empty card consumes one source row even if the
        // previous fragment ended with LF. Its separator is placed BEFORE
        // the next card, not before the empty card, so neither an initial
        // nor an internal empty source row disappears during copying.
        // This also avoids doubling LF in continued macros or CMake text.
        if(i&&(fragments[order[i-1]].empty()||!output.ends_with('\n')))
            output+='\n';
        output+=fragments[order[i]];
    }
    return output;
}

} // namespace lgaed::curriculum
