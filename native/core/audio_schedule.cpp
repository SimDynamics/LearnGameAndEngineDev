#include "core/audio_schedule.hpp"
#include <set>

namespace lgaed::narration {
std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage> pages,
    const curriculum::CatalogLocation& context,std::optional<std::size_t> current,std::string_view next) {
    std::vector<std::size_t> result;
    std::set<std::size_t> seen;
    auto append=[&](auto matches){for(std::size_t i=0;i<pages.size();++i)
        if(matches(pages[i])&&seen.insert(i).second)result.push_back(i);};
    const auto in_course=[&](const auto& p){return p.location.route==context.route&&p.location.course==context.course;};
    if(current&&!context.lesson.empty())append([&](const auto& p){return in_course(p)&&p.location.lesson==context.lesson&&p.page==*current;});
    if(!context.unit.empty())append([&](const auto& p){return in_course(p)&&p.location.unit==context.unit;});
    if(!context.course.empty())append(in_course);
    if(!context.route.empty()){
        append([&](const auto& p){return p.location.route==context.route;});
        append([](const auto&){return true;});
    } else {
        // Scan cache hits in the chosen route until its first missing page.
        // The producer recalculates immediately after actual generation; a
        // cached first page must not turn this into cache-dependent alternation.
        append([&](const auto& p){return p.location.route==next;});
        append([](const auto&){return true;});
    }
    return result;
}
}
