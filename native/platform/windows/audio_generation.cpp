#include "platform/windows/audio_generation.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/narration_preferences.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <condition_variable>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>

namespace lgaed::windows {
namespace {
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
std::string text(const JsonObject& object,const wchar_t* key,std::string_view fallback={}) {
    return winrt::to_string(object.GetNamedString(key,winrt::to_hstring(fallback)));
}
std::string read_json_source(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path,8*1024*1024);
    std::string result(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    // Same transport policy as the existing catalog loader: an optional UTF-8
    // BOM is an encoding marker, never curricular text. Nothing else is trimmed.
    if(result.starts_with("\xEF\xBB\xBF"))result.erase(0,3);
    return result;
}
std::optional<std::string> explicit_language(const JsonObject& object){
    const auto direct=[](const JsonObject& source)->std::optional<std::string>{
        for(const auto* key:{L"language",L"locale",L"lang",L"idioma"}){
            const auto value=source.GetNamedValue(key,JsonValue::CreateNullValue());
            if(value.ValueType()!=JsonValueType::String)continue;
            if(auto tag=narration_language_tag(winrt::to_string(value.GetString())))return tag;
        }return {};
    };
    if(auto tag=direct(object))return tag;
    if(object.HasKey(L"metadata")&&object.GetNamedValue(L"metadata").ValueType()==JsonValueType::Object)
        return direct(object.GetNamedObject(L"metadata"));
    return {};
}
std::vector<narration::CurriculumAudioPage> catalog(const std::filesystem::path& root,
    const AudioGenerationSelection& selection,std::stop_token stop) {
    std::vector<narration::CurriculumAudioPage> result;
    const auto check=[&]{if(stop.stop_requested())throw std::runtime_error("Audio generation cancelled");};
    const auto ordered=[&](const curriculum::CatalogLocation& where){auto values=load_catalog(root,where).entries;
        std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b){return a.number<b.number;});return values;};
    for(const auto& route:ordered({selection.location.locale})){
        check();if(!route.available)continue;
        const auto courses=curriculum::catalog_child({selection.location.locale},route.id);
        for(const auto& course:ordered(courses)){
            check();if(!course.available)continue;
            const auto units=curriculum::catalog_child(courses,course.id);
            for(const auto& unit:ordered(units)){
                check();if(!unit.available)continue;
                const auto lessons=curriculum::catalog_child(units,unit.id);
                for(const auto& lesson:ordered(lessons)){
                    check();if(!lesson.available)continue;
                    const auto json=JsonObject::Parse(winrt::to_hstring(read_json_source(lesson.asset)));
                    const auto pages=json.GetNamedArray(L"pages");
                    for(unsigned i=0;i<pages.Size();++i){
                        const auto lang=explicit_language(pages.GetObjectAt(i)).value_or(
                            explicit_language(json).value_or(selection.language));
                        auto location=lessons;location.lesson=lesson.id;curriculum::validate_catalog_location(location);
                        result.push_back({std::move(location),lesson.asset,i,lang});
                    }
                }
            }
        }
    }
    return result;
}
}
AudioGenerationSelection audio_generation_selection(const state::StateData& data,
    curriculum::CatalogLocation location,std::optional<std::size_t> page) {
    AudioGenerationSelection result;result.location=std::move(location);result.page=page;
    result.language=selected_narration_language(data,result.location.locale);
    result.ui_language=state::string_value(data,"native_windows","ui_language","es");
    const auto enabled=[&](const char* key){const auto value=state::find(data,"lesson_narration_settings",key);
        return !value||!std::holds_alternative<bool>(*value)||std::get<bool>(*value);};
    const auto engine=state::string_value(data,"lesson_narration_settings","engine","KOKORO");
    result.engine=engine=="WINDOWS"||engine=="ANDROID"?"WINDOWS":"KOKORO";
    result.enabled=enabled("enabled")&&enabled("kokoro_advance_generation")&&
        (engine=="KOKORO"||engine=="AUTO"||engine=="WINDOWS"||engine=="ANDROID");
    const auto budget=state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes",-1);
    result.maximum_audio_bytes=budget<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(budget);
    const auto runtime=std::filesystem::path(build::source_root)/L"out/narration/runtime.json";
    const auto system=result.engine=="WINDOWS"?system_voices():std::vector<SystemVoice>{};
    for(const auto* lang:{"es","fr","en-US","en-GB"})
        result.voices[lang]=result.engine=="WINDOWS"?selected_system_voice(data,lang,system):selected_kokoro_voice(data,lang,runtime);
    if(const auto domain=data.preferences.find("lesson_narration_settings");domain!=data.preferences.end())
        for(const auto& [key,value]:domain->second)if(key.starts_with("kokoro_paused_course:")&&
            std::holds_alternative<bool>(value)&&std::get<bool>(value))result.paused_courses.insert(key.substr(std::string_view("kokoro_paused_course:").size()));
    return result;
}
struct AudioGeneration::Impl {
    struct Event {
        HANDLE value=nullptr;
        ~Event(){if(value)CloseHandle(value);}
    } cache_stop;
    std::shared_ptr<NarrationWorker> worker;
    std::filesystem::path content,cache,snapshot;
    mutable std::mutex mutex;
    std::condition_variable_any changed;
    AudioGenerationSelection selection;
    AudioGenerationStatus progress;
    std::uint64_t epoch=0,cache_epoch=0;
    bool cache_watch_failed=false;
    std::string cache_watch_error;
    std::stop_source active;
    std::string next_route="programming";
    std::jthread watcher,producer;
    Impl(std::shared_ptr<NarrationWorker> value,std::filesystem::path root,std::filesystem::path output,std::filesystem::path profile)
        :worker(std::move(value)),content(std::move(root)),cache(std::move(output)),snapshot(std::move(profile)/L"audio-generation.json") {
        ensure_winrt_apartment();
        state::require_safe_path(cache);
        std::filesystem::create_directories(state::native_io_path(cache));
        cache_stop.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!cache_stop.value)throw std::runtime_error("Cannot create the private audio cache notification event");
        try{if(std::filesystem::exists(snapshot)){
            const auto bytes=state::read_bytes(snapshot,4096);
            const auto json=JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
            if(text(json,L"nextRoute")=="math_physics")next_route="math_physics";
        }}catch(const std::exception& error){progress.error=error.what();
        }catch(const winrt::hresult_error& error){progress.error=winrt::to_string(error.message());}
        watcher=std::jthread([this](std::stop_token stop){watch_cache(stop);});
        producer=std::jthread([this](std::stop_token stop){run(stop);});
    }
    ~Impl(){producer.request_stop();watcher.request_stop();SetEvent(cache_stop.value);
        {std::lock_guard lock(mutex);active.request_stop();}changed.notify_all();}
    void watch_cache(std::stop_token stop){
        struct Change {
            HANDLE value=INVALID_HANDLE_VALUE;
            ~Change(){if(value!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(value);}
        } notification;
        try{
            const auto root=cache.filename()==L"kokoro"?cache.parent_path():cache;
            state::require_safe_path(root);
            notification.value=FindFirstChangeNotificationW(state::native_io_path(root).c_str(),TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE);
            if(notification.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot observe changes to the private audio cache");
            std::stop_callback interrupt(stop,[this]{SetEvent(cache_stop.value);});
            const HANDLE events[]{cache_stop.value,notification.value};
            while(!stop.stop_requested()){
                const auto event=WaitForMultipleObjects(2,events,FALSE,INFINITE);
                if(event==WAIT_OBJECT_0)break;
                if(event!=WAIT_OBJECT_0+1)throw std::runtime_error("Cannot wait for private audio cache changes");
                {std::lock_guard lock(mutex);++cache_epoch;}changed.notify_all();
                if(!FindNextChangeNotification(notification.value))throw std::runtime_error("Cannot renew private audio cache notification");
            }
        }catch(const std::exception& error){
            {std::lock_guard lock(mutex);cache_watch_failed=true;cache_watch_error=error.what();++cache_epoch;progress.error=cache_watch_error;}
            changed.notify_all();
        }
    }
    void save_turn(){
        // Scheduler state only; no curriculum, preferences or reading history.
        const auto pending=std::filesystem::path(snapshot.native()+L".pending");
        {std::ofstream stream(pending,std::ios::binary|std::ios::trunc);stream<<"{\"format\":1,\"nextRoute\":\""<<next_route<<"\"}";stream.flush();
            if(!stream)throw std::runtime_error("Cannot save audio scheduler turn");}
        if(!MoveFileExW(pending.c_str(),snapshot.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace audio scheduler turn");
    }
    struct CachedArtifact {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type modified;
        bool unchanged()const{
            std::error_code error;
            const auto current_size=std::filesystem::file_size(path,error);
            if(error||current_size!=size)return false;
            const auto stamp=std::filesystem::last_write_time(path,error);
            return !error&&stamp==modified;
        }
    };
    void run(std::stop_token shutdown){
        ensure_winrt_apartment();std::map<std::string,std::vector<CachedArtifact>> complete;
        struct Retry {std::chrono::steady_clock::time_point at;std::string error;};
        std::map<std::string,Retry> retry;
        std::vector<narration::CurriculumAudioPage> pages;
        std::string profile_key;
        bool catalog_loaded=false;
        std::optional<std::uint64_t> previous_budget;
        while(!shutdown.stop_requested()){
            AudioGenerationSelection current;std::uint64_t revision,observed_cache;std::stop_token stop;
            {std::unique_lock lock(mutex);changed.wait(lock,shutdown,[&]{return epoch&&selection.enabled;});
                if(shutdown.stop_requested())break;current=selection;revision=epoch;observed_cache=cache_epoch;
                active=std::stop_source();stop=active.get_token();progress.waiting=false;++progress.scans;}
            std::stop_callback cancel(shutdown,[this]{std::lock_guard lock(mutex);active.request_stop();});
            try{
                std::string profile=current.location.locale+'|'+current.language+'|'+current.ui_language+'|'+current.engine;
                for(const auto& [lang,voice]:current.voices)profile+='|'+lang+'='+voice;
                if(profile!=profile_key){complete.clear();retry.clear();pages.clear();catalog_loaded=false;profile_key=profile;}
                if(previous_budget!=current.maximum_audio_bytes){retry.clear();previous_budget=current.maximum_audio_bytes;}
                if(!catalog_loaded){pages=catalog(content,current,stop);catalog_loaded=true;}
                bool generated_page=false,preempted=false;
                const auto identity=[&](const auto& page){return page.location.route+'/'+page.location.lesson+'/'+std::to_string(page.page)+'|'+page.language+'|'+current.voices.at(page.language)+'|'+current.engine;};
                std::set<std::string> eligible_ids;
                for(const auto& page:pages)if(!current.paused_courses.contains(page.location.route+':'+page.location.course))eligible_ids.insert(identity(page));
                std::erase_if(retry,[&](const auto& entry){return !eligible_ids.contains(entry.first);});
                {std::lock_guard lock(mutex);progress.engine=current.engine;progress.language=current.language;
                    progress.total=eligible_ids.size();progress.completed=static_cast<std::size_t>(std::ranges::count_if(eligible_ids,[&](const auto& id){return complete.contains(id);}));progress.finished=false;}
                for(const auto index:narration::audio_schedule(pages,current.location,current.page,next_route)){
                    if(stop.stop_requested())break;
                    const auto& page=pages[index];const auto voice=current.voices.at(page.language);
                    if(current.paused_courses.contains(page.location.route+':'+page.location.course))continue;
                    const auto id=identity(page);
                    if(const auto done=complete.find(id);done!=complete.end()){
                        if(std::ranges::all_of(done->second,[](const auto& artifact){return artifact.unchanged();}))continue;
                        complete.erase(done);std::lock_guard lock(mutex);--progress.completed;
                    }
                    if(retry.contains(id)&&retry.at(id).at>std::chrono::steady_clock::now())continue;
                    {std::lock_guard lock(mutex);if(revision!=epoch)break;progress.running=true;progress.page=id;progress.voice=voice;progress.error.clear();}
                    try{
                        const auto raw=read_json_source(page.asset);
                        const auto plan=JsonObject::Parse(winrt::to_hstring(worker->curriculum_plan(raw,page.page,page.language,voice,current.ui_language,stop)));
                        bool generated=false;std::vector<CachedArtifact> artifacts;
                        for(const auto& item:plan.GetNamedArray(L"segments")){
                            if(stop.stop_requested())break;const auto value=item.GetObject();
                            NarrationSegment segment;segment.text=text(value,L"text");segment.prepared=text(value,L"preparedText");
                            segment.spoken=text(value,L"spokenText",segment.text);
                            const auto audio=current.engine=="WINDOWS"?
                                worker->prepare_system(segment,page.language,voice,cache.parent_path(),stop):
                                worker->prepare(segment,page.language,voice,cache,0,stop,NarrationCpuClass::background);
                            artifacts.push_back({audio.wav,std::filesystem::file_size(audio.wav),std::filesystem::last_write_time(audio.wav)});
                            generated|=!audio.cache_hit;
                        }
                        if(stop.stop_requested())break;
                        bool save=false;
                        {std::lock_guard lock(mutex);if(revision!=epoch)break;complete[id]=std::move(artifacts);++progress.completed;
                            retry.erase(id);
                            if(generated){
                                next_route=page.location.route=="programming"?"math_physics":"programming";save=true;}}
                        if(save)save_turn();
                        // Recompute after a real page so cache-only pages never
                        // consume the other route's next generation turn.
                        if(generated){generated_page=true;break;}
                    }catch(const NarrationPreempted&){preempted=true;break;
                    }catch(const std::exception& error){
                        if(stop.stop_requested())break;
                        retry[id]={std::chrono::steady_clock::now()+20s,error.what()};
                        std::lock_guard lock(mutex);progress.error=error.what();
                    }catch(const winrt::hresult_error& error){
                        if(stop.stop_requested())break;
                        const auto message=winrt::to_string(error.message());
                        retry[id]={std::chrono::steady_clock::now()+20s,message};
                        std::lock_guard lock(mutex);progress.error=message;
                    }
                }
                {std::unique_lock lock(mutex);progress.running=false;
                    if(revision!=epoch||stop.stop_requested()||generated_page||preempted)continue;
                    progress.finished=progress.completed==progress.total&&retry.empty();
                    if(!retry.empty())progress.error=retry.begin()->second.error;
                    if(progress.finished){progress.page.clear();progress.error=cache_watch_error;}
                    progress.waiting=true;
                    const auto woke=[&]{return revision!=epoch||observed_cache!=cache_epoch;};
                    auto next=std::chrono::steady_clock::time_point::max();
                    for(const auto& [id,failed]:retry)next=std::min(next,failed.at);
                    // A notification failure is reported and retains a bounded
                    // recovery scan. Healthy completed caches have no timer.
                    if(cache_watch_failed)next=std::min(next,std::chrono::steady_clock::now()+20s);
                    if(next==std::chrono::steady_clock::time_point::max())changed.wait(lock,shutdown,woke);
                    else changed.wait_until(lock,shutdown,next,woke);
                    progress.waiting=false;}
            }catch(const std::exception& error){
                std::unique_lock lock(mutex);progress.running=false;if(!stop.stop_requested())progress.error=error.what();
                if(stop.stop_requested()||revision!=epoch)continue;
                progress.finished=false;progress.waiting=true;
                changed.wait_for(lock,shutdown,20s,[&]{return revision!=epoch||observed_cache!=cache_epoch;});progress.waiting=false;
            }catch(const winrt::hresult_error& error){
                std::unique_lock lock(mutex);progress.running=false;progress.error=winrt::to_string(error.message());
                if(stop.stop_requested()||revision!=epoch)continue;
                progress.finished=false;progress.waiting=true;
                changed.wait_for(lock,shutdown,20s,[&]{return revision!=epoch||observed_cache!=cache_epoch;});progress.waiting=false;
            }
        }
    }
};
AudioGeneration::AudioGeneration(std::shared_ptr<NarrationWorker> worker,std::filesystem::path content,std::filesystem::path cache,std::filesystem::path profile)
    :impl_(std::make_unique<Impl>(std::move(worker),std::move(content),std::move(cache),std::move(profile))){}
AudioGeneration::~AudioGeneration()=default;
void AudioGeneration::update(AudioGenerationSelection value){
    // Foreground reading owns its separate worker reservation. Merely opening
    // a course, moving a page or showing Options cannot cancel/reorder the
    // beginning-to-end background inventory.
    value.location=curriculum::CatalogLocation{value.location.locale};value.page.reset();
    impl_->worker->set_storage_budget(value.maximum_audio_bytes);
    {std::lock_guard lock(impl_->mutex);if(impl_->epoch&&impl_->selection==value)return;
        impl_->selection=std::move(value);++impl_->epoch;impl_->progress.finished=false;impl_->active.request_stop();}impl_->changed.notify_all();
}
AudioGenerationStatus AudioGeneration::status()const{std::lock_guard lock(impl_->mutex);return impl_->progress;}
}
