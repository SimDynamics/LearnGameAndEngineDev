#include "core/cpp_project.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Action>void rejects(Action action,const char* reason){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,reason);}
}
int main(){using namespace lgaed;using namespace curriculum;try{
    windows::ensure_winrt_apartment();
    const auto parsed=windows::parse_lesson_json(R"({"id":"project","title":"Project","pages":[{"blocks":[{"type":"cpp_compiler","projectFiles":[{"path":"lib/value.cpp","initialCode":"int value(){return 4;}\n"},{"path":"main.cpp","initialCode":"int main(){}\n"},{"path":"value.hpp","initialCode":"\tint value();\r\n"}]}]}]})");
    const auto& block=parsed.pages[0].blocks[0];check(block.project_files.size()==3&&block.project_files[0].first=="lib/value.cpp"&&block.project_files[2].second=="\tint value();\r\n"&&block.source_files==std::vector<std::string>{"lib/value.cpp","main.cpp"},"Project parser changed file order/text or implicit source selection");
    const auto cmake=windows::parse_lesson_json(R"({"id":"cmake","title":"CMake","pages":[{"blocks":[{"type":"cpp_compiler","projectFiles":[{"path":"CMakeLists.txt","initialCode":"project(test)\n"}],"cmake":{"target":"example","options":{"MESSAGE":" a = b "},"buildType":"RelWithDebInfo"},"outputCheck":{"cases":[{"standardInput":"","outputs":["ok"],"target":"example"}]}}]}]})");
    const auto& project=cmake.pages[0].blocks[0];check(project.cmake&&project.cmake->target=="example"&&project.cmake->options.at("MESSAGE")==" a = b "&&project.cmake->build_type=="RelWithDebInfo"&&project.source_files.empty()&&project.output_check->cases[0].executable=="example","CMake parser changed the original target/options/type/output contract");
    for(const auto* path:{"../escape.cpp","/absolute.cpp","a//b.cpp","a/./b.cpp","a\\b.cpp","C:drive.cpp","file.cpp ","nul.cpp","COM1.hpp","directory./a.cpp","conin$"})rejects([&]{validate_cpp_project_path(path);},"Unsafe Windows project path accepted");
    rejects([]{validate_cpp_project_files({{"main.cpp","a"},{"MAIN.CPP","b"}});},"Case-colliding files accepted");rejects([]{validate_cpp_project_files({{"folder","a"},{"folder/main.cpp","b"}});},"File/directory conflict accepted");
    rejects([&]{validate_cpp_project(block.project_files,{"value.hpp"});},"Header selected as a translation unit");rejects([&]{validate_cpp_project(block.project_files,{"main.cpp","main.cpp"});},"Repeated source selection accepted");
    rejects([]{(void)parse_cmake_options("CMAKE_CXX_COMPILER=other");},"Reserved compiler override accepted");rejects([]{(void)parse_cmake_options("A=1\nA=2");},"Repeated options accepted");
    const auto options=parse_cmake_options(" A =  exact = value \r\nB=\n");check(options.at("A")=="  exact = value "&&options.at("B").empty(),"Option parsing stripped deliberate value whitespace");
    CppProjectFiles files{{"CMakeLists.txt","project(test)"},{"main.cpp","int main(){}"},{"value.hpp","int value();"}};CmakeConfig config{"first",{{"VALUE","1"}},"Debug"};
    const auto full=cpp_project_frame(files,{},config),configuration=cpp_project_frame(files,{},config,true);auto reordered=files;std::ranges::reverse(reordered);check(cpp_project_frame(reordered,{},config)==full,"File display order changed the build identity");
    files[1].second+="\n";check(cpp_project_frame(files,{},config)!=full&&cpp_project_frame(files,{},config,true)==configuration,"Source edits did not invalidate only built state");files[2].second+="\n";check(cpp_project_frame(files,{},config,true)==configuration,"A header edit unnecessarily invalidated configure state");
    auto target=config;target.target="second";check(cpp_project_frame(files,{},target,true)==configuration&&cpp_project_frame(files,{},target)!=cpp_project_frame(files,{},config),"Target selection did not separate build/configuration identities");
    files[0].second+="\n";check(cpp_project_frame(files,{},config,true)!=configuration,"CMake description edits reused old configuration");
    std::cout<<"PASS original project/CMake parser, exact text/options/source ordering, safe Windows paths and selective configuration/build identities\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}}
