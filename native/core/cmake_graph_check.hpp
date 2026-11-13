#pragma once

#include <string>
#include <vector>

namespace lgaed::curriculum {

// Lesson-root structure requirements do not alter the historical cpp_compiler
// payload, source draft, address, output cases or saved answers. A named target
// can require compiled sources and actual links to other generated targets.
struct CmakeRequiredTarget {
    std::string name, type;
    std::vector<std::string> sources, links_to;
    bool exact_sources = false;
    bool operator==(const CmakeRequiredTarget&) const = default;
};
struct CmakeGraphRequirement {
    std::string project_name;
    std::vector<CmakeRequiredTarget> targets;
    bool operator==(const CmakeGraphRequirement&) const = default;
};

} // namespace lgaed::curriculum
