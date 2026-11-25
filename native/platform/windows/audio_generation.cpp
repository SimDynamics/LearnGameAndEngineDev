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
#include <iterator>
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
                    const auto source=read_json_source(lesson.asset);
                    const auto source_revision=state::sha256(source);
                    const auto json=JsonObject::Parse(winrt::to_hstring(source));
                    const auto pages=json.GetNamedArray(L"pages");
                    for(unsigned i=0;i<pages.Size();++i){
                        const auto lang=explicit_language(pages.GetObjectAt(i)).value_or(
                            explicit_language(json).value_or(selection.language));
                        auto location=lessons;location.lesson=lesson.id;curriculum::validate_catalog_location(location);
                        auto stable=text(pages.GetObjectAt(i),L"id",std::to_string(i));
                        if(json.HasKey(L"stateLayout")){
                            const auto layout=json.GetNamedObject(L"stateLayout");
                            if(layout.HasKey(L"current")){const auto ids=layout.GetNamedArray(L"current");
                                if(i<ids.Size())stable=text(ids.GetObjectAt(i),L"id",stable);}
                        }
                        const auto page_id=text(json,L"id","anonymous-lesson")+'/'+location.locale+'/'+stable;
                        result.push_back({std::move(location),lesson.asset,i,lang,source_revision,page_id});
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
    std::uint64_t epoch=0,cache_epoch=0,content_epoch=0;
    bool cache_watch_failed=false,content_watch_failed=false;
    std::string cache_watch_error;
    std::stop_source active;
    std::string next_route="programming";
    std::map<std::string,std::set<std::string>> page_inventory,pending_pages;
    std::jthread watcher,content_watcher,producer;
    Impl(std::shared_ptr<NarrationWorker> value,std::filesystem::path root,std::filesystem::path output,std::filesystem::path profile)
        :worker(std::move(value)),content(std::move(root)),cache(std::move(output)),snapshot(std::move(profile)/L"audio-generation.json") {
        ensure_winrt_apartment();
        state::require_safe_path(cache);
        std::filesystem::create_directories(state::native_io_path(cache));
        cache_stop.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!cache_stop.value)throw std::runtime_error("Cannot create the private audio cache notification event");
        try{if(std::filesystem::exists(snapshot)){
            const auto bytes=state::read_bytes(snapshot,32*1024*1024);
            const auto json=JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
            if(text(json,L"nextRoute")=="math_physics")next_route="math_physics";
            const auto read_inventory=[&](const wchar_t* name,auto& target){
                if(!json.HasKey(name))return;
                const auto values=json.GetNamedObject(name);
                if(values.Size()>64)throw std::runtime_error("Too many audio inventory locales");
                for(const auto& entry:values){
                    const auto locale=winrt::to_string(entry.Key());
                    if(locale.empty()||locale.size()>64||locale.find('\0')!=std::string::npos)throw std::runtime_error("Invalid audio inventory locale");
                    const auto ids=entry.Value().GetArray();
                    if(ids.Size()>200000)throw std::runtime_error("Audio page inventory is too large");
                    auto& owned=target[locale];
                    for(const auto& value:ids){const auto id=winrt::to_string(value.GetString());
                        if(id.empty()||id.size()>2048||id.find('\0')!=std::string::npos||!owned.insert(id).second)
                            throw std::runtime_error("Invalid or repeated audio inventory page");}
                }
            };
            read_inventory(L"pageInventory",page_inventory);read_inventory(L"pendingPages",pending_pages);
        }}catch(const std::exception& error){page_inventory.clear();pending_pages.clear();progress.error=error.what();
        }catch(const winrt::hresult_error& error){page_inventory.clear();pending_pages.clear();progress.error=winrt::to_string(error.message());}
        watcher=std::jthread([this](std::stop_token stop){watch_cache(stop);});
        content_watcher=std::jthread([this](std::stop_token stop){watch_content(stop);});
        producer=std::jthread([this](std::stop_token stop){run(stop);});
    }
    ~Impl(){producer.request_stop();watcher.request_stop();content_watcher.request_stop();SetEvent(cache_stop.value);
        std::stop_source obsolete;{std::lock_guard lock(mutex);obsolete=active;}
        obsolete.request_stop();changed.notify_all();}
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
    void watch_content(std::stop_token stop){
        struct Notification{HANDLE value=INVALID_HANDLE_VALUE;
            ~Notification(){if(value!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(value);}} notification;
        try{
            state::require_safe_path(content);
            notification.value=FindFirstChangeNotificationW(state::native_io_path(content).c_str(),TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE);
            if(notification.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot observe curricular audio source changes");
            std::stop_callback interrupt(stop,[this]{SetEvent(cache_stop.value);});
            const HANDLE events[]{cache_stop.value,notification.value};
            while(!stop.stop_requested()){
                const auto event=WaitForMultipleObjects(2,events,FALSE,INFINITE);
                if(event==WAIT_OBJECT_0)break;
                if(event!=WAIT_OBJECT_0+1)throw std::runtime_error("Cannot wait for curricular audio source changes");
                std::stop_source obsolete;
                {std::lock_guard lock(mutex);++content_epoch;if(epoch){++epoch;obsolete=active;}progress.finished=false;}
                obsolete.request_stop();
                changed.notify_all();
                if(!FindNextChangeNotification(notification.value))throw std::runtime_error("Cannot renew curricular audio source notification");
            }
        }catch(const std::exception& error){
            std::stop_source obsolete;
            {std::lock_guard lock(mutex);content_watch_failed=true;++content_epoch;
                if(epoch){++epoch;obsolete=active;}progress.error=error.what();}
            obsolete.request_stop();
            changed.notify_all();
        }
    }
    void save_turn(){
        // Scheduler state only; no curriculum, preferences or reading history.
        JsonObject document;document.Insert(L"format",JsonValue::CreateNumberValue(2));
        document.Insert(L"nextRoute",JsonValue::CreateStringValue(winrt::to_hstring(next_route)));
        const auto encode=[](const auto& values){JsonObject result;
            for(const auto& [locale,pages]:values){JsonArray entries;
                for(const auto& page:pages)entries.Append(JsonValue::CreateStringValue(winrt::to_hstring(page)));
                result.Insert(winrt::to_hstring(locale),entries);}
            return result;
        };
        document.Insert(L"pageInventory",encode(page_inventory));document.Insert(L"pendingPages",encode(pending_pages));
        const auto pending=std::filesystem::path(snapshot.native()+L".pending");
        state::require_safe_path(snapshot);state::require_safe_path(pending);
        {std::ofstream stream(state::native_io_path(pending),std::ios::binary|std::ios::trunc);stream<<winrt::to_string(document.Stringify());stream.flush();
            if(!stream)throw std::runtime_error("Cannot save audio scheduler turn");}
        if(!MoveFileExW(state::native_io_path(pending).c_str(),state::native_io_path(snapshot).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
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
        struct Completed {std::string source_revision;std::vector<CachedArtifact> artifacts;};
        ensure_winrt_apartment();std::map<std::string,Completed> complete;
        struct Retry {std::chrono::steady_clock::time_point at;std::string error,source_revision;};
        std::map<std::string,Retry> retry;
        std::vector<narration::CurriculumAudioPage> pages;
        std::set<std::string> urgent_pages;
        std::string profile_key;
        bool catalog_loaded=false;
        std::uint64_t loaded_content=0;
        std::optional<std::pair<curriculum::CatalogLocation,std::optional<std::size_t>>> scanned_position;
        std::optional<std::uint64_t> previous_budget;
        while(!shutdown.stop_requested()){
            AudioGenerationSelection current;std::uint64_t revision,observed_cache,observed_content;std::stop_token stop;
            bool observed_content_failed=false;
            {std::unique_lock lock(mutex);changed.wait(lock,shutdown,[&]{return epoch&&selection.enabled;});
                if(shutdown.stop_requested())break;current=selection;revision=epoch;observed_cache=cache_epoch;observed_content=content_epoch;
                observed_content_failed=content_watch_failed;
                active=std::stop_source();stop=active.get_token();progress.waiting=false;++progress.scans;}
            std::stop_callback cancel(shutdown,[this]{std::stop_source obsolete;
                {std::lock_guard lock(mutex);obsolete=active;}obsolete.request_stop();});
            try{
                std::string profile=current.location.locale+'|'+current.language+'|'+current.ui_language+'|'+current.engine;
                for(const auto& [lang,voice]:current.voices)profile+='|'+lang+'='+voice;
                if(profile!=profile_key){complete.clear();retry.clear();pages.clear();urgent_pages=pending_pages[current.location.locale];catalog_loaded=false;scanned_position.reset();profile_key=profile;}
                if(previous_budget!=current.maximum_audio_bytes){retry.clear();previous_budget=current.maximum_audio_bytes;}
                const bool reload=!catalog_loaded||loaded_content!=observed_content||observed_content_failed;
                const bool had_catalog=catalog_loaded||page_inventory.contains(current.location.locale);
                std::set<std::string> previous_ids;
                if(reload){
                    if(const auto prior=page_inventory.find(current.location.locale);prior!=page_inventory.end())
                        previous_ids=prior->second;
                    for(const auto& page:pages)previous_ids.insert(page.page_id);
                    pages=catalog(content,current,stop);catalog_loaded=true;loaded_content=observed_content;
                    std::set<std::string> current_ids;for(const auto& page:pages)current_ids.insert(page.page_id);
                    std::erase_if(urgent_pages,[&](const auto& id){return !current_ids.contains(id);});
                    if(had_catalog)for(const auto& id:current_ids)if(!previous_ids.contains(id))urgent_pages.insert(id);}
                const auto position=std::pair{current.location,current.page};
                if(reload||scanned_position!=position){
                    const auto active_page=std::ranges::find_if(pages,[&](const auto& page){
                        return current.page&&page.location==current.location&&page.page==*current.page;});
                    if(active_page!=pages.end())for(auto next=std::next(active_page);next!=pages.end();++next){
                        if(stop.stop_requested())break;
                        const auto& page=*next;
                        if(page.location.route!=current.location.route||current.paused_courses.contains(page.location.route+':'+page.location.course))continue;
                        const auto voice=current.voices.at(page.language);
                        const auto page_cache=current.engine=="WINDOWS"?cache.parent_path():cache;
                        const auto published=worker->published_page_revision(page_cache,page.page_id,page.language,voice,current.engine);
                        const bool added=reload&&had_catalog&&!previous_ids.contains(page.page_id);
                        if(!published&&!added)continue;
                        const auto raw=read_json_source(page.asset);
                        const auto plan=worker->page_plan(raw,page.page,page.language,voice,current.ui_language,stop,
                            page.location.route=="programming"?"cpp":"auto",page.location.locale,NarrationCpuClass::background);
                        if(added||(published&&*published!=plan.revision))urgent_pages.insert(page.page_id);
                        else urgent_pages.erase(page.page_id);
                    }
                    if(stop.stop_requested())continue;
                    scanned_position=position;
                    auto& known=page_inventory[current.location.locale];known.clear();
                    for(const auto& page:pages)known.insert(page.page_id);
                    pending_pages[current.location.locale]=urgent_pages;save_turn();
                }
                bool generated_page=false,preempted=false;
                const auto identity=[&](const auto& page){return page.location.route+'/'+page.location.lesson+'/'+std::to_string(page.page)+'|'+page.language+'|'+current.voices.at(page.language)+'|'+current.engine;};
                std::map<std::string,std::string> eligible_ids;
                std::vector<std::size_t> revised;
                for(std::size_t index=0;index<pages.size();++index)if(urgent_pages.contains(pages[index].page_id))revised.push_back(index);
                const auto schedule=narration::audio_schedule(pages,current.location,current.page,next_route,revised);
                for(const auto index:schedule){const auto& page=pages[index];
                    if(!current.paused_courses.contains(page.location.route+':'+page.location.course))eligible_ids.emplace(identity(page),page.source_revision);}
                std::erase_if(complete,[&](const auto& entry){const auto found=eligible_ids.find(entry.first);
                    return found==eligible_ids.end()||found->second!=entry.second.source_revision;});
                std::erase_if(retry,[&](const auto& entry){const auto found=eligible_ids.find(entry.first);
                    return found==eligible_ids.end()||found->second!=entry.second.source_revision;});
                {std::lock_guard lock(mutex);progress.engine=current.engine;progress.language=current.language;
                    progress.total=eligible_ids.size();progress.completed=complete.size();progress.finished=false;}
                for(const auto index:schedule){
                    if(stop.stop_requested())break;
                    const auto& page=pages[index];const auto voice=current.voices.at(page.language);
                    if(current.paused_courses.contains(page.location.route+':'+page.location.course))continue;
                    const auto id=identity(page);
                    if(const auto done=complete.find(id);done!=complete.end()){
                        if(std::ranges::all_of(done->second.artifacts,[](const auto& artifact){return artifact.unchanged();}))continue;
                        complete.erase(done);std::lock_guard lock(mutex);--progress.completed;
                    }
                    if(retry.contains(id)&&retry.at(id).at>std::chrono::steady_clock::now())continue;
                    {std::lock_guard lock(mutex);if(revision!=epoch)break;progress.running=true;progress.page=id;progress.voice=voice;progress.error.clear();}
                    try{
                        const auto raw=read_json_source(page.asset);
                        const auto plan=worker->page_plan(raw,page.page,page.language,voice,current.ui_language,stop,
                            page.location.route=="programming"?"cpp":"auto",page.location.locale,NarrationCpuClass::background);
                        const auto page_cache=current.engine=="WINDOWS"?cache.parent_path():cache;
                        {std::lock_guard lock(mutex);if(revision!=epoch)break;progress.revision=plan.revision;}
                        const auto session=worker->begin_page(page_cache,plan.page_id,page.language,voice,current.engine,
                            plan,NarrationCpuClass::background,stop);
                        if(!session){preempted=true;break;}
                        const auto generation_stop=session->stop_token();
                        bool generated=false;std::vector<CachedArtifact> artifacts;std::vector<NarrationAudio> recordings;
                        for(auto segment:plan.segments){
                            if(stop.stop_requested()||generation_stop.stop_requested())break;
                            segment.cache_namespace=session->cache_namespace();
                            const auto audio=current.engine=="WINDOWS"?
                                worker->prepare_system(segment,page.language,voice,page_cache,generation_stop):
                                worker->prepare(segment,page.language,voice,page_cache,0,generation_stop,NarrationCpuClass::background);
                            artifacts.push_back({audio.wav,std::filesystem::file_size(audio.wav),std::filesystem::last_write_time(audio.wav)});
                            recordings.push_back(audio);
                            if(audio.synthesis_threads){std::lock_guard lock(mutex);
                                progress.synthesis_threads=audio.synthesis_threads;progress.cpu_class=audio.cpu_class;}
                            generated|=!audio.cache_hit;
                        }
                        if(stop.stop_requested())break;
                        if(generation_stop.stop_requested()||!session->current()){preempted=true;break;}
                        // Notifications are wakeups, never revision proofs. A
                        // concurrent replacement cannot publish an old source.
                        if(state::sha256(read_json_source(page.asset))!=page.source_revision){
                            std::stop_source obsolete;
                            {std::lock_guard lock(mutex);if(revision==epoch){++content_epoch;++epoch;obsolete=active;}}
                            obsolete.request_stop();
                            changed.notify_all();break;
                        }
                        if(!session->commit(recordings)){preempted=true;break;}
                        const auto was_urgent=urgent_pages.erase(page.page_id)>0;
                        pending_pages[current.location.locale]=urgent_pages;
                        bool save=false;
                        {std::lock_guard lock(mutex);if(revision!=epoch)break;complete[id]={page.source_revision,std::move(artifacts)};++progress.completed;
                            retry.erase(id);
                            if(generated){
                                next_route=page.location.route=="programming"?"math_physics":"programming";save=true;}}
                        save=save||was_urgent;
                        if(save)save_turn();
                        // Recompute after a real page so cache-only pages never
                        // consume the other route's next generation turn.
                        if(generated){generated_page=true;break;}
                    }catch(const NarrationPreempted&){preempted=true;break;
                    }catch(const std::exception& error){
                        if(stop.stop_requested())break;
                        retry[id]={std::chrono::steady_clock::now()+20s,error.what(),page.source_revision};
                        std::lock_guard lock(mutex);progress.error=error.what();
                    }catch(const winrt::hresult_error& error){
                        if(stop.stop_requested())break;
                        const auto message=winrt::to_string(error.message());
                        retry[id]={std::chrono::steady_clock::now()+20s,message,page.source_revision};
                        std::lock_guard lock(mutex);progress.error=message;
                    }
                }
                {std::unique_lock lock(mutex);progress.running=false;
                    if(revision!=epoch||stop.stop_requested()||generated_page)continue;
                    if(preempted){
                        // A foreground lease may finish entirely with cache
                        // hits, so do not depend on a WAV write to resume; also
                        // do not spin/replan while it still owns the page.
                        changed.wait_for(lock,shutdown,200ms,[&]{return revision!=epoch||observed_cache!=cache_epoch||observed_content!=content_epoch;});
                        continue;
                    }
                    progress.finished=progress.completed==progress.total&&retry.empty();
                    if(!retry.empty())progress.error=retry.begin()->second.error;
                    if(progress.finished){progress.page.clear();progress.error=cache_watch_error;}
                    progress.waiting=true;
                    const auto woke=[&]{return revision!=epoch||observed_cache!=cache_epoch||observed_content!=content_epoch;};
                    auto next=std::chrono::steady_clock::time_point::max();
                    for(const auto& [id,failed]:retry)next=std::min(next,failed.at);
                    // A notification failure is reported and retains a bounded
                    // recovery scan. Healthy completed caches have no timer.
                    if(cache_watch_failed||content_watch_failed)next=std::min(next,std::chrono::steady_clock::now()+20s);
                    if(next==std::chrono::steady_clock::time_point::max())changed.wait(lock,shutdown,woke);
                    else changed.wait_until(lock,shutdown,next,woke);
                    progress.waiting=false;}
            }catch(const NarrationPreempted&){
                std::unique_lock lock(mutex);progress.running=false;progress.waiting=true;
                changed.wait_for(lock,shutdown,200ms,[&]{return revision!=epoch||observed_cache!=cache_epoch||observed_content!=content_epoch;});progress.waiting=false;
            }catch(const std::exception& error){
                std::unique_lock lock(mutex);progress.running=false;if(!stop.stop_requested())progress.error=error.what();
                if(stop.stop_requested()||revision!=epoch)continue;
                progress.finished=false;progress.waiting=true;
                changed.wait_for(lock,shutdown,20s,[&]{return revision!=epoch||observed_cache!=cache_epoch||observed_content!=content_epoch;});progress.waiting=false;
            }catch(const winrt::hresult_error& error){
                std::unique_lock lock(mutex);progress.running=false;progress.error=winrt::to_string(error.message());
                if(stop.stop_requested()||revision!=epoch)continue;
                progress.finished=false;progress.waiting=true;
                changed.wait_for(lock,shutdown,20s,[&]{return revision!=epoch||observed_cache!=cache_epoch||observed_content!=content_epoch;});progress.waiting=false;
            }
        }
    }
};
AudioGeneration::AudioGeneration(std::shared_ptr<NarrationWorker> worker,std::filesystem::path content,std::filesystem::path cache,std::filesystem::path profile)
    :impl_(std::make_unique<Impl>(std::move(worker),std::move(content),std::move(cache),std::move(profile))){}
AudioGeneration::~AudioGeneration()=default;
void AudioGeneration::update(AudioGenerationSelection value){
    // Keep the concrete studied position: the foreground owns that page and
    // revised/missing following pages precede the persisted route turn.
    impl_->worker->set_storage_budget(value.maximum_audio_bytes);
    std::stop_source obsolete;
    {std::lock_guard lock(impl_->mutex);if(impl_->epoch&&impl_->selection==value)return;
        impl_->selection=std::move(value);++impl_->epoch;impl_->progress.finished=false;obsolete=impl_->active;}
    obsolete.request_stop();impl_->changed.notify_all();
}
AudioGenerationStatus AudioGeneration::status()const{std::lock_guard lock(impl_->mutex);return impl_->progress;}
}
