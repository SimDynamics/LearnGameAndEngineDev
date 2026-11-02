#include "platform/windows/system_speech_process.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace lgaed;
using namespace lgaed::windows;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate> void eventually(Predicate value,const char* message){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<end){if(value())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    throw std::runtime_error(message);
}
void non_silent(const std::filesystem::path& path){
    const auto bytes=state::read_bytes(path);check(bytes.size()>44,"SAPI returned an empty recording");
    const auto word=[&](std::size_t i){return std::uint32_t(bytes[i])|(std::uint32_t(bytes[i+1])<<8)|(std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+3])<<24);};
    bool sound=false,data=false;
    for(std::size_t at=12;at+8<=bytes.size();){const auto size=word(at+4);check(size<=bytes.size()-at-8,"Incomplete real SAPI WAV chunk");
        if(std::string_view(reinterpret_cast<const char*>(bytes.data()+at),4)=="data"){
            data=true;for(std::size_t i=at+8;i+1<at+8+size;i+=2)if(bytes[i]||bytes[i+1])sound=true;}
        at+=8+size+(size&1);}
    check(data&&sound,"The real SAPI PCM is silent");
}
bool pending(const std::filesystem::path& root){
    if(!std::filesystem::is_directory(root/L"windows_tts"))return false;
    for(const auto& entry:std::filesystem::directory_iterator(root/L"windows_tts"))
        if(entry.is_directory()&&entry.path().extension()==L".pending")return true;
    return false;
}
}
int main(){try{
    ensure_winrt_apartment();
    const auto instance=L"system-background-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto environment=resolve_windows_environment({ProfileId::Codex,instance,true});initialize_profile_directories(environment.paths);
    auto voices=system_voices();const auto chosen=std::ranges::find_if(voices,[](const auto& voice){return system_voice_matches(voice.language,"en-US");});
    check(chosen!=voices.end(),"A real installed American system voice is required for native speech acceptance");
    const auto voice=*chosen;
    DWORD_PTR before=0,system=0;const auto affinity_before=GetProcessAffinityMask(GetCurrentProcess(),&before,&system);
    const auto source=std::string("The blue circle moves across this small test.");
    NarrationSegment sentence;sentence.text=source;sentence.prepared=source;sentence.spoken=source;
    SystemSpeechProcess process;
    const auto first=process.prepare(sentence,"en-US",voice.id,environment.paths.cache);
    check(first.process_id!=GetCurrentProcessId()&&first.processors>0&&first.processors<=2,"SAPI did not run in its own bounded native child");
    check(!first.audio.cache_hit&&first.audio.provider=="windows-sapi5"&&first.audio.timings.size()>3,"The native child did not synthesize actual system speech and word boundaries");
    non_silent(first.audio.wav);
    const auto wav=state::read_bytes(first.audio.wav);const auto stamp=std::filesystem::last_write_time(first.audio.wav);
    const auto meta_bytes=state::read_bytes(first.audio.wav.parent_path()/L"timings.json");
    const auto metadata=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(meta_bytes.data()),meta_bytes.size())));
    check(winrt::to_string(metadata.GetNamedString(L"voiceId"))==voice.id&&metadata.GetNamedString(L"provider")==L"windows-sapi5","SAPI metadata lost the selected real voice token/provider");
    const auto again=process.prepare(sentence,"en-US",voice.id,environment.paths.cache,{},0);
    check(again.audio.cache_hit&&again.audio.key==first.audio.key&&again.audio.wav==first.audio.wav&&state::read_bytes(first.audio.wav)==wav&&std::filesystem::last_write_time(first.audio.wav)==stamp,"A second bounded child replaced compatible real SAPI audio");
    bool budget_blocked=false;auto absent=sentence;absent.text=absent.spoken=absent.prepared="A different blue circle must not be generated with a zero audio budget.";
    try{(void)process.prepare(absent,"en-US",voice.id,environment.paths.cache,{},0);}catch(const std::exception&){budget_blocked=true;}
    check(budget_blocked&&state::read_bytes(first.audio.wav)==wav&&std::filesystem::last_write_time(first.audio.wav)==stamp,"A zero ceiling generated a missing recording or changed a compatible cache hit");
    bool rejected=false;
    try{(void)process.prepare(sentence,"en-GB",voice.id,environment.paths.cache);}catch(const std::exception&){rejected=true;}
    check(rejected,"An explicit American token was silently used for a British request");
    std::cout<<"PASS actual selected SAPI token, separate child PID "<<first.process_id<<", hard CPU budget "<<first.processors<<", non-silent PCM, real word events, exact cache reuse and dialect refusal\n";

    // Wait for a real in-progress SAPI stream, then exercise cooperative
    // cancellation. No fabricated wave or fake process establishes this test.
    auto long_sentence=sentence;long_sentence.text.clear();
    for(int i=0;i<800;++i)long_sentence.text+="The blue circle moves right. ";
    long_sentence.spoken=long_sentence.prepared=long_sentence.text;
    const auto cancelled_cache=environment.paths.cache/L"cancelled";
    std::stop_source cancellation;std::atomic<bool> cancelled=false;
    std::jthread generating([&](std::stop_token stop){std::stop_callback unwinding(stop,[&]{cancellation.request_stop();});
        try{(void)process.prepare(long_sentence,"en-US",voice.id,cancelled_cache,cancellation.get_token());}
        catch(const std::exception&){cancelled=true;}catch(const winrt::hresult_error&){cancelled=true;}});
    eventually([&]{return pending(cancelled_cache);},"The real SAPI cancellation request never opened its own stream");
    cancellation.request_stop();generating.join();
    check(cancelled&&!pending(cancelled_cache),"Cancellation did not acknowledge and close its own SAPI stream");
    check(state::read_bytes(first.audio.wav)==wav&&std::filesystem::last_write_time(first.audio.wav)==stamp,"Cancelling another request changed finished audio");

    // Terminal shutdown kills only this process object's private job, without
    // waiting on a driver's cooperative cancellation or touching shared voices.
    const auto stopped_cache=environment.paths.cache/L"shutdown";std::atomic<bool> stopped=false;
    std::jthread stopping([&](std::stop_token stop){try{(void)process.prepare(long_sentence,"en-US",voice.id,stopped_cache,stop);}
        catch(const std::exception&){stopped=true;}catch(const winrt::hresult_error&){stopped=true;}});
    eventually([&]{return pending(stopped_cache);},"The private SAPI shutdown request never started");
    process.shutdown();stopping.join();check(stopped,"Terminal speech shutdown did not unblock its own caller");
    DWORD_PTR after=0,after_system=0;const auto affinity_after=GetProcessAffinityMask(GetCurrentProcess(),&after,&after_system);
    check(affinity_before==affinity_after&&before==after,"Background system speech changed the native application's hard affinity");
    check(state::read_bytes(first.audio.wav)==wav&&std::filesystem::last_write_time(first.audio.wav)==stamp,"Private speech shutdown changed a completed recording");
    std::cout<<"PASS real in-flight cancellation/private-job shutdown, untouched app affinity and preserved finished audio; QA="<<environment.paths.root.string()<<'\n';return 0;
}catch(const winrt::hresult_error& error){std::cerr<<"FAIL "<<winrt::to_string(error.message())<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
