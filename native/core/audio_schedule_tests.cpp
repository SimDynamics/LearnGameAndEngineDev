#include "core/audio_schedule.hpp"
#include <iostream>
#include <stdexcept>
using namespace lgaed;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    const std::vector<narration::CurriculumAudioPage> pages{
        {{"es","programming","p1","u1","l1"},{},0,"es"},
        {{"es","programming","p1","u1","l1"},{},1,"es"},
        {{"es","programming","p1","u2","l2"},{},0,"es"},
        {{"es","programming","p1","u2","l2"},{},1,"es"},
        {{"es","programming","p2","u3","l3"},{},0,"es"},
        {{"es","math_physics","m1","mu1","ml1"},{},0,"es"},
        {{"es","math_physics","m1","mu1","ml1"},{},1,"es"}};
    auto order=narration::audio_schedule(pages,{"es","programming","p1","u2","l2"},1);
    check(order==std::vector<std::size_t>{4,0,1,2,5,6},"The next curricular page did not precede the saved route turn or background included the active page");
    order=narration::audio_schedule(pages,{"es","programming","p1","u2"},{});
    check(order==std::vector<std::size_t>{0,1,2,3,4,5,6},"Opening a unit reordered background generation");
    order=narration::audio_schedule(pages,{"es","programming","p2"},{});
    check(order==std::vector<std::size_t>{0,1,2,3,4,5,6},"Opening a later course reordered the route's beginning-to-end generation");
    order=narration::audio_schedule(pages,{"es"},{},"math_physics");
    check(order==std::vector<std::size_t>{5,6,0,1,2,3,4},"Home must scan cached pages without consuming a route turn");
    order=narration::audio_schedule(pages,{"es"},{},"programming");
    check(order==std::vector<std::size_t>{0,1,2,3,4,5,6},"Next route recovery changed");
    order=narration::audio_schedule(pages,{"es","programming","p2"},{},"math_physics");
    check(order==std::vector<std::size_t>{5,6,0,1,2,3,4},"An open course starved the other route's saved turn");
    order=narration::audio_schedule(pages,{"es","math_physics","m1","mu1","ml1"},1,"programming");
    check(order==std::vector<std::size_t>{0,1,2,3,4,5},"The active mathematics page was included in background generation");
    const std::vector<narration::CurriculumAudioPage> one_route(pages.begin(),pages.begin()+5);
    check(narration::audio_schedule(one_route,{"es"},{},"math_physics")==std::vector<std::size_t>{0,1,2,3,4},"An exhausted or unavailable route prevented the other route from finishing");
    check(narration::audio_schedule({}, {"es"},{}).empty(),"Empty catalog must terminate");
    const std::vector<narration::CurriculumAudioPage> updated{
        {{"es","programming","p1","u1","l1"},{},0,"es"},
        {{"es","programming","p1","u1","l1"},{},1,"es"},
        {{"es","programming","p1","u1","l1"},{},2,"es"},
        {{"es","programming","p1","u1","l1"},{},3,"es"},
        {{"es","programming","p1","u1","l1"},{},4,"es"},
        {{"es","programming","p1","u1","l1"},{},5,"es"},
        {{"es","programming","p1","u1","l1"},{},6,"es"},
        {{"es","programming","p1","u1","l1"},{},7,"es"},
        {{"es","math_physics","m1","mu1","ml1"},{},0,"es"}};
    const std::vector<std::size_t> revisions{7,2,4};
    check(narration::audio_schedule(updated,{"es","programming","p1","u1","l1"},1,"math_physics",revisions)
        ==std::vector<std::size_t>{2,4,7,8,3,5,6,0},"Changed pages3,5,8 did not precede the saved route turn in curricular order");
    check(narration::audio_schedule(updated,{"es","programming","p1","u1","l1"},1,"math_physics")
        ==std::vector<std::size_t>{8,2,3,4,5,6,7,0},"After changed pages the normal route turn must resume before distant backlog");
    const std::vector<std::size_t> invalid_revisions{0,1,8,99};
    check(narration::audio_schedule(updated,{"es","programming","p1","u1","l1"},1,"math_physics",invalid_revisions)
        ==std::vector<std::size_t>{8,2,3,4,5,6,7,0},"Past/current/other-route/unknown entries escaped revision ordering bounds");
    std::cout<<"PASS studied-page exclusion, following curricular order, saved route turns, exhausted route and empty catalog\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
