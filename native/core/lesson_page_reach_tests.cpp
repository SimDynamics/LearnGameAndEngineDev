#include "core/lesson_page_reach.hpp"
#include "core/lesson.hpp"
#include "platform/windows/state_store.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace lgaed;
using namespace lgaed::state;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn>void rejects(Fn fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,"Malformed reachability was accepted");}
curriculum::Lesson lesson(bool mapped=false,int version=2){
    curriculum::Lesson result;result.id="qa_theory_sequence";result.locale="es";
    for(unsigned index=0;index<3;++index){curriculum::Page page;page.stable_id="page_000"+std::to_string(index);
        curriculum::Block block;block.stable_id="block_000"+std::to_string(index)+"_0000";
        block.type=index==2?curriculum::BlockType::multiple_choice:curriculum::BlockType::paragraph;
        block.address.page=index;page.blocks.push_back(block);result.pages.push_back(page);}
    if(mapped){auto& layout=result.state_layout;layout.version=version;layout.locale=version==2?"es":"";
        for(const auto& page:result.pages)layout.origin.push_back({page.stable_id,{page.blocks.front().stable_id}});
        layout.current=layout.origin;layout.lineage=version==2?"qa-v2-lineage":"qa-v1-lineage";layout.legacy_lineage="qa-v1-lineage";
        layout.family["es"].origin=layout.origin;layout.family["es"].sha256=std::string(64,'a');
        for(const auto& page:layout.origin)for(const auto& id:page.blocks)layout.signatures[id]=std::string(64,'b');}
    return result;
}
void unchanged(const StateData& before,const StateData& after){
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
        check(find(after,domain,key)&&*find(after,domain,key)==value,"Reach bookkeeping rewrote unrelated or historical data");
}
void sequence(bool mapped){
    const auto model=lesson(mapped);StateData data;
    if(mapped)prepare_lesson_state(data,model);
    const auto initial=data;
    check(can_reach_page(data,model,0)&&!can_reach_page(data,model,1)&&!can_reach_page(data,model,2),"Fresh theory pages could be skipped");
    check(clamp_reachable_page(data,model,99)==0&&!all_pages_reached(data,model)&&data==initial,"Reading reachability mutated data or inferred completion");
    check(mark_page_reached(data,model,0),"First theory arrival was not recorded");
    check(can_reach_page(data,model,1)&&!can_reach_page(data,model,2),"Theory-to-theory arrival admitted the puzzle too early");
    check(mark_page_reached(data,model,1),"Second theory arrival was not recorded");
    check(can_reach_page(data,model,2)&&!all_pages_reached(data,model),"Unvisited puzzle was credited as a reached page");
    check(mark_page_reached(data,model,2)&&all_pages_reached(data,model),"Admitted puzzle page remained unreachable");
    check(!mark_page_reached(data,model,0)&&can_reach_page(data,model,2),"Backward review reduced the frontier");
    check(!can_reach_page(data,model,3),"Out-of-range page was admitted");
    const auto reopened=decode(encode(data));check(reopened==data&&all_pages_reached(reopened,model),"Reached pages changed across state persistence");
    check(!find(data,"learning_progress","lesson_"+model.id+"_completed")&&!find(data,"lesson_narration_resume_bounded_v1","pages"),
        "Arrival invented an achievement or audio history");
}
void legacy_migration(){
    const auto original=lesson();StateData data;data.preferences["opaque"]["keep"]=std::string(" \tprevious\r\n");
    save_current_page(data,original,2);save_current_page(data,original,0);
    check(current_page(data,original)==0&&all_pages_reached(data,original),"Legacy backward review lost the maximum reached page");
    const auto historical=data;auto mapped=lesson(true);std::swap(mapped.pages[0],mapped.pages[2]);std::swap(mapped.state_layout.current[0],mapped.state_layout.current[2]);
    prepare_lesson_state(data,mapped);unchanged(historical,data);
    check(current_page(data,mapped)==2&&all_pages_reached(data,mapped),"First v2 adoption lost a historical bookmark or legacy frontier");
    auto imported=decode(encode(data));prepare_lesson_state(imported,mapped);
    check(imported==data&&all_pages_reached(imported,mapped),"Mapped imported state lost its navigation coverage");
    // Old exports have no new sidecar. A valid positional bookmark remains
    // compatible without requiring a solved response, started flag or audio.
    StateData old_export;old_export.preferences["learning_progress"]["lesson_"+original.id+"_page"]=std::int64_t{1};
    const auto untouched=old_export;check(can_reach_page(old_export,original,2),"A valid legacy export lost its reached prefix");
    prepare_lesson_state(old_export,mapped);unchanged(untouched,old_export);
    const auto reach=page_reachability(old_export,mapped);
    check(!reach.reached[0]&&reach.reached[1]&&reach.reached[2]&&!all_pages_reached(old_export,mapped),"Legacy origin pages were projected by visible order instead of stable identity");
}
void mapped_holes_and_new_content(){
    auto original=lesson(true);StateData data;prepare_lesson_state(data,original);mark_page_reached(data,original,2);
    const auto saved=data;
    check(can_reach_page(data,original,2)&&can_reach_page(data,original,0)&&!can_reach_page(data,original,1)&&!all_pages_reached(data,original),
        "An old isolated visit either unlocked its unseen gap or lost its review destination");
    auto reordered=original;std::swap(reordered.pages[0],reordered.pages[2]);std::swap(reordered.state_layout.current[0],reordered.state_layout.current[2]);
    prepare_lesson_state(data,reordered);unchanged(saved,data);
    check(can_reach_page(data,reordered,0)&&can_reach_page(data,reordered,1)&&!can_reach_page(data,reordered,2),"Reordered stable arrivals used positional indexes");
    mark_page_reached(data,reordered,1);mark_page_reached(data,reordered,2);check(all_pages_reached(data,reordered),"Filling historical gaps did not restore the complete reached set");
    auto revised=reordered;curriculum::Block added;added.type=curriculum::BlockType::paragraph;added.stable_id="new_theory";
    revised.pages[0].blocks.push_back(added);revised.state_layout.current[0].blocks.push_back(added.stable_id);
    revised.state_layout.additions.push_back({added.stable_id,std::string(64,'c'),{0,1},revised.state_layout.current[0].id});
    prepare_lesson_state(data,revised);const auto before=data;
    check(!page_reachability(data,revised).reached[0]&&!all_pages_reached(data,revised)&&data==before,
        "A newly added theoretical block borrowed the old page's visit");
    mark_page_reached(data,revised,0);check(all_pages_reached(data,revised),"Revisiting a revised page did not record its new visible block");
}
void v1_adoption_and_locale(){
    const auto v1=lesson(true,1);auto v2=lesson(true);StateData data;prepare_lesson_state(data,v1);
    save_current_page(data,v1,2);const auto old=data;prepare_lesson_state(data,v2);unchanged(old,data);
    const auto reached=page_reachability(data,v2);
    check(!reached.reached[0]&&!reached.reached[1]&&reached.reached[2],"v1-to-v2 migration fabricated a prefix from a mapped bookmark");
    const auto legacy=lesson();StateData localized;mark_page_reached(localized,legacy,2);auto french=legacy;french.locale="fr";
    check(!all_pages_reached(localized,french)&&clamp_reachable_page(localized,french,2)==0,"A new legacy frontier leaked into another locale");
    StateData mapped;prepare_lesson_state(mapped,v2);mark_page_reached(mapped,v2,0);auto translated=v2;translated.locale="fr";
    translated.state_layout.locale="fr";translated.state_layout.lineage="qa-v2-french";translated.state_layout.family["fr"]=translated.state_layout.family.at("es");
    prepare_lesson_state(mapped,translated);check(!page_reachability(mapped,translated).reached[0],"Mapped coverage leaked across locale lineage pins");
    // A proven OTHER locale's retained v1 pin is not adoption of this locale.
    auto old_language=v1;std::string origin_frame;
    for(const auto& page:old_language.state_layout.origin){if(!origin_frame.empty())origin_frame+='|';origin_frame+=page.id+':';
        for(std::size_t i=0;i<page.blocks.size();++i){if(i)origin_frame+=',';origin_frame+=page.blocks[i];}}
    old_language.state_layout.lineage=sha256(origin_frame);StateData languages;prepare_lesson_state(languages,old_language);
    save_current_page(languages,old_language,2);languages.preferences["learning_progress"]["lesson_"+v1.id+"_started"]=true;
    auto spanish=v2;spanish.state_layout.legacy_lineage=old_language.state_layout.lineage;prepare_lesson_state(languages,spanish);
    check(!page_reachability(languages,spanish).reached[0],"Same-locale adoption expanded sparse v1 coverage");
    translated.state_layout.legacy_lineage=std::string(64,'d');translated.state_layout.family["es"].origin=old_language.state_layout.origin;
    const auto previous_languages=languages;prepare_lesson_state(languages,translated);unchanged(previous_languages,languages);
    check(all_pages_reached(languages,translated),"A proven foreign v1 pin suppressed the translated family's historical prefix");
}
void malformed_and_no_invented_learning(){
    const auto legacy=lesson();const auto base="lesson_"+legacy.id;const auto frontier=base+"_reached_page_v1_es";
    for(const auto& bad:std::vector<Value>{std::string("2"),std::int64_t{-1},std::int64_t{3},true}){
        StateData data;data.preferences["learning_progress"][frontier]=bad;const auto before=data;
        rejects([&]{(void)can_reach_page(data,legacy,0);});rejects([&]{(void)mark_page_reached(data,legacy,0);});check(data==before,"Rejecting malformed legacy reach modified data");}
    StateData achievements;achievements.preferences["learning_progress"][base+"_completed"]=true;
    achievements.preferences["learning_progress"]["exercise_"+legacy.id+"_page_2_block_0"]=std::string("{\"correct\":true}");
    achievements.preferences["lesson_narration_resume_bounded_v1"]["opaque_pause"]=std::string("preserve");
    const auto untouched=achievements;check(!can_reach_page(achievements,legacy,2)&&achievements==untouched,"Answers/completion/audio fabricated visited theory pages");
    const auto mapped=lesson(true);StateData prepared;prepare_lesson_state(prepared,mapped);
    for(unsigned mode=0;mode<6;++mode){auto data=prepared;auto& prefs=data.preferences["learning_progress"];
        if(mode==0)prefs[base+"_state_lineage_v2_es"]=std::string("foreign");
        if(mode==1)prefs[base+"_read_pages_v2_es"]=std::string("wrong type");
        if(mode==2)prefs[base+"_read_pages_v2_es"]=std::vector<std::string>{"foreign"};
        if(mode==3)prefs[base+"_read_blocks_v2_es"]=std::vector<std::string>{"foreign"};
        if(mode==4)prefs.erase(base+"_read_blocks_v2_es");
        if(mode==5)prefs[base+"_state_lineage_v2_es"]=true;
        const auto before=data;rejects([&]{(void)page_reachability(data,mapped);});rejects([&]{(void)mark_page_reached(data,mapped,0);});
        check(data==before,"Rejecting malformed mapped coverage modified data");}
    StateData invalid_saved;invalid_saved.preferences["learning_progress"][base+"_page"]=std::string("2");const auto before=invalid_saved;
    rejects([&]{save_current_page(invalid_saved,legacy,0);});check(invalid_saved==before,"A refused frontier rewrote the old current bookmark");
}
}
int main()try{
    sequence(false);sequence(true);legacy_migration();mapped_holes_and_new_content();v1_adoption_and_locale();malformed_and_no_invented_learning();
    std::cout<<"PASS reached-page sequence, historical holes/review, v1/v2/legacy adoption, reorder, locale and state roundtrip; no UI/audio or student data.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL page reach: "<<error.what()<<'\n';return 1;}
