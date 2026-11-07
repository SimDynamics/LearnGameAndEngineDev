#include "core/lesson_page_reach.hpp"
#include "core/lesson.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace lgaed::state {
namespace {
constexpr std::string_view domain="learning_progress";
std::string base_key(const curriculum::Lesson& lesson){return "lesson_"+lesson.id;}
std::string frontier_key(const curriculum::Lesson& lesson){return base_key(lesson)+"_reached_page_v1_"+lesson.locale;}
std::string suffix(const curriculum::Lesson& lesson){return lesson.state_layout.version==2?"v2_"+lesson.state_layout.locale:"v1";}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::optional<std::size_t> index_value(const StateData& data,const std::string& key){
    const auto* value=find(data,domain,key);if(!value)return {};
    const auto* integer=std::get_if<std::int64_t>(value);
    require(integer&&*integer>=0,"La página alcanzada guardada no es válida; los datos se conservan.");
    return static_cast<std::size_t>(*integer);
}
std::vector<std::string> coverage(const StateData& data,const std::string& key){
    const auto* value=find(data,domain,key);
    const auto* items=value?std::get_if<std::vector<std::string>>(value):nullptr;
    require(items,"Falta la cobertura de páginas de esta familia; los datos se conservan.");return *items;
}
struct MappedCoverage {std::vector<std::string> pages,blocks;};
MappedCoverage mapped_coverage(const StateData& data,const curriculum::Lesson& lesson){
    const auto& layout=lesson.state_layout;
    require((layout.version==1||layout.version==2)&&!layout.origin.empty()&&layout.current.size()==lesson.pages.size(),
        "El mapa de páginas alcanzadas no es compatible.");
    const auto base=base_key(lesson),ending=suffix(lesson);
    const auto* value=find(data,domain,base+"_state_lineage_"+ending);
    const auto* pin=value?std::get_if<std::string>(value):nullptr;
    require(pin&&!layout.lineage.empty()&&*pin==layout.lineage,"La cobertura de páginas pertenece a otra familia.");
    MappedCoverage result{coverage(data,base+"_read_pages_"+ending),coverage(data,base+"_read_blocks_"+ending)};
    std::set<std::string> pages,blocks;
    for(const auto& page:layout.origin){pages.insert(page.id);blocks.insert(page.blocks.begin(),page.blocks.end());}
    for(const auto& item:layout.additions)blocks.insert(item.id);
    for(const auto& id:result.pages)require(pages.contains(id),"La cobertura contiene una página ajena.");
    for(const auto& id:result.blocks)require(blocks.contains(id),"La cobertura contiene un bloque ajeno.");
    for(const auto& page:layout.current){require(pages.contains(page.id),"El mapa actual contiene una página ajena.");
        for(const auto& id:page.blocks)require(blocks.contains(id),"El mapa actual contiene un bloque ajeno.");}
    return result;
}
}
std::optional<std::size_t> legacy_page_frontier(const StateData& data,const curriculum::Lesson& lesson,bool include_saved_bookmark){
    const auto count=lesson.state_layout.version?lesson.state_layout.origin.size():lesson.pages.size();
    require(count>0,"Una lección vacía no tiene páginas alcanzadas.");
    auto result=index_value(data,frontier_key(lesson));
    if(result)require(*result<count,"La frontera guardada está fuera de esta lección.");
    // A real old bookmark is arrival evidence, not a solved exercise. Keep the
    // existing historical clamping behavior for old positional bookmarks.
    if(auto saved=include_saved_bookmark?index_value(data,base_key(lesson)+"_page"):std::nullopt){
        *saved=std::min(*saved,count-1);
        if(!result||*saved>*result)result=*saved;
    }
    return result;
}
PageReachability page_reachability(const StateData& data,const curriculum::Lesson& lesson){
    require(!lesson.pages.empty(),"Una lección vacía no tiene páginas alcanzadas.");
    PageReachability result{std::vector<bool>(lesson.pages.size(),false),0};
    if(!lesson.state_layout.version){
        if(const auto last=legacy_page_frontier(data,lesson))
            for(std::size_t page=0;page<=*last;++page)result.reached[page]=true;
    }else{
        const auto stored=mapped_coverage(data,lesson);
        const std::set<std::string> pages(stored.pages.begin(),stored.pages.end()),blocks(stored.blocks.begin(),stored.blocks.end());
        for(std::size_t page=0;page<lesson.pages.size();++page){const auto& record=lesson.state_layout.current[page];
            result.reached[page]=pages.contains(record.id)&&std::ranges::all_of(record.blocks,[&](const auto& id){return blocks.contains(id);});}
    }
    while(result.first_unreached<result.reached.size()&&result.reached[result.first_unreached])++result.first_unreached;
    return result;
}
bool can_reach_page(const StateData& data,const curriculum::Lesson& lesson,std::size_t page){
    if(page>=lesson.pages.size())return false;
    const auto reach=page_reachability(data,lesson);return reach.reached[page]||page==reach.first_unreached;
}
std::size_t clamp_reachable_page(const StateData& data,const curriculum::Lesson& lesson,std::size_t requested){
    const auto reach=page_reachability(data,lesson);requested=std::min(requested,reach.reached.size()-1);
    return reach.reached[requested]?requested:std::min(requested,reach.first_unreached);
}
bool all_pages_reached(const StateData& data,const curriculum::Lesson& lesson){
    const auto reach=page_reachability(data,lesson);return reach.first_unreached==reach.reached.size();
}
bool mark_page_reached(StateData& data,const curriculum::Lesson& lesson,std::size_t page){
    if(page>=lesson.pages.size())throw std::out_of_range("Unknown reached page");
    const auto base=base_key(lesson);
    if(!lesson.state_layout.version){
        const auto previous=legacy_page_frontier(data,lesson);const auto last=previous?std::max(page,*previous):page;
        require(last<=static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()),"La página alcanzada supera el límite del estado.");
        const auto* existing=find(data,domain,frontier_key(lesson));
        if(existing&&std::get<std::int64_t>(*existing)==static_cast<std::int64_t>(last))return false;
        data.preferences[std::string(domain)][frontier_key(lesson)]=static_cast<std::int64_t>(last);return true;
    }
    auto stored=mapped_coverage(data,lesson);bool changed=false;const auto& record=lesson.state_layout.current.at(page);
    const auto insert=[&](auto& values,const auto& id){if(std::find(values.begin(),values.end(),id)==values.end()){values.push_back(id);changed=true;}};
    insert(stored.pages,record.id);for(const auto& id:record.blocks)insert(stored.blocks,id);
    if(changed){auto& prefs=data.preferences[std::string(domain)];const auto ending=suffix(lesson);
        prefs[base+"_read_pages_"+ending]=std::move(stored.pages);prefs[base+"_read_blocks_"+ending]=std::move(stored.blocks);}
    return changed;
}
}
