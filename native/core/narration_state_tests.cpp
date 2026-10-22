#include "core/narration_state.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace lgaed::narration;
void check(bool value,const char* what){if(!value)throw std::runtime_error(what);}
int main(){try{
    History history;history=begin(history,"A","ownerA",{0,0,0,false});
    history=checkpoint(history,"A","ownerA",{4,6123,3,true});check(history.entries.front().position.finished,"finish not retained");
    check(history.active_owner.empty(),"finish retains owner");check(restore(history,"A",5)==Position{4,6123,3,true},"finish resume position");
    for(int pause=0;pause<8;++pause){history=begin(history,"B","ownerB",{1,1234,0,false});history=checkpoint(history,"B","ownerB",{1,1500+pause,0,false});}
    check(history.entries.size()==2,"pauses consumed slots");check(restore(history,"A",5)->finished,"A lost when B paused");
    history=begin(history,"C","ownerC",{});history=checkpoint(history,"C","ownerC",{2,4000,2,true});check(history.entries.size()==3,"C not bounded");
    history=begin(history,"D","ownerD",{0,2000,0,false});check(!restore(history,"A",5),"fourth page did not evict oldest");
    check(restore(history,"B",3).has_value()&&restore(history,"C",3)->finished,"unrelated positions lost");
    const auto before=history;history=checkpoint(history,"B","ownerB",{1,0,0,false});check(history==before,"stale callback overwrote newer page");
    lgaed::state::StateData data;data.preferences[resume_domain]["external_metadata"]=std::string("untouched");write_history(data,history);
    check(read_history(data)==history,"Android slots did not roundtrip");check(lgaed::state::string_value(data,resume_domain,"external_metadata")=="untouched","unknown metadata lost");
    auto value=seek({3,3000,2,false},1,500);check(value.segment==1&&value.milliseconds==500&&value.heard_through==2,"rewind changed listened evidence");
    value=seek(value,9,5000);check(value.heard_through==2,"manual forward credited feedback");
    check(!restore(history,"D",0),"invalid empty script resumed");
    History voices;const std::string stable=R"({"version":1,"scope":"route/lesson/lineage","configuration":"unchanged-original-json","parts":[]})";
    voices=begin(voices,"voice-es-pageA","es-owner",{},stable,"pageA");
    voices=checkpoint(voices,"voice-es-pageA","es-owner",{1,3456,0,true});
    voices=begin(voices,"voice-fr-pageA","fr-owner",{},"","pageA");
    check(voices.entries.size()==1&&voices.entries.front().key=="voice-fr-pageA","voice change consumed a second page slot");
    check(voices.entries.front().position.milliseconds==0&&!voices.entries.front().position.finished,"new recording inherited an incompatible offset/finished flag");
    voices=begin(voices,"pageB","B",{},stable,"pageB");voices=begin(voices,"pageC","C",{},stable,"pageC");
    voices=begin(voices,"voice-gb-pageA","gb",{},stable,"pageA");check(voices.entries.size()==3,"same page voice change evicted another page");
    check(std::ranges::any_of(voices.entries,[](const auto& item){return item.page=="pageB";})&&std::ranges::any_of(voices.entries,[](const auto& item){return item.page=="pageC";}),"voice change removed unrelated page");
    voices=begin(voices,"voice-gb-pageA","gb2",{});check(voices.entries.front().stable==stable&&voices.entries.front().page=="pageA","exact-key resumption deleted existing provenance");
    lgaed::state::StateData provenance;write_history(provenance,voices);check(read_history(provenance)==voices,"page metadata or original stable JSON did not roundtrip");
    History legacy;legacy=begin(legacy,"legacyA","old",{});legacy=begin(legacy,"newA","new",{},"","pageA");check(legacy.entries.size()==2,"unknown legacy page identity was guessed");
    std::cout<<"Narration: bounded completed pages, exact offsets, stale owners and independent seeks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
