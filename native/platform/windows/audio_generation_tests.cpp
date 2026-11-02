#include "platform/windows/audio_generation.hpp"
#include "platform/windows/catalog_loader.hpp"
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
        ensure_winrt_apartment();check(argc==3,"Coordinator test requires configured Python and existing build directory");
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

            // Navigation no longer jumps the global background queue ahead.
            auto where=curriculum::catalog_child({"es"},"programming");
            const auto course=load_catalog(content,where).entries.front();where=curriculum::catalog_child(where,course.id);
            const auto unit=load_catalog(content,where).entries.at(1);where=curriculum::catalog_child(where,unit.id);
            const auto before_unit=preparations(log).size();
            const auto before_navigation_cancel=cancellations(log);
            selection=audio_generation_selection(data,where);coordinator.update(selection);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            check(preparations(log).size()==before_unit&&cancellations(log)==before_navigation_cancel,"Opening another unit cancelled or reordered the current background page");
            check(coordinator.status().error.empty(),"Navigation or profile cancellation produced a coordinator error");
            check(coordinator.status().completed==0,"Cancelled preparations manufactured completed page audio");
        }
        worker.reset();
        snapshot_recovery(root,argv[1],build);
        completed_artifact_invalidation(root,argv[1],build);
        std::cout<<"PASS native coordinator over real catalog: bounded background request, foreground preemption/resume without backoff, Santa voice switch, narration cancellation, French resumption, navigation-independent order and shutdown acknowledgement (controlled transport, not synthesis/performance evidence)\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
