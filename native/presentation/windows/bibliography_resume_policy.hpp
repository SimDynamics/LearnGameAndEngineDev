#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace lgaed::presentation::bibliography_resume {
template<class Part>
bool same_recording_sentence(const Part& before,const Part& after){
    const auto owner=[](const auto& part)->std::string_view{return part.region.anchor.empty()?part.region.key:part.region.anchor;};
    return owner(before)==owner(after)&&before.occurrence==after.occurrence&&
        before.sentence.prepared==after.sentence.prepared&&before.sentence.text==after.sentence.text&&
        before.sentence.start==after.sentence.start&&before.sentence.end==after.sentence.end;
}
// The complete archived page key is authenticated by the caller first. This
// helper only bounds the first incompatible sentence of one changed region.
// Compatible earlier sentences retain their own offsets; this grants no audio
// or listening evidence to the replacement citation.
template<class Part,class Same>
std::optional<std::size_t> first_changed_segment(std::span<const Part> previous,
        std::span<const Part> current,std::string_view region,std::size_t paused,
        bool finished,Same same) {
    std::size_t old=0,now=0;
    while(old<previous.size()&&previous[old].region.key!=region)++old;
    while(now<current.size()&&current[now].region.key!=region)++now;
    if(old==previous.size()||now==current.size()||(!finished&&paused<old))return {};
    while(old<previous.size()&&now<current.size()&&previous[old].region.key==region&&
            current[now].region.key==region&&same(previous[old],current[now])){++old;++now;}
    if(!finished&&paused<old)return {};
    // Removing only obsolete trailing material does not invent a new passage.
    if(now==current.size()||current[now].region.key!=region)return {};
    return now;
}
}
