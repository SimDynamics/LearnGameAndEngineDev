#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace lgaed::windows {
namespace {
using namespace winrt::Windows::Data::Json;
struct Handle{HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}void close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}};
std::wstring quote(std::wstring_view text){std::wstring result=L"\"";std::size_t slashes=0;for(auto c:text){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;}else{result.append(slashes,L'\\');result+=c;}slashes=0;}result.append(slashes*2,L'\\');return result+L'\"';}
void put(JsonObject& value,std::wstring_view key,std::string_view text){value.Insert(winrt::hstring(key),JsonValue::CreateStringValue(winrt::to_hstring(text)));}
std::string string(const JsonObject& object,const wchar_t* key){return winrt::to_string(object.GetNamedString(key,L""));}
std::size_t index(const JsonObject& object,const wchar_t* key){const auto value=object.GetNamedNumber(key,0);if(value<0||value>20000000||std::trunc(value)!=value)throw std::runtime_error("Invalid narration offset");return static_cast<std::size_t>(value);}
}
struct NarrationWorker::Impl {
    std::filesystem::path runtime;Handle process,job,input,output,error;
    std::mutex request_mutex,write_mutex;std::uint64_t serial=0;std::string buffered;
    explicit Impl(std::filesystem::path file):runtime(std::move(file)){}
    ~Impl(){if(process.value){try{ensure_winrt_apartment();JsonObject command;put(command,L"id","shutdown");put(command,L"command","shutdown");send(winrt::to_string(command.Stringify()));}catch(...){}
        if(WaitForSingleObject(process.value,1000)==WAIT_TIMEOUT&&job.value)TerminateJobObject(job.value,0);}}
    void start(){if(process.value){if(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)return;throw std::runtime_error("El proceso de Kokoro terminó. Cierra y vuelve a abrir la lectura.");}
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
        ResumeThread(thread.value);
    }
    void send(const std::string& source){const std::lock_guard lock(write_mutex);const auto line=source+'\n';std::size_t offset=0;while(offset<line.size()){DWORD written=0;if(!WriteFile(input.value,line.data()+offset,static_cast<DWORD>(line.size()-offset),&written,nullptr)||!written)throw std::runtime_error("Kokoro cerró el canal de entrada.");offset+=written;}}
    JsonObject call(JsonObject request,std::stop_token stop,std::string* original_reply=nullptr){
        const std::lock_guard lock(request_mutex);ensure_winrt_apartment();if(stop.stop_requested())throw std::runtime_error("Lectura cancelada");start();
        const auto id=std::to_string(++serial);put(request,L"id",id);send(winrt::to_string(request.Stringify()));
        std::stop_callback cancel(stop,[&]{try{ensure_winrt_apartment();JsonObject value;put(value,L"command","cancel");put(value,L"id",id);send(winrt::to_string(value.Stringify()));}catch(...) {}});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
        for(;;){const auto end=buffered.find('\n');if(end!=std::string::npos){const auto line=buffered.substr(0,end);buffered.erase(0,end+1);if(line.empty())continue;
                JsonObject reply;try{reply=JsonObject::Parse(winrt::to_hstring(line));}catch(...){throw std::runtime_error("Kokoro devolvió una respuesta no válida.");}
                if(string(reply,L"id")!=id)continue;if(stop.stop_requested())throw std::runtime_error("Lectura cancelada");
                if(string(reply,L"command")!=string(request,L"command"))continue;
                if(reply.HasKey(L"error"))throw std::runtime_error("Kokoro: "+string(reply,L"error"));if(original_reply)*original_reply=line;return reply;}
            DWORD available=0;if(!PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr))throw std::runtime_error("Kokoro terminó antes de responder. Consulta el diagnóstico de narración.");
            if(available){char bytes[8192];DWORD read=0;if(!ReadFile(output.value,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr)||!read)throw std::runtime_error("Kokoro cerró su salida.");buffered.append(bytes,read);if(buffered.size()>16*1024*1024)throw std::runtime_error("Respuesta de Kokoro demasiado grande.");}
            else{if(WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)throw std::runtime_error("Kokoro terminó antes de responder.");if(std::chrono::steady_clock::now()>deadline){TerminateJobObject(job.value,1);throw std::runtime_error("Kokoro excedió el tiempo de preparación.");}std::this_thread::sleep_for(std::chrono::milliseconds(15));}
        }
    }
};
NarrationWorker::NarrationWorker(std::filesystem::path runtime):impl_(std::make_unique<Impl>(std::move(runtime))){}
NarrationWorker::~NarrationWorker()=default;
std::vector<NarrationSegment> NarrationWorker::plan(std::string_view text,std::string_view language,std::stop_token stop){ensure_winrt_apartment();JsonObject request;put(request,L"command","plan");put(request,L"text",text);put(request,L"language",language);const auto reply=impl_->call(request,stop);std::vector<NarrationSegment> result;
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
NarrationAudio NarrationWorker::prepare(const NarrationSegment& segment,std::string_view language,std::string_view voice,const std::filesystem::path& output,unsigned threads,std::stop_token stop){
    ensure_winrt_apartment();JsonObject request;put(request,L"command","prepare");put(request,L"text",segment.text);put(request,L"preparedText",segment.prepared);put(request,L"language",language);put(request,L"voice",voice);
    put(request,L"outputDirectory",winrt::to_string(output.native()));request.Insert(L"threads",JsonValue::CreateNumberValue(std::clamp(threads,1u,64u)));const auto reply=impl_->call(request,stop);
    NarrationAudio audio;audio.key=string(reply,L"key");audio.source=string(reply,L"sourceText");audio.spoken=string(reply,L"spokenText");audio.provider=string(reply,L"provider");audio.wav=std::filesystem::path(reply.GetNamedString(L"wav").c_str());audio.duration_ms=reply.GetNamedNumber(L"durationMs");audio.cache_hit=reply.GetNamedBoolean(L"cacheHit",false);
    for(const auto& item:reply.GetNamedArray(L"timings")){const auto timing=item.GetObject();audio.timings.push_back({index(timing,L"start"),index(timing,L"end"),timing.GetNamedNumber(L"startMs"),timing.GetNamedNumber(L"endMs")});}
    if(!std::filesystem::is_regular_file(audio.wav)||audio.duration_ms<=0)throw std::runtime_error("Kokoro no produjo una grabación válida.");return audio;
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
}
