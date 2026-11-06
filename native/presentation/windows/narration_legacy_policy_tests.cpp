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
    for(const std::string_view mode:{"auto","text","inline","block"})for(const std::string_view context:{"auto","cpp"}) {
        const auto old_context=std::string(previous_output_prefix)+std::string(context);
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
        const auto combined=std::string(previous_output_prefix)+std::string(context);
        auto old_output=old_cpp;old_output.context=combined;
        check(same_previous_passage(old_output,modern),"The output-policy transition could not compose with the previous C++ context");
        auto explicit_mode=modern;explicit_mode.math_mode="text";
        check(!same_previous_passage(old_output,explicit_mode),"A C++ context migration silently changed explicit mathMode:text");
        ++groups;
    }
    auto unmarked=modern;unmarked.prepared=previous;
    check(!same_previous_passage(unmarked,modern),"A generic current-policy change acquired an unreviewed legacy alias");
    auto unrelated=modern;unrelated.context="math";
    const Passage old_auto{owner,source,"previous_output_data/auto","auto",previous,0,source.size()};
    check(!same_previous_passage(old_auto,unrelated),"A foreign reading context acquired an output-data policy alias");
    std::cout<<"PASS "<<groups<<" pure historical output/context passage groups; exact owner/source/ranges, explicit modes and changed-speech guards. No playback or listening credit.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL historical narration policy: "<<error.what()<<'\n';return 1;
}
