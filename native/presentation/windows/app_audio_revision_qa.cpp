#include "presentation/windows/app_audio_revision_qa.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson_state.hpp"
#include "core/narration_state.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <richedit.h>
#include <fstream>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool beneath(const std::filesystem::path& child,const std::filesystem::path& parent){
    const auto relative=child.lexically_relative(parent);
    return !relative.empty()&&!relative.is_absolute()&&*relative.begin()!=L"..";
}
void require_plain_directory(const std::filesystem::path& path){
    const auto attributes=GetFileAttributesW(path.c_str());
    require(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&
        !(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"QA directories must exist and cannot be junctions or symbolic links");
}
std::filesystem::path qa_root(const std::filesystem::path& path){
    require(path.is_absolute(),"QA control needs an absolute directory");
    state::require_safe_path(path);
    const auto allowed=std::filesystem::weakly_canonical(L"C:\\LGAED-QA");
    const auto resolved=std::filesystem::weakly_canonical(path);
    require(beneath(resolved,allowed),"Application audio QA is confined to C:\\LGAED-QA");
    auto at=allowed;require_plain_directory(at);
    for(const auto& part:resolved.lexically_relative(allowed)){at/=part;require_plain_directory(at);}
    return resolved;
}
void string(JsonObject& row,std::wstring_view name,std::string_view value){row.Insert(name,JsonValue::CreateStringValue(winrt::to_hstring(value)));}
void boolean(JsonObject& row,std::wstring_view name,bool value){row.Insert(name,JsonValue::CreateBooleanValue(value));}
void number(JsonObject& row,std::wstring_view name,double value){row.Insert(name,JsonValue::CreateNumberValue(value));}
template<class T> requires std::is_integral_v<T>
void number(JsonObject& row,std::wstring_view name,T value){number(row,name,static_cast<double>(value));}
std::string source_bytes(const std::filesystem::path& path){const auto bytes=state::read_bytes(path);return {bytes.begin(),bytes.end()};}
HWND control(HWND view,int id){struct Query{int id;HWND result=nullptr;} query{id};
    EnumChildWindows(view,[](HWND target,LPARAM opaque)->BOOL{auto& query=*reinterpret_cast<Query*>(opaque);
        if(GetDlgCtrlID(target)==query.id){query.result=target;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&query));
    require(query.result!=nullptr,"The requested QA lesson control does not exist");return query.result;
}
}
std::optional<AppAudioRevisionQaConfig> parse_app_audio_revision_qa_arguments(std::vector<std::wstring_view>& args){
    std::optional<AppAudioRevisionQaConfig> result;
    for(std::size_t index=0;index<args.size();){
        if(args[index]!=L"--qa-audio-control"){++index;continue;}
        require(!result&&index+1<args.size(),"--qa-audio-control needs exactly one existing QA directory");
        const auto root=qa_root(std::filesystem::path(args[index+1]));
        require_plain_directory(root/L"content");
        result=AppAudioRevisionQaConfig{root,root/L"content"};args.erase(args.begin()+index,args.begin()+index+2);
    }
    return result;
}
void bind_app_audio_revision_qa_environment(const AppAudioRevisionQaConfig& config,WindowsEnvironment& env){
    require(env.options.profile==ProfileId::Codex&&env.options.qa_instance&&!env.options.smoke_test,
        "Application audio QA requires --profile Codex and an isolated --qa-instance");
    const auto root=qa_root(config.root);const auto setup=JsonObject::Parse(winrt::to_hstring(source_bytes(root/L"qa-config.json")));
    require(setup.GetNamedString(L"qaInstance")==*env.options.qa_instance,"QA control token does not match the isolated profile");
    const auto profile=root/L"profile";
    if(std::filesystem::exists(profile)){
        require_plain_directory(profile);
        // A resumed QA may contain old recordings and state. Reject a link
        // anywhere in that owned tree before initialize_profile_directories
        // or any other startup writer can touch its destination.
        for(std::filesystem::recursive_directory_iterator entry(profile),end;entry!=end;++entry)
            state::require_safe_path(entry->path());
    }
    for(const auto& path:{profile,profile/L"state",profile/L"state/active",profile/L"progress",
            profile/L"drafts",profile/L"preferences",profile/L"logs",profile/L"projects",profile/L"cache"})
        state::require_safe_path(path);
    env.paths={profile,profile/L"progress",profile/L"drafts",profile/L"preferences",profile/L"logs",profile/L"projects",profile/L"cache"};
}
struct AppAudioRevisionQa::Impl {
    AppAudioRevisionQaConfig config;WindowsEnvironment environment;AppAudioRevisionQaAccess access;
    double acknowledged=0;std::string error;std::uint64_t samples=0;std::string prior_trace;
    bool ticking=false;
    Impl(AppAudioRevisionQaConfig c,WindowsEnvironment e,AppAudioRevisionQaAccess a):config(std::move(c)),environment(std::move(e)),access(std::move(a)){
        // A close command from the previous ordinary app lifetime is evidence,
        // not a request to close the new instance again.
        const auto command_file=config.root/L"command.json";
        if(std::filesystem::exists(command_file))acknowledged=JsonObject::Parse(winrt::to_hstring(source_bytes(command_file))).GetNamedNumber(L"sequence");
    }
    void inspect_state(){
        state::StateData data;
        if(const auto lesson=access.lesson())data=read_lesson_state(lesson);
        else{state::StateStore store(environment.paths.root);data=store.load();}
        JsonObject result;
        for(const auto& [domain,entries]:data.preferences){JsonObject values;
            for(const auto& [key,value]:entries)std::visit([&](const auto& item){using T=std::decay_t<decltype(item)>;
                if constexpr(std::is_same_v<T,std::string>)string(values,winrt::to_hstring(key),item);
                else if constexpr(std::is_same_v<T,bool>)boolean(values,winrt::to_hstring(key),item);
                else if constexpr(std::is_arithmetic_v<T>)number(values,winrt::to_hstring(key),static_cast<double>(item));
                else{JsonArray array;for(const auto& text:item)array.Append(JsonValue::CreateStringValue(winrt::to_hstring(text)));values.Insert(winrt::to_hstring(key),array);}
            },value);
            result.Insert(winrt::to_hstring(domain),values);
        }
        const auto observation=config.root/L"state-observation.json";state::require_safe_path(observation);
        std::ofstream output(observation,std::ios::binary|std::ios::trunc);
        output<<winrt::to_string(result.Stringify());output.flush();require(static_cast<bool>(output),"Cannot inspect isolated QA state");
    }
    void command(const JsonObject& value){
        const auto name=value.GetNamedString(L"command");auto* reader=access.reader();const auto lesson=access.lesson();
        if(name==L"open"){
            auto relative=std::filesystem::path(value.GetNamedString(L"path").c_str());
            require(!relative.is_absolute(),"QA lesson must be relative to its isolated curriculum");
            const auto path=std::filesystem::weakly_canonical(config.content/relative);
            require(beneath(path,config.content)&&path.extension()==L".json","QA lesson escaped its isolated curriculum");
            const auto where=value.GetNamedObject(L"location");
            access.open(path,{winrt::to_string(where.GetNamedString(L"locale",L"es")),
                winrt::to_string(where.GetNamedString(L"route")),winrt::to_string(where.GetNamedString(L"course")),
                winrt::to_string(where.GetNamedString(L"unit"))});
        }else if(name==L"preferences"){
            const auto prefs=value.GetNamedObject(L"values");
            access.preferences([&](state::StateData& data){
                static const std::set<std::wstring> allowed{L"rate",L"enabled",L"kokoro_advance_generation",L"kokoro_voice:es",L"engine",L"language"};
                for(const auto& entry:prefs){require(allowed.contains(std::wstring(entry.Key())),"Unsupported QA narration preference");
                    const auto key=winrt::to_string(entry.Key());const auto item=entry.Value();
                    switch(item.ValueType()){
                    case JsonValueType::Boolean:data.preferences["lesson_narration_settings"][key]=item.GetBoolean();break;
                    case JsonValueType::Number:data.preferences["lesson_narration_settings"][key]=item.GetNumber();break;
                    case JsonValueType::String:data.preferences["lesson_narration_settings"][key]=winrt::to_string(item.GetString());break;
                    default:throw std::runtime_error("Unsupported QA preference value");}
                }
            });if(reader)reader->settings_changed();
        }else if(name==L"play"){require(reader,"No QA reader");if(!reader->snapshot().playing)reader->toggle();}
        else if(name==L"pause"){require(reader,"No QA reader");reader->pause();}
        else if(name==L"restart"){require(reader,"No QA reader");reader->restart();}
        else if(name==L"seek"){require(reader,"No QA reader");reader->seek_seconds(static_cast<int>(value.GetNamedNumber(L"seconds")));}
        else if(name==L"page"){require(lesson&&navigate_lesson_view(lesson,static_cast<std::size_t>(value.GetNamedNumber(L"page"))),"Ordinary page navigation rejected this QA command");}
        else if(name==L"reset_answer"){
            require(lesson&&reader,"No QA lesson/reader");const auto block=static_cast<int>(value.GetNamedNumber(L"block"));
            const auto reset=control(lesson,1012+block*100);
            // Use the same explicit reset action offered to a learner. A
            // fresh empty quiz already has nothing to reset; a solved one
            // must actually re-enable its editor before another submission.
            if(IsWindowEnabled(reset))SendMessageW(reset,BM_CLICK,0,0);
            const auto edit=control(lesson,1002+block*100);
            require(IsWindowEnabled(edit)&&GetWindowTextLengthW(edit)==0,"The ordinary QA answer reset did not clear and enable its editor");
            const auto check=control(lesson,1003+block*100);wchar_t caption[64]{};
            GetWindowTextW(check,caption,64);
            require(!IsWindowEnabled(check)&&std::wstring_view(caption)==L"Comprobar",
                "The ordinary QA answer reset did not restore its pending check control");
        }
        else if(name==L"answer"){
            require(lesson&&reader,"No QA lesson/reader");const auto block=static_cast<int>(value.GetNamedNumber(L"block"));
            const auto answer=value.GetNamedString(L"answer");const auto edit=control(lesson,1002+block*100);
            require(IsWindowEnabled(edit),"The QA answer editor is disabled; use the ordinary reset action before answering again");
            CHARRANGE range{0,-1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
            SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(answer.c_str()));
            const auto check=control(lesson,1003+block*100);require(IsWindowEnabled(check),"The ordinary QA check button is not ready for submission");
            SendMessageW(check,BM_CLICK,0,0);
        }else if(name==L"flush"){require(!lesson||flush_lesson_view(lesson),"Cannot flush the isolated QA lesson");}
        else if(name==L"inspect"){inspect_state();}
        else if(name==L"close"){PostMessageW(access.application,WM_CLOSE,0,0);}
        else throw std::runtime_error("Unknown application audio QA command");
    }
    JsonObject snapshot(){
        JsonObject result;number(result,L"pid",GetCurrentProcessId());number(result,L"sequence",acknowledged);number(result,L"samples",++samples);
        number(result,L"tick",static_cast<double>(GetTickCount64()));string(result,L"error",error);
        number(result,L"workerAvailableProcessors",access.available_processors());
        string(result,L"profileRoot",environment.paths.root.string());string(result,L"contentRoot",config.content.string());
        boolean(result,L"hidden",!IsWindowVisible(access.application));number(result,L"foreground",static_cast<double>(reinterpret_cast<std::uintptr_t>(GetForegroundWindow())));
        boolean(result,L"ownsForeground",GetAncestor(GetForegroundWindow(),GA_ROOT)==access.application);
        const auto lesson=access.lesson();
        if(lesson){const auto page=lesson_page_state(lesson);JsonObject data;number(data,L"index",page.index);number(data,L"count",page.count);
            number(data,L"contentRevision",page.content_revision);string(data,L"id",page.lesson_id);string(data,L"pageId",page.page_id);result.Insert(L"lesson",data);}
        if(auto* reader=access.reader()){
            const auto snapshot=reader->snapshot();JsonObject data;
            boolean(data,L"playing",snapshot.playing);boolean(data,L"allocated",snapshot.allocated);boolean(data,L"planned",snapshot.planned);
            boolean(data,L"preparing",snapshot.preparing);boolean(data,L"finished",snapshot.finished);boolean(data,L"waitingGate",snapshot.waiting_gate);
            number(data,L"index",snapshot.index);number(data,L"offset",static_cast<double>(snapshot.offset));number(data,L"heardThrough",static_cast<double>(snapshot.heard_through));
            number(data,L"rate",snapshot.rate);number(data,L"playbackRate",snapshot.playback_rate);string(data,L"key",snapshot.key);string(data,L"error",snapshot.error);
            string(data,L"pageAudioRevision",snapshot.page_audio_revision);boolean(data,L"pageAudioComplete",snapshot.page_audio_complete);
            number(data,L"foregroundSynthesisThreads",snapshot.foreground_synthesis_threads);
            JsonArray segments;for(const auto& part:snapshot.segments){JsonObject segment;string(segment,L"source",part.source);string(segment,L"region",part.region);
                string(segment,L"recording",part.recording.string());number(segment,L"block",part.block);number(segment,L"duration",part.duration_ms);
                boolean(segment,L"available",part.available);boolean(segment,L"feedback",part.feedback);boolean(segment,L"correct",part.correct);
                boolean(segment,L"feedbackFullyPlayed",part.feedback_fully_played);number(segment,L"timings",part.timings.size());segments.Append(segment);}
            data.Insert(L"segments",segments);result.Insert(L"reader",data);
        }
        if(auto* generation=access.generation()){
            const auto snapshot=generation->status();JsonObject data;string(data,L"page",snapshot.page);string(data,L"error",snapshot.error);
            string(data,L"revision",snapshot.revision);string(data,L"cpuClass",snapshot.cpu_class);number(data,L"synthesisThreads",snapshot.synthesis_threads);
            number(data,L"completed",snapshot.completed);number(data,L"total",snapshot.total);number(data,L"scans",static_cast<double>(snapshot.scans));
            boolean(data,L"running",snapshot.running);boolean(data,L"finished",snapshot.finished);boolean(data,L"waiting",snapshot.waiting);result.Insert(L"generation",data);
        }
        const auto file=environment.paths.root/L"state/active/state.bin";
        if(std::filesystem::exists(file))string(result,L"stateSha256",state::sha256(state::read_bytes(file)));
        return result;
    }
    void publish(const JsonObject& result){
        const auto text=winrt::to_string(result.Stringify());const auto temporary=config.root/L"snapshot.pending";
        const auto snapshot=config.root/L"snapshot.json",observations=config.root/L"observations.jsonl";
        state::require_safe_path(temporary);state::require_safe_path(snapshot);state::require_safe_path(observations);
        {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<text;output.flush();require(static_cast<bool>(output),"Cannot write QA snapshot");}
        const auto publish_deadline=GetTickCount64()+250;unsigned attempts=0;
        for(;;){
            ++attempts;
            const auto attributes=GetFileAttributesW(snapshot.c_str());
            const auto attribute_failure=attributes==INVALID_FILE_ATTRIBUTES?GetLastError():ERROR_SUCCESS;
            const bool existing=attributes!=INVALID_FILE_ATTRIBUTES;
            if(!existing&&attribute_failure!=ERROR_FILE_NOT_FOUND&&attribute_failure!=ERROR_PATH_NOT_FOUND)
                throw std::runtime_error("Cannot inspect QA snapshot destination; win32="+std::to_string(attribute_failure)+
                    "; destination="+snapshot.string());
            // Replacing an existing shared-read snapshot uses the file
            // replacement operation. MoveFileEx's replace-existing rename can
            // reject a still-open target even when its reader shares delete.
            const char* operation=existing?"ReplaceFileW":"MoveFileExW";
            const auto published=existing?ReplaceFileW(snapshot.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr):
                MoveFileExW(temporary.c_str(),snapshot.c_str(),MOVEFILE_WRITE_THROUGH);
            if(published)break;
            const auto failure=GetLastError();
            const bool temporarily_unavailable=failure==ERROR_SHARING_VIOLATION||failure==ERROR_ACCESS_DENIED;
            if(!temporarily_unavailable||GetTickCount64()>=publish_deadline)
                throw std::runtime_error("Cannot publish QA snapshot; operation="+std::string(operation)+"; win32="+std::to_string(failure)+
                    "; attempts="+std::to_string(attempts)+"; source="+temporary.string()+"; destination="+snapshot.string());
            // This one owned QA output can briefly remain unavailable while
            // an old read handle closes, including a delete-pending name.
            // Both observed access-denied and sharing violations have the
            // same strict deadline; a permanent denial still fails closed.
            Sleep(5);
            state::require_safe_path(temporary);state::require_safe_path(snapshot);
        }
        const auto trace=result.HasKey(L"reader")?result.GetNamedObject(L"reader"):JsonObject{};
        const auto generation=result.HasKey(L"generation")?winrt::to_string(result.GetNamedObject(L"generation").Stringify()):std::string{};
        const auto identity=error+'\0'+winrt::to_string(trace.Stringify())+generation;
        // Keep raw snapshots as evidence. No log contains personal state.
        if(identity!=prior_trace){std::ofstream log(observations,std::ios::binary|std::ios::app);log<<text<<'\n';prior_trace=identity;}
    }
    void tick(){
        const auto command_file=config.root/L"command.json";
        if(std::filesystem::exists(command_file)){
            const auto value=JsonObject::Parse(winrt::to_hstring(source_bytes(command_file)));const auto sequence=value.GetNamedNumber(L"sequence");
            if(sequence>acknowledged){acknowledged=sequence;error.clear();try{command(value);}catch(const std::exception& failure){error=failure.what();}}
        }
        publish(snapshot());
    }
};
AppAudioRevisionQa::AppAudioRevisionQa(AppAudioRevisionQaConfig config,WindowsEnvironment env,AppAudioRevisionQaAccess access):
    impl_(std::make_unique<Impl>(std::move(config),std::move(env),std::move(access))){}
AppAudioRevisionQa::~AppAudioRevisionQa()=default;
void AppAudioRevisionQa::tick() noexcept{
    // WinRT/COM work on this UI thread can pump another WM_TIMER. A nested
    // observer must not truncate or rename the outer observer's pending file,
    // execute its next command, or publish an older snapshot over a new one.
    if(impl_->ticking)return;
    impl_->ticking=true;
    struct TickScope{bool& active;~TickScope(){active=false;}} scope{impl_->ticking};
    try{impl_->tick();}
    catch(const std::exception& failure){impl_->error=failure.what();OutputDebugStringW(winrt::to_hstring(impl_->error).c_str());}
    catch(const winrt::hresult_error& failure){impl_->error=winrt::to_string(failure.message());OutputDebugStringW(failure.message().c_str());}
}
}
