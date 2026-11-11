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
        // Never invent an extra empty source line between such fragments:
        // it can break a continued preprocessor directive or CMake syntax.
        // An explicitly empty card still represents an extra physical row.
        if(i&&(!output.ends_with('\n')||fragments[order[i]].empty()))
            output+='\n';
        output+=fragments[order[i]];
    }
    return output;
}

} // namespace lgaed::curriculum
