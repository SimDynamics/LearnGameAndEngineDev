#include "presentation/windows/narration_legacy_policy.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed::presentation::narration_legacy;
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
}
int main() try {
    const std::string owner="lesson/es/page/block/part/4";
    const std::string source="La salida es `N=26`.";
    const std::string current="La salida es ene igual a veintiséis.";
    const std::string previous="La salida es ene asigna veintiséis.";
    unsigned groups=0;
    for(const auto prefix:{previous_output_prefix,b42_output_prefix})
    for(const std::string_view mode:{"auto","text","inline","block"})for(const std::string_view context:{"auto","cpp"}) {
        const auto old_context=std::string(prefix)+std::string(context);
        const Passage before{owner,source,old_context,mode,previous,0,source.size()};
        const Passage after{owner,source,context,mode,current,0,source.size()};
        check(same_previous_passage(before,after),"An exact output-policy passage lost its authenticated current owner/range");
        auto different=after;different.owner="another block";
        check(!same_previous_passage(before,different),"A different block borrowed an output-policy bookmark");
        different=after;different.source="La salida es `N=27`.";
        check(!same_previous_passage(before,different),"Changed visible output reused an old policy alias");
        different=after;++different.start;
        check(!same_previous_passage(before,different),"A changed sentence start reused an old recording range");
        different=after;--different.end;
        check(!same_previous_passage(before,different),"A changed sentence end reused an old recording range");
        different=after;different.math_mode=mode=="text"?"auto":"text";
        check(!same_previous_passage(before,different),"A changed explicit mathematical mode borrowed historical speech");
        different=after;different.prepared=previous;
        check(!same_previous_passage(before,different),"An unchanged segment incorrectly required a changed-speech alias");
        ++groups;
    }
    const Passage modern{owner,source,"cpp","auto",current,0,source.size()};
    for(const std::string_view context:{"auto","cpp_previous_parentheses"}) {
        const Passage old_cpp{owner,source,context,"auto",previous,0,source.size()};
        check(same_previous_passage(old_cpp,modern),"The existing bounded C++ transition regressed");
        for(const auto prefix:{previous_output_prefix,b42_output_prefix}) {
            const auto combined=std::string(prefix)+std::string(context);
            auto old_output=old_cpp;old_output.context=combined;
            check(same_previous_passage(old_output,modern),"The output-policy transition could not compose with the previous C++ context");
            auto explicit_mode=modern;explicit_mode.math_mode="text";
            check(!same_previous_passage(old_output,explicit_mode),"A C++ context migration silently changed explicit mathMode:text");
        }
        ++groups;
    }
    auto unmarked=modern;unmarked.prepared=previous;
    check(!same_previous_passage(unmarked,modern),"A generic current-policy change acquired an unreviewed legacy alias");
    auto unrelated=modern;unrelated.context="math";
    const Passage old_auto{owner,source,"previous_output_data/auto","auto",previous,0,source.size()};
    check(!same_previous_passage(old_auto,unrelated),"A foreign reading context acquired an output-data policy alias");
    for(const std::string_view invalid:{"previous_output_b42/previous_output_data/auto","previous_output_data/previous_output_b42/auto",
        "previous_output_b42/math","previous_output_b42/","previous_output_unknown/auto"}) {
        auto forged=old_auto;forged.context=invalid;
        check(!same_previous_passage(forged,modern),"A malformed or nested historical policy borrowed a current passage");
    }
    // Real bounded state serialization, with each historical generation in
    // turn. No audio offset or hearing evidence is inferred from a page key.
    namespace narration=lgaed::narration;
    namespace state=lgaed::state;
    for(const std::string_view generation:{"current","b42","original"})for(const std::size_t slot:{0u,1u,2u}) {
        narration::History history;
        for(std::size_t i=0;i<3;++i) {
            narration::Position saved{static_cast<std::int64_t>(i+1),static_cast<std::int64_t>(250+i*100),0,false,
                narration::HeardPrefix{std::string(64,static_cast<char>('a'+i)),12000,30000}};
            history.entries.push_back({"page-key-"+std::to_string(i),"stable-"+std::to_string(i),saved,"page-"+std::to_string(i)});
        }
        history.entries[slot].key=std::string(generation)+"-authenticated-page";
        history.active_key=history.entries[slot].key;history.active_owner="paused-owner";
        state::StateData data;narration::write_history(data,history);
        auto roundtrip=narration::read_history(data);
        check(roundtrip==history,"The original three bounded pauses did not round-trip");
        const auto old_key=roundtrip.entries[slot].key;
        auto mapped=roundtrip.entries[slot].position;
        if(generation!="current") {
            mapped.milliseconds=0;mapped.finished=false;mapped.heard_through=0;mapped.heard_prefix.reset();
        }
        check(rekey_authenticated_bookmark(roundtrip,old_key,"current-authenticated-new-key",mapped,"same-visible-page"),
            "An authenticated historical key could not rename its own existing slot");
        narration::write_history(data,roundtrip);roundtrip=narration::read_history(data);
        check(roundtrip.entries.size()==3,"A policy transition added or removed a bookmark slot");
        check(roundtrip.active_key=="current-authenticated-new-key"&&roundtrip.active_owner==history.active_owner,
            "A policy transition changed the paused reader owner");
        for(std::size_t i=0;i<3;++i) {
            if(i!=slot)check(roundtrip.entries[i]==history.entries[i],"A policy transition changed an unrelated pause or its recency");
            else check(roundtrip.entries[i].position==mapped&&roundtrip.entries[i].stable==history.entries[i].stable,
                "A policy transition invented an audio offset or listening credit");
        }
        const auto unchanged=roundtrip;
        check(!rekey_authenticated_bookmark(roundtrip,"absent-key","new-key",mapped,"page")&&roundtrip==unchanged,
            "A missing historical entry created a fourth bookmark");
        check(!rekey_authenticated_bookmark(roundtrip,roundtrip.entries[0].key,roundtrip.entries[1].key,mapped,"page")&&roundtrip==unchanged,
            "A colliding key overwrote another stored pause");
        ++groups;
    }
    std::cout<<"PASS "<<groups<<" pure historical output/context passage groups; exact owner/source/ranges, explicit modes and changed-speech guards. No playback or listening credit.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL historical narration policy: "<<error.what()<<'\n';return 1;
}
