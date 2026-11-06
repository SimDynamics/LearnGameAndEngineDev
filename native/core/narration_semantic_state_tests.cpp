#include "core/narration_state.hpp"
#include "presentation/windows/narration_legacy_policy.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::narration;
using namespace lgaed::curriculum;
using namespace lgaed::presentation::narration_legacy;
void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
template<class Action>void rejects(Action&& action,std::string_view message){bool caught=false;try{action();}catch(const std::invalid_argument&){caught=true;}check(caught,message);}
SemanticPlanProof plan(bool marked,SemanticRole role=SemanticRole::cpp){
    SemanticRegionProof region{"lesson/es/page/block/part/0","letras y palabras: y","cpp","auto",{}};
    if(marked)region.semantics=TextSemantics{region.source,{{19,20,"y",role,SemanticReading::automatic}}};
    return {"windows-semantic-v1",{region}};
}
History three(){
    History value;
    for(unsigned i=0;i<3;++i)value.entries.push_back({"key-"+std::to_string(i),"stable-"+std::to_string(i),
        Position{1,500+static_cast<std::int64_t>(i),1,false,HeardPrefix{std::string(64,static_cast<char>('a'+i)),12000,30000}},"page-"+std::to_string(i),{}});
    value.active_key=value.entries[1].key;value.active_owner="real-existing-paused-owner";return value;
}
}
int main()try{
    unsigned passed=0;
    for(const bool marked:{false,true})for(const unsigned slot:{0u,1u,2u}){
        auto history=three();history.entries[slot].semantic_plan=plan(marked);state::StateData data;
        data.preferences[resume_domain]["unrelated"]=std::string("preserve");write_history(data,history);
        check(read_history(data)==history,"Exact three-slot semantic proof did not round trip");
        check(state::string_value(data,resume_domain,"unrelated")=="preserve","Writing semantic provenance changed unrelated metadata");
        const auto resumed=begin(history,history.entries[slot].key,"new-owner",history.entries[slot].position);
        check(resumed.entries.front().semantic_plan==history.entries[slot].semantic_plan,"Exact-key resumption lost its source provenance");
        ++passed;
    }
    for(const unsigned slot:{0u,1u,2u}){
        auto before=three();before.entries[slot].semantic_plan=plan(false);state::StateData data;write_history(data,before);
        auto restored=read_history(data);const auto first=restored.entries[slot];
        const std::string context=std::string(semantic_prefix)+"cpp";
        Passage prior{"owner","letras y palabras: y",context,"auto","old prepared",0,20};
        Passage current{"owner","letras y palabras: y","cpp","auto","new prepared",0,20};
        check(same_previous_passage(prior,current),"First exact semantic adoption could not map its genuine old source");
        const auto mapped=changed_speech_position(first.position,false);
        check(mapped.milliseconds==0&&!mapped.finished&&!mapped.heard_prefix&&mapped.heard_through==first.position.heard_through,
            "Changed pronunciation inherited an audio offset/certificate or invented feedback credit");
        check(rekey_authenticated_bookmark(restored,first.key,"semantic-first",mapped,first.page),"First adoption lost its existing bookmark");
        restored.entries[slot].semantic_plan=plan(true);write_history(data,restored);
        const auto first_adoption=read_history(data);check(first_adoption==restored,"First authored ranges did not round trip");
        auto second=first_adoption;const auto first_marked=second.entries[slot];
        check(first_marked.semantic_plan->regions.front().semantics.has_value(),"A saved marked plan lost its exact original range");
        check(rekey_authenticated_bookmark(second,first_marked.key,"semantic-second",changed_speech_position(first_marked.position,false),first_marked.page),
            "Second exact semantic edit consumed another page slot");
        second.entries[slot].semantic_plan=plan(true,SemanticRole::output);write_history(data,second);
        check(read_history(data)==second&&second.entries.size()==3,"Successive annotations lost provenance or created a fourth pause");
        for(unsigned i=0;i<3;++i)if(i!=slot)check(second.entries[i]==before.entries[i],"A semantic edit changed an unrelated page slot/recency");
        auto changed=current;changed.source="letras y otra palabra: y";check(!same_previous_passage(prior,changed),"Partial source similarity borrowed semantic history");
        changed=current;changed.owner="another owner";check(!same_previous_passage(prior,changed),"Another owner borrowed semantic history");
        changed=current;--changed.end;check(!same_previous_passage(prior,changed),"A different source range borrowed semantic history");
        check(changed_speech_position(first.position,true)==first.position,"Unchanged actual recording lost its offset or played prefix");
        ++passed;
    }
    {
        auto history=three();history.entries[0].semantic_plan=plan(true);state::StateData data;write_history(data,history);
        const auto key="semantic_plan_v1_0";const auto encoded=std::get<std::vector<std::string>>(*state::find(data,resume_domain,key));
        for(const std::string_view mutation:{"type","policy","key","page","count","owner","source","presence","overlap","trailing","truncated","surrogate"}){
            auto corrupt=data;
            if(mutation=="type")corrupt.preferences[resume_domain][key]=std::int64_t{1};
            else{
                auto rows=encoded;
                if(mutation=="policy")rows[0]="unknown-policy";
                if(mutation=="key")rows[1]="another-key";
                if(mutation=="page")rows[2]="another-page";
                if(mutation=="count")rows[3]="01";
                if(mutation=="owner")rows[4]="";
                if(mutation=="source")rows[5]="different";
                if(mutation=="presence")rows[8]="true";
                if(mutation=="overlap")rows[11]="18";
                if(mutation=="trailing")rows.push_back("extra");
                if(mutation=="truncated")rows.pop_back();
                if(mutation=="surrogate")rows[5]=std::string("\xed\xa0\x80",3);
                corrupt.preferences[resume_domain][key]=std::move(rows);
            }
            const auto unchanged=corrupt;rejects([&]{(void)read_history(corrupt);},"Corrupt typed semantic provenance was silently accepted");
            check(corrupt==unchanged,"Reading bad semantic provenance modified its saved state");++passed;
        }
        auto invalid=history;invalid.entries[0].semantic_plan->regions[0].semantics->spans[0].text="wrong";
        const auto preserved=data;rejects([&]{write_history(data,invalid);},"An invalid semantic proof was written");
        check(data==preserved,"Failed semantic serialization partially mutated history");
        auto orphan=data;orphan.preferences[resume_domain].erase("key_0");rejects([&]{(void)read_history(orphan);},"Orphan semantic provenance created a bookmark");
    }
    std::cout<<"PASS "<<passed<<" semantic bookmark groups: bounded exact provenance, first/successive annotations, unchanged audio and corrupt-data rejection. No UI or audio.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL semantic bookmark: "<<error.what()<<'\n';return 1;}
