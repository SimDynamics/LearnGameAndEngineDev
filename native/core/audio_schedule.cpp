#include "core/audio_schedule.hpp"
#include <set>
#include <algorithm>

namespace lgaed::narration {
std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage> pages,
    const curriculum::CatalogLocation& current,std::optional<std::size_t> current_page,std::string_view next,
    std::span<const std::size_t> revised) {
    std::vector<std::size_t> result;
    std::set<std::size_t> seen;
    const auto active=std::ranges::find_if(pages,[&](const auto& page){
        return current_page&&page.location==current&&page.page==*current_page;
    });
    const auto active_index=static_cast<std::size_t>(active-pages.begin());
    if(active!=pages.end())seen.insert(active_index);
    auto append=[&](auto matches){for(std::size_t i=0;i<pages.size();++i)
        if(matches(i,pages[i])&&seen.insert(i).second)result.push_back(i);};
    // Changed published pages and new pages after the studied page take
    // precedence. Ordinary distant backlog still follows the saved route turn.
    if(active!=pages.end())append([&](auto index,const auto& page){
        return index>active_index&&page.location.route==current.route&&std::ranges::find(revised,index)!=revised.end();
    });
    // Cached pages never consume the next generation turn. Once the student's
    // following pages are ready, the remaining inventory keeps its route order.
    const auto route=[&](std::string_view name){
        if(active!=pages.end()&&name==current.route)append([&](auto index,const auto& page){
            return index>active_index&&page.location.route==name;});
        append([&](auto,const auto& page){return page.location.route==name;});
    };
    route(next);
    if(active!=pages.end()&&next!=current.route)route(current.route);
    append([](auto,const auto&){return true;});
    return result;
}
}
