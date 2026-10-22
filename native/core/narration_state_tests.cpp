#include "core/narration_state.hpp"
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
    std::cout<<"Narration: bounded completed pages, exact offsets, stale owners and independent seeks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
