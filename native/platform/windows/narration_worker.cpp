#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "platform/windows/system_speech_process.hpp"
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lgaed::windows {
namespace {
using namespace winrt::Windows::Data::Json;
struct Handle{HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}void close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}};
std::wstring quote(std::wstring_view text){std::wstring result=L"\"";std::size_t slashes=0;for(auto c:text){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;}else{result.append(slashes,L'\\');result+=c;}slashes=0;}result.append(slashes*2,L'\\');return result+L'\"';}
void put(JsonObject& value,std::wstring_view key,std::string_view text){value.Insert(winrt::hstring(key),JsonValue::CreateStringValue(winrt::to_hstring(text)));}
std::string string(const JsonObject& object,const wchar_t* key){return winrt::to_string(object.GetNamedString(key,L""));}
std::size_t index(const JsonObject& object,const wchar_t* key){const auto value=object.GetNamedNumber(key,0);if(value<0||value>20000000||std::trunc(value)!=value)throw std::runtime_error("Invalid narration offset");return static_cast<std::size_t>(value);}
struct PriorityState {
    std::mutex mutex;
    std::condition_variable_any changed;
    unsigned current=0,heavy=0;
    bool background=false,inference=false,closed=false;
    std::stop_source cancel_background,cancel_inference;
};
struct PageRegistry {
    struct Entry {
        std::uint64_t token=0;bool foreground=false;
        std::weak_ptr<void> session;std::stop_source cancel;
    };
    std::mutex mutex;std::uint64_t serial=0;
    std::map<std::string,Entry> pages;
};
bool digest_string(std::string_view value){return value.size()==64&&std::ranges::all_of(value,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string page_framing(std::initializer_list<std::string_view> fields){std::string result;
    for(const auto field:fields){result+=std::to_string(field.size())+':';result+=field;}return result;}
std::string page_owner(std::string_view page,std::string_view language,std::string_view voice,std::string_view engine){
    return state::sha256(page_framing({"narration-page-owner-v1",page,language,voice,engine}));
}
std::string page_namespace(std::string_view owner,std::string_view revision){
    return state::sha256(page_framing({"narration-page-pcm-v1",owner,revision}));
}
std::filesystem::path page_manifest(const std::filesystem::path& cache,std::string_view owner){
    return cache/L"page-audio"/(std::wstring(winrt::to_hstring(owner).c_str())+L".json");
}
struct PublishedPage {
    JsonObject proof;
    std::string revision,pcm_seed;
};
std::optional<PublishedPage> read_page_manifest(const std::filesystem::path& path,std::string_view owner){
    state::require_safe_path(path);
    if(!std::filesystem::is_regular_file(state::native_io_path(path)))return {};
    try{
        ensure_winrt_apartment();
        const auto bytes=state::read_bytes(path,64*1024*1024);
        const auto proof=JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
        const auto format=proof.GetNamedNumber(L"format",0);
        if((format!=1&&format!=2)||owner!=page_owner(string(proof,L"page"),string(proof,L"language"),
            string(proof,L"voice"),string(proof,L"engine")))return {};
        const auto revision=string(proof,L"revision");
        const auto seed=format==1?revision:string(proof,L"pcmSeedRevision");
        if(!digest_string(revision)||!digest_string(seed)||string(proof,L"cacheNamespace")!=page_namespace(owner,seed))return {};
        return PublishedPage{proof,revision,seed};
    }catch(const winrt::hresult_error&){return {};}
}
// The old layout salted each utterance with the page revision. Pin its last
// published salt (or the current one on first use), keeping even existing
// word timings at their original paths. Persist before preparing any audio so
// an edit or restart during the first, incomplete generation can reuse it too.
std::string page_pcm_seed(const std::filesystem::path& manifest,std::string_view owner,
        std::string_view current_revision,bool initialize){
    auto marker=manifest;marker.replace_extension(L".pcm-seed");
    const auto read_marker=[&]() -> std::optional<std::string> {
        state::require_safe_path(marker);
        if(!std::filesystem::is_regular_file(state::native_io_path(marker)))return {};
        if(std::filesystem::file_size(state::native_io_path(marker))!=64)return {};
        const auto bytes=state::read_bytes(marker,64);
        std::string seed(bytes.begin(),bytes.end());
        if(!digest_string(seed))return {};
        return seed;
    };
    if(auto seed=read_marker())return *seed;
    const auto published=read_page_manifest(manifest,owner);
    const std::string seed=published?published->pcm_seed:std::string(current_revision);
    if(!initialize)return seed;
    static std::atomic_uint64_t serial{};
    const auto pending=std::filesystem::path(marker.native()+L"."+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial)+L".pending");
    state::require_safe_path(pending);
    const auto temp=state::native_io_path(pending),target=state::native_io_path(marker);
    {
        std::ofstream stream(temp,std::ios::binary|std::ios::noreplace);
        if(!stream)throw std::runtime_error("Cannot stage the narration page cache identity");
        stream.write(seed.data(),static_cast<std::streamsize>(seed.size()));stream.close();
        if(!stream){std::filesystem::remove(temp);throw std::runtime_error("Cannot write the narration page cache identity");}
    }
    if(MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))return seed;
    const auto error=GetLastError();std::filesystem::remove(temp);
    if(error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS){
        if(auto winner=read_marker())return *winner;
        // An unreadable/malformed marker does not own any recording. Keep the
        // original evidence and recover from the independently validated proof.
        return seed;
    }
    throw std::system_error(static_cast<int>(error),std::system_category(),"Cannot publish the narration page cache identity");
}
std::function<void()> take_current(const std::shared_ptr<PriorityState>& state,std::stop_token stop) {
    std::stop_source cancel;
    {const std::lock_guard lock(state->mutex);if(state->closed||stop.stop_requested())throw std::runtime_error("Lectura cancelada");
        ++state->current;if(state->background)cancel=state->cancel_background;}
    // Cancellation callbacks can write to the worker; never invoke them while
    // holding the arbitration lock needed by the acknowledgement cleanup.
    cancel.request_stop();
    std::unique_lock lock(state->mutex);
    if(!state->changed.wait(lock,stop,[&]{return state->closed||!state->background;})||state->closed||stop.stop_requested()) {
        --state->current;state->changed.notify_all();throw std::runtime_error("Lectura cancelada");
    }
    return [state]{const std::lock_guard release(state->mutex);--state->current;state->changed.notify_all();};
}
std::function<void()> take_heavy(const std::shared_ptr<PriorityState>& state,std::stop_token stop) {
    std::stop_source cancel_background,cancel_inference;
    {const std::lock_guard lock(state->mutex);if(state->closed||stop.stop_requested())throw std::runtime_error("Lectura cancelada");
        ++state->heavy;if(state->background)cancel_background=state->cancel_background;
        if(state->inference)cancel_inference=state->cancel_inference;}
    cancel_background.request_stop();cancel_inference.request_stop();
    std::unique_lock lock(state->mutex);
    if(!state->changed.wait(lock,stop,[&]{return state->closed||(!state->inference&&!state->background);})||state->closed||stop.stop_requested()) {
        --state->heavy;state->changed.notify_all();throw std::runtime_error("Lectura cancelada");
    }
    return [state]{const std::lock_guard release(state->mutex);--state->heavy;state->changed.notify_all();};
}
struct BackgroundReservation {
    std::shared_ptr<PriorityState> state;
    std::stop_token stop;
    BackgroundReservation(std::shared_ptr<PriorityState> value,std::stop_token cancelled):state(std::move(value)) {
        std::unique_lock lock(state->mutex);
        if(!state->changed.wait(lock,cancelled,[&]{return state->closed||(!state->current&&!state->heavy&&!state->background);})||state->closed)
            throw std::runtime_error("Lectura cancelada");
        state->background=true;state->cancel_background=std::stop_source{};stop=state->cancel_background.get_token();
    }
    ~BackgroundReservation(){const std::lock_guard lock(state->mutex);state->background=false;state->changed.notify_all();}
};
struct InferenceReservation {
    std::shared_ptr<PriorityState> state;
    std::stop_token stop;
    InferenceReservation(std::shared_ptr<PriorityState> value,std::stop_token cancelled):state(std::move(value)) {
        std::unique_lock lock(state->mutex);
        if(!state->changed.wait(lock,cancelled,[&]{return state->closed||(!state->heavy&&!state->inference);})||state->closed||cancelled.stop_requested())
            throw std::runtime_error("Lectura cancelada");
        state->inference=true;state->cancel_inference=std::stop_source{};stop=state->cancel_inference.get_token();
    }
    ~InferenceReservation(){const std::lock_guard lock(state->mutex);state->inference=false;state->changed.notify_all();}
};
}
struct NarrationPageSession::Impl {
    std::shared_ptr<PageRegistry> registry;
    std::string owner,page,revision,pcm_seed,name_space,language,voice,engine;
    std::filesystem::path cache,manifest;
    std::vector<NarrationSegment> segments;
    std::uint64_t token=0;std::stop_source cancel;
    std::unique_ptr<std::stop_callback<std::function<void()>>> upstream;
};
const std::string& NarrationPageSession::cache_namespace()const{
    if(!impl_)throw std::logic_error("Missing narration page session");return impl_->name_space;
}
std::stop_token NarrationPageSession::stop_token()const{return impl_?impl_->cancel.get_token():std::stop_token{};}
bool NarrationPageSession::current()const{
    if(!impl_||impl_->cancel.stop_requested())return false;
    const std::lock_guard lock(impl_->registry->mutex);const auto found=impl_->registry->pages.find(impl_->owner);
    return found!=impl_->registry->pages.end()&&found->second.token==impl_->token&&!impl_->cancel.stop_requested();
}
bool NarrationPageSession::commit(std::span<const NarrationAudio> audio)const{
    if(!current())return false;
    if(audio.size()!=impl_->segments.size())throw std::invalid_argument("Only a complete narration page can be committed");
    ensure_winrt_apartment();JsonObject manifest;manifest.Insert(L"format",JsonValue::CreateNumberValue(2));
    put(manifest,L"pcmSeedRevision",impl_->pcm_seed);
    put(manifest,L"page",impl_->page);put(manifest,L"revision",impl_->revision);put(manifest,L"cacheNamespace",impl_->name_space);
    put(manifest,L"language",impl_->language);put(manifest,L"voice",impl_->voice);put(manifest,L"engine",impl_->engine);JsonArray entries;
    for(std::size_t i=0;i<audio.size();++i){
        if(!current())return false;
        const auto& item=audio[i];const auto& expected=impl_->segments[i];
        if(item.source!=expected.text||item.spoken!=expected.spoken||item.prepared!=expected.prepared||
            item.cache_namespace!=impl_->name_space||!digest_string(item.key)||!std::isfinite(item.duration_ms)||item.duration_ms<=0)
            throw std::invalid_argument("Narration page completion has a different source or order");
        const auto relative=item.wav.lexically_normal().lexically_relative(impl_->cache.lexically_normal());
        if(relative.empty()||relative.is_absolute()||std::ranges::any_of(relative,[](const auto& part){return part==L"..";}))
            throw std::invalid_argument("Narration page audio escaped its cache");
        state::require_safe_path(item.wav);
        JsonObject entry;put(entry,L"key",item.key);put(entry,L"wav",winrt::to_string(relative.native()));
        put(entry,L"preparedSha256",state::sha256(expected.prepared));
        const auto pcm=state::read_bytes(item.wav);put(entry,L"audioSha256",state::sha256(pcm));
        entry.Insert(L"bytes",JsonValue::CreateNumberValue(static_cast<double>(pcm.size())));entries.Append(entry);
    }
    manifest.Insert(L"segments",entries);
    const auto pending=std::filesystem::path(impl_->manifest.native()+L"."+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(impl_->token)+L".pending");
    try{
        {std::ofstream stream(state::native_io_path(pending),std::ios::binary|std::ios::trunc);
            stream<<winrt::to_string(manifest.Stringify());stream.flush();if(!stream)throw std::runtime_error("Cannot stage the narration page completion");}
        // Keep this check and publication in the same critical section as
        // begin_page. A cancelled/older revision can never replace its successor.
        const std::lock_guard lock(impl_->registry->mutex);const auto found=impl_->registry->pages.find(impl_->owner);
        if(found==impl_->registry->pages.end()||found->second.token!=impl_->token||impl_->cancel.stop_requested()){
            std::filesystem::remove(state::native_io_path(pending));return false;}
        if(!MoveFileExW(state::native_io_path(pending).c_str(),state::native_io_path(impl_->manifest).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish the narration page completion");
        return true;
    }catch(...){std::error_code ignored;std::filesystem::remove(state::native_io_path(pending),ignored);throw;}
}
NarrationPriorityLease::~NarrationPriorityLease(){if(release_)release_();}
NarrationPriorityLease::NarrationPriorityLease(NarrationPriorityLease&& other) noexcept:release_(std::exchange(other.release_,{})){}
NarrationPriorityLease& NarrationPriorityLease::operator=(NarrationPriorityLease&& other) noexcept {
    if(this!=&other){if(release_)release_();release_=std::exchange(other.release_,{});}return *this;
}
struct NarrationWorker::Impl {
    struct TransportFailure : std::runtime_error {using std::runtime_error::runtime_error;};
    // A caller holds its exact process generation until its write, reply,
    // cancellation callback and CPU scope have all unwound. Replacing a dead
    // generation must never close a handle still borrowed by another caller.
    struct Connection {
        Handle process,job,input,output,error;
        std::unique_ptr<NarrationCpuPolicy> cpu;
        std::mutex write_mutex,response_mutex;
        std::atomic<bool> failed{false};
        std::string buffered,failure;
        struct Pending {std::string command,reply;bool lightweight=false;};
        std::map<std::string,std::shared_ptr<Pending>> pending;
        std::condition_variable responses;
        std::jthread pump;
        ~Connection(){fail("El proceso de Kokoro está cerrado.");if(pump.joinable()){pump.request_stop();responses.notify_all();pump.join();}}
        bool usable()const noexcept {
            return !failed.load()&&process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;
        }
        void fail(std::string_view message) noexcept {
            // Do not wait for the writer: terminating the owned child also
            // releases WriteFile if the child stopped reading its pipe.
            {const std::lock_guard lock(response_mutex);
                if(failed.exchange(true))return;
                try{failure.assign(message);}catch(...){}}
            if(process.value){
                if(!job.value||!TerminateJobObject(job.value,1))TerminateProcess(process.value,1);
            }
            responses.notify_all();
        }
        std::string reason(){
            const std::lock_guard lock(response_mutex);
            return failure.empty()?"Kokoro terminó antes de responder.":failure;
        }
        void start(const std::filesystem::path& runtime){
        ensure_winrt_apartment();if(!std::filesystem::is_regular_file(runtime))throw std::runtime_error("Kokoro aún no está preparado para Windows. Compila la aplicación con su componente de narración.");
        const auto bytes=state::read_bytes(runtime,1024*1024);const auto config=JsonObject::Parse(winrt::to_hstring(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
        const auto java=std::filesystem::path(config.GetNamedString(L"java").c_str());if(!std::filesystem::is_regular_file(java))throw std::runtime_error("No se encontró el entorno de narración configurado.");
        auto command=quote(java.native());for(const auto& arg:config.GetNamedArray(L"arguments"))command+=L" "+quote(arg.GetString().c_str());
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};Handle child_input,child_output;
        if(!CreatePipe(&child_input.value,&input.value,&security,0)||!CreatePipe(&output.value,&child_output.value,&security,0))throw std::runtime_error("No se pudo conectar Kokoro.");
        SetHandleInformation(input.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0);
        const auto logs=runtime.parent_path()/L"worker-stderr.log";error.value=CreateFileW(logs.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(error.value==INVALID_HANDLE_VALUE)throw std::runtime_error("No se pudo abrir el diagnóstico de Kokoro.");SetFilePointer(error.value,0,nullptr,FILE_END);
        job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("No se pudo aislar el proceso de Kokoro.");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow=SW_HIDE;startup.StartupInfo.hStdInput=child_input.value;startup.StartupInfo.hStdOutput=child_output.value;startup.StartupInfo.hStdError=error.value;
        SIZE_T attribute_bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&attribute_bytes);std::vector<std::uint8_t> storage(attribute_bytes);
        startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&attribute_bytes))throw std::runtime_error("No se pudo preparar el proceso de Kokoro.");
        struct Attributes{LPPROC_THREAD_ATTRIBUTE_LIST list;~Attributes(){DeleteProcThreadAttributeList(list);}} attributes{startup.lpAttributeList};
        const HANDLE handles[]{child_input.value,child_output.value,error.value};
        if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(handles),sizeof(handles),nullptr,nullptr))throw std::runtime_error("No se pudieron aislar los canales de Kokoro.");
        PROCESS_INFORMATION info{};
        if(!CreateProcessW(java.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,runtime.parent_path().c_str(),&startup.StartupInfo,&info))throw std::runtime_error("No se pudo iniciar Kokoro para Windows.");
        process.value=info.hProcess;Handle thread{info.hThread};
        if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);throw std::runtime_error("No se pudo vincular el proceso de Kokoro.");}
        try{cpu=std::make_unique<NarrationCpuPolicy>(process.value,job.value);}
        catch(...){TerminateProcess(process.value,1);process.close();throw;}
        if(ResumeThread(thread.value)==static_cast<DWORD>(-1))throw std::runtime_error("No se pudo activar Kokoro.");
        pump=std::jthread([this](std::stop_token stop){read_replies(stop);});
    }

        void send(const std::string& source){
            const std::lock_guard lock(write_mutex);
            if(failed.load())throw TransportFailure(reason());
            const auto line=source+'\n';std::size_t offset=0;
            while(offset<line.size()){
                DWORD written=0;
                const auto count=static_cast<DWORD>(std::min<std::size_t>(line.size()-offset,64*1024));
                if(!WriteFile(input.value,line.data()+offset,count,&written,nullptr)||!written){
                    fail("Kokoro cerró el canal de entrada.");throw TransportFailure(reason());
                }
                offset+=written;
            }
        }
        void read_replies(std::stop_token stop){
            std::stop_callback wake(stop,[&]{responses.notify_all();});
            try{ensure_winrt_apartment();while(!stop.stop_requested()&&!failed.load()){
                const auto end=buffered.find('\n');
                if(end!=std::string::npos){
                    const auto line=buffered.substr(0,end);buffered.erase(0,end+1);if(line.empty())continue;
                    JsonObject reply;
                    try{reply=JsonObject::Parse(winrt::to_hstring(line));}
                    catch(...){throw TransportFailure("Kokoro devolvió una respuesta no válida.");}
                    const auto id=string(reply,L"id"),command=string(reply,L"command");
                    if(id.empty()||command.empty()||(id=="invalid"&&command=="invalid"))
                        throw TransportFailure("Kokoro devolvió un error sin identificador de petición.");
                    {const std::lock_guard lock(response_mutex);const auto found=pending.find(id);
                        if(found!=pending.end()){
                            if(found->second->command==command)found->second->reply=line;
                            else if(command!="cancel")throw TransportFailure("Kokoro devolvió una respuesta para otro comando.");
                        }}
                    // Late cancelled replies and cancel acknowledgements are
                    // deliberately discarded; only the original ID can finish.
                    responses.notify_all();continue;
                }
                DWORD available=0;
                if(!PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr))
                    throw TransportFailure("Kokoro terminó antes de responder. Consulta el diagnóstico de narración.");
                if(available){
                    char bytes[8192];DWORD read=0;
                    if(!ReadFile(output.value,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr)||!read)
                        throw TransportFailure("Kokoro cerró su salida.");
                    buffered.append(bytes,read);
                    if(buffered.size()>16*1024*1024)throw TransportFailure("Respuesta de Kokoro demasiado grande.");
                }else{
                    if(WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)
                        throw TransportFailure("Kokoro terminó antes de responder.");
                    std::unique_lock lock(response_mutex);
                    if(pending.empty())responses.wait_for(lock,std::chrono::seconds(1),[&]{return stop.stop_requested()||failed.load()||!pending.empty();});
                    else {const bool interactive=std::ranges::any_of(pending,[](const auto& item){return item.second->lightweight;});
                        responses.wait_for(lock,std::chrono::milliseconds(interactive?2:15));}
                }
            }}catch(const std::exception& error){fail(error.what());}
            catch(...){fail("Kokoro devolvió un error de transporte no válido.");}
        }
    };
    std::filesystem::path runtime;
    std::shared_ptr<Connection> active;
    SystemSpeechProcess system_speech;
    std::shared_ptr<PriorityState> priority=std::make_shared<PriorityState>();
    std::shared_ptr<PageRegistry> pages=std::make_shared<PageRegistry>();
    // A fresh generation has its own pending IDs and immutable handle lifetime.
    // Inference and the lightweight mathematical lane keep their arbitration.
    std::mutex request_mutex,start_mutex,shutdown_mutex;
    std::atomic<bool> closed{false};
    std::atomic<std::uint64_t> serial{0};
    std::atomic<std::uint64_t> storage_budget{std::numeric_limits<std::uint64_t>::max()};
    explicit Impl(std::filesystem::path file):runtime(std::move(file)){}
    ~Impl(){shutdown();}
    void shutdown() noexcept {
        const std::lock_guard closing(shutdown_mutex);
        if(closed.exchange(true))return;
        system_speech.shutdown();
        {const std::lock_guard lock(priority->mutex);priority->closed=true;}
        priority->changed.notify_all();
        const std::lock_guard starting(start_mutex);
        if(active)active->fail("El servicio de narración está cerrado.");
    }
    std::shared_ptr<Connection> start(){
        const std::lock_guard starting(start_mutex);
        if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");
        if(active&&active->usable())return active;
        if(active){
            active->fail("El proceso anterior de Kokoro terminó.");
            // Wait for the terminated child, not for callers or pipe writers.
            // Never let two live inference generations own the same cache.
            if(active->process.value&&WaitForSingleObject(active->process.value,5000)!=WAIT_OBJECT_0)
                throw std::runtime_error("No se pudo terminar el proceso anterior de Kokoro.");
        }
        auto next=std::make_shared<Connection>();
        next->start(runtime);
        active=next;return next;
    }
    JsonObject call(JsonObject request,std::stop_token stop,std::string* original_reply=nullptr,bool lightweight=false,
                    NarrationCpuClass work=NarrationCpuClass::current_page,bool inference=false){
        if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");
        const bool background=work==NarrationCpuClass::background;
        NarrationPriorityLease reservation(background?std::function<void()>{}:take_current(priority,stop));
        std::unique_ptr<BackgroundReservation> background_slot;
        if(background)background_slot=std::make_unique<BackgroundReservation>(priority,stop);
        const auto caller_stop=stop;
        std::stop_source operation;
        std::stop_callback external_cancel(stop,[&]{operation.request_stop();});
        std::stop_callback preempt(background_slot?background_slot->stop:std::stop_token{},[&]{operation.request_stop();});
        std::unique_ptr<InferenceReservation> inference_slot;
        if(inference)try{inference_slot=std::make_unique<InferenceReservation>(priority,operation.get_token());}
            catch(...){if(background_slot&&background_slot->stop.stop_requested()&&!caller_stop.stop_requested())throw NarrationPreempted{};throw;}
        std::stop_callback heavy_preempt(inference_slot?inference_slot->stop:std::stop_token{},[&]{operation.request_stop();});
        stop=operation.get_token();
        const auto cancelled=[&]() {
            if(!caller_stop.stop_requested()&&((background_slot&&background_slot->stop.stop_requested())||
                (inference_slot&&inference_slot->stop.stop_requested())))throw NarrationPreempted{};
            throw std::runtime_error("Lectura cancelada");
        };
        std::unique_lock serialized(request_mutex,std::defer_lock);if(!lightweight)serialized.lock();
        ensure_winrt_apartment();if(stop.stop_requested())cancelled();
        const auto command=string(request,L"command");
        const auto requested_threads=static_cast<unsigned>(request.GetNamedNumber(L"threads",0));
        // These operations are immutable requests or cache-keyed preparation.
        // A retry may reuse an already-published PCM, but cannot commit a
        // cancelled page (the separate PageRegistry still owns publication).
        // Never replay a stateful structured-editor mutation or validation.
        const bool replayable=command=="plan"||command=="curriculum_plan"||
            command=="identity"||command=="prepare"||command=="describe_math";
        for(unsigned attempt=0;;++attempt){
            if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");
            if(stop.stop_requested())cancelled();
            const auto connection=start();
            try{
                std::unique_ptr<NarrationCpuScope> cpu_scope;
                try{
                    if(inference||background)cpu_scope=std::make_unique<NarrationCpuScope>(*connection->cpu,work);
                    else connection->cpu->apply(NarrationCpuClass::current_page);
                }catch(...){
                    if(connection->usable())throw;
                    connection->fail("Kokoro terminó al preparar la afinidad de CPU.");
                    throw TransportFailure(connection->reason());
                }
                if(inference){
                    const auto capacity=connection->cpu->inference_threads(work);
                    request.Insert(L"threads",JsonValue::CreateNumberValue(requested_threads?std::clamp(requested_threads,1u,capacity):capacity));
                }
                const auto id=std::to_string(++serial);put(request,L"id",id);
                const auto response=std::make_shared<Connection::Pending>();response->command=command;response->lightweight=lightweight;
                {const std::lock_guard lock(connection->response_mutex);connection->pending.emplace(id,response);}
                struct Remove {Connection& owner;const std::string& id;~Remove(){const std::lock_guard lock(owner.response_mutex);owner.pending.erase(id);}} remove{*connection,id};
                connection->send(winrt::to_string(request.Stringify()));
                connection->responses.notify_all();
                std::stop_callback cancel(stop,[&]{
                    if(closed.load()||connection->failed.load())return;
                    try{ensure_winrt_apartment();JsonObject value;put(value,L"command","cancel");put(value,L"id",id);connection->send(winrt::to_string(value.Stringify()));}catch(...){}
                    if(lightweight)connection->responses.notify_all();
                });
                std::string line;
                {std::unique_lock lock(connection->response_mutex);
                    if(!connection->responses.wait_for(lock,std::chrono::minutes(5),[&]{return closed.load()||
                        (lightweight&&stop.stop_requested())||!response->reply.empty()||connection->failed.load();})){
                        lock.unlock();connection->fail("Kokoro excedió el tiempo de preparación.");throw TransportFailure(connection->reason());
                    }
                    if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");
                    if(lightweight&&stop.stop_requested())cancelled();
                    if(response->reply.empty()){lock.unlock();throw TransportFailure(connection->reason());}
                    line=response->reply;
                }
                if(cpu_scope&&connection->usable()){
                    try{cpu_scope->finish();}catch(...){if(connection->usable())throw;}
                }
                if(stop.stop_requested())cancelled();
                const auto reply=JsonObject::Parse(winrt::to_hstring(line));
                // A correlated application error is final for this request,
                // never a transport failure that can restart a healthy JVM.
                if(reply.HasKey(L"error"))throw std::runtime_error("Kokoro: "+string(reply,L"error"));
                if(original_reply)*original_reply=line;return reply;
            }catch(const TransportFailure& failure){
                if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");
                if(stop.stop_requested())cancelled();
                if(!replayable||attempt!=0)throw;
                connection->fail(failure.what());
            }
        }
    }
};
NarrationWorker::NarrationWorker(std::filesystem::path runtime):impl_(std::make_unique<Impl>(std::move(runtime))){}
NarrationWorker::~NarrationWorker()=default;
void NarrationWorker::shutdown() noexcept {impl_->shutdown();}
void NarrationWorker::set_storage_budget(std::uint64_t bytes){impl_->storage_budget.store(bytes);}
unsigned NarrationWorker::available_processors()const{
    const std::lock_guard lock(impl_->start_mutex);return impl_->active&&impl_->active->cpu?impl_->active->cpu->available_processors():0;
}
NarrationPriorityLease NarrationWorker::current_page_lease(std::stop_token stop){return NarrationPriorityLease(take_current(impl_->priority,stop));}
NarrationPriorityLease NarrationWorker::heavy_work_lease(std::stop_token stop){return NarrationPriorityLease(take_heavy(impl_->priority,stop));}
std::optional<NarrationPriorityLease> NarrationWorker::try_idle_maintenance_lease(){
    const auto state=impl_->priority;const std::lock_guard lock(state->mutex);
    if(state->closed||state->current||state->heavy||state->background||state->inference)return {};
    ++state->heavy;
    return NarrationPriorityLease([state]{const std::lock_guard release(state->mutex);--state->heavy;state->changed.notify_all();});
}
std::optional<NarrationPageSession> NarrationWorker::begin_page(const std::filesystem::path& cache,
    std::string_view page_id,std::string_view language,std::string_view voice,std::string_view engine,
    const NarrationPagePlan& plan,NarrationCpuClass work,std::stop_token stop){
    if(page_id.empty()||plan.page_id!=page_id||!digest_string(plan.revision)||!cache.is_absolute())
        throw std::invalid_argument("Invalid narration page version or cache");
    if(stop.stop_requested())throw std::runtime_error("Lectura cancelada");
    const auto value=std::make_shared<NarrationPageSession::Impl>();value->registry=impl_->pages;
    value->page=page_id;value->revision=plan.revision;value->cache=cache.lexically_normal();value->segments=plan.segments;
    value->language=language;value->voice=voice;value->engine=engine;
    value->owner=page_owner(page_id,language,voice,engine);
    const auto directory=cache/L"page-audio";state::require_safe_path(directory);
    std::filesystem::create_directories(state::native_io_path(directory));
    value->manifest=page_manifest(cache,value->owner);
    value->pcm_seed=page_pcm_seed(value->manifest,value->owner,plan.revision,true);
    value->name_space=page_namespace(value->owner,value->pcm_seed);
    auto cancel=value->cancel;
    value->upstream=std::make_unique<std::stop_callback<std::function<void()>>>(stop,[cancel]()mutable{cancel.request_stop();});
    std::stop_source previous;
    {const std::lock_guard lock(impl_->pages->mutex);auto& entry=impl_->pages->pages[value->owner];
        if(const auto existing=entry.session.lock();existing&&!entry.cancel.stop_requested()){
            if(work==NarrationCpuClass::background&&(entry.foreground||
                std::static_pointer_cast<NarrationPageSession::Impl>(existing)->revision==plan.revision))return {};
            previous=entry.cancel;
        }
        value->token=++impl_->pages->serial;entry={value->token,work!=NarrationCpuClass::background,value,value->cancel};
    }
    previous.request_stop();return NarrationPageSession(value);
}
std::string NarrationWorker::page_cache_namespace(const std::filesystem::path& cache,const NarrationPagePlan& plan,std::string_view language,
    std::string_view voice,std::string_view engine)const{
    if(!cache.is_absolute()||plan.page_id.empty()||!digest_string(plan.revision))throw std::invalid_argument("Invalid narration page version or cache");
    const auto owner=page_owner(plan.page_id,language,voice,engine);
    return page_namespace(owner,page_pcm_seed(page_manifest(cache,owner),owner,plan.revision,false));
}
std::optional<std::string> NarrationWorker::published_page_revision(const std::filesystem::path& cache,
    std::string_view page_id,std::string_view language,std::string_view voice,std::string_view engine)const{
    try{
        ensure_winrt_apartment();if(page_id.empty()||!cache.is_absolute())return {};
        const auto owner=page_owner(page_id,language,voice,engine);const auto path=page_manifest(cache,owner);
        const auto published=read_page_manifest(path,owner);if(!published)return {};
        const auto segments=published->proof.GetNamedArray(L"segments");if(segments.Size()>100000)return {};
        for(const auto& value:segments){const auto entry=value.GetObject();
            if(!digest_string(string(entry,L"key"))||!digest_string(string(entry,L"preparedSha256"))||!digest_string(string(entry,L"audioSha256")))return {};
            const auto relative=std::filesystem::path(entry.GetNamedString(L"wav").c_str());
            if(relative.empty()||relative.is_absolute()||std::ranges::any_of(relative,[](const auto& part){return part==L"..";}))return {};
            const auto wave=cache/relative;state::require_safe_path(wave);
            const auto size=entry.GetNamedNumber(L"bytes",0);
            if(size<=44||size>128*1024*1024||std::trunc(size)!=size||
                std::filesystem::file_size(state::native_io_path(wave))!=static_cast<std::uintmax_t>(size))return {};
        }
        return published->revision;
    }catch(const std::exception&){return {};}catch(const winrt::hresult_error&){return {};}
}
std::vector<NarrationSegment> NarrationWorker::plan(std::string_view text,std::string_view language,std::stop_token stop,
    std::string_view reading_context,std::string_view math_mode,bool previous_output_data){
    return plan(text,language,stop,reading_context,math_mode,
        previous_output_data?NarrationOutputDataPolicy::original:NarrationOutputDataPolicy::current);
}
std::vector<NarrationSegment> NarrationWorker::plan(std::string_view text,std::string_view language,std::stop_token stop,
    std::string_view reading_context,std::string_view math_mode,NarrationOutputDataPolicy output_data_policy,
    const std::optional<curriculum::TextSemantics>& semantics,NarrationSemanticPolicy semantic_policy,bool previous_prose_inference,std::string_view recording_voice){
    if(reading_context!="auto"&&reading_context!="cpp"&&reading_context!="cpp_previous_parentheses")throw std::invalid_argument("Invalid narration reading context");
    if(math_mode!="auto"&&math_mode!="text"&&math_mode!="inline"&&math_mode!="block")throw std::invalid_argument("Invalid narration mathematical mode");
    ensure_winrt_apartment();JsonObject request;put(request,L"command","plan");put(request,L"text",text);put(request,L"language",language);
    put(request,L"readingContext",reading_context);put(request,L"mathMode",math_mode);
    if(!recording_voice.empty())put(request,L"voice",recording_voice);
    if(semantics){
        curriculum::validate_text_semantics(*semantics);
        if(semantics->source!=text)throw std::invalid_argument("Narration semantics require their exact original source");
        JsonObject field;JsonArray spans;
        for(const auto& span:semantics->spans){
            JsonObject item;item.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(span.start)));
            item.Insert(L"end",JsonValue::CreateNumberValue(static_cast<double>(span.end)));
            put(item,L"text",span.text);put(item,L"role",curriculum::semantic_role_name(span.role));
            put(item,L"reading",curriculum::semantic_reading_name(span.reading));spans.Append(item);
        }
        field.Insert(L"spans",spans);request.Insert(L"textSemantics",field);
    }
    switch(output_data_policy){
    case NarrationOutputDataPolicy::current:break;
    case NarrationOutputDataPolicy::b42:put(request,L"outputDataPolicy","b42");break;
    case NarrationOutputDataPolicy::original:put(request,L"outputDataPolicy","original");break;
    default:throw std::invalid_argument("Invalid narration output-data policy");
    }
    switch(semantic_policy){
    case NarrationSemanticPolicy::current:break;
    case NarrationSemanticPolicy::original:
        if(semantics)put(request,L"semanticProjectionPolicy","original");break;
    default:throw std::invalid_argument("Invalid narration semantic projection policy");
    }
    if(previous_prose_inference)request.Insert(L"previousProseInference",JsonValue::CreateBooleanValue(true));
    const auto reply=impl_->call(request,stop);std::vector<NarrationSegment> result;
    for(const auto& item:reply.GetNamedArray(L"segments")){const auto object=item.GetObject();NarrationSegment segment{string(object,L"text"),string(object,L"preparedText"),index(object,L"sourceStart"),index(object,L"sourceEnd")};
        segment.spoken=string(object,L"spokenText");
        segment.recording_key=string(object,L"key");
        if(object.HasKey(L"key")&&!digest_string(segment.recording_key))throw std::runtime_error("Invalid narration recording key");
        const auto ranges=[&](const wchar_t* key,std::vector<SpeechRange>& target){if(!object.HasKey(key))return;
            for(const auto& value:object.GetNamedArray(key)){const auto range=value.GetObject();target.push_back({index(range,L"start"),index(range,L"end")});}};
        ranges(L"spokenRanges",segment.spoken_ranges);ranges(L"displayedRanges",segment.displayed_ranges);
        const auto spoken_size=winrt::to_hstring(segment.spoken).size(),displayed_size=winrt::to_hstring(segment.text).size();
        if(segment.spoken_ranges.size()!=segment.displayed_ranges.size()||segment.spoken_ranges.size()>4096)throw std::runtime_error("Invalid narration source projection");
        for(std::size_t i=0;i<segment.spoken_ranges.size();++i){const auto a=segment.spoken_ranges[i],b=segment.displayed_ranges[i];
            if(a.start>=a.end||a.end>spoken_size||b.start>=b.end||b.end>displayed_size)throw std::runtime_error("Invalid narration source projection");}
        result.push_back(std::move(segment));}return result;}
NarrationAudio NarrationWorker::prepare(const NarrationSegment& segment,std::string_view language,std::string_view voice,const std::filesystem::path& output,unsigned threads,std::stop_token stop,NarrationCpuClass work,bool audio_only){
    ensure_winrt_apartment();JsonObject request;put(request,L"command","prepare");put(request,L"text",segment.text);put(request,L"preparedText",segment.prepared);put(request,L"language",language);put(request,L"voice",voice);
    if(audio_only||work==NarrationCpuClass::background)request.Insert(L"audioOnly",JsonValue::CreateBooleanValue(true));
    put(request,L"cpuClass",work==NarrationCpuClass::background?"background":"current_page");
    if(!segment.cache_namespace.empty()){
        if(!digest_string(segment.cache_namespace))throw std::invalid_argument("Invalid narration cache namespace");
        put(request,L"cacheNamespace",segment.cache_namespace);
    }
    put(request,L"outputDirectory",winrt::to_string(output.native()));request.Insert(L"threads",JsonValue::CreateNumberValue(threads));
    JsonObject reply;
    for(;;)try{
        const auto budget=impl_->storage_budget.load();
        if(budget!=std::numeric_limits<std::uint64_t>::max())
            request.Insert(L"audioBudgetBytes",JsonValue::CreateNumberValue(static_cast<double>(budget)));
        else if(request.HasKey(L"audioBudgetBytes"))request.Remove(L"audioBudgetBytes");
        reply=impl_->call(request,stop,nullptr,false,work,true);break;}
        catch(const NarrationPreempted&){if(work==NarrationCpuClass::background)throw;}
    NarrationAudio audio;audio.key=string(reply,L"key");audio.source=string(reply,L"sourceText");audio.spoken=string(reply,L"spokenText");audio.provider=string(reply,L"provider");audio.wav=std::filesystem::path(reply.GetNamedString(L"wav").c_str());audio.duration_ms=reply.GetNamedNumber(L"durationMs");audio.cache_hit=reply.GetNamedBoolean(L"cacheHit",false);
    for(const auto& item:reply.GetNamedArray(L"timings")){const auto timing=item.GetObject();audio.timings.push_back({index(timing,L"start"),index(timing,L"end"),timing.GetNamedNumber(L"startMs"),timing.GetNamedNumber(L"endMs")});}
    audio.cache_namespace=string(reply,L"cacheNamespace");audio.prepared=segment.prepared;
    audio.synthesis_threads=static_cast<unsigned>(index(reply,L"synthesisThreads"));
    audio.cpu_class=work==NarrationCpuClass::background?"background":"current_page";
    if(reply.HasKey(L"cpuClass")&&string(reply,L"cpuClass")!=audio.cpu_class)throw std::runtime_error("Kokoro returned another narration CPU class");
    if(audio.cache_namespace!=segment.cache_namespace)throw std::runtime_error("Kokoro returned another page revision");
    if(!std::filesystem::is_regular_file(audio.wav)||audio.duration_ms<=0)throw std::runtime_error("Kokoro no produjo una grabación válida.");return audio;
}
NarrationAudio NarrationWorker::prepare_system(const NarrationSegment& segment,std::string_view language,std::string_view voice,
    const std::filesystem::path& output,std::stop_token stop){
    ensure_winrt_apartment();BackgroundReservation background(impl_->priority,stop);std::stop_source operation;
    std::stop_callback cancelled(stop,[&]{operation.request_stop();});
    std::stop_callback preempted(background.stop,[&]{operation.request_stop();});
    try{return impl_->system_speech.prepare(segment,language,voice,output,operation.get_token(),impl_->storage_budget.load()).audio;}
    catch(...){if(background.stop.stop_requested()&&!stop.stop_requested())throw NarrationPreempted{};throw;}
}
std::string NarrationWorker::curriculum_plan(std::string_view raw_lesson,std::size_t page,std::string_view language,std::string_view voice,std::string_view ui_language,std::stop_token stop,std::string_view reading_context,std::string_view content_language){
    if(raw_lesson.size()>32u*1024u*1024u)throw std::length_error("Lesson file too large for narration planning");
    if(reading_context!="auto"&&reading_context!="cpp")throw std::invalid_argument("Invalid curriculum narration reading context");
    ensure_winrt_apartment();JsonObject request;put(request,L"command","curriculum_plan");put(request,L"lessonJson",raw_lesson);
    put(request,L"readingContext",reading_context);
    // Catalog callers use the asset's language for both content and speech;
    // retain a distinct source language when planning another selected voice.
    put(request,L"contentLanguage",content_language.empty()?language:content_language);
    request.Insert(L"page",JsonValue::CreateNumberValue(static_cast<double>(page)));put(request,L"language",language);put(request,L"voice",voice);put(request,L"uiLanguage",ui_language);
    std::string reply;(void)impl_->call(request,stop,&reply,false,NarrationCpuClass::background);return reply;
}
NarrationPagePlan NarrationWorker::page_plan(std::string_view raw_lesson,std::size_t page,std::string_view language,
    std::string_view voice,std::string_view ui_language,std::stop_token stop,std::string_view reading_context,
    std::string_view content_language,NarrationCpuClass work){
    if(raw_lesson.size()>32u*1024u*1024u)throw std::length_error("Lesson file too large for narration planning");
    if(reading_context!="auto"&&reading_context!="cpp")throw std::invalid_argument("Invalid curriculum narration reading context");
    ensure_winrt_apartment();JsonObject request;put(request,L"command","curriculum_plan");put(request,L"lessonJson",raw_lesson);
    put(request,L"readingContext",reading_context);put(request,L"contentLanguage",content_language.empty()?language:content_language);
    request.Insert(L"page",JsonValue::CreateNumberValue(static_cast<double>(page)));put(request,L"language",language);
    put(request,L"voice",voice);put(request,L"uiLanguage",ui_language);
    const auto reply=impl_->call(request,stop,nullptr,false,work);NarrationPagePlan result;
    result.page_id=string(reply,L"pageId");result.revision=string(reply,L"revision");
    if(result.page_id.empty()||!digest_string(result.revision))throw std::runtime_error("Narration worker returned no authenticated page revision");
    for(const auto& item:reply.GetNamedArray(L"segments")){const auto value=item.GetObject();NarrationSegment segment;
        segment.text=string(value,L"text");segment.prepared=string(value,L"preparedText");segment.spoken=string(value,L"spokenText");
        segment.recording_key=string(value,L"key");
        if(value.HasKey(L"key")&&!digest_string(segment.recording_key))throw std::runtime_error("Invalid canonical narration recording key");
        if(segment.spoken.empty())segment.spoken=segment.text;
        if(segment.text.empty()||segment.prepared.empty())throw std::runtime_error("Invalid canonical page narration segment");
        result.segments.push_back(std::move(segment));}
    return result;
}
std::string NarrationWorker::identity(std::string_view page,std::string_view language,std::string_view voice,const std::vector<NarrationSegment>& segments,std::stop_token stop,std::string_view engine){
    ensure_winrt_apartment();JsonObject request;put(request,L"command","identity");put(request,L"page",page);put(request,L"language",language);put(request,L"voice",voice);put(request,L"engine",engine);JsonArray texts;
    for(const auto& segment:segments)texts.Append(JsonValue::CreateStringValue(winrt::to_hstring(segment.prepared)));request.Insert(L"texts",texts);return string(impl_->call(request,stop),L"key");
}
std::string NarrationWorker::describe_math(std::string_view raw_block,std::string_view language,std::stop_token stop){
    ensure_winrt_apartment();JsonObject request;put(request,L"command","describe_math");put(request,L"blockJson",raw_block);put(request,L"language",language);
    std::string reply;(void)impl_->call(request,stop,&reply);return reply;
}
std::string NarrationWorker::validate_math(std::string_view raw_block,std::string_view raw_responses,std::string_view language,std::stop_token stop){
    ensure_winrt_apartment();JsonObject request;put(request,L"command","validate_math");put(request,L"blockJson",raw_block);put(request,L"responsesJson",raw_responses);put(request,L"language",language);
    std::string reply;(void)impl_->call(request,stop,&reply);return reply;
}
std::string NarrationWorker::math_editor(std::string_view request_json,std::stop_token stop){
    ensure_winrt_apartment();auto request=JsonObject::Parse(winrt::to_hstring(request_json));put(request,L"command","math_editor");
    std::string reply;(void)impl_->call(request,stop,&reply,true);return reply;
}
std::string NarrationWorker::math_calculate(std::string_view request_json,std::stop_token stop){
    auto heavy=heavy_work_lease(stop);
    ensure_winrt_apartment();auto request=JsonObject::Parse(winrt::to_hstring(request_json));put(request,L"command","math_calculate");
    std::string reply;(void)impl_->call(request,stop,&reply,true);return reply;
}
std::string NarrationWorker::math_keyboard(std::string_view request_json,std::stop_token stop){
    ensure_winrt_apartment();auto request=JsonObject::Parse(winrt::to_hstring(request_json));put(request,L"command","math_keyboard");
    std::string reply;(void)impl_->call(request,stop,&reply,true);return reply;
}
}
