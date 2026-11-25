#pragma once

#include "core/text_semantics.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lgaed::curriculum {
// Resolved presentation data. The loader authenticates the source representation
// and every referenced label before supplying this tree to the native reader.
// Nonempty children identify a container; leaves are source-bound code or notes.
struct CodeScopeNode {
    std::string key,text;
    bool code=false;
    std::vector<CodeScopeNode> children;
    // Source-bound reading notes retain their original semantic intent. The
    // icon is visual decoration and is never inserted into spoken text.
    std::string decorative_icon,source_block,source_path;
    std::optional<TextSemantics> text_semantics;
};
struct CodeScopePresentation {
    std::size_t representation_index=0;
    CodeScopeNode root;
    bool read_only=false;
    std::vector<std::string> consumed_blocks;
};
} // namespace lgaed::curriculum
