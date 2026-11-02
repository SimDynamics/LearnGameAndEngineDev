#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <objbase.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace lgaed::windows;
namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Fn> void rejects(Fn action) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"Expected a precise refusal, never another voice or overwritten cache");
}
bool has_pending(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".pending") return true;
    return false;
}
void dispatch_com_messages() {
    MSG message{};
    while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void validate(const NarrationAudio& audio,std::size_t source_length,bool words) {
    require(audio.provider == "windows-sapi5" && audio.duration_ms > 100 && !audio.key.empty(),"The provider must produce real SAPI audio");
    const auto bytes = lgaed::state::read_bytes(audio.wav);
    require(bytes.size() > 44 && std::string(bytes.begin(),bytes.begin() + 4) == "RIFF","Expected an actual PCM WAV");
    if (words) require(audio.timings.size() >= 3,"Installed SAPI voice omitted real word boundary events");
    double previous = -1;
    for (const auto& row : audio.timings) {
        require(row.start < row.end && row.end <= source_length && row.start_ms >= previous && row.end_ms > row.start_ms && row.end_ms <= audio.duration_ms + .1,
            "SAPI timing must be within the original UTF-16 source and actual WAV duration");
        previous = row.start_ms;
    }
}
std::string legacy_key(const NarrationSegment& segment,const SystemVoice& voice){
    auto language=voice.language;for(auto& c:language){if(c=='_')c='-';if(c>='A'&&c<='Z')c+= 'a'-'A';}
    std::string framed="windows-sapi5-pcm24k-v1/";const auto token=[&](std::string_view value){framed+=std::to_string(value.size())+':';framed+=value;};
    token(language);token(voice.id);token(voice.language);token(segment.text);token(segment.spoken);
    for(std::size_t index=0;index<segment.spoken_ranges.size();++index){const auto a=segment.spoken_ranges[index],b=segment.displayed_ranges[index];
        token(std::to_string(a.start));token(std::to_string(a.end));token(std::to_string(b.start));token(std::to_string(b.end));}
    return lgaed::state::sha256(framed);
}
void write_json(const std::filesystem::path& path,const winrt::Windows::Data::Json::JsonObject& object){
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);const auto text=winrt::to_string(object.Stringify());stream.write(text.data(),static_cast<std::streamsize>(text.size()));stream.flush();require(static_cast<bool>(stream),"Write isolated SAPI QA metadata");
}
void legacy_preservation(const std::filesystem::path& root,const NarrationSegment& segment,const SystemVoice& voice,const NarrationAudio& audio){
    using namespace winrt::Windows::Data::Json;
    // A real WAV and the documented v1 sidecar frame reproduce an existing
    // installation, alongside a valid v2 entry for the identical request.
    const auto key=legacy_key(segment,voice);const auto cache=root/"legacy-compatibility";
    const auto old=cache/"windows_tts"/key,modern=cache/"windows_tts"/audio.key;
    require(std::filesystem::create_directories(old)&&std::filesystem::create_directories(modern),"Legacy QA may only create fresh paths");
    std::filesystem::copy_file(audio.wav,old/"audio.wav");std::filesystem::copy_file(audio.wav,modern/"audio.wav");
    std::filesystem::copy_file(audio.wav.parent_path()/"timings.json",modern/"timings.json");
    JsonObject metadata;metadata.Insert(L"key",JsonValue::CreateStringValue(winrt::to_hstring(key)));metadata.Insert(L"source",JsonValue::CreateStringValue(winrt::to_hstring(segment.text)));
    metadata.Insert(L"spoken",JsonValue::CreateStringValue(winrt::to_hstring(segment.spoken)));metadata.Insert(L"provider",JsonValue::CreateStringValue(L"windows-sapi5"));
    metadata.Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(lgaed::state::sha256(lgaed::state::read_bytes(audio.wav)))));
    JsonArray rows;for(const auto& timing:audio.timings){JsonObject row;row.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(timing.start)));row.Insert(L"end",JsonValue::CreateNumberValue(static_cast<double>(timing.end)));
        row.Insert(L"startMs",JsonValue::CreateNumberValue(timing.start_ms));row.Insert(L"endMs",JsonValue::CreateNumberValue(timing.end_ms));rows.Append(row);}metadata.Insert(L"timings",rows);write_json(old/"timings.json",metadata);
    const auto old_pcm=lgaed::state::read_bytes(old/"audio.wav"),old_metadata=lgaed::state::read_bytes(old/"timings.json");const auto old_time=std::filesystem::last_write_time(old/"audio.wav"),metadata_time=std::filesystem::last_write_time(old/"timings.json"),modern_time=std::filesystem::last_write_time(modern/"audio.wav");
    require(system_speech_storage_snapshot(cache).stored_bytes==old_pcm.size()*2,"SAPI storage omitted finalized v1/v2 bytes from its shared engine total");
    const auto reused=synthesize_system_speech_with_budget(segment,voice.language,voice.id,cache,{},0);
    require(reused.cache_hit&&reused.key==key&&reused.wav==old/"audio.wav"&&reused.timings.size()==audio.timings.size(),"A valid exact v1 entry must take precedence without migration");
    for(std::size_t index=0;index<audio.timings.size();++index){const auto a=audio.timings[index],b=reused.timings[index];require(a.start==b.start&&a.end==b.end&&std::abs(a.start_ms-b.start_ms)<.001&&std::abs(a.end_ms-b.end_ms)<.001,"Reading v1 changed its saved source-specific timings");}
    require(lgaed::state::read_bytes(old/"audio.wav")==old_pcm&&lgaed::state::read_bytes(old/"timings.json")==old_metadata&&std::filesystem::last_write_time(old/"audio.wav")==old_time&&std::filesystem::last_write_time(old/"timings.json")==metadata_time&&std::filesystem::last_write_time(modern/"audio.wav")==modern_time,
        "Reading v1 rewrote a recording/sidecar or the neighboring v2 WAV");
    {std::ofstream stream(old/"timings.json",std::ios::binary|std::ios::trunc);stream<<"{}";}const auto corrupt=lgaed::state::read_bytes(old/"timings.json");
    rejects([&]{(void)synthesize_system_speech(segment,voice.language,voice.id,cache);});
    require(lgaed::state::read_bytes(old/"audio.wav")==old_pcm&&lgaed::state::read_bytes(old/"timings.json")==corrupt&&std::filesystem::last_write_time(old/"audio.wav")==old_time&&std::filesystem::last_write_time(modern/"audio.wav")==modern_time,
        "A corrupt v1 entry was replaced or silently bypassed using v2");
}
void storage_budget_tests(const std::filesystem::path& root,const NarrationSegment& segment,const SystemVoice& voice,const NarrationAudio& audio){
    const auto cache=root/"storage-budget";const auto zero=root/"storage-zero";const auto one_byte_cache=root/"storage-one-byte";
    const auto fresh=system_speech_storage_snapshot(zero,0);require(!std::filesystem::exists(zero)&&fresh.stored_bytes==0&&fresh.configured_bytes==0&&fresh.effective_bytes==0,"Reading an empty SAPI budget created files or disregarded zero");
    require(fresh.maximum_bytes==(fresh.available_bytes>narration_storage_reserve?fresh.available_bytes-narration_storage_reserve:0),"SAPI capacity did not retain exactly one GiB of disk reserve");
    rejects([&]{(void)synthesize_system_speech_with_budget(segment,voice.language,voice.id,zero,{},0);});require(!std::filesystem::exists(zero),"A zero-budget SAPI miss created synthesis resources");
    const auto final=cache/"windows_tts"/audio.key;require(std::filesystem::create_directories(final),"Budget QA may only create a new cache");
    std::filesystem::copy_file(audio.wav,final/"audio.wav");std::filesystem::copy_file(audio.wav.parent_path()/"timings.json",final/"timings.json");
    const auto bytes=lgaed::state::read_bytes(final/"audio.wav");const auto modified=std::filesystem::last_write_time(final/"audio.wav"),metadata_time=std::filesystem::last_write_time(final/"timings.json");
    const auto size=static_cast<std::uint64_t>(bytes.size());const auto full=system_speech_storage_snapshot(cache,size);
    require(full.stored_bytes==size&&full.configured_bytes==size&&full.effective_bytes==size&&full.maximum_bytes==size+full.available_bytes-narration_storage_reserve,"SAPI storage rounded its exact-byte ceiling or included sidecars");
    const auto pending=cache/"windows_tts"/".isolated-qa.pending",unrelated=cache/"windows_tts"/"unrelated-qa";
    require(std::filesystem::create_directory(pending)&&std::filesystem::create_directory(unrelated),"Create isolated pending/unrelated storage fixtures");
    std::filesystem::copy_file(audio.wav,pending/"audio.wav");std::filesystem::copy_file(audio.wav,unrelated/"audio.wav");
    require(system_speech_storage_snapshot(cache,size).stored_bytes==size,"SAPI usage counted unfinished/unrelated files as finalized recordings");
    for(const auto budget:{std::uint64_t{0},size}){const auto hit=synthesize_system_speech_with_budget(segment,voice.language,voice.id,cache,{},budget);
        require(hit.cache_hit&&hit.key==audio.key&&hit.wav==final/"audio.wav","A zero/full SAPI budget prevented reuse of a valid existing recording");}
    auto miss=segment;miss.text=miss.spoken="A separate uncached budget sentence.";miss.spoken_ranges.clear();miss.displayed_ranges.clear();
    rejects([&]{(void)synthesize_system_speech_with_budget(miss,voice.language,voice.id,cache,{},size);});
    // One byte of remaining quota passes the preflight but cannot contain an
    // actual PCM WAV. This exercises the post-synthesis guard using real SAPI.
    rejects([&]{(void)synthesize_system_speech_with_budget(miss,voice.language,voice.id,one_byte_cache,{},1);});
    require(system_speech_storage_snapshot(one_byte_cache,1).stored_bytes==0&&!has_pending(one_byte_cache/"windows_tts"),"An over-budget real SAPI result was published or left pending resources");
    require(lgaed::state::read_bytes(final/"audio.wav")==bytes&&std::filesystem::last_write_time(final/"audio.wav")==modified&&std::filesystem::last_write_time(final/"timings.json")==metadata_time&&std::filesystem::exists(pending/"audio.wav")&&std::filesystem::exists(unrelated/"audio.wav"),
        "Budget admission changed existing recordings, sidecars or unrelated/pending data");
    std::cout<<"PASS SAPI budget: read-only final-recording usage, exact bytes and1GiB reserve, zero miss refusal, v2 hits at zero/full quota, real post-synthesis refusal and preserved data\n";
}
}
int main(int argc,char** argv) {
    unsigned groups = 0, skipped = 0;
    try {
        std::cout << std::unitbuf;
        require(argc == 2,"Supply an isolated QA output directory");
        const auto root = std::filesystem::absolute(argv[1]) / ("sapi-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        require(!std::filesystem::exists(root),"QA output may not replace an existing directory");
        require(system_voice_matches("en-US","en_US") && system_voice_matches("fr-FR","fr") && system_voice_matches("es-MX","es"),"Language tag matching regression");
        require(!system_voice_matches("en-US","en-GB") && !system_voice_matches("fr-FR","en") && !system_voice_matches("en-US",""),"Do not fall back across languages or English dialects"); ++groups;
        const auto installed = system_voices();
        for (const auto& voice : installed) std::cout << "Installed SAPI: " << voice.name << " [" << voice.language << "]\n";
        NarrationSegment basic; basic.text = "The program reads three values and prints the result.";
        basic.spoken = basic.text;
        rejects([&] { (void)synthesize_system_speech(basic,"zz-ZZ","",root); }); ++groups;
        if (installed.empty()) {
            std::cout << groups << " groups passed; real synthesis skipped: no installed SAPI5 voices.\n"; return 0;
        }
        for (const auto language : {"es","en-US","en-GB","fr"}) {
            const auto chosen = std::ranges::find_if(installed,[&](const auto& voice) { return system_voice_matches(voice.language,language); });
            if (chosen == installed.end()) { ++skipped; std::cout << "SKIP " << language << ": no compatible installed SAPI5 voice; no engine installed by the test.\n"; continue; }
            NarrationSegment segment;
            segment.text = std::string(language).starts_with("es") ? "El programa lee tres datos y muestra el resultado." : std::string(language).starts_with("fr") ? "Le programme lit trois valeurs et affiche le résultat." : basic.text;
            segment.spoken = segment.text;
            const auto audio = synthesize_system_speech(segment,language,chosen->id,root);
            const auto source_length = static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,segment.text.data(),static_cast<int>(segment.text.size()),nullptr,0));
            validate(audio,source_length,true); require(!audio.cache_hit,"First synthesis must not claim a cache hit");
            const auto hash = lgaed::state::sha256(lgaed::state::read_bytes(audio.wav)); const auto modified = std::filesystem::last_write_time(audio.wav);
            const auto cached = synthesize_system_speech(segment,language,chosen->id,root);
            require(cached.cache_hit && cached.key == audio.key && lgaed::state::sha256(lgaed::state::read_bytes(cached.wav)) == hash && std::filesystem::last_write_time(audio.wav) == modified,
                "A valid system recording must be reused without rewriting");
            std::cout << "Synthesized " << language << ": " << audio.timings.size() << " actual word events, " << audio.duration_ms << " ms; cache reused.\n"; ++groups;
        }
        const auto chosen = std::ranges::find_if(installed,[](const auto& voice) { return system_voice_matches(voice.language,"en-US"); });
        const auto& voice = chosen == installed.end() ? installed.front() : *chosen;
        rejects([&] { (void)synthesize_system_speech(basic,voice.language,"missing-token",root); });
        rejects([&] { (void)synthesize_system_speech(basic,"zz-ZZ",voice.id,root); }); ++groups;

        NarrationSegment technical; technical.text = "std:: indicates the selected namespace.";
        technical.spoken = "S T D scope resolution operator indicates the selected namespace.";
        // Deliberately repeated owners are the shared linguistic worker contract:
        // the source abbreviation owns three spoken letters and :: owns three words.
        technical.spoken_ranges = {{0,1},{2,3},{4,5},{6,11},{12,22},{23,31},{32,41},{42,45},{46,54},{55,64}};
        technical.displayed_ranges = {{0,3},{0,3},{0,3},{3,5},{3,5},{3,5},{6,15},{16,19},{20,28},{29,38}};
        const auto mapped = synthesize_system_speech(technical,voice.language,voice.id,root);
        std::cout << "Mapped technical WAV produced.\n";
        validate(mapped,technical.text.size(),true);
        require(std::ranges::any_of(mapped.timings,[](const auto& row) { return row.start == 0 && row.end == 3; }) &&
            std::ranges::any_of(mapped.timings,[](const auto& row) { return row.start == 3 && row.end == 5; }) &&
            std::ranges::any_of(mapped.timings,[](const auto& row) { return row.start == 6 && row.end == 15; }),"Technical spoken words must project to their actual source owners");
        require(std::ranges::none_of(mapped.timings,[&](const auto& row) { return row.start == 0 && row.end == technical.text.size(); }),"A technical token may not illuminate the whole paragraph"); ++groups;
        auto unmapped = technical; unmapped.spoken_ranges.clear(); unmapped.displayed_ranges.clear();
        const auto conservative = synthesize_system_speech(unmapped,voice.language,voice.id,root);
        std::cout << "Conservative unmapped WAV produced.\n";
        validate(conservative,unmapped.text.size(),false);
        const auto pcm_before=lgaed::state::read_bytes(mapped.wav);const auto pcm_time=std::filesystem::last_write_time(mapped.wav),sidecar_time=std::filesystem::last_write_time(mapped.wav.parent_path()/"timings.json");
        require(conservative.timings.empty()&&conservative.cache_hit&&conservative.key==mapped.key&&conservative.wav==mapped.wav,"Same speech without source mapping must reuse PCM without inventing another source's timings");++groups;
        auto shifted=technical;const std::string prefix="Visible \xF0\x9F\x98\x80: ";shifted.text=prefix+technical.text;
        const auto prefix_length=static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,prefix.data(),static_cast<int>(prefix.size()),nullptr,0));
        for(auto& range:shifted.displayed_ranges){range.start+=prefix_length;range.end+=prefix_length;}
        const auto reprojected=synthesize_system_speech(shifted,voice.language,voice.id,root);
        require(reprojected.cache_hit&&reprojected.key==mapped.key&&reprojected.wav==mapped.wav&&reprojected.source==shifted.text&&reprojected.timings.size()==mapped.timings.size(),"A displayed-source change regenerated or confused a shared speech recording");
        for(std::size_t index=0;index<mapped.timings.size();++index){const auto a=mapped.timings[index],b=reprojected.timings[index];require(b.start==a.start+prefix_length&&b.end==a.end+prefix_length&&std::abs(a.start_ms-b.start_ms)<.001&&std::abs(a.end_ms-b.end_ms)<.001,"Shared SAPI PCM did not project each real word to its current UTF-16 source");}
        NarrationSegment background;background.text=background.spoken=technical.spoken;const auto raw=synthesize_system_speech(background,voice.language,voice.id,root);
        require(raw.cache_hit&&raw.wav==mapped.wav&&raw.key==mapped.key&&raw.source==technical.spoken&&raw.timings.size()==mapped.timings.size()&&raw.timings.front().end<mapped.timings.front().end,
            "Canonical background speech and visible technical text must share PCM while retaining their distinct word owners");
        require(lgaed::state::read_bytes(mapped.wav)==pcm_before&&std::filesystem::last_write_time(mapped.wav)==pcm_time&&std::filesystem::last_write_time(mapped.wav.parent_path()/"timings.json")==sidecar_time,"Reprojection rewrote an existing recording or raw-event sidecar");++groups;
        using namespace winrt::Windows::Data::Json;
        const auto metadata_bytes=lgaed::state::read_bytes(mapped.wav.parent_path()/"timings.json");const auto pcm_metadata=JsonObject::Parse(winrt::to_hstring(std::string(metadata_bytes.begin(),metadata_bytes.end())));
        auto actual_language=voice.language;for(auto& c:actual_language){if(c=='_')c='-';if(c>='A'&&c<='Z')c+='a'-'A';}
        require(pcm_metadata.GetNamedNumber(L"version")==2&&pcm_metadata.GetNamedString(L"provider")==L"windows-sapi5"&&pcm_metadata.GetNamedString(L"audioFormat")==L"pcm_s16le/24000/mono"&&
            winrt::to_string(pcm_metadata.GetNamedString(L"voiceId"))==voice.id&&winrt::to_string(pcm_metadata.GetNamedString(L"voiceLanguage"))==actual_language&&winrt::to_string(pcm_metadata.GetNamedString(L"language"))==actual_language&&
            pcm_metadata.GetNamedArray(L"words").Size()>0&&!pcm_metadata.HasKey(L"source")&&!pcm_metadata.HasKey(L"timings"),"PCM v2 omitted the real provider/voice/format/language or stored another caller's source projection");++groups;
        const auto other=std::ranges::find_if(installed,[&](const auto& entry){return entry.id!=voice.id&&system_voice_matches(entry.language,voice.language);});
        if(other!=installed.end()){const auto distinct=synthesize_system_speech(technical,other->language,other->id,root);validate(distinct,technical.text.size(),true);require(distinct.key!=mapped.key&&distinct.wav!=mapped.wav,"A different installed system voice reused another voice's recording");++groups;}
        else{++skipped;std::cout<<"SKIP second same-language SAPI token: unavailable, no voice installed by test.\n";}
        legacy_preservation(root,technical,voice,mapped);++groups;
        storage_budget_tests(root,technical,voice,mapped);++groups;

        std::stop_source stopped; stopped.request_stop();
        const auto cancelled_root = root / "cancel-before";
        rejects([&] { (void)synthesize_system_speech(basic,voice.language,voice.id,cancelled_root,stopped.get_token()); });
        require(!std::filesystem::exists(cancelled_root),"Cancellation before synthesis must not create files"); ++groups;
        NarrationSegment long_text; for (unsigned i = 0; i < 1500; ++i) long_text.text += "calculation "; long_text.spoken = long_text.text;
        const auto in_progress = root / "cancel-during"; std::atomic<bool> finished = false, was_cancelled = false;
        std::jthread task([&](std::stop_token stop) {
            try { (void)synthesize_system_speech(long_text,voice.language,voice.id,in_progress,stop); }
            catch (const std::exception& error) { was_cancelled = stop.stop_requested(); std::cout << "Cancellation returned: " << error.what() << '\n'; }
            finished = true;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        // The host UI normally pumps its STA. This console fixture must do the
        // same while a SAPI engine created earlier on that STA is marshalled.
        while (!finished && !has_pending(in_progress / "windows_tts") && std::chrono::steady_clock::now() < deadline) {
            dispatch_com_messages(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const bool entered = !finished && has_pending(in_progress / "windows_tts");
        std::cout << "SAPI cancellation fixture entered: " << entered << ".\n";
        task.request_stop();
        HANDLE thread = task.native_handle();
        while (WaitForSingleObject(thread,0) == WAIT_TIMEOUT) {
            DWORD signaled = 0;
            (void)CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,20,1,&thread,&signaled);
            dispatch_com_messages();
        }
        task.join();
        std::cout << "SAPI cancellation thread released.\n";
        require(entered,"Cancellation fixture must enter a real synthesis operation");
        require(was_cancelled && !has_pending(in_progress / "windows_tts"),"Cancellation must purge SAPI, close the stream and remove its pending files"); ++groups;

        const auto before = lgaed::state::read_bytes(mapped.wav);
        const auto metadata = mapped.wav.parent_path() / "timings.json";
        { std::ofstream output(metadata,std::ios::binary | std::ios::trunc); output << "{}"; require(static_cast<bool>(output),"Cannot change isolated QA metadata"); }
        rejects([&] { (void)synthesize_system_speech(technical,voice.language,voice.id,root); });
        require(lgaed::state::read_bytes(mapped.wav) == before,"Invalid system cache metadata must not replace an existing WAV"); ++groups;
        require(!has_pending(root / "windows_tts"),"No pending synthesis resources may remain");
        std::cout << groups << " SAPI groups passed; " << skipped << " unavailable-language checks skipped.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "SAPI test failed after " << groups << " groups: " << error.what() << '\n'; return 1; }
}
