#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lgaed::curriculum {
// Resolved presentation data. The loader authenticates the source representation
// and every referenced label before supplying this tree to the native reader.
// Nonempty children identify a container; leaves are individual code lines.
struct CodeScopeNode {
    std::string key,text;
    bool code=false;
    std::vector<CodeScopeNode> children;
};
struct CodeScopePresentation {
    std::size_t representation_index=0;
    CodeScopeNode root;
};
} // namespace lgaed::curriculum
