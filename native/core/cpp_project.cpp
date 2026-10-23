#include "core/cpp_project.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace lgaed::curriculum {
namespace {
std::string lower(std::string_view value){std::string result(value);for(auto& ch:result)if(ch>='A'&&ch<='Z')ch=static_cast<char>(ch-'A'+'a');return result;}
void require(bool value,const char* reason){if(!value)throw std::invalid_argument(reason);}
bool identifier(std::string_view value,bool target){if(value.empty()||value.size()>128||!((value.front()>='a'&&value.front()<='z')||(value.front()>='A'&&value.front()<='Z')||value.front()=='_'))return false;
    return std::ranges::all_of(value,[target](unsigned char ch){return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||(target&&(ch=='.'||ch=='+'||ch=='-'));});}
void option(std::string_view key,std::string_view value){const auto canonical=lower(key);require(identifier(key,false)&&!canonical.starts_with("cmake_")&&!canonical.starts_with("lgp_"),"Use a project option; CMAKE_ and LGP_ are reserved");
    require(value.size()<=4096&&std::ranges::none_of(value,[](unsigned char ch){return ch<32||ch==127;}),"Invalid CMake option value");}
bool translation_input(std::string_view path){if(is_cpp_source_file(path))return true;const auto dot=path.rfind('.');const auto extension=lower(dot==path.npos?std::string_view{}:path.substr(dot+1));return extension=="h"||extension=="hh"||extension=="hpp"||extension=="hxx"||extension=="inc"||extension=="inl";}
void framed(std::string& out,std::string_view value){const auto length=static_cast<std::uint32_t>(value.size());for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<char>(length>>shift));out.append(value);}
}
bool is_cpp_source_file(std::string_view path){const auto dot=path.rfind('.');if(dot==path.npos)return false;const auto extension=lower(path.substr(dot+1));return extension=="cpp"||extension=="cc"||extension=="cxx"||extension=="c++";}
void validate_cpp_project_path(std::string_view path){
    require(!path.empty()&&!std::isspace(static_cast<unsigned char>(path.front()))&&!std::isspace(static_cast<unsigned char>(path.back())),"A project file needs a relative path without outer whitespace");
    require(std::ranges::none_of(path,[](unsigned char ch){return ch<32||ch==127||std::string_view("\\:<>\"|?*").find(static_cast<char>(ch))!=std::string_view::npos;}),"Invalid project path character");
    for(std::size_t start=0;;){const auto slash=path.find('/',start);const auto part=path.substr(start,slash==path.npos?path.size()-start:slash-start);
        require(!part.empty()&&part!="."&&part!=".."&&part.back()!='.'&&part.back()!=' ',"Invalid project path segment");
        const auto stem=lower(part.substr(0,part.find('.')));const bool numbered=(stem.starts_with("com")||stem.starts_with("lpt"))&&((stem.size()==4&&stem.back()>='1'&&stem.back()<='9')||stem.substr(3)=="\xc2\xb9"||stem.substr(3)=="\xc2\xb2"||stem.substr(3)=="\xc2\xb3");
        require(!numbered&&stem!="con"&&stem!="prn"&&stem!="aux"&&stem!="nul"&&stem!="conin$"&&stem!="conout$","Windows device names are not project files");
        if(slash==path.npos)break;start=slash+1;}
}
void validate_cpp_project_files(const CppProjectFiles& files){require(!files.empty()&&files.size()<=256,"A project needs between1 and256 files");std::set<std::string> names;std::size_t bytes=0;
    for(const auto& [path,text]:files){validate_cpp_project_path(path);require(names.insert(lower(path)).second,"Duplicate or case-colliding project file");require(text.size()<=16*1024*1024&&text.size()<=32*1024*1024-bytes,"Project text exceeds the original local history limits");bytes+=text.size();}
    for(const auto& name:names)for(auto slash=name.find('/');slash!=name.npos;slash=name.find('/',slash+1))require(!names.contains(name.substr(0,slash)),"A project file cannot also be a directory");
}
void validate_cpp_project(const CppProjectFiles& files,const std::vector<std::string>& sources){validate_cpp_project_files(files);require(!sources.empty(),"Select at least one C++ source file");std::set<std::string> seen;
    for(const auto& source:sources){validate_cpp_project_path(source);require(is_cpp_source_file(source)&&seen.insert(lower(source)).second,"Invalid or repeated C++ source selection");require(std::ranges::any_of(files,[&](const auto& item){return item.first==source;}),"Selected source is not a project file");}}
void validate_cmake_config(const CmakeConfig& value){require(identifier(value.target,true),"Invalid CMake target");require(value.build_type=="Debug"||value.build_type=="Release"||value.build_type=="RelWithDebInfo"||value.build_type=="MinSizeRel","Unsupported CMake build type");require(value.options.size()<=64,"A CMake project supports at most64 options");for(const auto& [key,text]:value.options)option(key,text);}
std::map<std::string,std::string> parse_cmake_options(std::string_view text){std::map<std::string,std::string> result;
    while(!text.empty()){const auto newline=text.find_first_of("\r\n");auto line=text.substr(0,newline);
        if(std::ranges::any_of(line,[](unsigned char ch){return !std::isspace(ch);})){const auto equals=line.find('=');require(equals!=line.npos&&equals>0,"Use one KEY=value CMake option per line");auto key=line.substr(0,equals);while(!key.empty()&&std::isspace(static_cast<unsigned char>(key.front())))key.remove_prefix(1);while(!key.empty()&&std::isspace(static_cast<unsigned char>(key.back())))key.remove_suffix(1);const auto value=line.substr(equals+1);option(key,value);require(result.emplace(key,value).second,"Repeated CMake option");}
        if(newline==text.npos)break;const auto consumed=newline+(text[newline]=='\r'&&newline+1<text.size()&&text[newline+1]=='\n'?2:1);text.remove_prefix(consumed);}
    require(result.size()<=64,"Too many CMake options");return result;
}
std::string cpp_project_frame(const CppProjectFiles& files,const std::vector<std::string>& sources,const std::optional<CmakeConfig>& cmake,bool configuration_only){
    std::string result;framed(result,"lgaed-native-cpp-project-v1");framed(result,cmake?"cmake":"msvc");
    if(cmake){framed(result,cmake->build_type);if(!configuration_only)framed(result,cmake->target);framed(result,std::to_string(cmake->options.size()));for(const auto& [key,value]:cmake->options){framed(result,key);framed(result,value);}}
    auto sorted=files;std::ranges::sort(sorted,[](const auto& left,const auto& right){return left.first<right.first;});framed(result,std::to_string(sorted.size()));for(const auto& [path,text]:sorted){framed(result,path);framed(result,configuration_only&&translation_input(path)?std::string_view{}:text);}
    if(!cmake){framed(result,std::to_string(sources.size()));for(const auto& source:sources)framed(result,source);}return result;
}
}
