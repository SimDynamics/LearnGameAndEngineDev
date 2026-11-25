#include "core/audio_schedule.hpp"
#include <set>
#include <algorithm>

namespace lgaed::narration {
std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage> pages,
    const curriculum::CatalogLocation& current,std::optional<std::size_t> current_page,std::string_view next) {
    std::vector<std::size_t> result;
    std::set<std::size_t> seen;
    const auto active=std::ranges::find_if(pages,[&](const auto& page){
        return current_page&&page.location==current&&page.page==*current_page;
    });
    const auto active_index=static_cast<std::size_t>(active-pages.begin());
    if(active!=pages.end())seen.insert(active_index);
    auto append=[&](auto matches){for(std::size_t i=0;i<pages.size();++i)
        if(matches(i,pages[i])&&seen.insert(i).second)result.push_back(i);};
    // Missing or changed pages after the studied page take precedence over the
    // saved route turn. The active page belongs to foreground preparation.
    if(active!=pages.end())append([&](auto index,const auto& page){
        return index>active_index&&page.location.route==current.route&&page.location.course==current.course;
    });
    // Cached pages never consume the next generation turn. Once the student's
    // following pages are ready, the remaining inventory keeps its route order.
    append([&](auto,const auto& page){return page.location.route==next;});
    append([](auto,const auto&){return true;});
    return result;
}
}
