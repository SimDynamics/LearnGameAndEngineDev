#include "core/audio_schedule.hpp"
#include <set>

namespace lgaed::narration {
std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage> pages,
    const curriculum::CatalogLocation&,std::optional<std::size_t>,std::string_view next) {
    std::vector<std::size_t> result;
    std::set<std::size_t> seen;
    auto append=[&](auto matches){for(std::size_t i=0;i<pages.size();++i)
        if(matches(pages[i])&&seen.insert(i).second)result.push_back(i);};
    // Scan the selected route until its first missing page. The producer
    // recalculates after a real page, so the two routes alternate without
    // letting cached or empty pages steal the other route's turn.
    append([&](const auto& p){return p.location.route==next;});
    append([](const auto&){return true;});
    return result;
}
}
