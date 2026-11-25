#include "platform/windows/audio_generation.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace lgaed;
using namespace windows;
using namespace winrt::Windows::Data::Json;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Predicate> void eventually(Predicate predicate,const char* message) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));} while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error(message);
}
std::vector<JsonObject> records(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    std::vector<JsonObject> result;std::string line;
    while(std::getline(stream,line)) {
        try {result.push_back(JsonObject::Parse(winrt::to_hstring(line)));}
        catch(const winrt::hresult_error&){break;} // a live final line can be incomplete
    }
    return result;
}
std::vector<JsonObject> preparations(const std::filesystem::path& path) {
    auto result=records(path);
    std::erase_if(result,[](const auto& value){return value.GetNamedString(L"command")!=L"prepare";});
    return result;
}
std::size_t cancellations(const std::filesystem::path& path) {
    const auto log=records(path);
    return std::ranges::count_if(log,[](const auto& value){return value.GetNamedString(L"command")==L"cancel";});
}
std::shared_ptr<NarrationWorker> fixture(const std::filesystem::path& root,const std::filesystem::path& python,const std::filesystem::path& build,const std::filesystem::path& log,const std::filesystem::path& wave={}) {
    check(std::filesystem::is_directory(build)&&std::filesystem::is_regular_file(python),"Coordinator fixture requires its configured Python and existing build directory");
    // Clear only this test-owned transport log before waiting for the newly
    // started child; a previous run's final French request is not new evidence.
    {std::ofstream stream(log,std::ios::binary|std::ios::trunc);check(static_cast<bool>(stream),"Cannot initialize the coordinator fixture log");}
    const auto runtime=build/L"audio-generation-fixture-runtime.json";
    JsonObject config;config.Insert(L"java",JsonValue::CreateStringValue(python.native()));
    JsonArray arguments;
    for(const auto& value:{std::wstring(L"-u"),(root/L"native/platform/windows/audio_generation_protocol_fixture.py").native(),log.native()})
        arguments.Append(JsonValue::CreateStringValue(value));
    if(!wave.empty())arguments.Append(JsonValue::CreateStringValue(wave.native()));
    config.Insert(L"arguments",arguments);
    {std::ofstream stream(runtime,std::ios::binary|std::ios::trunc);check(static_cast<bool>(stream),"Cannot write coordinator fixture configuration");stream<<winrt::to_string(config.Stringify());}
    return std::make_shared<NarrationWorker>(runtime);
}
struct OwnedFile {
    std::filesystem::path path;
    ~OwnedFile(){std::error_code error;std::filesystem::remove(path,error);}
};
void write_owned(const std::filesystem::path& path,std::string_view source){
    state::require_safe_path(path);std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(state::native_io_path(path),std::ios::binary|std::ios::trunc);stream<<source;stream.flush();
    check(static_cast<bool>(stream),"Cannot write the isolated coordinator source");
}
JsonObject document(const std::filesystem::path& path){
    const auto bytes=state::read_bytes(path,32*1024*1024);
    return JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
}
bool inventory_has(const JsonObject& snapshot,const wchar_t* name,std::string_view id){
    if(!snapshot.HasKey(name))return false;const auto locales=snapshot.GetNamedObject(name);
    if(!locales.HasKey(L"es"))return false;
    for(const auto& item:locales.GetNamedArray(L"es"))if(item.GetString()==winrt::to_hstring(id))return true;
    return false;
}
struct QueueContent {
    std::filesystem::path root,programming_asset;
    curriculum::CatalogLocation current{"es","programming","qa_queue_programming","qa_queue_programming__unit","qa_queue_programming__unit__lesson"};
    const std::string programming_lesson="qa_queue_programming__unit__lesson",math_lesson="qa_queue_math__unit__lesson";
    explicit QueueContent(std::filesystem::path value):root(std::move(value)){
        for(const auto* route:{"programming","math_physics"}){
            const std::string course=std::string_view(route)=="programming"?"qa_queue_programming":"qa_queue_math";
            const auto unit=course+"__unit",lesson=unit+"__lesson";auto where=curriculum::catalog_child({"es"},route);
            write_owned(root/"es"/curriculum::catalog_index_path(where),"[{\"id\":\""+course+"\",\"title\":\"QA\",\"number\":1}]");
            where=curriculum::catalog_child(where,course);
            write_owned(root/"es"/curriculum::catalog_index_path(where),"{\"id\":\""+course+"\",\"title\":\"QA\",\"units\":[{\"id\":\""+unit+"\",\"title\":\"QA\",\"number\":1}]}");
            where=curriculum::catalog_child(where,unit);
            write_owned(root/"es"/curriculum::catalog_index_path(where),"{\"courseId\":\""+course+"\",\"unitId\":\""+unit+"\",\"title\":\"QA\",\"lessons\":[{\"id\":\""+lesson+"\",\"title\":\"QA\",\"number\":1}]}");
            const auto asset=root/"es"/curriculum::catalog_lesson_path(where,lesson);
            if(std::string_view(route)=="programming")programming_asset=asset;
            else write_owned(asset,"{\"id\":\""+lesson+"\",\"title\":\"QA math\",\"pages\":[{\"id\":\"math_0001\",\"blocks\":[{\"type\":\"paragraph\",\"text\":\"QA math source.\"}]}]}");
        }
        program(false);
    }
    void program(bool insert){
        JsonObject lesson;lesson.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(programming_lesson)));
        lesson.Insert(L"title",JsonValue::CreateStringValue(L"QA programming"));JsonArray pages;
        for(const auto number:insert?std::vector<int>{1,2,3,4}:std::vector<int>{1,2,4}){
            JsonObject page,paragraph;const auto id="page_000"+std::to_string(number);
            page.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));
            paragraph.Insert(L"type",JsonValue::CreateStringValue(L"paragraph"));
            paragraph.Insert(L"text",JsonValue::CreateStringValue(winrt::to_hstring("QA source "+id+".")));
            JsonArray blocks;blocks.Append(paragraph);page.Insert(L"blocks",blocks);pages.Append(page);
        }
        lesson.Insert(L"pages",pages);write_owned(programming_asset,winrt::to_string(lesson.Stringify()));
    }
    AudioGenerationSelection selection()const{
        auto result=audio_generation_selection({},current,1);check(result.enabled,"Isolated queue selection unexpectedly disabled");return result;
    }
    std::string page_id(std::string_view stable)const{return programming_lesson+"/es/"+std::string(stable);}
};
void revision_queue_recovery(const std::filesystem::path& root,const std::filesystem::path& python,const std::filesystem::path& qa_parent){
    check(qa_parent.is_absolute(),"Queue regression requires an absolute isolated QA parent");
    const auto relative=qa_parent.lexically_normal().lexically_relative(std::filesystem::path(L"C:\\LGAED-QA"));
    check(!relative.empty()&&!relative.is_absolute()&&std::ranges::none_of(relative,[](const auto& part){return part==L"..";}),
        "Queue regression must remain inside C:\\LGAED-QA");
    const auto owned=qa_parent/(L"coordinator-revision-queue-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    state::require_safe_path(owned);check(!std::filesystem::exists(owned),"An existing queue fixture must never be overwritten");
    std::filesystem::create_directories(owned);
    {
        QueueContent content(owned/L"initial-content");const auto profile=owned/L"initial-profile",cache=owned/L"initial-cache",phase=owned/L"initial-transport";
        std::filesystem::create_directories(phase);write_owned(profile/L"audio-generation.json",R"({"format":1,"nextRoute":"math_physics"})");
        write_owned(content.programming_asset,"{incomplete QA lesson");
        const auto log=phase/L"transport.jsonl";auto worker=fixture(root,python,phase,log);
        {
            AudioGeneration coordinator(worker,content.root,cache,profile);auto selected=content.selection();coordinator.update(selected);
            eventually([&]{return coordinator.status().scans>0&&!coordinator.status().error.empty();},"Initial incomplete catalog did not report its bounded failure");
            selected.enabled=false;coordinator.update(selected);
            eventually([&]{return !coordinator.status().running;},"Initial incomplete catalog could not be cancelled");
            check(preparations(log).empty(),"An incomplete catalog reached audio generation");
            content.program(false);selected.enabled=true;coordinator.update(selected);
            eventually([&]{return !preparations(log).empty();},"Repairing the cancelled initial catalog did not restart generation");
            const auto first=preparations(log).front();
            check(first.GetNamedString(L"fixturePageId")==winrt::to_hstring(content.math_lesson+"/es/math_0001"),
                "A cancelled initial load created a false empty inventory and promoted the entire catalog as new pages");
            eventually([&]{const auto saved=document(profile/L"audio-generation.json");return saved.GetNamedNumber(L"format")==2&&inventory_has(saved,L"pageInventory",content.page_id("page_0004"));},
                "The first successful catalog did not persist its real format2 inventory");
        }
        worker->shutdown();
    }
    {
        QueueContent content(owned/L"restart-content");const auto profile=owned/L"restart-profile",cache=owned/L"restart-cache";
        write_owned(profile/L"audio-generation.json",R"({"format":1,"nextRoute":"math_physics"})");
        const auto run=[&](const wchar_t* name,bool new_page){
            const auto phase=owned/name;std::filesystem::create_directories(phase);const auto log=phase/L"transport.jsonl";
            auto worker=fixture(root,python,phase,log);
            {
                AudioGeneration coordinator(worker,content.root,cache,profile);auto selected=content.selection();coordinator.update(selected);
                eventually([&]{return !preparations(log).empty();},"Reopened coordinator did not submit a controlled preparation");
                const auto first=preparations(log).front();const auto expected=new_page?content.page_id("page_0003"):content.math_lesson+"/es/math_0001";
                check(first.GetNamedString(L"fixturePageId")==winrt::to_hstring(expected),
                    "New or persisted pending page3 did not precede the saved mathematics turn after current page2");
                check(first.GetNamedNumber(L"threads")<=2&&first.GetNamedBoolean(L"audioOnly")&&first.GetNamedString(L"cpuClass")==L"background",
                    "Reopened queue escaped the real worker's background CPU policy");
                eventually([&]{const auto saved=document(profile/L"audio-generation.json");
                    return saved.GetNamedNumber(L"format")==2&&inventory_has(saved,L"pageInventory",content.page_id(new_page?"page_0003":"page_0004"))&&
                        (!new_page||inventory_has(saved,L"pendingPages",content.page_id("page_0003")));},
                    "Format2 did not preserve the inventory and unfinished new-page work");
                const auto before=cancellations(log);selected.enabled=false;coordinator.update(selected);
                eventually([&]{return cancellations(log)>before&&!coordinator.status().running;},"Closing the isolated queue did not acknowledge its pending preparation cancellation");
                check(coordinator.status().completed==0,"A retained transport request manufactured completed page audio");
                check(std::ranges::none_of(preparations(log),[&](const auto& value){return value.GetNamedString(L"fixturePageId")==winrt::to_hstring(content.page_id("page_0002"));}),
                    "The background queue generated the studied page reserved for foreground work");
            }
            worker->shutdown();
        };
        run(L"baseline-transport",false);
        // Both producer and worker are closed before the owned source changes.
        content.program(true);run(L"new-page-after-close-transport",true);run(L"restored-pending-transport",true);
    }
    std::cout<<"PASS real native coordinator: cancelled initial load has no false inventory; format2 inventory/pending survives closure; new page3 precedes saved math turn after studied page2; no audio synthesis or personal data. QA="<<owned.string()<<'\n';
}
std::string first_lesson(const std::filesystem::path& content,const char* route) {
    auto where=curriculum::catalog_child({"es"},route);
    for(int depth=0;depth<2;++depth){const auto page=load_catalog(content,where);
        const auto found=std::ranges::find_if(page.entries,[](const auto& item){return item.available;});
        check(found!=page.entries.end(),"Snapshot test needs an installed course/unit");where=curriculum::catalog_child(where,found->id);}
    const auto page=load_catalog(content,where);
    const auto found=std::ranges::find_if(page.entries,[](const auto& item){return item.available;});
    check(found!=page.entries.end(),"Snapshot test needs an installed lesson");return found->id;
}
void snapshot_recovery(const std::filesystem::path& root,const std::filesystem::path& python,const std::filesystem::path& build) {
    const auto content=root/L"app/src/main/assets/content",log=build/L"audio-generation-fixture.jsonl";
    OwnedFile snapshot{build/L"audio-generation.json"};
    const auto run=[&](const char* saved,const char* expected,bool malformed) {
        {std::ofstream stream(snapshot.path,std::ios::binary|std::ios::trunc);stream<<saved;check(static_cast<bool>(stream),"Cannot initialize the test-owned scheduler snapshot");}
        auto worker=fixture(root,python,build,log);
        {
            AudioGeneration coordinator(worker,content,build,build);
            if(malformed)check(!coordinator.status().error.empty(),"A malformed snapshot was silently accepted");
            coordinator.update(audio_generation_selection({}, {"es"}));
            eventually([&]{return !preparations(log).empty();},"Scheduler did not recover its generation after reading a saved turn");
            const auto first=winrt::to_string(preparations(log).front().GetNamedString(L"text"));
            check(first=="generation:"+first_lesson(content,expected)+":0","Restart did not restore the next route or use the safe default after an invalid snapshot");
            check(coordinator.status().error.empty(),"An invalid historical snapshot prevented normal coordinator recovery");
        }
    };
    run(R"({"format":1,"nextRoute":"math_physics"})","math_physics",false);
    run("{invalid historical scheduler JSON","programming",true);
    std::cout<<"PASS native coordinator restart restores the mathematics turn; malformed scheduler state reports its error and safely resumes the default route without touching preferences/history\n";
}
void completed_artifact_invalidation(const std::filesystem::path& root,const std::filesystem::path& python,const std::filesystem::path& build) {
    std::filesystem::path original;
    for(const auto* directory:{L"out/qa/narration-native-tests",L"out/qa/narration-worker-tests"}) {
        const auto qa=root/directory;
        if(std::filesystem::is_directory(qa))for(const auto& entry:std::filesystem::directory_iterator(qa))
            if(entry.is_regular_file()&&entry.path().extension()==L".wav"){original=entry.path();break;}
        if(!original.empty())break;
    }
    if(original.empty()) {
        // This test must also work before any other speech test has run. Reuse
        // a constant real synthesis key in the already-existing build output;
        // a valid recording is preserved and reused on later invocations.
        NarrationWorker real(root/L"out/narration/runtime.json");
        const auto plan=real.plan("La línea #include y std::cout muestran una salida.","es");
        check(plan.size()==1,"The real Kokoro QA sentence no longer has one stable segment");
        const auto audio=real.prepare(plan.front(),"es","ef_dora",build,2,{},NarrationCpuClass::background);
        check(audio.duration_ms>0&&std::filesystem::is_regular_file(audio.wav),"Real Kokoro did not provide the independently requested QA recording");
        original=audio.wav;
        std::cout<<"PASS independent real Kokoro QA recording "<<(audio.cache_hit?"reused":"synthesized")<<" for the subsequent controlled cache-index test\n";
    }
    const auto original_stamp=std::filesystem::last_write_time(original);
    const auto original_size=std::filesystem::file_size(original);
    OwnedFile wave{build/L"audio-generation-cache-fixture.wav"};
    std::filesystem::copy_file(original,wave.path,std::filesystem::copy_options::overwrite_existing);
    const auto log=build/L"audio-generation-fixture.jsonl",content=root/L"app/src/main/assets/content";
    auto worker=fixture(root,python,build,log,wave.path);
    {
        AudioGeneration coordinator(worker,content,build,build);
        coordinator.update(audio_generation_selection({}, {"es"}));
        eventually([&]{return coordinator.status().completed>=1&&preparations(log).size()>=2;},"Controlled cache reuse did not complete a page before the next request");
        const auto first=preparations(log).front().GetNamedString(L"text");
        auto priority=worker->current_page_lease();
        std::filesystem::last_write_time(wave.path,std::filesystem::last_write_time(wave.path)+std::chrono::seconds(2));
        priority={};
        eventually([&]{const auto values=preparations(log);return std::ranges::count_if(values,[&](const auto& value){return value.GetNamedString(L"text")==first;})>=2;},"A changed completed artifact was skipped by the scheduler's in-memory index");
        check(coordinator.status().error.empty(),"Rechecking a changed completed artifact introduced a coordinator error");
    }
    check(std::filesystem::file_size(original)==original_size&&std::filesystem::last_write_time(original)==original_stamp,"The original real Kokoro recording was modified by a transport fixture");
    std::cout<<"PASS native coordinator rechecks a completed artifact after its mtime changes; borrowed real Kokoro QA waveform remains untouched (controlled cache transport, not neural generation evidence)\n";
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        ensure_winrt_apartment();
        if(argc==4&&std::wstring_view(argv[1])==L"--revision-queue"){
            revision_queue_recovery(std::filesystem::path(build::source_root),argv[2],std::filesystem::path(argv[3]));return 0;
        }
        check(argc==3,"Coordinator test requires Python/build arguments, or --revision-queue Python isolated-QA-parent");
        const auto root=std::filesystem::path(build::source_root),build=std::filesystem::path(argv[2]);
        const auto content=root/L"app/src/main/assets/content",log=build/L"audio-generation-fixture.jsonl";
        auto worker=fixture(root,argv[1],build,log);
        state::StateData data;
        data.preferences["lesson_narration_settings"]["engine"]=std::string("KOKORO");
        data.preferences["lesson_narration_settings"]["enabled"]=true;
        data.preferences["lesson_narration_settings"]["kokoro_advance_generation"]=true;
        auto selection=audio_generation_selection(data,{"es"});
        check(audio_generation_selection(data,{"fr"}).language=="fr"&&audio_generation_selection(data,{"en"}).language=="en-US", "An absent narration preference did not follow the selected content language");
        auto paused_data=data;
        paused_data.preferences["lesson_narration_settings"]["kokoro_paused_course:programming:cpp_fundamentals"]=true;
        paused_data.preferences["lesson_narration_settings"]["kokoro_paused_course:math_physics:math_foundations"]=false;
        check(audio_generation_selection(paused_data,{"es"}).paused_courses==std::set<std::string>{"programming:cpp_fundamentals"}, "Imported per-course generation pauses were lost or expanded to unrelated courses");
        auto inherited=data;
        inherited.preferences["lesson_narration_settings"]["engine"]=std::string("AUTO");
        inherited.preferences["lesson_narration_settings"]["kokoro_voice"]=std::string("em_santa");
        const auto automatic=audio_generation_selection(inherited,{"es"});
        check(automatic.enabled&&automatic.voices.at("es")=="em_santa", "AUTO generation or the valid legacy Santa preference was lost");
        inherited.preferences["lesson_narration_settings"]["language"]=std::string("FR_FR");
        const auto french_alias=audio_generation_selection(inherited,{"es"});
        check(french_alias.language=="fr"&&french_alias.voices.at("fr")=="ff_siwis", "French narration aliases or per-language voice isolation were lost");
        {
            AudioGeneration coordinator(worker,content,build,build);
            coordinator.update(selection);
            eventually([&]{const auto status=coordinator.status();if(!status.error.empty())throw std::runtime_error("Coordinator: "+status.error);return preparations(log).size()>=1;},"Real coordinator never submitted its first catalog preparation");
            auto first=preparations(log).back();
            check(first.GetNamedString(L"language")==L"es"&&first.GetNamedString(L"voice")==L"ef_dora", "Coordinator ignored the selected Spanish voice/language");
            check(first.GetNamedNumber(L"threads")<=2&&first.GetNamedBoolean(L"audioOnly"),"Coordinator failed to request bounded background-only audio");

            const auto before=preparations(log).size();
            auto foreground=worker->current_page_lease();
            check(cancellations(log)>=1,"Current-page reservation did not cancel the coordinator's in-flight request");
            check(coordinator.status().error.empty(),"Foreground preemption was exposed as a failed resource");
            foreground={};
            eventually([&]{return preparations(log).size()>before;},"Coordinator applied a failure delay instead of resuming after current-page preemption");
            check(coordinator.status().error.empty(),"Coordinator kept a preemption error after resuming");

            const auto before_voice=preparations(log).size();
            data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_santa");
            selection=audio_generation_selection(data,{"es"});coordinator.update(selection);
            eventually([&]{const auto values=preparations(log);return values.size()>before_voice&&values.back().GetNamedString(L"voice")==L"em_santa";},"Changing voice did not cancel the old request and resume with Santa");

            data.preferences["lesson_narration_settings"]["enabled"]=false;
            selection=audio_generation_selection(data,{"es"});
            check(!selection.enabled,"Disabling narration did not disable advance generation");
            const auto before_off=cancellations(log);coordinator.update(selection);
            eventually([&]{return cancellations(log)>before_off&&!coordinator.status().running;},"Disabling narration did not acknowledge and stop in-flight generation");
            const auto paused=preparations(log).size();
            data.preferences["lesson_narration_settings"]["enabled"]=true;
            data.preferences["lesson_narration_settings"]["language"]=std::string("fr");
            selection=audio_generation_selection(data,{"es"});coordinator.update(selection);
            eventually([&]{const auto values=preparations(log);return values.size()>paused&&values.back().GetNamedString(L"language")==L"fr"&&values.back().GetNamedString(L"voice")==L"ff_siwis";},"Returning to Kokoro did not resume the newly selected French profile");

            // A changed studied position is reconciled through cancellation.
            // An open unit without a concrete page keeps the ordinary order.
            auto where=curriculum::catalog_child({"es"},"programming");
            const auto course=load_catalog(content,where).entries.front();where=curriculum::catalog_child(where,course.id);
            const auto unit=load_catalog(content,where).entries.at(1);where=curriculum::catalog_child(where,unit.id);
            const auto before_unit=preparations(log).size();
            const auto before_navigation_cancel=cancellations(log);
            const auto previous_source=preparations(log).back().GetNamedString(L"text");
            selection=audio_generation_selection(data,where);coordinator.update(selection);
            eventually([&]{return cancellations(log)>before_navigation_cancel&&preparations(log).size()>before_unit;},
                "A changed selection did not cancel and reconcile the actual background request");
            check(preparations(log).back().GetNamedString(L"text")==previous_source,
                "Opening a unit without a concrete page reordered the ordinary background inventory");
            check(coordinator.status().error.empty(),"Navigation or profile cancellation produced a coordinator error");
            check(coordinator.status().completed==0,"Cancelled preparations manufactured completed page audio");
        }
        worker.reset();
        snapshot_recovery(root,argv[1],build);
        completed_artifact_invalidation(root,argv[1],build);
        std::cout<<"PASS native coordinator over real catalog: bounded background request, foreground preemption/resume without backoff, Santa voice switch, narration cancellation, French resumption, navigation reconciliation and shutdown acknowledgement (controlled transport, not synthesis/performance evidence)\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
