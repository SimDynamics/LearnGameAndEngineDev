#include "presentation/windows/audio_course_view.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/card_list.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/settings_view.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <utility>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
void check_cancelled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Course audio scan cancelled");}
bool cache_key(std::string_view key){return key.size()==64&&std::ranges::all_of(key,[](char c){return (c>='a'&&c<='f')||(c>='0'&&c<='9');});}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;explicit Handle(HANDLE handle):value(handle){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
};
std::uint32_t little32(const std::vector<std::uint8_t>& bytes,std::size_t index){return bytes[index]|static_cast<std::uint32_t>(bytes[index+1])<<8|static_cast<std::uint32_t>(bytes[index+2])<<16|static_cast<std::uint32_t>(bytes[index+3])<<24;}
std::uint16_t little16(const std::vector<std::uint8_t>& bytes,std::size_t index){return bytes[index]|static_cast<std::uint16_t>(bytes[index+1])<<8;}
bool verified_audio(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){
    if(path.extension()==L".ogg")return bytes.size()>=64&&bytes.size()<=96*1024*1024&&std::memcmp(bytes.data(),"OggS",4)==0&&
        std::string_view(reinterpret_cast<const char*>(bytes.data()),std::min<std::size_t>(128,bytes.size())).find("OpusHead")!=std::string_view::npos;
    if(path.extension()!=L".wav"||bytes.size()<46||bytes.size()>96*1024*1024)return false;
    return std::memcmp(bytes.data(),"RIFF",4)==0&&std::memcmp(bytes.data()+8,"WAVE",4)==0&&std::memcmp(bytes.data()+12,"fmt ",4)==0&&
        std::memcmp(bytes.data()+36,"data",4)==0&&little32(bytes,16)==16&&little16(bytes,20)==1&&little16(bytes,22)==1&&
        little32(bytes,24)==24000&&little32(bytes,28)==48000&&little16(bytes,32)==2&&little16(bytes,34)==16&&
        static_cast<std::uint64_t>(little32(bytes,4))+8==bytes.size()&&static_cast<std::uint64_t>(little32(bytes,40))+44==bytes.size()&&little32(bytes,40)%2==0;
}
std::vector<std::uint8_t> read_handle(HANDLE file,std::uint64_t size,std::stop_token stop){
    if(size>96*1024*1024)throw std::runtime_error("Course audio file exceeds the verified cache limit");
    LARGE_INTEGER beginning{};if(!SetFilePointerEx(file,beginning,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot read a verified audio file");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));std::size_t offset=0;
    while(offset<bytes.size()){check_cancelled(stop);DWORD read=0;if(!ReadFile(file,bytes.data()+offset,static_cast<DWORD>(std::min<std::size_t>(1024*1024,bytes.size()-offset)),&read,nullptr)||!read)throw std::runtime_error("The audio changed while it was being read");offset+=read;}
    return bytes;
}
std::optional<CourseAudioFile> fingerprint_audio(const std::filesystem::path& path,std::stop_token stop){
    if(!cache_key(path.stem().string())||(path.extension()!=L".wav"&&path.extension()!=L".ogg"))return {};
    Handle handle(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(handle.value==INVALID_HANDLE_VALUE)return {};
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(handle.value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return {};
    const auto size=static_cast<std::uint64_t>(info.nFileSizeHigh)<<32|info.nFileSizeLow;if(size<46||size>96*1024*1024)return {};
    const auto bytes=read_handle(handle.value,size,stop);if(!verified_audio(path,bytes))return {};
    BY_HANDLE_FILE_INFORMATION after{};if(!GetFileInformationByHandle(handle.value,&after)||info.nFileIndexHigh!=after.nFileIndexHigh||info.nFileIndexLow!=after.nFileIndexLow||info.nFileSizeHigh!=after.nFileSizeHigh||info.nFileSizeLow!=after.nFileSizeLow||CompareFileTime(&info.ftLastWriteTime,&after.ftLastWriteTime)!=0)return {};
    return CourseAudioFile{path,path.stem().string(),state::sha256(bytes),size,static_cast<std::uint64_t>(info.nFileIndexHigh)<<32|info.nFileIndexLow,info.dwVolumeSerialNumber};
}
bool same_source(const CourseAudioSource& source,bool digest,std::stop_token stop){
    check_cancelled(stop);std::error_code error;const bool exists=std::filesystem::exists(source.path,error);if(error||exists!=source.exists)return false;if(!exists)return true;if(!std::filesystem::is_regular_file(source.path,error)||error)return false;
    if(std::filesystem::file_size(source.path,error)!=source.bytes||error||std::filesystem::last_write_time(source.path,error)!=source.modified||error)return false;
    return !digest||state::sha256(state::read_bytes(source.path,8*1024*1024))==source.sha256;
}
std::optional<std::string> explicit_language(const JsonObject& json){
    const auto direct=[](const JsonObject& value)->std::optional<std::string>{for(const auto key:{L"language",L"locale",L"lang",L"idioma"}){
        const auto item=value.GetNamedValue(key,JsonValue::CreateNullValue());if(item.ValueType()==JsonValueType::String)if(const auto tag=windows::narration_language_tag(winrt::to_string(item.GetString())))return tag;}return {};};
    if(const auto language=direct(json))return language;
    const auto metadata=json.GetNamedValue(L"metadata",JsonValue::CreateNullValue());return metadata.ValueType()==JsonValueType::Object?direct(metadata.GetObject()):std::nullopt;
}
CourseAudioCoverage coverage(const std::set<std::string>& expected,const std::map<std::string,std::vector<CourseAudioFile>>& inventory){
    CourseAudioCoverage result;result.expected=expected.size();for(const auto& key:expected)if(const auto available=inventory.find(key);available!=inventory.end()&&!available->second.empty()){
        ++result.generated;result.files+=available->second.size();for(const auto& file:available->second)result.bytes+=file.bytes;}return result;
}
}
std::optional<unsigned> CourseAudioCoverage::percentage()const{return expected?std::optional<unsigned>(static_cast<unsigned>((generated*100+expected/2)/expected)):std::nullopt;}
CourseAudioReport scan_course_audio(const std::filesystem::path& content,const std::filesystem::path& cache,CourseAudioProfile profile,std::string_view route,bool complete,const CourseAudioPlanner& plan,std::stop_token stop){
    windows::ensure_winrt_apartment();if(!plan||(!route.empty()&&route!="programming"&&route!="math_physics"))throw std::invalid_argument("Invalid course audio scan");
    curriculum::validate_catalog_location({profile.locale});CourseAudioReport result;result.profile=std::move(profile);result.complete=complete;
    std::error_code error;if(std::filesystem::is_directory(cache,error)){state::require_safe_path(cache);for(const auto& file:std::filesystem::directory_iterator(cache)){
        check_cancelled(stop);if(const auto item=fingerprint_audio(file.path(),stop))result.inventory[item->key].push_back(*item);}}
    std::set<std::filesystem::path> observed;
    const auto observe=[&](const std::filesystem::path& relative){for(const auto& locale:{result.profile.locale,std::string("es")}){
        const auto path=content/locale/relative;if(!observed.insert(path).second)continue;state::require_safe_path(path);CourseAudioSource source;source.path=path;
        source.exists=std::filesystem::is_regular_file(path);if(source.exists){const auto bytes=state::read_bytes(path,8*1024*1024);source.bytes=bytes.size();source.modified=std::filesystem::last_write_time(path);source.sha256=state::sha256(bytes);}result.sources.push_back(std::move(source));}};
    const auto ordered=[&](curriculum::CatalogLocation location){check_cancelled(stop);observe(curriculum::catalog_index_path(location));auto entries=windows::load_catalog(content,location).entries;std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.number<b.number;});return entries;};
    for(const auto path:{"programming","math_physics"}){
        if(!complete&&!route.empty()&&route!=path)continue;
        const curriculum::CatalogLocation courses{result.profile.locale,path};
        for(const auto& course:ordered(courses)){
            CourseAudioCourse item;item.owner={path,course.id};item.number=course.number;item.title=course.title;
            const auto units=curriculum::catalog_child(courses,course.id);observe(curriculum::catalog_index_path(units));
            // The published roadmap intentionally includes courses with no course
            // manifest. Missing units/lessons inside a packaged course are errors.
            if(course.available)for(const auto& unit:ordered(units)){
                if(!unit.available){if(complete)throw std::runtime_error("A published unit is missing; ownership is incomplete");continue;}
                const auto lessons=curriculum::catalog_child(units,unit.id);
                for(const auto& lesson:ordered(lessons)){
                    observe(curriculum::catalog_lesson_path(lessons,lesson.id));
                    if(!lesson.available){if(complete)throw std::runtime_error("A published lesson is missing; ownership is incomplete");continue;}
                    auto bytes=state::read_bytes(lesson.asset,8*1024*1024);std::string raw(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(raw.starts_with("\xEF\xBB\xBF"))raw.erase(0,3);
                    const auto json=JsonObject::Parse(winrt::to_hstring(raw));const auto pages=json.GetNamedArray(L"pages");
                    for(unsigned page=0;page<pages.Size();++page){
                        check_cancelled(stop);const auto language=explicit_language(pages.GetObjectAt(page)).value_or(explicit_language(json).value_or(result.profile.language));
                        const auto voice=result.profile.voices.find(language);if(voice==result.profile.voices.end())throw std::runtime_error("The selected language has no valid Kokoro voice");
                        std::string reply;
                        while(true){check_cancelled(stop);try{reply=plan(raw,page,language,voice->second,result.profile.ui_language,stop);break;}catch(const windows::NarrationPreempted&){
                            for(int wait=0;wait<20;++wait){check_cancelled(stop);std::this_thread::sleep_for(10ms);}}}
                        const auto segments=JsonObject::Parse(winrt::to_hstring(reply)).GetNamedArray(L"segments");for(const auto& value:segments){const auto key=winrt::to_string(value.GetObject().GetNamedString(L"key"));if(!cache_key(key))throw std::runtime_error("The narration plan returned an invalid cache key");item.expected.insert(key);}
                    }
                }
            }
            item.coverage=coverage(item.expected,result.inventory);result.courses.push_back(std::move(item));
        }
    }
    for(const auto& source:result.sources)if(!same_source(source,true,stop))throw std::runtime_error("The curriculum changed during the audio scan");
    return result;
}
CourseAudioRemovalPreview preview_course_audio_removal(CourseAudioReport report,CourseAudioRef target){
    if(!report.complete)throw std::invalid_argument("Safe deletion requires a complete scan of both learning paths");
    const auto course=std::ranges::find_if(report.courses,[&](const auto& entry){return entry.owner==target;});if(course==report.courses.end())throw std::invalid_argument("The course is absent from the current curriculum");
    CourseAudioRemovalPreview preview;preview.target=std::move(target);
    for(const auto& key:course->expected)if(const auto available=report.inventory.find(key);available!=report.inventory.end()){
        const bool shared=std::ranges::any_of(report.courses,[&](const auto& entry){return entry.owner!=preview.target&&entry.expected.contains(key);});
        if(shared)preview.shared_files+=available->second.size();else for(const auto& file:available->second){preview.exclusive.push_back(file);preview.removable_bytes+=file.bytes;}}
    preview.report=std::move(report);return preview;
}
CourseAudioRemovalResult remove_confirmed_course_audio(const CourseAudioRemovalPreview& preview,const CourseAudioProfile& current,const std::function<bool()>& permit,std::stop_token stop){
    CourseAudioRemovalResult result;try{if(!preview.report.complete){result.status=CourseAudioRemovalStatus::changed;return result;}if(!permit||!permit()){result.status=CourseAudioRemovalStatus::busy;return result;}
    if(current!=preview.report.profile){result.status=CourseAudioRemovalStatus::changed;return result;}
    if(stop.stop_requested())return result;
    for(const auto& source:preview.report.sources)if(!same_source(source,true,stop)){result.status=CourseAudioRemovalStatus::changed;return result;}
    // Lock the exact verified files by identity before deleting anything. Existing
    // writers, substitutions and symlinks fail closed; no pathname wildcard exists.
    std::vector<Handle> handles;handles.reserve(preview.exclusive.size());
    for(const auto& file:preview.exclusive){
        if(stop.stop_requested())return result;if(!permit()){result.status=CourseAudioRemovalStatus::busy;return result;}
        Handle handle(CreateFileW(file.path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(handle.value==INVALID_HANDLE_VALUE){result.status=CourseAudioRemovalStatus::changed;return result;}
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(handle.value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||info.dwVolumeSerialNumber!=file.volume||
            (static_cast<std::uint64_t>(info.nFileIndexHigh)<<32|info.nFileIndexLow)!=file.file_id||
            (static_cast<std::uint64_t>(info.nFileSizeHigh)<<32|info.nFileSizeLow)!=file.bytes){result.status=CourseAudioRemovalStatus::changed;return result;}
        const auto bytes=read_handle(handle.value,file.bytes,stop);if(!verified_audio(file.path,bytes)||state::sha256(bytes)!=file.sha256){result.status=CourseAudioRemovalStatus::changed;return result;}handles.push_back(std::move(handle));
    }
    result.status=CourseAudioRemovalStatus::completed;
    for(std::size_t index=0;index<handles.size();++index){
        if(stop.stop_requested()||!permit()){result.status=CourseAudioRemovalStatus::cancelled;return result;}
        if(std::ranges::any_of(preview.report.sources,[&](const auto& source){return !same_source(source,false,stop);})){result.status=CourseAudioRemovalStatus::changed;return result;}
        FILE_DISPOSITION_INFO disposition{TRUE};if(SetFileInformationByHandle(handles[index].value,FileDispositionInfo,&disposition,sizeof(disposition))){++result.deleted;result.bytes+=preview.exclusive[index].bytes;}else ++result.failed;
    }
    return result;
    }catch(const std::exception&){if(stop.stop_requested()){result.status=CourseAudioRemovalStatus::cancelled;return result;}throw;}
}

namespace {
constexpr wchar_t course_window_class[]=L"LearnGameAndEngineDev.CourseAudio";
constexpr int back_id=6100,refresh_id=6101,info_id=6102,list_id=6103,title_id=6104,status_id=6105,details_id=6106,pause_id=6107,delete_id=6108;
constexpr UINT activate_course_message=WM_APP+270,job_message=WM_APP+271;
enum class Operation {none,report,preview,remove};
std::string audio_size(std::uint64_t bytes,std::string_view locale={}){char label[64]{};std::snprintf(label,sizeof(label),"%.1f",static_cast<double>(bytes)/(1024.*1024.));std::string result(label);if(locale=="es"||locale=="fr")std::replace(result.begin(),result.end(),'.',',');return result+(locale=="fr"?" Mio":" MiB");}
struct CourseAudioWindow {
    HWND window=nullptr;CourseAudioAccess access;std::string route;CourseAudioProfile profile;
    HFONT font=nullptr,title_font=nullptr;bool owns=false,closing=false;
    Operation operation=Operation::none;std::optional<std::size_t> selected;
    std::shared_ptr<CourseAudioReport> report;
    std::shared_ptr<void> maintenance;
    std::jthread job;std::mutex result_mutex;
    struct Reply{Operation operation;std::shared_ptr<CourseAudioReport> report;std::shared_ptr<CourseAudioRemovalPreview> preview;CourseAudioRemovalResult removal;std::string error;bool cancelled=false;};
    std::optional<Reply> pending;
    CourseAudioWindow(std::string path,CourseAudioAccess callbacks):access(std::move(callbacks)),route(std::move(path)),profile(access.profile()){}
    ~CourseAudioWindow(){if(job.joinable()){job.request_stop();job.join();}maintenance.reset();if(font)DeleteObject(font);if(title_font)DeleteObject(title_font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    std::string tr(const char* es,const char* en,const char* fr)const{return profile.ui_language=="fr"?fr:profile.ui_language.starts_with("en")?en:es;}
    std::string route_title()const{return route=="programming"?tr("Programación","Programming","Programmation"):tr("Matemáticas y Física","Mathematics and Physics","Mathématiques et physique");}
    void status(std::string_view text){SetWindowTextW(GetDlgItem(window,status_id),wide(text).c_str());}
    HWND control(const wchar_t* kind,std::string text,int id,DWORD style=0){const auto child=CreateWindowExW(0,kind,wide(text).c_str(),WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child)throw std::runtime_error("Cannot create a course-audio control");reader_theme::apply(child,id==title_id||id==status_id?reader_theme::Surface::screen:reader_theme::Surface::catalog_card);
        if(id==title_id)reader_theme::set_text_color(child,RGB(255,255,255));else if(id==status_id||id==details_id)reader_theme::set_text_color(child,RGB(210,214,220));
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(id==title_id?title_font:font),FALSE);return child;}
    const CourseAudioCourse* course()const{return report&&selected&&*selected<report->courses.size()?&report->courses[*selected]:nullptr;}
    std::string count_text(const CourseAudioCourse& course)const{const auto& coverage=course.coverage;const auto percentage=coverage.percentage();return std::to_string(coverage.generated)+"/"+std::to_string(coverage.expected)+" ("+(percentage?std::to_string(*percentage)+"%":tr("Sin frases previstas","No utterances expected","Aucun énoncé prévu"))+") · "+std::to_string(coverage.files)+tr(" archivos · "," files · "," fichiers · ")+audio_size(coverage.bytes,profile.ui_language);}
    std::string row_text(std::size_t index)const{if(!report||index>=report->courses.size())return {};const auto& item=report->courses[index];return tr("Curso ","Course ","Cours ")+std::to_string(item.number)+tr(": ",": "," : ")+item.title+"\n"+count_text(item)+(access.paused(item.owner)?"\n"+tr("Generación anticipada pausada","Advance generation paused","Génération anticipée en pause"):std::string{});}
    int row_height(std::size_t index)const{RECT bounds{};GetClientRect(GetDlgItem(window,list_id),&bounds);const auto dc=GetDC(window);const auto old=SelectObject(dc,font);RECT text{0,0,std::max(px(80),static_cast<int>(bounds.right)-px(40)),0};DrawTextW(dc,wide(row_text(index)).c_str(),-1,&text,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);return std::max(px(100),static_cast<int>(text.bottom)+px(32));}
    void refresh_fonts(){
        const auto body=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        const auto heading=CreateFontW(-px(26),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(!body||!heading){if(body)DeleteObject(body);if(heading)DeleteObject(heading);throw std::runtime_error("Cannot create course-audio fonts");}
        for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetDlgCtrlID(child)==title_id?heading:body),FALSE);
        if(font)DeleteObject(font);if(title_font)DeleteObject(title_font);font=body;title_font=heading;
    }
    void initialize(){
        reader_theme::apply(window,reader_theme::Surface::screen);refresh_fonts();
        reader_theme::primary_button(control(L"BUTTON",tr("Volver","Back","Retour"),back_id,WS_TABSTOP|BS_OWNERDRAW));
        control(L"STATIC",route_title(),title_id,SS_LEFT);control(L"STATIC","",status_id,SS_LEFT);
        (void)create_card_list(window,list_id,activate_course_message);
        control(MSFTEDIT_CLASS,"",details_id,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL);
        SendDlgItemMessageW(window,details_id,EM_SETTARGETDEVICE,0,0);
        reader_theme::primary_button(control(L"BUTTON",tr("Actualizar","Refresh","Actualiser"),refresh_id,WS_TABSTOP|BS_PUSHBUTTON));
        control(L"BUTTON",tr("Información","Information","Informations"),info_id,WS_TABSTOP|BS_PUSHBUTTON);
        reader_theme::primary_button(control(L"BUTTON","",pause_id,WS_TABSTOP|BS_PUSHBUTTON|BS_MULTILINE));
        control(L"BUTTON",tr("Eliminar audios","Delete audio","Supprimer les audios"),delete_id,WS_TABSTOP|BS_PUSHBUTTON|BS_MULTILINE);
        layout();scan(false);
    }
    void layout(){RECT bounds{};GetClientRect(window,&bounds);const int margin=px(24),gap=px(12),width=std::max(px(60),static_cast<int>(bounds.right)-2*margin),button=px(54);
        const auto height=[&](HWND control,HFONT selected,int minimum){const auto text=wide_text(control);RECT box{0,0,width,0};const auto dc=GetDC(control);const auto old=SelectObject(dc,selected);DrawTextW(dc,wide(text).c_str(),-1,&box,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(control,dc);return std::max(px(minimum),static_cast<int>(box.bottom));};
        const int title_height=height(GetDlgItem(window,title_id),title_font,36),status_top=px(92)+title_height+px(12),status_height=height(GetDlgItem(window,status_id),font,48),list_top=status_top+status_height+px(18);
        MoveWindow(GetDlgItem(window,back_id),margin,px(18),px(56),button,TRUE);MoveWindow(GetDlgItem(window,title_id),margin,px(92),width,title_height,TRUE);MoveWindow(GetDlgItem(window,status_id),margin,status_top,width,status_height,TRUE);
        const int bottom=std::max(px(320),static_cast<int>(bounds.bottom)-margin),actions=button*2+gap;
        MoveWindow(GetDlgItem(window,list_id),margin,list_top,width,std::max(px(50),bottom-list_top-button-gap),TRUE);
        MoveWindow(GetDlgItem(window,details_id),margin,list_top,width,std::max(px(50),bottom-list_top-actions-gap),TRUE);
        MoveWindow(GetDlgItem(window,pause_id),margin,bottom-actions,width,button,TRUE);MoveWindow(GetDlgItem(window,delete_id),margin,bottom-button,width,button,TRUE);
        MoveWindow(GetDlgItem(window,refresh_id),margin,bottom-button,(width-gap)/2,button,TRUE);MoveWindow(GetDlgItem(window,info_id),margin+(width+gap)/2,bottom-button,(width-gap)/2,button,TRUE);
        const bool detail=course()!=nullptr;for(const auto id:{list_id,refresh_id,info_id})ShowWindow(GetDlgItem(window,id),detail?SW_HIDE:SW_SHOW);for(const auto id:{details_id,pause_id,delete_id})ShowWindow(GetDlgItem(window,id),detail?SW_SHOW:SW_HIDE);
        if(report)for(std::size_t index=0;index<report->courses.size();++index)SendDlgItemMessageW(window,list_id,LB_SETITEMHEIGHT,index,row_height(index));
        const bool enabled=access.profile().enabled;
        for(const auto id:{list_id,pause_id,delete_id,info_id})EnableWindow(GetDlgItem(window,id),enabled&&operation==Operation::none);
        EnableWindow(GetDlgItem(window,back_id),operation!=Operation::remove);EnableWindow(GetDlgItem(window,refresh_id),enabled&&operation!=Operation::remove);
        SetWindowTextW(GetDlgItem(window,refresh_id),wide(operation==Operation::none?tr("Actualizar","Refresh","Actualiser"):tr("Cancelar","Cancel","Annuler")).c_str());
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void show_detail(){const auto* item=course();if(!item)return;
        const auto active=access.paused(item->owner);
        SetWindowTextW(GetDlgItem(window,title_id),wide(route_title()+tr(" · Curso "," · Course "," · Cours ")+std::to_string(item->number)+tr(": ",": "," : ")+item->title).c_str());
        const auto& coverage=item->coverage;const auto percentage=coverage.percentage();
        const auto text=tr("Audio generado: ","Audio generated: ","Audio généré : ")+std::to_string(coverage.generated)+tr(" de "," of "," sur ")+std::to_string(coverage.expected)+tr(" frases distintas ("," unique utterances ("," énoncés distincts (")+(percentage?std::to_string(*percentage)+"%":tr("Sin frases previstas","No utterances expected","Aucun énoncé prévu"))+").\n"+
            tr("Archivos verificados: ","Verified files: ","Fichiers vérifiés : ")+std::to_string(coverage.files)+tr(" · Tamaño: "," · Size: "," · Taille : ")+audio_size(coverage.bytes,profile.ui_language)+".\n\n"+
            tr("Los audios pueden compartirse entre cursos. El cálculo refleja los idiomas y las voces actualmente seleccionados. La eliminación requiere una confirmación explícita y conserva los audios compartidos.","Audio can be shared across courses. These figures reflect the currently selected languages and voices. Deletion requires explicit confirmation and keeps shared recordings.","Des audios peuvent être communs à plusieurs cours. Ces chiffres concernent les langues et voix actuellement sélectionnées. La suppression exige une confirmation explicite et préserve les audios partagés.")+"\n\n"+
            (active?tr("Generación anticipada pausada","Advance generation paused","Génération anticipée en pause"):tr("Generación anticipada permitida en este curso","Advance generation permitted for this course","Génération anticipée autorisée pour ce cours"))+"\n"+
            tr("Solo controla la generación anticipada de este curso. Las páginas que visites podrán generar audio y los archivos guardados seguirán disponibles. No se elimina ningún audio.","Only advance audio generation for this course is affected. Pages you visit can still generate audio, and saved recordings remain available. No audio is deleted.","Seule la génération anticipée de ce cours est concernée. Les pages visitées peuvent encore produire de l’audio et les enregistrements conservés restent disponibles. Aucun audio n’est supprimé.");
        set_edit_text(GetDlgItem(window,details_id),text);CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE|CFM_FACE|CFM_COLOR;format.yHeight=rich_text_height();format.crTextColor=reader_theme::foreground(GetDlgItem(window,details_id));wcscpy_s(format.szFaceName,L"Roboto");SendDlgItemMessageW(window,details_id,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));SendDlgItemMessageW(window,details_id,EM_SETSEL,0,0);SetWindowTextW(GetDlgItem(window,pause_id),wide(active?tr("Reanudar generación","Resume generation","Reprendre la génération"):tr("Pausar generación","Pause generation","Suspendre la génération")).c_str());layout();
    }
    void begin(Operation next,std::function<Reply(std::stop_token)> work){if(job.joinable()){job.request_stop();job.join();}operation=next;layout();job=std::jthread([this,work=std::move(work)](std::stop_token stop){Reply reply;reply.operation=operation;
        try{reply=work(stop);}catch(const winrt::hresult_error& error){reply.error=winrt::to_string(error.message());reply.cancelled=stop.stop_requested();}catch(const std::exception& error){reply.error=error.what();reply.cancelled=stop.stop_requested();}
        {std::lock_guard lock(result_mutex);pending=std::move(reply);}PostMessageW(window,job_message,0,0);});}
    void scan(bool removal){
        if(operation!=Operation::none)return;profile=access.profile();const auto target=course()?std::optional<CourseAudioRef>(course()->owner):std::nullopt;
        status(removal?tr("Identificando los audios exclusivos y los compartidos entre cursos…","Identifying exclusive audio and audio shared between courses…","Identification des audios exclusifs et partagés entre les cours…"):tr("Comprobando los audios guardados para las voces e idiomas actuales…","Checking saved audio for the current voices and languages…","Vérification des audios enregistrés pour les voix et langues actuelles…"));
        const auto callbacks=access;const auto selected_profile=profile;const auto selected_route=route;
        begin(removal?Operation::preview:Operation::report,[callbacks,selected_profile,selected_route,target,removal](std::stop_token stop){Reply reply;reply.operation=removal?Operation::preview:Operation::report;
            const CourseAudioPlanner planner=[backend=callbacks.worker](std::string_view raw,std::size_t page,std::string_view language,std::string_view voice,std::string_view ui,std::stop_token cancelled){return backend->curriculum_plan(raw,page,language,voice,ui,cancelled);};
            auto report=scan_course_audio(callbacks.content,callbacks.cache,selected_profile,selected_route,removal,planner,stop);
            if(removal){if(!target)throw std::runtime_error("No selected course");reply.preview=std::make_shared<CourseAudioRemovalPreview>(preview_course_audio_removal(std::move(report),*target));}
            else reply.report=std::make_shared<CourseAudioReport>(std::move(report));return reply;});
    }
    void received(){std::optional<Reply> reply;{std::lock_guard lock(result_mutex);reply=std::move(pending);pending.reset();}if(!reply)return;if(job.joinable())job.join();operation=Operation::none;
        if(reply->operation==Operation::remove)maintenance.reset();
        if(reply->cancelled){status(tr("Operación cancelada.","Operation cancelled.","Opération annulée."));layout();if(closing)DestroyWindow(window);return;}
        if(!reply->error.empty()){status(reply->error);layout();if(closing)DestroyWindow(window);return;}
        if(reply->operation==Operation::report){
            if(access.profile()!=reply->report->profile){status(tr("Cambió el idioma o la voz. Actualiza la lista.","The language or voice changed. Refresh the list.","La langue ou la voix a changé. Actualisez la liste."));layout();return;}
            report=std::move(reply->report);std::erase_if(report->courses,[&](const auto& item){return item.owner.route!=route;});selected.reset();SendDlgItemMessageW(window,list_id,LB_RESETCONTENT,0,0);
            for(std::size_t index=0;index<report->courses.size();++index)SendDlgItemMessageW(window,list_id,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(row_text(index)).c_str()));
            SetWindowTextW(GetDlgItem(window,title_id),wide(route_title()).c_str());status(tr("Elige un curso para consultar sus audios.","Choose a course to view its audio.","Choisissez un cours pour consulter ses audios."));layout();return;
        }
        if(reply->operation==Operation::preview){confirm(std::move(reply->preview));return;}
        const auto& result=reply->removal;
        if(result.status==CourseAudioRemovalStatus::changed)status(tr("Cambió el contenido, el idioma, la voz o un archivo. Actualiza la lista antes de reintentar.","The content, language, voice or a file changed. Refresh before retrying.","Le contenu, la langue, la voix ou un fichier a changé. Actualisez avant de réessayer."));
        else if(result.status==CourseAudioRemovalStatus::busy)status(tr("La lectura o la generación están ocupadas. Pausa y vuelve a intentarlo.","Reading or generation is busy. Pause it and retry.","La lecture ou la génération est occupée. Suspendez-la et réessayez."));
        else status(tr("Eliminados ","Deleted ","Supprimés : ")+std::to_string(result.deleted)+tr(" archivos · "," files · "," fichiers · ")+audio_size(result.bytes)+(result.failed?" · "+std::to_string(result.failed)+tr(" fallos"," failures"," échecs"):std::string{})+"\n"+tr("La generación anticipada del curso quedó pausada. Actualiza la lista.","Advance generation for the course remains paused. Refresh the list.","La génération anticipée du cours reste suspendue. Actualisez la liste."));
        layout();if(closing){show_settings_notice(GetAncestor(window,GA_ROOT),wide_text(GetDlgItem(window,status_id)));DestroyWindow(window);}
    }
    static std::string wide_text(HWND control){std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');text.resize(GetWindowTextW(control,text.data(),static_cast<int>(text.size())));return winrt::to_string(text);}
    void confirm(std::shared_ptr<CourseAudioRemovalPreview> preview){
        if(access.profile()!=preview->report.profile){status(tr("Cambió el idioma o la voz. Actualiza la lista.","The language or voice changed. Refresh the list.","La langue ou la voix a changé. Actualisez la liste."));layout();return;}
        if(preview->exclusive.empty()){status(tr("No hay audios exclusivos para eliminar. Archivos compartidos conservados: ","There is no exclusive audio to delete. Shared files preserved: ","Aucun audio exclusif à supprimer. Fichiers partagés conservés : ")+std::to_string(preview->shared_files));layout();return;}
        const auto question=tr("Se eliminarán ","Delete ","Supprimer ")+std::to_string(preview->exclusive.size())+tr(" archivos exclusivos ("," exclusive files ("," fichiers exclusifs (")+audio_size(preview->removable_bytes)+").\n"+
            tr("Archivos compartidos conservados: ","Shared files preserved: ","Fichiers partagés conservés : ")+std::to_string(preview->shared_files)+".\n\n"+
            tr("La generación anticipada del curso quedará pausada. Los audios de otros idiomas o voces, los modelos y el progreso se conservarán. Esta acción no se puede deshacer.","Advance generation for this course will be paused. Audio from other languages or voices, models and learning progress are preserved. This action cannot be undone.","La génération anticipée du cours sera suspendue. Les audios d’autres langues ou voix, les modèles et la progression seront conservés. Cette action est irréversible.");
        if(internal_message_box(window,wide(question).c_str(),wide(tr("Eliminar audios","Delete audio","Supprimer les audios")).c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES){status(tr("No se ha eliminado nada.","Nothing was deleted.","Aucun fichier n’a été supprimé."));layout();return;}
        const auto current=access.profile();if(current!=preview->report.profile){status(tr("Cambió el idioma o la voz. Actualiza la lista.","The language or voice changed. Refresh the list.","La langue ou la voix a changé. Actualisez la liste."));layout();return;}
        maintenance=access.idle_maintenance?access.idle_maintenance():nullptr;
        if(!maintenance){status(tr("No se puede eliminar durante la lectura, generación u otra operación. Pausa y vuelve a intentarlo.","Audio cannot be deleted while reading, generation or another operation is active. Pause it and retry.","Impossible de supprimer pendant une lecture, une génération ou une autre opération. Suspendez-la et réessayez."));layout();return;}
        access.pause(preview->target,true);status(tr("Eliminando únicamente los audios exclusivos confirmados…","Deleting only the confirmed exclusive audio…","Suppression des seuls audios exclusifs confirmés…"));
        begin(Operation::remove,[preview,current,lease=maintenance](std::stop_token stop){Reply result;result.operation=Operation::remove;result.removal=remove_confirmed_course_audio(*preview,current,[lease]{return static_cast<bool>(lease);},stop);return result;});
    }
    void command(int id){
        // A second settings window may disable narration after this dialog was
        // opened. A queued click must not mutate preparation or remove audio.
        if(id!=back_id&&!access.profile().enabled){layout();return;}
        if(id==back_id){
            if(operation==Operation::remove)return;
            if(operation!=Operation::none){SendMessageW(window,WM_CLOSE,0,0);return;}
            if(selected){selected.reset();SetWindowTextW(GetDlgItem(window,title_id),wide(route_title()).c_str());layout();}else DestroyWindow(window);return;
        }
        if(id==refresh_id&&operation!=Operation::none){job.request_stop();return;}
        if(operation!=Operation::none)return;
        if(id==refresh_id){scan(false);return;}
        if(id==info_id){internal_message_box(window,wide(tr("En cada lista solo aparecen los cursos de su ruta. Un audio puede utilizarse en varios cursos y se cuenta dentro de cada uno, pero sigue siendo un único archivo físico. Los porcentajes consideran las voces e idiomas actualmente seleccionados. La eliminación solo ocurre tras la confirmación del usuario y nunca afecta archivos compartidos.","Each list shows only courses from its learning path. One recording can be used by several courses and counted in each, while remaining one physical file. Percentages use the current voices and languages. Deletion requires explicit confirmation and never affects shared files.","Chaque liste ne contient que les cours de son parcours. Un audio partagé est compté dans chaque cours, tout en restant un seul fichier physique. Les pourcentages reflètent les voix et langues sélectionnées. La suppression nécessite une confirmation et n’affecte jamais les fichiers partagés.")).c_str(),wide(route_title()).c_str(),MB_OK|MB_ICONINFORMATION);return;}
        if(id==pause_id&&course()){const auto item=course()->owner;access.pause(item,!access.paused(item));show_detail();return;}
        if(id==delete_id&&course()){scan(true);return;}
    }
    void draw(const DRAWITEMSTRUCT& item){FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::catalog_card));auto bounds=item.rcItem;InflateRect(&bounds,-px(18),-px(14));const auto old=SelectObject(item.hDC,font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,reader_theme::palette(window).text);DrawTextW(item.hDC,wide(row_text(item.itemID)).c_str(),-1,&bounds,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(item.hDC,old);reader_theme::paint_keyboard_focus(item.hwndItem,item.hDC,item.rcItem,reader_theme::palette(window).accent);}
};
LRESULT CALLBACK course_audio_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<CourseAudioWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<CourseAudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    try{switch(message){
    case WM_CREATE:self->initialize();return 0;
    case WM_SIZE:self->layout();return 0;
    case WM_DPICHANGED:{const auto* suggested=reinterpret_cast<const RECT*>(lp);if(suggested)SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);self->refresh_fonts();self->layout();return 0;}
    case WM_DPICHANGED_AFTERPARENT:self->refresh_fonts();self->layout();return 0;
    case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE)self->layout();return 0;
    case WM_ERASEBKGND:{RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::screen));return 1;}
    case WM_COMMAND:if(LOWORD(wp)==IDCANCEL){SendMessageW(window,WM_CLOSE,0,0);return 0;}if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
    case activate_course_message:{const auto chosen=SendDlgItemMessageW(window,list_id,LB_GETCURSEL,0,0);if(self->operation==Operation::none&&chosen>=0&&self->report&&static_cast<std::size_t>(chosen)<self->report->courses.size()){self->selected=static_cast<std::size_t>(chosen);self->show_detail();}return 0;}
    case WM_MEASUREITEM:{auto& item=*reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item.CtlID==list_id){item.itemHeight=self->row_height(item.itemID);return TRUE;}break;}
    case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item.CtlID==list_id){self->draw(item);return TRUE;}if(item.CtlID==back_id){const auto fill=CreateSolidBrush(reader_theme::palette(window).accent);FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);auto bounds=item.rcItem;InflateRect(&bounds,-self->px(18),-self->px(12));draw_icon(item.hDC,bounds,Icon::back,reader_theme::accent_foreground(window));return TRUE;}return reader_theme::draw_button(item);}
    case job_message:self->received();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={self->px(420),self->px(540)};return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);return 0;}break;
    case WM_CLOSE:if(self->operation==Operation::remove){self->closing=true;self->job.request_stop();return 0;}DestroyWindow(window);return 0;
    case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owns)delete self;break;
    default:break;}}
    catch(const std::exception& error){if(message==WM_CREATE)return -1;self->status(error.what());self->operation=Operation::none;self->maintenance.reset();self->layout();}
    catch(const winrt::hresult_error& error){if(message==WM_CREATE)return -1;self->status(winrt::to_string(error.message()));self->operation=Operation::none;self->maintenance.reset();self->layout();}
    return DefWindowProcW(window,message,wp,lp);
}
}
HWND open_course_audio_window(HWND owner,std::string route,CourseAudioAccess access){
    if((route!="programming"&&route!="math_physics")||!access.worker||!access.profile||!access.pause||!access.paused)throw std::invalid_argument("Incomplete course-audio access");
    static const auto rich_edit=LoadLibraryW(L"Msftedit.dll");if(!rich_edit)throw std::runtime_error("Cannot load the native information view");
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.lpszClassName=course_window_class;cls.lpfnWndProc=course_audio_proc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    auto model=std::make_unique<CourseAudioWindow>(std::move(route),std::move(access));
    const auto result=create_internal_window(owner,course_window_class,wide(model->route_title()).c_str(),model.get());
    model->owns=true;model.release();ShowWindow(result,SW_SHOW);SetFocus(result);return result;
}
}
