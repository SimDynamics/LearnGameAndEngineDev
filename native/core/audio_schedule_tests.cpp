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
    check(order==std::vector<std::size_t>{3,2,0,1,4,5,6},"Current page/unit/course/route priority changed");
    order=narration::audio_schedule(pages,{"es","programming","p1","u2"},{});
    check(order==std::vector<std::size_t>{2,3,0,1,4,5,6},"Unit menu lost its independent priority");
    order=narration::audio_schedule(pages,{"es","programming","p2"},{});
    check(order==std::vector<std::size_t>{4,0,1,2,3,5,6},"Course menu lost manifest order");
    order=narration::audio_schedule(pages,{"es"},{},"math_physics");
    check(order==std::vector<std::size_t>{5,6,0,1,2,3,4},"Home must scan cached pages without consuming a route turn");
    order=narration::audio_schedule(pages,{"es"},{},"programming");
    check(order==std::vector<std::size_t>{0,1,2,3,4,5,6},"Next route recovery changed");
    check(narration::audio_schedule({}, {"es"},{}).empty(),"Empty catalog must terminate");
    std::cout<<"PASS page/unit/course/menu priority, manifest order, route turns, empty catalog\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
