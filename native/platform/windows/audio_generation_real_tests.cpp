#include "platform/windows/audio_generation.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_preferences.hpp"
#include "platform/windows/narration_storage.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

using namespace lgaed;
using namespace lgaed::windows;
namespace {
using namespace winrt::Windows::Data::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void write_new(const std::filesystem::path& path,std::string_view content){
    state::require_safe_path(path);check(!std::filesystem::exists(path),"A real generation test must preserve earlier fixtures");
    std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<content;stream.flush();check(static_cast<bool>(stream),"Cannot create the isolated generation fixture");
}
struct Inventory {
    std::filesystem::path root;
    std::vector<std::string> expected_order;
    std::vector<std::string> lessons;
};
Inventory content(const std::filesystem::path& root,std::string_view nonce,bool english){
    Inventory result{root};
    for(const auto* route:{"programming","math_physics"}){
        const auto course=std::string(route)=="programming"?"qa_programming":"qa_math";
        const auto unit=std::string(course)+"__unit",lesson=unit+"__lesson";
        auto location=curriculum::catalog_child({"es"},route);
        write_new(root/"es"/curriculum::catalog_index_path(location),"[{\"id\":\""+std::string(course)+"\",\"title\":\"QA\",\"number\":1}]");
        location=curriculum::catalog_child(location,course);
        write_new(root/"es"/curriculum::catalog_index_path(location),"{\"id\":\""+std::string(course)+"\",\"title\":\"QA\",\"units\":[{\"id\":\""+unit+"\",\"title\":\"QA\",\"number\":1}]}");
        location=curriculum::catalog_child(location,unit);
        write_new(root/"es"/curriculum::catalog_index_path(location),"{\"courseId\":\""+std::string(course)+"\",\"unitId\":\""+unit+"\",\"title\":\"QA\",\"lessons\":[{\"id\":\""+lesson+"\",\"title\":\"QA\",\"number\":1}]}");
        JsonObject fixture;fixture.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(lesson)));
        fixture.Insert(L"title",JsonValue::CreateStringValue(winrt::to_hstring((english?"Voice test ":"Prueba de voz ")+std::string(route))));
        JsonArray pages;
        for(int page=0;page<2;++page){JsonObject paragraph;paragraph.Insert(L"type",JsonValue::CreateStringValue(L"paragraph"));
            const auto sentence=english?"The blue circle moves to the right. ":"El círculo azul se mueve a la derecha. ";
            paragraph.Insert(L"text",JsonValue::CreateStringValue(winrt::to_hstring(std::string(sentence)+std::string(nonce)+" "+route+" "+std::to_string(page)+".")));
            JsonArray blocks;blocks.Append(paragraph);JsonObject body;body.Insert(L"blocks",blocks);pages.Append(body);}
        fixture.Insert(L"pages",pages);write_new(root/"es"/curriculum::catalog_lesson_path(location,lesson),winrt::to_string(fixture.Stringify()));
        result.lessons.push_back(lesson);
    }
    result.expected_order={"programming/"+result.lessons[0]+"/0","math_physics/"+result.lessons[1]+"/0",
        "programming/"+result.lessons[0]+"/1","math_physics/"+result.lessons[1]+"/1"};
    return result;
}
std::map<std::filesystem::path,std::pair<std::uintmax_t,std::filesystem::file_time_type>> recordings(const std::filesystem::path& cache){
    std::map<std::filesystem::path,std::pair<std::uintmax_t,std::filesystem::file_time_type>> result;
    if(std::filesystem::is_directory(cache))for(const auto& entry:std::filesystem::recursive_directory_iterator(cache))
        if(entry.is_regular_file()&&entry.path().extension()==L".wav")result.emplace(entry.path(),std::pair{entry.file_size(),entry.last_write_time()});
    return result;
}
void real_pcm(const std::filesystem::path& path){
    const auto bytes=state::read_bytes(path);check(bytes.size()>44&&std::string(bytes.begin(),bytes.begin()+4)=="RIFF","The producer did not store a real WAV");
    const auto word=[&](std::size_t i){return std::uint32_t(bytes[i])|(std::uint32_t(bytes[i+1])<<8)|(std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+3])<<24);};
    bool sound=false;
    for(std::size_t i=12;i+8<=bytes.size();){const auto size=word(i+4);check(size<=bytes.size()-i-8,"Incomplete generated WAV");
        if(std::string_view(reinterpret_cast<const char*>(bytes.data()+i),4)=="data")for(std::size_t sample=i+8;sample+1<i+8+size;sample+=2)sound|=bytes[sample]||bytes[sample+1];
        i+=8+size+(size&1);}
    check(sound,"The producer's actual PCM is silent");
}
std::vector<std::string> finish(AudioGeneration& generation){
    std::vector<std::string> order;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    while(std::chrono::steady_clock::now()<deadline){const auto status=generation.status();
        if(!status.error.empty())throw std::runtime_error("Actual producer: "+status.error);
        if(status.running&&!status.page.empty()){
            const auto page=status.page.substr(0,status.page.find('|'));
            if(order.empty()||order.back()!=page)order.push_back(page);}
        if(status.finished&&status.waiting){check(status.completed==4&&status.total==4,"Completed inventory did not cover every eligible page");return order;}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    throw std::runtime_error("Actual background generation did not finish its finite inventory");
}
void idle(AudioGeneration& generation){
    // Let the last OS cache notification settle, then require a true event wait,
    // not the previous one-second rescan loop over a completed inventory.
    auto stable=generation.status();auto since=std::chrono::steady_clock::now();const auto deadline=since+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline){const auto now=generation.status();
        if(!now.finished||!now.waiting||now.scans!=stable.scans){stable=now;since=std::chrono::steady_clock::now();}
        if(now.finished&&now.waiting&&std::chrono::steady_clock::now()-since>std::chrono::milliseconds(500))break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    const auto before=generation.status();check(before.finished&&before.waiting,"Finished audio did not enter its event wait");
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));const auto after=generation.status();
    check(after.finished&&after.waiting&&!after.running&&after.scans==before.scans,"Finished audio kept polling/rescanning instead of stopping");
}
void check_selected_kokoro(NarrationWorker& worker,const Inventory& fixture,const AudioGenerationSelection& selection,const std::filesystem::path& cache){
    std::size_t hits=0;
    for(const auto* route:{"programming","math_physics"}){
        const auto which=std::string(route)=="programming"?0u:1u;const auto course=which==0?"qa_programming":"qa_math";
        const curriculum::CatalogLocation location{"es",route,course,std::string(course)+"__unit"};
        const auto bytes=state::read_bytes(fixture.root/"es"/curriculum::catalog_lesson_path(location,fixture.lessons[which]));
        const std::string source(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        for(unsigned page=0;page<2;++page){
            const auto plan=JsonObject::Parse(winrt::to_hstring(worker.curriculum_plan(source,page,"es",selection.voices.at("es"),"es")));
            for(const auto& value:plan.GetNamedArray(L"segments")){const auto row=value.GetObject();const auto key=winrt::to_string(row.GetNamedString(L"key"));
                const auto wav=cache/winrt::to_hstring(key+".wav").c_str();check(std::filesystem::is_regular_file(wav),"The selected voice's independently planned key was not stored");real_pcm(wav);++hits;}
        }
    }
    check(hits>=4,"The actual selected-voice inventory had no speech");
}
}
int main(){try{
    ensure_winrt_apartment();const auto instance=L"audio-generation-real-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto environment=resolve_windows_environment({ProfileId::Codex,instance,true});initialize_profile_directories(environment.paths);
    const auto nonce="sol luna mesa libro "+std::to_string(GetTickCount64());
    const auto neural=content(environment.paths.projects/L"kokoro-content",nonce,false);
    auto worker=std::make_shared<NarrationWorker>(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
    state::StateData data;data.preferences["lesson_narration_settings"]["enabled"]=true;
    data.preferences["lesson_narration_settings"]["kokoro_advance_generation"]=true;
    data.preferences["lesson_narration_settings"]["engine"]=std::string("KOKORO");
    data.preferences["lesson_narration_settings"]["language"]=std::string("es");
    data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_santa");
    data.preferences["learning_progress"]["qa-sentinel"]=std::string("never altered by audio");
    state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
    const auto original_state=state::read_bytes(environment.paths.root/L"state/active/state.bin");
    const auto cache=environment.paths.cache/L"kokoro";auto selection=audio_generation_selection(data,{"es","programming","qa_programming"});
    check(selection.voices.at("es")=="em_santa","Advance generation ignored the explicitly selected Santa voice");
    {
        AudioGeneration generation(worker,neural.root,cache,environment.paths.root);generation.update(selection);
        const auto order=finish(generation);check(order==neural.expected_order,"Actual neural pages did not alternate routes from beginning to end");
        idle(generation);check_selected_kokoro(*worker,neural,selection,cache);
        const auto santa=recordings(cache);check(!santa.empty()&&narration_storage_snapshot(cache).stored_bytes>0,"Actual Santa synthesis did not increase saved audio bytes");
        data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("ef_dora");
        selection=audio_generation_selection(data,{"es","math_physics","qa_math"});generation.update(selection);
        check(finish(generation)==neural.expected_order,"Changing the selected voice did not restart its own finite manifest order");
        idle(generation);check_selected_kokoro(*worker,neural,selection,cache);
        const auto both=recordings(cache);check(both.size()>santa.size(),"The independently selected Dora voice reused Santa recordings");
        for(const auto& [file,original]:santa)check(both.at(file)==original,"Selecting Dora rewrote existing Santa audio");
        const auto before=generation.status().scans;
        const auto current_voice=std::ranges::find_if(both,[&](const auto& row){return !santa.contains(row.first);});
        check(current_voice!=both.end(),"The changed voice has no separate actual recording");const auto changed=current_voice->first;
        std::filesystem::last_write_time(changed,std::filesystem::last_write_time(changed)+std::chrono::seconds(1));
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(generation.status().scans==before&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(generation.status().scans>before,"The completed producer did not wake after an OS cache change");(void)finish(generation);idle(generation);
    }
    std::cout<<"PASS actual Santa/Dora neural synthesis, matching independently planned keys/non-silent PCM, increasing bytes, alternating pages, navigation-independent order, idle completion and OS cache wake\n";
    const auto installed=system_voices();const auto system=std::ranges::find_if(installed,[](const auto& voice){return system_voice_matches(voice.language,"en-US");});
    check(system!=installed.end(),"Actual advance system-speech acceptance needs an installed American voice");
    const auto native=content(environment.paths.projects/L"system-content",nonce,true);
    data.preferences["lesson_narration_settings"]["engine"]=std::string("WINDOWS");data.preferences["lesson_narration_settings"]["language"]=std::string("en-US");
    data.preferences["lesson_narration_settings"]["windows_voice:en-US"]=system->id;
    selection=audio_generation_selection(data,{"es"});check(selection.enabled&&selection.engine=="WINDOWS"&&selection.voices.at("en-US")==system->id,"The selected system engine/token was replaced with Kokoro");
    {
        AudioGeneration generation(worker,native.root,cache,environment.paths.root);generation.update(selection);
        check(finish(generation)==native.expected_order,"Actual SAPI pages did not alternate in manifest order");idle(generation);
        std::size_t count=0;
        for(const auto& entry:std::filesystem::directory_iterator(environment.paths.cache/L"windows_tts"))if(entry.is_directory()&&entry.path().extension()!=L".pending"){
            const auto bytes=state::read_bytes(entry.path()/L"timings.json");const auto meta=JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
            check(winrt::to_string(meta.GetNamedString(L"voiceId"))==system->id&&meta.GetNamedString(L"provider")==L"windows-sapi5","System generation metadata used a different selected provider/voice");
            real_pcm(entry.path()/L"audio.wav");++count;}
        check(count>=4,"The actual system producer did not store its complete speech inventory");
    }
    const auto stored_system=recordings(environment.paths.cache/L"windows_tts");
    const auto limited=content(environment.paths.projects/L"system-zero-budget-content",nonce+" quota",true);
    data.preferences["lesson_narration_settings"]["offline_audio_budget_bytes"]=std::int64_t{0};
    selection=audio_generation_selection(data,{"es"});
    {
        AudioGeneration generation(worker,limited.root,cache,environment.paths.root);generation.update(selection);
        const auto failed_by=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(generation.status().error.empty()&&std::chrono::steady_clock::now()<failed_by)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(!generation.status().error.empty()&&!generation.status().finished,"A zero system-speech ceiling did not report its missing recordings");
        selection.paused_courses={"programming:qa_programming","math_physics:qa_math"};generation.update(selection);
        const auto paused_by=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!(generation.status().finished&&generation.status().waiting)&&std::chrono::steady_clock::now()<paused_by)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto paused=generation.status();
        check(paused.finished&&paused.waiting&&paused.completed==0&&paused.total==0&&paused.error.empty(),"Pausing the last failed course left an obsolete generation error");
        check(recordings(environment.paths.cache/L"windows_tts")==stored_system,"A zero budget or course pause changed completed system recordings");
    }
    worker->shutdown();check(state::read_bytes(environment.paths.root/L"state/active/state.bin")==original_state,"Background audio changed preferences, progress or drafts");
    std::cout<<"PASS actual selected Windows TTS advance generation, finite idle completion, real PCM/provider/token, zero-budget refusal, paused-error recovery and preserved learning state; QA="<<environment.paths.root.string()<<'\n';return 0;
}catch(const winrt::hresult_error& error){std::cerr<<"FAIL "<<winrt::to_string(error.message())<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
