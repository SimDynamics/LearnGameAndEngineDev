#include "platform/windows/system_speech_process.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace lgaed::windows {
namespace {
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
void put(JsonObject& json,const wchar_t* key,std::string_view value){json.Insert(key,JsonValue::CreateStringValue(winrt::to_hstring(value)));}
std::string string(const JsonObject& json,const wchar_t* key){return winrt::to_string(json.GetNamedString(key));}
std::size_t index(const JsonObject& json,const wchar_t* key){
    const auto value=json.GetNamedNumber(key);require(std::isfinite(value)&&value>=0&&value<=20000000&&std::trunc(value)==value,"Invalid system speech source offset");
    return static_cast<std::size_t>(value);
}
std::wstring quote(std::wstring_view value){
    std::wstring result=L"\"";std::size_t slashes=0;
    for(const auto c:value){if(c==L'\\'){++slashes;continue;}
        if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;}
        else{result.append(slashes,L'\\');result+=c;}slashes=0;}
    result.append(slashes*2,L'\\');return result+L'\"';
}
std::filesystem::path executable(){
    std::wstring path(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    require(size&&size<path.size(),"Cannot locate the native speech helper");path.resize(size);
    const auto directory=std::filesystem::path(path).parent_path();
    const auto installed=directory/L"lgaed_system_speech_host.exe";
    if(std::filesystem::is_regular_file(state::native_io_path(installed)))return installed;
    // Native tests live at the build root; the application/helper share bin/.
    const auto built=directory/L"bin/lgaed_system_speech_host.exe";
    return std::filesystem::is_regular_file(state::native_io_path(built))?built:installed;
}
std::string token(){
    GUID guid{};require(SUCCEEDED(CoCreateGuid(&guid)),"Cannot create a private speech request identity");
    wchar_t text[40]{};require(StringFromGUID2(guid,text,40)>0,"Cannot format a private speech request identity");
    std::string result;
    for(const auto c:std::wstring_view(text))if(c!=L'{'&&c!=L'}'&&c!=L'-')result+=static_cast<char>(c>=L'A'&&c<=L'F'?c-L'A'+L'a':c);
    require(result.size()==32,"Invalid private speech request identity");return result;
}
void write_new(const std::filesystem::path& path,std::string_view bytes){
    state::require_safe_path(path);Handle file;
    file.value=CreateFileW(state::native_io_path(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(file.value!=INVALID_HANDLE_VALUE,"Cannot create the private system speech message");
    require(bytes.size()<=4*1024*1024,"System speech message is too large");
    DWORD written=0;require(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value),"Cannot write the private system speech message");
}
JsonObject read_json(const std::filesystem::path& path){
    const auto bytes=state::read_bytes(path,4*1024*1024);
    return JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
}
struct RequestFiles {
    std::filesystem::path root,request,reply;
    explicit RequestFiles(const std::filesystem::path& cache){
        require(cache.is_absolute(),"System speech cache must be absolute");state::require_safe_path(cache);
        const auto parent=cache/L"system_speech_ipc";state::require_safe_path(parent);
        std::filesystem::create_directories(state::native_io_path(parent));
        root=parent/(L"request-"+std::wstring(winrt::to_hstring(token()).c_str()));state::require_safe_path(root);
        require(std::filesystem::create_directory(state::native_io_path(root)),"Cannot reserve a private system speech request");
        request=root/L"request.json";reply=root/L"reply.json";
    }
    ~RequestFiles(){
        // Only the files this request owns. An unexpected extra file prevents
        // removal of the directory and is preserved; no recursive cleanup.
        try{state::require_safe_path(root);std::error_code error;
            for(const auto* name:{L"request.json",L"reply.json",L"reply.pending"}){
                const auto path=root/name;state::require_safe_path(path);std::filesystem::remove(state::native_io_path(path),error);}
            std::filesystem::remove(state::native_io_path(root),error);
        }catch(...){}
    }
};
JsonArray ranges(const std::vector<SpeechRange>& source){
    JsonArray result;for(const auto& range:source){JsonObject row;
        row.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(range.start)));
        row.Insert(L"end",JsonValue::CreateNumberValue(static_cast<double>(range.end)));result.Append(row);}return result;
}
NarrationSegment segment(const JsonObject& json){
    NarrationSegment result;result.text=string(json,L"text");result.prepared=string(json,L"preparedText");result.spoken=string(json,L"spokenText");
    for(const auto& [key,target]:{std::pair{L"spokenRanges",&result.spoken_ranges},std::pair{L"displayedRanges",&result.displayed_ranges}})
        for(const auto& value:json.GetNamedArray(key)){const auto row=value.GetObject();target->push_back({index(row,L"start"),index(row,L"end")});}
    return result;
}
JsonObject encoded_audio(const NarrationAudio& audio){
    JsonObject result;put(result,L"key",audio.key);put(result,L"source",audio.source);put(result,L"spoken",audio.spoken);put(result,L"provider",audio.provider);
    result.Insert(L"wav",JsonValue::CreateStringValue(audio.wav.native()));result.Insert(L"durationMs",JsonValue::CreateNumberValue(audio.duration_ms));
    result.Insert(L"cacheHit",JsonValue::CreateBooleanValue(audio.cache_hit));JsonArray timings;
    for(const auto& timing:audio.timings){JsonObject row;row.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(timing.start)));
        row.Insert(L"end",JsonValue::CreateNumberValue(static_cast<double>(timing.end)));row.Insert(L"startMs",JsonValue::CreateNumberValue(timing.start_ms));
        row.Insert(L"endMs",JsonValue::CreateNumberValue(timing.end_ms));timings.Append(row);}result.Insert(L"timings",timings);return result;
}
NarrationAudio decoded_audio(const JsonObject& json,const std::filesystem::path& cache){
    NarrationAudio result;result.key=string(json,L"key");result.source=string(json,L"source");result.spoken=string(json,L"spoken");result.provider=string(json,L"provider");
    result.wav=std::filesystem::path(json.GetNamedString(L"wav").c_str());result.duration_ms=json.GetNamedNumber(L"durationMs");result.cache_hit=json.GetNamedBoolean(L"cacheHit");
    state::require_safe_path(result.wav);
    const auto relative=result.wav.lexically_normal().lexically_relative((cache/L"windows_tts").lexically_normal());
    require(!relative.empty()&&!relative.is_absolute()&&std::ranges::none_of(relative,[](const auto& part){return part==L"..";}),"System speech reply escaped its private cache");
    require(result.provider=="windows-sapi5"&&!result.key.empty()&&std::isfinite(result.duration_ms)&&result.duration_ms>0&&std::filesystem::is_regular_file(state::native_io_path(result.wav)),"The native speech helper did not return a valid recording");
    for(const auto& value:json.GetNamedArray(L"timings")){const auto row=value.GetObject();result.timings.push_back({index(row,L"start"),index(row,L"end"),row.GetNamedNumber(L"startMs"),row.GetNamedNumber(L"endMs")});}
    return result;
}
unsigned effective_processors(){
    // NULL queries this child's immediate private job, including nested jobs:
    // https://learn.microsoft.com/windows/win32/api/jobapi2/nf-jobapi2-queryinformationjobobject
    DWORD bytes=0;(void)QueryInformationJobObject(nullptr,JobObjectGroupInformationEx,nullptr,0,&bytes);
    unsigned job_processors=0;
    if(bytes&&bytes<=sizeof(GROUP_AFFINITY)*256&&bytes%sizeof(GROUP_AFFINITY)==0){
        std::vector<GROUP_AFFINITY> groups(bytes/sizeof(GROUP_AFFINITY));
        if(QueryInformationJobObject(nullptr,JobObjectGroupInformationEx,groups.data(),bytes,&bytes))
            for(const auto& group:groups)job_processors+=std::popcount(static_cast<unsigned long long>(group.Mask));
    }
    DWORD_PTR process_mask=0,system_mask=0;
    const auto process_processors=GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask)&&process_mask?
        static_cast<unsigned>(std::popcount(static_cast<unsigned long long>(process_mask))):0u;
    const auto count=job_processors&&process_processors?std::min(job_processors,process_processors):job_processors?job_processors:process_processors;
    require(count&&count<=2,"The native speech helper has no verified two-processor hard affinity");return count;
}
}

struct SystemSpeechProcess::Impl {
    struct Active {Handle process,job,cancel;};
    std::filesystem::path host;
    std::atomic<bool> closed=false;
    std::timed_mutex lane;std::mutex mutex;std::shared_ptr<Active> active;
    explicit Impl(std::filesystem::path value):host(value.empty()?executable():std::move(value)){}
    void shutdown() noexcept{
        closed.store(true);std::shared_ptr<Active> child;
        {std::lock_guard lock(mutex);child=active;}
        if(child){SetEvent(child->cancel.value);if(child->job.value)TerminateJobObject(child->job.value,0);}
    }
};
SystemSpeechProcess::SystemSpeechProcess(std::filesystem::path host):impl_(std::make_unique<Impl>(std::move(host))){}
SystemSpeechProcess::~SystemSpeechProcess(){shutdown();}
void SystemSpeechProcess::shutdown() noexcept{impl_->shutdown();}
SystemSpeechProcessResult SystemSpeechProcess::prepare(const NarrationSegment& source,std::string_view language,std::string_view voice,
    const std::filesystem::path& cache,std::stop_token stop,std::uint64_t maximum_audio_bytes){
    ensure_winrt_apartment();std::unique_lock lane(impl_->lane,std::defer_lock);
    while(!lane.try_lock_for(20ms))require(!impl_->closed.load()&&!stop.stop_requested(),"System speech preparation cancelled");
    require(!impl_->closed.load()&&!stop.stop_requested(),"System speech preparation cancelled");
    require(impl_->host.is_absolute()&&std::filesystem::is_regular_file(state::native_io_path(impl_->host)),"The native Windows speech helper is not installed");
    RequestFiles files(cache);JsonObject request;put(request,L"text",source.text);put(request,L"preparedText",source.prepared);put(request,L"spokenText",source.spoken);
    put(request,L"language",language);put(request,L"voice",voice);request.Insert(L"outputDirectory",JsonValue::CreateStringValue(cache.native()));
    // Decimal text preserves the full configured integer; no JSON double can
    // round the storage ceiling. The provider applies it only to new audio.
    put(request,L"audioBudgetBytes",std::to_string(maximum_audio_bytes));
    request.Insert(L"spokenRanges",ranges(source.spoken_ranges));request.Insert(L"displayedRanges",ranges(source.displayed_ranges));
    write_new(files.request,winrt::to_string(request.Stringify()));
    auto child=std::make_shared<Impl::Active>();SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    child->cancel.value=CreateEventW(&security,TRUE,FALSE,nullptr);require(child->cancel.value!=nullptr,"Cannot create a private speech cancellation event");
    child->job.value=CreateJobObjectW(nullptr,nullptr);require(child->job.value!=nullptr,"Cannot create the private Windows speech job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(child->job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Cannot contain the native speech helper");
    SIZE_T bytes=0;(void)InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> attributes(bytes);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    require(InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)!=FALSE,"Cannot prepare speech handle inheritance");
    struct AttributeCleanup {LPPROC_THREAD_ATTRIBUTE_LIST value;~AttributeCleanup(){DeleteProcThreadAttributeList(value);}} cleanup{startup.lpAttributeList};
    require(UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&child->cancel.value,sizeof(HANDLE),nullptr,nullptr)!=FALSE,"Cannot restrict speech handle inheritance");
    auto command=quote(impl_->host.native())+L" --request "+quote(files.request.native())+L" --cancel-handle "+std::to_wstring(reinterpret_cast<std::uintptr_t>(child->cancel.value));
    PROCESS_INFORMATION process{};
    require(CreateProcessW(impl_->host.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,
        nullptr,impl_->host.parent_path().c_str(),&startup.StartupInfo,&process)!=FALSE,"Cannot start the private Windows speech helper");
    child->process.value=process.hProcess;Handle thread;thread.value=process.hThread;
    if(!AssignProcessToJobObject(child->job.value,child->process.value)){
        // This is our still-suspended child and has not entered the private job.
        // Closing a job cannot contain it yet, so terminate that child explicitly.
        TerminateProcess(child->process.value,0);(void)WaitForSingleObject(child->process.value,1000);
        throw std::runtime_error("Cannot assign the native speech helper to its private job");
    }
    NarrationCpuPolicy cpu(child->process.value,child->job.value);cpu.apply(NarrationCpuClass::background);
    {std::lock_guard lock(impl_->mutex);require(!impl_->closed.load(),"System speech preparation cancelled");impl_->active=child;}
    struct Clear {Impl* owner;std::shared_ptr<Impl::Active> child;~Clear(){std::lock_guard lock(owner->mutex);if(owner->active==child)owner->active.reset();}} clear{impl_.get(),child};
    std::stop_callback cancel(stop,[child]{SetEvent(child->cancel.value);});
    require(ResumeThread(thread.value)!=static_cast<DWORD>(-1),"Cannot resume the bounded native speech helper");
    const HANDLE events[]{child->process.value,child->cancel.value};
    const auto wait=WaitForMultipleObjects(2,events,FALSE,210000);
    if(wait!=WAIT_OBJECT_0){
        SetEvent(child->cancel.value);
        if(WaitForSingleObject(child->process.value,5000)!=WAIT_OBJECT_0){TerminateJobObject(child->job.value,0);(void)WaitForSingleObject(child->process.value,1000);}
        require(wait==WAIT_OBJECT_0+1,"The native speech helper did not complete its request");
        throw std::runtime_error("System speech preparation cancelled");
    }
    require(!impl_->closed.load()&&!stop.stop_requested(),"System speech preparation cancelled");
    const auto reply=read_json(files.reply);
    if(reply.HasKey(L"error"))throw std::runtime_error(string(reply,L"error"));
    require(reply.GetNamedNumber(L"processId")==process.dwProcessId,"The native speech reply has a different process identity");
    const auto processors=index(reply,L"processors");require(processors&&processors<=2,"The native speech reply did not verify its hard CPU budget");
    return {decoded_audio(reply.GetNamedObject(L"audio"),cache),static_cast<unsigned>(processors),process.dwProcessId};
}

int run_system_speech_host(std::span<const std::wstring_view> args) noexcept{
    std::filesystem::path request_path,reply_path;
    try{
        ensure_winrt_apartment();require(args.size()==4&&args[0]==L"--request"&&args[2]==L"--cancel-handle","Invalid native speech helper arguments");
        request_path=std::filesystem::path(args[1]);require(request_path.is_absolute()&&request_path.filename()==L"request.json","Invalid native speech request location");
        const auto folder=request_path.parent_path().filename().string();
        require(folder.size()==40&&folder.starts_with("request-")&&std::ranges::all_of(std::string_view(folder).substr(8),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid native speech request owner");
        require(request_path.parent_path().parent_path().filename()==L"system_speech_ipc","Invalid native speech request root");
        state::require_safe_path(request_path);reply_path=request_path.parent_path()/L"reply.json";
        const auto handle_text=winrt::to_string(args[3]);std::uintptr_t handle=0;const auto parsed=std::from_chars(handle_text.data(),handle_text.data()+handle_text.size(),handle);
        require(parsed.ec==std::errc{}&&parsed.ptr==handle_text.data()+handle_text.size()&&handle,"Invalid inherited speech cancellation event");
        const auto cancellation=reinterpret_cast<HANDLE>(handle);require(WaitForSingleObject(cancellation,0)!=WAIT_FAILED,"Unavailable inherited speech cancellation event");
        const auto request=read_json(request_path);const auto cache=std::filesystem::path(request.GetNamedString(L"outputDirectory").c_str());
        require(cache.is_absolute()&&cache.lexically_normal()==request_path.parent_path().parent_path().parent_path().lexically_normal(),"Native speech output does not belong to its request owner");
        std::uint64_t budget=0;const auto budget_text=string(request,L"audioBudgetBytes");
        const auto budget_value=std::from_chars(budget_text.data(),budget_text.data()+budget_text.size(),budget);
        require(budget_value.ec==std::errc{}&&budget_value.ptr==budget_text.data()+budget_text.size(),"Invalid system speech storage ceiling");
        const auto processors=effective_processors();std::stop_source operation;
        std::jthread cancellation_reader([&](std::stop_token stopping){while(!stopping.stop_requested()){
            if(WaitForSingleObject(cancellation,20)==WAIT_OBJECT_0){operation.request_stop();break;}}});
        if(WaitForSingleObject(cancellation,0)==WAIT_OBJECT_0)operation.request_stop();
        const auto audio=synthesize_system_speech_with_budget(segment(request),string(request,L"language"),string(request,L"voice"),cache,operation.get_token(),budget);
        JsonObject reply;reply.Insert(L"audio",encoded_audio(audio));reply.Insert(L"processors",JsonValue::CreateNumberValue(processors));
        reply.Insert(L"processId",JsonValue::CreateNumberValue(GetCurrentProcessId()));
        const auto pending=request_path.parent_path()/L"reply.pending";write_new(pending,winrt::to_string(reply.Stringify()));
        require(MoveFileExW(state::native_io_path(pending).c_str(),state::native_io_path(reply_path).c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"Cannot publish the native speech reply");return 0;
    }catch(const winrt::hresult_error& error){
        try{if(!reply_path.empty()){JsonObject reply;put(reply,L"error",winrt::to_string(error.message()));write_new(reply_path,winrt::to_string(reply.Stringify()));}}catch(...){}return 1;
    }catch(const std::exception& error){
        try{if(!reply_path.empty()){JsonObject reply;put(reply,L"error",error.what());write_new(reply_path,winrt::to_string(reply.Stringify()));}}catch(...){}return 1;
    }catch(...){return 1;}
}
}
