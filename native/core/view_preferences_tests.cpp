#include "core/view_preferences.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}}
int main(){using namespace lgaed;using namespace appearance;
    try{
        state::StateData data;data.preferences["learning_progress"]["answer"]=std::string("  <<\n");
        const auto progress=data.preferences["learning_progress"];
        check(global_text_scale(data)==100&&text_scale(data,"cpp_fundamentals")==100,"Original text defaults changed");
        check(!global_code_wrap(data)&&!code_wrap(data,"cpp_fundamentals")&&allows_global_wrap(data,"cpp_fundamentals"),"Original wrap defaults changed");
        save_global_text_scale(data,140);save_course_text_scale(data,"course_a",false,85);
        check(text_scale(data,"course_a")==85&&text_scale(data,"course_b")==140,"Course text exception changed global inheritance");
        save_global_text_scale(data,500);check(global_text_scale(data)==150&&text_scale(data,"course_a")==85,"Text range clamp or course exception failed");
        save_course_text_scale(data,"course_a",true,70);check(text_scale(data,"course_a")==150&&course_text_scale(data,"course_a")==75,"Restoring inheritance lost its separate saved scale");
        data.preferences["native_windows"]["code_wrap"]=true;check(global_code_wrap(data),"Early native explicit wrap preference was lost");
        const std::array<std::string,3> courses{"course_a","course_b","course_c"};
        save_course_wrap(data,"course_a",true);save_wrap_overwrite(data,"course_a",false);save_global_wrap(data,false,courses);
        check(!global_code_wrap(data)&&code_wrap(data,"course_a")&&!code_wrap(data,"course_b"),"Global wrap ignored a protected course");
        save_course_wrap(data,"unrelated_course",true);save_wrap_overwrite(data,"unrelated_course",false);
        reset_wrap_exceptions(data,courses);check(!code_wrap(data,"course_a")&&allows_global_wrap(data,"course_a"),"Route wrap exceptions were not reset");
        check(code_wrap(data,"unrelated_course")&&!allows_global_wrap(data,"unrelated_course"),"Reset changed another route");
        data.preferences["native_windows"]["code_wrap"]=std::string("old invalid inactive prototype field");
        check(!global_code_wrap(data),"Inactive legacy preference overrode Android's canonical wrap value");
        check(data.preferences["learning_progress"]==progress,"Appearance changed exact student data");
        check(state::decode(state::encode(data))==data,"Original domains did not round trip");
        std::cout<<"PASS original text scale inheritance, wrap domains, protected courses, scoped reset, limits and preserved student state\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL view preferences: "<<error.what()<<'\n';return 1;}
}
