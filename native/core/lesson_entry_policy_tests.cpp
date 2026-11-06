#include "core/lesson_entry_policy.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::curriculum;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main() try {
    using E=EntryCompletion;
    const auto check=[](std::size_t requested,std::span<const EntryRequirement> states,std::size_t expected,bool waiting){
        const auto result=lesson_entry_decision(requested,7,states);
        require(result.page==expected&&result.pending_math==waiting,"Incorrect linear entry or premature math verdict");
    };
    check(6,{},6,false);
    check(99,{},6,false);
    check(6,std::array{EntryRequirement{3,E::complete},EntryRequirement{1,E::incomplete}},1,false);
    check(6,std::array{EntryRequirement{4,E::incomplete},EntryRequirement{1,E::incomplete}},1,false);
    check(6,std::array{EntryRequirement{1,E::pending_math}},6,true);
    check(6,std::array{EntryRequirement{1,E::pending_math},EntryRequirement{4,E::incomplete}},4,true);
    check(6,std::array{EntryRequirement{1,E::incomplete},EntryRequirement{4,E::pending_math}},1,false);
    check(6,std::array{EntryRequirement{1,E::complete},EntryRequirement{4,E::incomplete}},4,false);
    check(6,std::array{EntryRequirement{1,E::incomplete},EntryRequirement{1,E::pending_math}},1,false);
    check(1,std::array{EntryRequirement{1,E::incomplete},EntryRequirement{1,E::pending_math}},1,false);
    check(0,std::array{EntryRequirement{0,E::incomplete}},0,false);
    // An unsupported compiler is an ordinary incomplete requirement, never a
    // synthetic proof. The policy needs no authored output or student state.
    check(4,std::array{EntryRequirement{2,E::incomplete}},2,false);
    DeferredLessonEntry intent;intent.defer(6);const auto before_import=intent.generation();
    require(intent.requested(before_import)==6,"The requested saved page was discarded before verification");
    intent.invalidate();require(intent.waiting()&&!intent.requested(before_import),"Back navigation did not invalidate its historical callback while preserving capture suspension");
    intent.cancel();require(!intent.waiting()&&!intent.requested(before_import),"A cancelled callback could reopen an old page");
    intent.defer(3);const auto after_import=intent.generation();
    require(!intent.requested(before_import)&&intent.requested(after_import)==3,"Import reused an old entry generation");
    intent.cancel();require(!intent.requested(after_import),"Navigation left a historical entry leap active");
    bool invalid=false;try{(void)lesson_entry_decision(0,0,{});}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"An empty lesson acquired a visible page");
    invalid=false;try{(void)lesson_entry_decision(0,2,std::array{EntryRequirement{2,E::complete}});}
    catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"A requirement outside the lesson was ignored");
    std::cout<<"PASS pure linear entry: ordinary/unsupported compiler barriers, provisional math, clamped pages and stale entry generations. No state or UI created.\n";
    return 0;
} catch(const std::exception& error){std::cerr<<"FAIL lesson entry policy: "<<error.what()<<'\n';return 1;}
