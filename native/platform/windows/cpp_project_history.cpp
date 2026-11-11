#include "platform/windows/cpp_project_history.hpp"
#include "platform/windows/state_store.hpp"
#include <windows.h>
#include <objbase.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <optional>
#include <set>
#include <stdexcept>

namespace lgaed::windows {
namespace {
constexpr std::size_t text_limit=16*1024*1024,revision_limit=32*1024*1024;
bool hex_id(std::string_view id){return id.size()==64&&std::ranges::all_of(id,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool portable_path(std::string_view path){
    if(path.empty()||path.front()==' '||path.back()==' ')return false;
    for(unsigned char c:path)if(c<32||std::string_view("\\:<>\"|?*").find(static_cast<char>(c))!=std::string_view::npos)return false;
    for(std::size_t start=0;;){const auto slash=path.find('/',start);const auto part=path.substr(start,slash==path.npos?path.size()-start:slash-start);
        if(part.empty()||part=="."||part=="..")return false;if(slash==path.npos)break;start=slash+1;}
    return true;
}
void validate(const curriculum::CppProjectFiles& files,const std::vector<std::string>& sources,
    const std::map<std::string,std::string>& indentation){
    require(files.size()<=256&&sources.size()<=256&&indentation.size()<=256,"Too many files in a project revision");std::set<std::string> paths,selected;
    for(const auto& [path,text]:files){require(path.size()<=4096&&portable_path(path)&&paths.insert(path).second,"Invalid or duplicate revision path");require(text.size()<=text_limit,"Revision source exceeds16 MiB");}
    for(const auto& path:paths)for(auto slash=path.find('/');slash!=path.npos;slash=path.find('/',slash+1))require(!paths.contains(path.substr(0,slash)),"A revision file is also a directory");
    for(const auto& path:sources)require(paths.contains(path)&&curriculum::is_cpp_source_file(path)&&selected.insert(path).second,"Invalid or duplicate revision source");
    for(const auto& [path,text]:indentation)require(paths.contains(path)&&text.size()<=text_limit,"Invalid revision indentation");
}
class Reader {
    std::span<const std::uint8_t> bytes_;std::size_t offset_=0;
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes){}
    std::uint64_t number(unsigned count){require(count<=bytes_.size()-offset_,"Incomplete project revision");std::uint64_t value=0;while(count--)value=(value<<8)|bytes_[offset_++];return value;}
    std::string text(std::size_t limit=text_limit){const auto size=number(4);require(size<=limit&&size<=bytes_.size()-offset_,"Invalid revision text length");
        std::string value(reinterpret_cast<const char*>(bytes_.data()+offset_),static_cast<std::size_t>(size));offset_+=static_cast<std::size_t>(size);
        require(winrt::to_string(winrt::to_hstring(value))==value,"Invalid revision UTF-8");return value;}
    std::size_t count(){const auto count=number(4);require(count<=256,"Too many files in a revision");return static_cast<std::size_t>(count);}
    bool done()const{return offset_==bytes_.size();}
};
void number(std::vector<std::uint8_t>& bytes,std::uint64_t value,unsigned count){for(int shift=static_cast<int>((count-1)*8);shift>=0;shift-=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
void text(std::vector<std::uint8_t>& bytes,std::string_view value){require(winrt::to_string(winrt::to_hstring(value))==value,"Invalid revision Unicode");number(bytes,value.size(),4);bytes.insert(bytes.end(),value.begin(),value.end());require(bytes.size()<=revision_limit,"Revision exceeds32 MiB");}
std::string uuid(){GUID id{};require(SUCCEEDED(CoCreateGuid(&id)),"Cannot create revision identity");std::array<wchar_t,40> value{};require(StringFromGUID2(id,value.data(),static_cast<int>(value.size()))>0,"Cannot encode revision identity");return winrt::to_string(std::wstring_view(value.data()+1,36));}
const std::string* file(const curriculum::CppProjectFiles& files,std::string_view path){const auto found=std::ranges::find(files,path,[](const auto& item)->const std::string&{return item.first;});return found==files.end()?nullptr:&found->second;}
CppHistoryChange change(const std::string* before,const std::string* after){if(!before&&!after)return CppHistoryChange::unchanged;if(!before)return CppHistoryChange::added;if(!after)return CppHistoryChange::removed;return *before==*after?CppHistoryChange::unchanged:CppHistoryChange::modified;}
struct Line{std::string text,ending;bool operator==(const Line&)const=default;};
std::vector<Line> lines(std::string_view text){std::vector<Line> result;std::size_t start=0;
    for(std::size_t i=0;i<text.size();++i)if(text[i]=='\r'||text[i]=='\n'){const auto end=i;if(text[i]=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;
        require(result.size()<100000,"Too many lines to compare");result.push_back({std::string(text.substr(start,end-start)),std::string(text.substr(end,i+1-end))});start=i+1;}
    if(start<text.size()){require(result.size()<100000,"Too many lines to compare");result.push_back({std::string(text.substr(start)),""});}return result;}
}
CppHistoryRevision decode_cpp_revision(std::span<const std::uint8_t> bytes,std::string_view id,std::string_view project_key)try{
    require(bytes.size()<=revision_limit&&hex_id(id)&&state::sha256(bytes)==id,"Revision SHA-256 does not match");Reader reader(bytes);CppHistoryRevision result;result.id=id;result.project_key=project_key;
    require(reader.text(128)=="lgp-cpp-local-history-v1"&&reader.text(128)==project_key,"Incompatible revision format or project");result.uuid=reader.text(128);
    require(result.uuid.size()==36,"Invalid revision UUID");for(std::size_t i=0;i<result.uuid.size();++i){const auto c=result.uuid[i];require(i==8||i==13||i==18||i==23?c=='-':(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'),"Invalid revision UUID");}
    result.created_at=static_cast<std::int64_t>(reader.number(8));result.label=reader.text(8192);require(!result.label.empty(),"Revision has no label");
    for(auto count=reader.count();count;--count){auto path=reader.text(4096);auto source=reader.text();result.files.emplace_back(std::move(path),std::move(source));}
    for(auto count=reader.count();count;--count)result.sources.push_back(reader.text(4096));
    for(auto count=reader.count();count;--count){auto path=reader.text(4096);auto value=reader.text();require(result.indentation.emplace(std::move(path),std::move(value)).second,"Duplicate revision indentation");}
    require(reader.done(),"Unknown trailing revision data");validate(result.files,result.sources,result.indentation);return result;
}catch(const winrt::hresult_error& error){throw std::runtime_error("Invalid project revision Unicode: "+winrt::to_string(error.message()));}
CppProjectHistory::CppProjectHistory(std::filesystem::path root,std::string_view identity):root_(std::move(root)),key_(state::sha256(identity)){
    require(!identity.empty()&&identity.size()<=16384,"Invalid project history identity");state::require_safe_path(root_);
}
CppHistoryRevision CppProjectHistory::load(std::string_view id)const{require(hex_id(id),"Invalid revision identity");const auto path=root_/key_/(std::string(id)+".revision");
    return decode_cpp_revision(state::read_bytes(path,revision_limit),id,key_);}
std::vector<CppHistorySummary> CppProjectHistory::summaries(std::stop_token stop)const{const auto directory=root_/key_;state::require_safe_path(directory);std::vector<CppHistorySummary> result;
    if(!std::filesystem::exists(state::native_io_path(directory)))return result;
    for(const auto& entry:std::filesystem::directory_iterator(state::native_io_path(directory)))if(entry.path().extension()==L".revision"){
        if(stop.stop_requested())return {};
        const auto revision=load(entry.path().stem().string());result.push_back({revision.id,revision.label,revision.created_at});}
    std::ranges::sort(result,[](const auto& a,const auto& b){return a.created_at!=b.created_at?a.created_at>b.created_at:a.id>b.id;});return result;}
CppHistoryRevision CppProjectHistory::record(const curriculum::CppProjectFiles& files,const std::vector<std::string>& sources,
    std::string_view label,const std::map<std::string,std::string>& indentation)const{
    validate(files,sources,indentation);const auto name=winrt::to_hstring(label);
    require(label.size()<=8192&&std::ranges::any_of(name,[](wchar_t c){return !std::iswspace(c);}),"Invalid revision label");std::vector<std::uint8_t> bytes;
    text(bytes,"lgp-cpp-local-history-v1");text(bytes,key_);text(bytes,uuid());number(bytes,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),8);text(bytes,label);
    number(bytes,files.size(),4);for(const auto& [path,value]:files){text(bytes,path);text(bytes,value);}number(bytes,sources.size(),4);for(const auto& path:sources)text(bytes,path);
    number(bytes,indentation.size(),4);for(const auto& [path,value]:indentation){text(bytes,path);text(bytes,value);}const auto id=state::sha256(bytes);const auto directory=root_/key_;
    state::require_safe_path(directory);std::filesystem::create_directories(state::native_io_path(directory));state::require_safe_path(directory);
    const auto pending=directory/(id+".pending"),destination=directory/(id+".revision");state::require_safe_path(pending);state::require_safe_path(destination);
    const auto handle=CreateFileW(state::native_io_path(pending).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,"Cannot create pending revision");bool complete=false;
    try{std::size_t cursor=0;while(cursor<bytes.size()){DWORD written=0;const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-cursor,1024*1024));
            require(WriteFile(handle,bytes.data()+cursor,count,&written,nullptr)&&written==count,"Cannot write complete revision");cursor+=written;}
        require(FlushFileBuffers(handle),"Cannot sync revision");CloseHandle(handle);complete=true;state::require_safe_path(directory);
        require(MoveFileExW(state::native_io_path(pending).c_str(),state::native_io_path(destination).c_str(),MOVEFILE_WRITE_THROUGH),"Cannot publish immutable revision");
    }catch(...){if(!complete)CloseHandle(handle);std::error_code error;std::filesystem::remove(state::native_io_path(pending),error);throw;}
    return decode_cpp_revision(bytes,id,key_);
}
std::vector<CppHistoryStatus> CppProjectHistory::statuses(const curriculum::CppProjectFiles& current,const CppHistoryRevision& revision)const{
    require(revision.project_key==key_,"Revision belongs to another project");validate(current,{},{});validate(revision.files,revision.sources,revision.indentation);std::set<std::string> paths;for(const auto& item:revision.files)paths.insert(item.first);for(const auto& item:current)paths.insert(item.first);
    std::vector<CppHistoryStatus> result;for(const auto& path:paths)result.push_back({path,change(file(revision.files,path),file(current,path))});return result;
}
curriculum::CppProjectFiles CppProjectHistory::restore_file(std::string_view path,const curriculum::CppProjectFiles& current,const CppHistoryRevision& revision)const{
    require(portable_path(path)&&revision.project_key==key_,"Invalid revision file or project");validate(current,{},{});validate(revision.files,revision.sources,revision.indentation);auto result=current;
    const auto saved=file(revision.files,path);auto target=std::ranges::find(result,path,[](const auto& item)->const std::string&{return item.first;});
    if(saved){if(target==result.end())result.emplace_back(path,*saved);else target->second=*saved;}else if(target!=result.end())result.erase(target);validate(result,{},{});return result;
}
CppHistoryVerifiedRestoration CppProjectHistory::restore_file_verified(std::string_view path,
        const curriculum::CppProjectFiles& current,const CppHistoryRevision& revision)const{
    // The caller obtains this revision through load(), which verifies the
    // immutable on-disk SHA-256. A different project or invalid id is not
    // admissible as recovery evidence.
    require(hex_id(revision.id)&&revision.project_key==key_,"Invalid recovery revision identity");
    auto recovered=restore_file(path,current,revision);
    const auto saved=file(revision.files,path);
    const auto prior=file(current,path);
    const auto actual=file(recovered,path);
    require(prior||saved,"No saved or current file exists at the requested recovery path");
    require((saved==nullptr)==(actual==nullptr),"Recovered path existence differs from the revision");
    require(!saved||*saved==*actual,"Recovered path differs from the selected revision");
    std::size_t preserved=0;
    for(const auto& [other,content]:current)if(other!=path){
        const auto after=file(recovered,other);
        require(after&&*after==content,"A selective recovery modified an unrelated file");
        ++preserved;
    }
    for(const auto& [other,content]:recovered)if(other!=path)
        require(file(current,other)!=nullptr,
            "A selective recovery created an unrelated file");
    const auto digest=[](const std::string* source)->std::optional<std::string>{
        return source?std::optional<std::string>{state::sha256(*source)}:std::nullopt;};
    const bool changed=(prior==nullptr)!=(actual==nullptr) ||
        (prior&&actual&&*prior!=*actual);
    CppHistoryRecoveryEvidence evidence{revision.id,std::string(path),
        digest(prior),digest(actual),preserved,changed};
    return {std::move(recovered),std::move(evidence)};
}
std::vector<CppHistoryDiffLine> CppProjectHistory::diff(std::string_view path,const curriculum::CppProjectFiles& current,const CppHistoryRevision& revision)const{
    require(portable_path(path)&&revision.project_key==key_,"Invalid revision file or project");validate(current,{},{});validate(revision.files,revision.sources,revision.indentation);const auto before=file(revision.files,path),after=file(current,path);
    const auto old=lines(before?*before:std::string{}),now=lines(after?*after:std::string{});std::vector<CppHistoryDiffLine> result;
    const auto emit=[&](CppHistoryLineType type,const Line& line){result.push_back({type,line.text,line.ending});};std::size_t prefix=0;
    while(prefix<old.size()&&prefix<now.size()&&old[prefix]==now[prefix]){emit(CppHistoryLineType::context,old[prefix]);++prefix;}
    auto a_end=old.size(),b_end=now.size();while(a_end>prefix&&b_end>prefix&&old[a_end-1]==now[b_end-1]){--a_end;--b_end;}
    const auto a_count=a_end-prefix,b_count=b_end-prefix;
    if((a_count+1)*(b_count+1)<=1000000){const auto width=b_count+1;std::vector<std::uint32_t> lcs((a_count+1)*width);
        for(std::size_t a=a_count;a-->0;)for(std::size_t b=b_count;b-->0;)lcs[a*width+b]=old[prefix+a]==now[prefix+b]?lcs[(a+1)*width+b+1]+1:std::max(lcs[(a+1)*width+b],lcs[a*width+b+1]);
        std::size_t a=0,b=0;while(a<a_count||b<b_count){if(a<a_count&&b<b_count&&old[prefix+a]==now[prefix+b]){emit(CppHistoryLineType::context,old[prefix+a]);++a;++b;}
            else if(a<a_count&&(b==b_count||lcs[(a+1)*width+b]>=lcs[a*width+b+1]))emit(CppHistoryLineType::removed,old[prefix+a++]);
            else emit(CppHistoryLineType::added,now[prefix+b++]);}
    }else{for(auto a=prefix;a<a_end;++a)emit(CppHistoryLineType::removed,old[a]);for(auto b=prefix;b<b_end;++b)emit(CppHistoryLineType::added,now[b]);}
    for(auto a=a_end;a<old.size();++a)emit(CppHistoryLineType::context,old[a]);return result;
}
}
