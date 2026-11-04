#include "presentation/windows/audio_course_view.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <commctrl.h>
using namespace lgaed;
using namespace lgaed::presentation;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void write(const std::filesystem::path& path,std::string_view text){std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream<<text;if(!stream)throw std::runtime_error("Cannot create isolated course audio fixture");}
std::string wav(unsigned samples=12){std::string bytes(44+samples*2,'\0');const auto mark=[&](std::size_t at,const char* text){bytes.replace(at,4,text,4);};const auto put=[&](std::size_t at,std::uint32_t value,int length){for(int i=0;i<length;++i)bytes[at+i]=static_cast<char>(value>>(8*i));};mark(0,"RIFF");mark(8,"WAVE");mark(12,"fmt ");mark(36,"data");put(4,static_cast<std::uint32_t>(bytes.size()-8),4);put(16,16,4);put(20,1,2);put(22,1,2);put(24,24000,4);put(28,48000,4);put(32,2,2);put(34,16,2);put(40,samples*2,4);return bytes;}
const std::string unique(64,'a'),shared(64,'b'),historical(64,'c'),bad(64,'d');
struct Fixture {
    std::filesystem::path root,content,cache;
    std::vector<std::filesystem::path> created;
    explicit Fixture(const std::filesystem::path& parent):root(parent/(L"course-audio-"+std::to_wstring(GetCurrentProcessId()))),content(root/L"content"),cache(root/L"cache"){
        check(!std::filesystem::exists(root),"An existing fixture must never be overwritten");
        for(const auto route:{"programming","math_physics"}){
            const auto base=content/"es"/route;
            file(base/"courses.json",R"([{"id":"qa","number":1,"title":"QA"},{"id":"future","number":2,"title":"Future"}])");
            file(base/"qa/course_qa.json",R"({"id":"qa","title":"QA","units":[{"id":"qa__u","number":1,"title":"Unit"}]})");
            file(base/"qa/u/unit_u.json",R"({"courseId":"qa","unitId":"qa__u","title":"Unit","lessons":[{"id":"qa__u__a","number":1,"title":"Lesson"}]})");
            file(base/"qa/u/lesson_a.json",std::string("{\"route\":\"")+route+R"(","pages":[{"blocks":[]},{"blocks":[]}]})");
        }
        file(cache/(unique+".wav"),wav());file(cache/(shared+".wav"),wav(24));file(cache/(historical+".wav"),wav(30));file(cache/(bad+".wav"),"invalid");file(cache/(unique+".timings.json"),"timings preserved");file(cache/("."+unique+".part"),"partial preserved");
    }
    void file(const std::filesystem::path& path,std::string_view text){write(path,text);created.push_back(path);}
    ~Fixture(){for(auto at=created.rbegin();at!=created.rend();++at){std::error_code error;std::filesystem::remove(*at,error);}std::vector<std::filesystem::path> directories;
        if(std::filesystem::exists(root))for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_directory())directories.push_back(entry.path());
        std::ranges::sort(directories,[](const auto& a,const auto& b){return a.native().size()>b.native().size();});for(const auto& directory:directories){std::error_code error;std::filesystem::remove(directory,error);}std::error_code error;std::filesystem::remove(root,error);}
};
CourseAudioProfile profile(){CourseAudioProfile result;result.voices={{"es","em_santa"},{"fr","ff_siwis"},{"en-US","af_heart"},{"en-GB","bf_emma"}};return result;}
std::wstring caption(HWND control){std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');result.resize(GetWindowTextW(control,result.data(),static_cast<int>(result.size())));return result;}
void unicode_window_tests(const Fixture& fixture){
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&common)!=FALSE,"Cannot register course-audio test controls");
    const auto owner=CreateWindowExW(0,L"STATIC",L"Isolated course audio Unicode test",WS_OVERLAPPEDWINDOW,0,0,900,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(owner!=nullptr,"Cannot create a native course-audio test owner");
    auto selected=profile();CourseAudioAccess access;access.content=fixture.content;access.cache=fixture.cache;
    // This runtime deliberately does not exist. The real view can plan/cancel,
    // but cannot launch a worker or synthesize audio during Unicode UI checks.
    access.worker=std::make_shared<windows::NarrationWorker>(fixture.root/L"absent-ui-test-runtime.json");
    access.profile=[&]{return selected;};access.paused=[](const auto&){return false;};access.pause=[](const auto&,bool){};
    const auto verify=[&](const char* language,const char* route,std::wstring_view title,std::wstring_view information,std::wstring_view pending){
        selected.ui_language=language;const auto window=open_course_audio_window(owner,route,access);
        const bool correct=caption(window)==title&&caption(GetDlgItem(window,6104))==title&&caption(GetDlgItem(window,6102))==information&&caption(GetDlgItem(window,6105))==pending;
        const auto heading=GetDlgItem(window,6104),status=GetDlgItem(window,6105),list=GetDlgItem(window,6103);const auto before=caption(status);
        SendMessageW(window,WM_DPICHANGED_AFTERPARENT,0,0);LOGFONTW font{};check(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(heading,WM_GETFONT,0,0)),sizeof(font),&font)==sizeof(font)&&std::abs(font.lfHeight)==MulDiv(26,static_cast<int>(GetDpiForWindow(window)),96),"Course audio typography did not refresh at the current DPI");
        check(GetDlgItem(window,6103)==list&&GetDlgItem(window,6105)==status&&caption(status)==before,"DPI refresh replaced course controls or restarted its scan");
        selected.enabled=false;SendMessageW(window,WM_ACTIVATE,WA_ACTIVE,0);
        for(const int id:{6101,6102,6103,6107,6108})check(!IsWindowEnabled(GetDlgItem(window,id)),"A second window disabling narration left course-audio actions enabled");
        check(IsWindowEnabled(GetDlgItem(window,6100)),"Disabling narration blocked the course-audio Back action");selected.enabled=true;
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(6100,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,6100)));check(!IsWindow(window),"Enabled Back did not close/cancel the course-audio scan");check(correct,"Course audio window corrupted an accented title, button or ellipsis");};
    verify("es","programming",L"Programación",L"Información",L"Comprobando los audios guardados para las voces e idiomas actuales…");
    verify("fr","math_physics",L"Mathématiques et physique",L"Informations",L"Vérification des audios enregistrés pour les voix et langues actuelles…");
    verify("en","programming",L"Programming",L"Information",L"Checking saved audio for the current voices and languages…");
    DestroyWindow(owner);
}
std::string plan(std::string_view raw,std::size_t,std::string_view language,std::string_view voice,std::string_view,std::stop_token,std::string_view reading_context){check(language=="es"&&voice=="em_santa","Scan changed the effective voice or language");const bool programming=raw.find("programming")!=std::string_view::npos;check(reading_context==(programming?"cpp":"auto"),"Cross-route audio ownership planning lost its explicit reading context");return std::string("{\"segments\":[{\"key\":\"")+shared+"\"}"+(programming?",{\"key\":\""+unique+"\"}":std::string{})+"]}";}
}
int wmain(int argc,wchar_t** argv){
    try{check(argc==2,"The test needs its existing isolated QA parent");
        // The real application's UI apartment outlives all auxiliary windows
        // and their scan threads. Keep that same lifetime in this test host;
        // otherwise the first cancelled scan can tear down the process's last
        // COM apartment while the next scan is activating a cached factory.
        windows::ensure_winrt_apartment();
        const auto parent=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(parent);Fixture fixture(parent);
        for(int reopen=0;reopen<3;++reopen)unicode_window_tests(fixture);
        const auto before=state::sha256(state::read_bytes(fixture.cache/(historical+".wav")));const auto shared_before=state::sha256(state::read_bytes(fixture.cache/(shared+".wav")));
        auto report=scan_course_audio(fixture.content,fixture.cache,profile(),"programming",false,plan);
        check(report.courses.size()==2&&report.inventory.size()==3,"Read-only inventory omitted valid audio or counted invalid/staged data");
        check(report.courses.front().coverage.expected==2&&report.courses.front().coverage.generated==2&&report.courses.front().coverage.files==2&&report.courses.front().coverage.percentage()==100u,"Unique sentence coverage is incorrect");
        check(!report.courses.back().coverage.percentage(),"An unpublished course received a fake percentage");
        bool incomplete=false;try{(void)preview_course_audio_removal(report,{"programming","qa"});}catch(const std::invalid_argument&){incomplete=true;}check(incomplete,"A route-only scan was accepted as proof of exclusive ownership");
        auto complete=scan_course_audio(fixture.content,fixture.cache,profile(),"programming",true,plan);auto preview=preview_course_audio_removal(std::move(complete),{"programming","qa"});
        check(preview.exclusive.size()==1&&preview.exclusive.front().key==unique&&preview.shared_files==1,"Shared audio in the other route was selected for deletion");
        auto busy=remove_confirmed_course_audio(preview,profile(),[]{return false;});check(busy.status==CourseAudioRemovalStatus::busy&&std::filesystem::exists(fixture.cache/(unique+".wav")),"Busy maintenance deleted a recording");
        auto french=profile();french.language="fr";check(remove_confirmed_course_audio(preview,french,[]{return true;}).status==CourseAudioRemovalStatus::changed,"A changed voice/language profile was accepted");
        std::stop_source cancelled;cancelled.request_stop();check(remove_confirmed_course_audio(preview,profile(),[]{return true;},cancelled.get_token()).status==CourseAudioRemovalStatus::cancelled,"Cancelled removal was executed");
        bool scan_cancelled=false;try{(void)scan_course_audio(fixture.content,fixture.cache,profile(),"programming",false,plan,cancelled.get_token());}catch(const std::runtime_error&){scan_cancelled=true;}check(scan_cancelled,"A cancelled report continued scanning");
        const auto manifest=fixture.content/"es/math_physics/qa/u/unit_u.json";const auto original=state::read_bytes(manifest);write(manifest,"{}");
        check(remove_confirmed_course_audio(preview,profile(),[]{return true;}).status==CourseAudioRemovalStatus::changed&&std::filesystem::exists(fixture.cache/(unique+".wav")),"A changed curriculum allowed stale ownership deletion");write(manifest,std::string_view(reinterpret_cast<const char*>(original.data()),original.size()));
        preview=preview_course_audio_removal(scan_course_audio(fixture.content,fixture.cache,profile(),"programming",true,plan),{"programming","qa"});write(fixture.cache/(unique+".wav"),wav(13));
        check(remove_confirmed_course_audio(preview,profile(),[]{return true;}).status==CourseAudioRemovalStatus::changed,"A replaced recording was deleted from an old preview");
        preview=preview_course_audio_removal(scan_course_audio(fixture.content,fixture.cache,profile(),"programming",true,plan),{"programming","qa"});
        auto removed=remove_confirmed_course_audio(preview,profile(),[]{return true;});check(removed.status==CourseAudioRemovalStatus::completed&&removed.deleted==1&&removed.failed==0&&!std::filesystem::exists(fixture.cache/(unique+".wav")),"The explicitly confirmed exclusive fixture was not removed");
        check(state::sha256(state::read_bytes(fixture.cache/(shared+".wav")))==shared_before&&state::sha256(state::read_bytes(fixture.cache/(historical+".wav")))==before,"Shared or historical voice audio changed");
        check(std::filesystem::exists(fixture.cache/(bad+".wav"))&&std::filesystem::exists(fixture.cache/(unique+".timings.json"))&&std::filesystem::exists(fixture.cache/("."+unique+".part")),"Deletion touched invalid files, timing sidecars or partial writes");
        // A missing published lesson is a destructive-scan error, while a future
        // course without its manifest remains a supported empty roadmap entry.
        const auto lesson=fixture.content/"es/math_physics/qa/u/lesson_a.json";std::filesystem::remove(lesson);bool missing=false;
        try{(void)scan_course_audio(fixture.content,fixture.cache,profile(),"programming",true,plan);}catch(const std::runtime_error&){missing=true;}check(missing,"Incomplete published content was treated as safe exclusive ownership");
        std::cout<<"PASS course audio: canonical-key coverage, cross-route ownership, unpublished courses, cancelled scans, busy/profile/source/file guards, confirmed handle deletion and preserved shared/historical/staged audio\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL course audio: "<<error.what()<<'\n';return 1;}
}
