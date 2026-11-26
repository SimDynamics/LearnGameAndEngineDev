#include "core/narration_state.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>
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
    {
        const HeardPrefix certificate{std::string(64,'a'),123456,1000000};
        Position listened{1,800,0,false,certificate};
        History proofs=begin({},"recording-A","owner-A",{},"","proof-page");
        proofs=checkpoint(proofs,"recording-A","owner-A",listened);
        lgaed::state::StateData evidence;evidence.preferences[resume_domain]["external_metadata"]=std::string("preserved");
        write_history(evidence,proofs);check(read_history(evidence)==proofs,"real prefix did not roundtrip in its existing bookmark slot");
        for(const auto& binding:{std::string(64,'b'),std::string(passage_binding)}){
            auto revised=proofs;
            revised.entries.front().position.heard_prefix->recording_binding=binding;
            auto reopened=evidence;write_history(reopened,revised);
            check(read_history(reopened)==revised,"Reopening lost the origin or identity of the partially heard recording");
        }
        check(heard_prefix_matches(listened,std::string(64,'a'),1000000),"matching audio proof was rejected");
        check(!heard_prefix_matches(listened,std::string(64,'b'),1000000)
            &&!heard_prefix_matches(listened,std::string(64,'a'),1000001),"wrong recording or duration inherited listening credit");
        const auto preview=seek(listened,1,9000);
        check(preview.heard_prefix==listened.heard_prefix&&preview.heard_through==listened.heard_through,
            "manual forward enlarged a real prefix or coarse listening evidence");
        check(!seek(preview,2,300).heard_prefix,"a seek to a different segment retained the old segment's certificate");
        Position zero{1,900,0,false,HeardPrefix{std::string(64,'a'),0,1000000}};
        check(heard_prefix_matches(zero,std::string(64,'a'),1000000)&&zero.heard_prefix->microseconds==0,
            "known zero-credit preview is indistinguishable from legacy data");
        auto stale=begin(proofs,"recording-A","new-owner",listened,"","proof-page");
        const auto current=stale;stale=checkpoint(stale,"recording-A","owner-A",zero);
        check(stale==current,"a stale owner overwrote real prefix evidence");
        const auto other_voice=begin(proofs,"recording-new-voice","voice-owner",listened,"","proof-page");
        check(other_voice.entries.size()==1&&!other_voice.entries[0].position.heard_prefix,
            "a different voice/key inherited an old recording certificate");
        const std::vector<lgaed::state::Value> malformed{
            std::string("wrong type"),std::vector<std::string>{},
            std::vector<std::string>{std::string(64,'a'),"-1","1000000"},
            std::vector<std::string>{std::string(64,'a'),"1000001","1000000"},
            std::vector<std::string>{std::string(64,'a'),"0","0"},
            std::vector<std::string>{std::string(64,'a'),"NaN","1000000"},
            std::vector<std::string>{std::string(64,'a'),"1e6","1000000"},
            std::vector<std::string>{std::string(64,'a'),"9223372036854775808","1000000"},
            std::vector<std::string>{std::string(63,'a'),"100","1000000"},
            std::vector<std::string>{std::string(64,'Z'),"100","1000000"},
            std::vector<std::string>{std::string(64,'a'),"01","1000000"},
            std::vector<std::string>{std::string(64,'a'),"100","1000000","unknown-binding"}
        };
        for(const auto& bad:malformed){auto corrupt=evidence;corrupt.preferences[resume_domain]["heard_prefix_v1_0"]=bad;
            const auto restored=restore(read_history(corrupt),"recording-A",3);
            check(restored&&!restored->heard_prefix&&restored->segment==listened.segment
                &&restored->milliseconds==listened.milliseconds&&restored->heard_through==listened.heard_through,
                "corrupt optional evidence changed the exact legacy bookmark or granted a prefix");}
        auto old=evidence;old.preferences[resume_domain].erase("heard_prefix_v1_0");
        check(restore(read_history(old),"recording-A",3)&&!restore(read_history(old),"recording-A",3)->heard_prefix,
            "legacy offsets without a certificate became listening evidence");
        for(const auto* key:{"B","C","D"})proofs=begin(std::move(proofs),key,key,{});
        write_history(evidence,proofs);check(read_history(evidence)==proofs&&proofs.entries.size()==3,
            "prefix metadata created a fourth history slot or survived its eviction");
        check(!lgaed::state::find(evidence,resume_domain,"heard_prefix_v1_0")
            &&!lgaed::state::find(evidence,resume_domain,"heard_prefix_v1_1")
            &&!lgaed::state::find(evidence,resume_domain,"heard_prefix_v1_2")
            &&lgaed::state::string_value(evidence,resume_domain,"external_metadata")=="preserved",
            "evicting certificates removed external data or kept orphaned slot metadata");
        std::cout<<"Narration: exact bounded prefix certificates, zero-credit preview, malformed/mismatched/legacy data, voice keys, stale owners and seeks passed\n";
    }
    {
        const auto first=std::string(64,'a'),second=std::string(64,'b');
        Position preview{5,6690,4,false,HeardPrefix{first,1689854,9575000},{first,second}};
        History admitted=begin({},"admission-key","owner",{},"","admission-page");
        admitted=checkpoint(admitted,"admission-key","owner",preview);
        lgaed::state::StateData stored;stored.preferences[resume_domain]["external_metadata"]=std::string("untouched");
        write_history(stored,admitted);check(read_history(stored)==admitted,"Feedback admissions failed the existing-slot round trip");
        const auto restored=restore(read_history(stored),"admission-key",8);
        check(restored&&restored->heard_prefix==preview.heard_prefix&&restored->heard_through==preview.heard_through,
            "Navigation admission manufactured contiguous listening evidence");
        check(has_forward_preview_gap(preview,first,9575000),"The validated legacy five-second preview gap was lost");
        check(!has_forward_preview_gap(preview,second,9575000)&&!has_forward_preview_gap(preview,first,9575001),
            "Another actual recording or duration inherited a legacy preview");
        auto missing=preview;missing.heard_prefix.reset();check(!has_forward_preview_gap(missing,first,9575000),"An absent certificate granted a legacy preview");
        auto rounded=preview;rounded.milliseconds=1690;check(!has_forward_preview_gap(rounded,first,9575000),"Clock rounding was mistaken for a manual seek");
        auto clock_gap=preview;clock_gap.heard_prefix->microseconds=1000000;clock_gap.milliseconds=1040;
        check(!has_forward_preview_gap(clock_gap,first,9575000),"A short legacy pause clock delay granted forward admission");
        clock_gap.milliseconds=1250;check(has_forward_preview_gap(clock_gap,first,9575000),"The conservative legacy preview threshold was not inclusive");
        auto outside=preview;outside.milliseconds=9576;check(!has_forward_preview_gap(outside,first,9575000),"A position beyond the actual recording granted a preview");
        auto zero=preview;zero.heard_prefix->microseconds=0;check(has_forward_preview_gap(zero,first,9575000),"A verified zero-credit explicit preview could not resume");
        check(seek(preview,6,4000).feedback_admissions==preview.feedback_admissions&&seek(preview,6,4000).heard_through==4,
            "A visual seek changed navigation permissions or fabricated hearing");
        const std::vector<lgaed::state::Value> malformed{
            std::string("wrong type"),std::vector<std::string>{},
            std::vector<std::string>{"other-key","admission-page",first},
            std::vector<std::string>{"admission-key","other-page",first},
            std::vector<std::string>{"admission-key","admission-page",first,first},
            std::vector<std::string>{"admission-key","admission-page",std::string(64,'Z')},
            std::vector<std::string>{"admission-key","admission-page",std::string(63,'a')},
            std::vector<std::string>(1027,first)};
        for(const auto& value:malformed){auto corrupt=stored;corrupt.preferences[resume_domain]["feedback_admissions_v1_0"]=value;
            const auto recovered=restore(read_history(corrupt),"admission-key",8);
            check(recovered&&recovered->feedback_admissions.empty()&&recovered->segment==preview.segment&&recovered->milliseconds==preview.milliseconds&&
                recovered->heard_prefix==preview.heard_prefix&&recovered->heard_through==preview.heard_through,
                "Malformed or foreign optional admissions granted passage or moved a real bookmark");}
        auto old=stored;old.preferences[resume_domain].erase("feedback_admissions_v1_0");
        check(restore(read_history(old),"admission-key",8)->feedback_admissions.empty(),"An old slot acquired invented admissions");
        const auto changed=begin(admitted,"another-voice-key","next",preview,"","admission-page");
        check(changed.entries.size()==1&&changed.entries.front().position.feedback_admissions.empty(),"An unknown recording key inherited another voice's admissions");
        auto invalid=admitted;invalid.entries.front().position.feedback_admissions.push_back(first);
        bool rejected=false;try{auto target=stored;write_history(target,invalid);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"The persister accepted duplicate admissions");
        invalid=admitted;invalid.entries.front().page.clear();rejected=false;
        try{auto target=stored;write_history(target,invalid);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"The persister accepted orphaned admissions");
        invalid=admitted;auto& limit=invalid.entries.front().position.feedback_admissions;limit.clear();
        for(unsigned index=0;index<1024;++index){std::string digest(64,'a');
            for(unsigned digit=0;digit<4;++digit)digest[63-digit]="0123456789abcdef"[(index>>(digit*4))&15];limit.push_back(std::move(digest));}
        auto bounded=stored;write_history(bounded,invalid);check(read_history(bounded)==invalid,"The exact bounded admission capacity failed its round trip");
        limit.push_back(std::string(64,'f'));rejected=false;
        try{auto target=stored;write_history(target,invalid);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"An admission list exceeded its bounded slot");
        for(const auto* key:{"next-B","next-C","next-D"})admitted=begin(std::move(admitted),key,key,{});
        write_history(stored,admitted);check(read_history(stored)==admitted&&admitted.entries.size()==3,
            "Navigation admissions created parallel/fourth history slots");
        for(int index=0;index<3;++index)check(!lgaed::state::find(stored,resume_domain,"feedback_admissions_v1_"+std::to_string(index)),
            "Evicted admissions survived in another page's slot");
        check(lgaed::state::string_value(stored,resume_domain,"external_metadata")=="untouched","Admission eviction removed unrelated data");
        std::cout<<"Narration: bounded source admissions, separate genuine heard evidence, strict owners, legacy preview gaps and eviction passed\n";
    }
    std::cout<<"Narration: bounded completed pages, exact offsets, stale owners and independent seeks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
