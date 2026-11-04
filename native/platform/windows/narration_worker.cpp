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
NarrationPriorityLease::~NarrationPriorityLease(){if(release_)release_();}
NarrationPriorityLease::NarrationPriorityLease(NarrationPriorityLease&& other) noexcept:release_(std::exchange(other.release_,{})){}
NarrationPriorityLease& NarrationPriorityLease::operator=(NarrationPriorityLease&& other) noexcept {
    if(this!=&other){if(release_)release_();release_=std::exchange(other.release_,{});}return *this;
}
struct NarrationWorker::Impl {
    std::filesystem::path runtime;Handle process,job,input,output,error;
    std::unique_ptr<NarrationCpuPolicy> cpu;
    SystemSpeechProcess system_speech;
    std::shared_ptr<PriorityState> priority=std::make_shared<PriorityState>();
    // Inference still has its original serialized lane. The single process's
    // lightweight editor lane can exchange replies without waiting for ONNX.
    std::mutex request_mutex,write_mutex,start_mutex,response_mutex,shutdown_mutex;
    std::atomic<bool> closed{false};
    std::atomic<std::uint64_t> serial{0};std::string buffered,failure;
    std::atomic<std::uint64_t> storage_budget{std::numeric_limits<std::uint64_t>::max()};
    struct Pending { std::string command,reply; bool lightweight=false; };
    std::map<std::string,std::shared_ptr<Pending>> pending;
    std::condition_variable responses;
    std::jthread pump;
    explicit Impl(std::filesystem::path file):runtime(std::move(file)){}
    ~Impl(){shutdown();if(pump.joinable())pump.join();}
    void shutdown() noexcept {
        const std::lock_guard closing(shutdown_mutex);
        if(closed.exchange(true))return;
        system_speech.shutdown();
        // Do not request operation stop here: its synchronous callbacks may be
        // writing to a child that has stopped reading. Close the owned process
        // first, without taking the writer/request locks or sending another RPC.
        {const std::lock_guard lock(priority->mutex);priority->closed=true;}
        priority->changed.notify_all();
        {const std::lock_guard lock(response_mutex);failure="El servicio de narración está cerrado.";}
        responses.notify_all();
        {const std::lock_guard starting(start_mutex);
            if(process.value){
                if(!job.value||!TerminateJobObject(job.value,0))TerminateProcess(process.value,0);
                (void)WaitForSingleObject(process.value,1000);
            }
            // Handles and CPU policy stay alive until callers have unwound and
            // the reply pump has joined; closing a handle during ReadFile or a
            // CPU scope restoration would introduce another lifetime race.
            if(pump.joinable())pump.request_stop();
        }
        responses.notify_all();
    }
    void start(){const std::lock_guard starting(start_mutex);if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");if(process.value){if(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)return;throw std::runtime_error("El proceso de Kokoro terminó. Cierra y vuelve a abrir la lectura.");}
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
        ResumeThread(thread.value);
        pump=std::jthread([this](std::stop_token stop){read_replies(stop);});
    }
    void send(const std::string& source){if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");const std::lock_guard lock(write_mutex);if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");const auto line=source+'\n';std::size_t offset=0;while(offset<line.size()){DWORD written=0;if(!WriteFile(input.value,line.data()+offset,static_cast<DWORD>(line.size()-offset),&written,nullptr)||!written)throw std::runtime_error("Kokoro cerró el canal de entrada.");offset+=written;}}
    void read_replies(std::stop_token stop) {
        std::stop_callback wake(stop,[&]{responses.notify_all();});
        try {ensure_winrt_apartment();while(!stop.stop_requested()){
            const auto end=buffered.find('\n');
            if(end!=std::string::npos){const auto line=buffered.substr(0,end);buffered.erase(0,end+1);if(line.empty())continue;
                JsonObject reply;try{reply=JsonObject::Parse(winrt::to_hstring(line));}catch(...){throw std::runtime_error("Kokoro devolvió una respuesta no válida.");}
                {const std::lock_guard lock(response_mutex);const auto found=pending.find(string(reply,L"id"));
                    if(found!=pending.end()&&found->second->command==string(reply,L"command"))found->second->reply=line;}
                responses.notify_all();continue;
            }
            DWORD available=0;if(!PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr))throw std::runtime_error("Kokoro terminó antes de responder. Consulta el diagnóstico de narración.");
            if(available){char bytes[8192];DWORD read=0;if(!ReadFile(output.value,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr)||!read)throw std::runtime_error("Kokoro cerró su salida.");buffered.append(bytes,read);if(buffered.size()>16*1024*1024)throw std::runtime_error("Respuesta de Kokoro demasiado grande.");}
            else{if(WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)throw std::runtime_error("Kokoro terminó antes de responder.");
                std::unique_lock lock(response_mutex);
                if(pending.empty())responses.wait_for(lock,std::chrono::seconds(1),[&]{return stop.stop_requested()||!pending.empty();});
                else {const bool interactive=std::ranges::any_of(pending,[](const auto& item){return item.second->lightweight;});
                    responses.wait_for(lock,std::chrono::milliseconds(interactive?2:15));}
            }
        }}catch(const std::exception& error){const std::lock_guard lock(response_mutex);failure=error.what();responses.notify_all();}
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
        ensure_winrt_apartment();if(stop.stop_requested())cancelled();start();
        std::unique_ptr<NarrationCpuScope> cpu_scope;
        if(inference||background)cpu_scope=std::make_unique<NarrationCpuScope>(*cpu,work);
        else cpu->apply(NarrationCpuClass::current_page);
        if(inference) {
            const auto requested=static_cast<unsigned>(request.GetNamedNumber(L"threads",0));
            const auto capacity=cpu->inference_threads(work);
            request.Insert(L"threads",JsonValue::CreateNumberValue(requested?std::clamp(requested,1u,capacity):capacity));
        }
        const auto id=std::to_string(++serial);put(request,L"id",id);
        const auto response=std::make_shared<Pending>();response->command=string(request,L"command");response->lightweight=lightweight;
        {const std::lock_guard lock(response_mutex);pending.emplace(id,response);}
        struct Remove {Impl& owner;const std::string& id;~Remove(){const std::lock_guard lock(owner.response_mutex);owner.pending.erase(id);}} remove{*this,id};
        send(winrt::to_string(request.Stringify()));
        responses.notify_all();
        std::stop_callback cancel(stop,[&]{
            if(closed.load())return;
            try{ensure_winrt_apartment();JsonObject value;put(value,L"command","cancel");put(value,L"id",id);send(winrt::to_string(value.Stringify()));}catch(...) {}
            // An interactive math request may leave immediately; its late reply
            // is discarded by ID. Inference retains the serialized wait until
            // ONNX acknowledges completion/cancellation on its existing lane.
            if(lightweight){const std::lock_guard lock(response_mutex);responses.notify_all();}
        });
        std::string line;
        {std::unique_lock lock(response_mutex);
            if(!responses.wait_for(lock,std::chrono::minutes(5),[&]{return closed.load()||(lightweight&&stop.stop_requested())||!response->reply.empty()||!failure.empty();})){
                TerminateJobObject(job.value,1);throw std::runtime_error("Kokoro excedió el tiempo de preparación.");}
            if(closed.load())throw std::runtime_error("El servicio de narración está cerrado.");
            if(lightweight&&stop.stop_requested())cancelled();
            if(response->reply.empty())throw std::runtime_error(failure);line=response->reply;
        }
        if(cpu_scope)cpu_scope->finish();
        if(stop.stop_requested())cancelled();
        const auto reply=JsonObject::Parse(winrt::to_hstring(line));
        if(reply.HasKey(L"error"))throw std::runtime_error("Kokoro: "+string(reply,L"error"));
        if(original_reply)*original_reply=line;return reply;
    }
};
NarrationWorker::NarrationWorker(std::filesystem::path runtime):impl_(std::make_unique<Impl>(std::move(runtime))){}
NarrationWorker::~NarrationWorker()=default;
void NarrationWorker::shutdown() noexcept {impl_->shutdown();}
void NarrationWorker::set_storage_budget(std::uint64_t bytes){impl_->storage_budget.store(bytes);}
NarrationPriorityLease NarrationWorker::current_page_lease(std::stop_token stop){return NarrationPriorityLease(take_current(impl_->priority,stop));}
NarrationPriorityLease NarrationWorker::heavy_work_lease(std::stop_token stop){return NarrationPriorityLease(take_heavy(impl_->priority,stop));}
std::optional<NarrationPriorityLease> NarrationWorker::try_idle_maintenance_lease(){
    const auto state=impl_->priority;const std::lock_guard lock(state->mutex);
    if(state->closed||state->current||state->heavy||state->background||state->inference)return {};
    ++state->heavy;
    return NarrationPriorityLease([state]{const std::lock_guard release(state->mutex);--state->heavy;state->changed.notify_all();});
}
std::vector<NarrationSegment> NarrationWorker::plan(std::string_view text,std::string_view language,std::stop_token stop,
    std::string_view reading_context,std::string_view math_mode){
    if(reading_context!="auto"&&reading_context!="cpp"&&reading_context!="cpp_previous_parentheses")throw std::invalid_argument("Invalid narration reading context");
    if(math_mode!="auto"&&math_mode!="text"&&math_mode!="inline"&&math_mode!="block")throw std::invalid_argument("Invalid narration mathematical mode");
    ensure_winrt_apartment();JsonObject request;put(request,L"command","plan");put(request,L"text",text);put(request,L"language",language);
    put(request,L"readingContext",reading_context);put(request,L"mathMode",math_mode);
    const auto reply=impl_->call(request,stop);std::vector<NarrationSegment> result;
    for(const auto& item:reply.GetNamedArray(L"segments")){const auto object=item.GetObject();NarrationSegment segment{string(object,L"text"),string(object,L"preparedText"),index(object,L"sourceStart"),index(object,L"sourceEnd")};
        segment.spoken=string(object,L"spokenText");
        const auto ranges=[&](const wchar_t* key,std::vector<SpeechRange>& target){if(!object.HasKey(key))return;
            for(const auto& value:object.GetNamedArray(key)){const auto range=value.GetObject();target.push_back({index(range,L"start"),index(range,L"end")});}};
        ranges(L"spokenRanges",segment.spoken_ranges);ranges(L"displayedRanges",segment.displayed_ranges);
        const auto spoken_size=winrt::to_hstring(segment.spoken).size(),displayed_size=winrt::to_hstring(segment.text).size();
        if(segment.spoken_ranges.size()!=segment.displayed_ranges.size()||segment.spoken_ranges.size()>4096)throw std::runtime_error("Invalid narration source projection");
        for(std::size_t i=0;i<segment.spoken_ranges.size();++i){const auto a=segment.spoken_ranges[i],b=segment.displayed_ranges[i];
            if(a.start>=a.end||a.end>spoken_size||b.start>=b.end||b.end>displayed_size)throw std::runtime_error("Invalid narration source projection");}
        result.push_back(std::move(segment));}return result;}
NarrationAudio NarrationWorker::prepare(const NarrationSegment& segment,std::string_view language,std::string_view voice,const std::filesystem::path& output,unsigned threads,std::stop_token stop,NarrationCpuClass work){
    ensure_winrt_apartment();JsonObject request;put(request,L"command","prepare");put(request,L"text",segment.text);put(request,L"preparedText",segment.prepared);put(request,L"language",language);put(request,L"voice",voice);
    if(work==NarrationCpuClass::background)request.Insert(L"audioOnly",JsonValue::CreateBooleanValue(true));
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
std::string NarrationWorker::curriculum_plan(std::string_view raw_lesson,std::size_t page,std::string_view language,std::string_view voice,std::string_view ui_language,std::stop_token stop,std::string_view reading_context){
    if(reading_context!="auto"&&reading_context!="cpp")throw std::invalid_argument("Invalid curriculum narration reading context");
    ensure_winrt_apartment();JsonObject request;put(request,L"command","curriculum_plan");put(request,L"lessonJson",raw_lesson);
    put(request,L"readingContext",reading_context);
    request.Insert(L"page",JsonValue::CreateNumberValue(static_cast<double>(page)));put(request,L"language",language);put(request,L"voice",voice);put(request,L"uiLanguage",ui_language);
    std::string reply;(void)impl_->call(request,stop,&reply,false,NarrationCpuClass::background);return reply;
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
