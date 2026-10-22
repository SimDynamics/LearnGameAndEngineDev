#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
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
}
int main(int argc,char** argv) {
    unsigned groups = 0, skipped = 0;
    try {
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
        validate(mapped,technical.text.size(),true);
        require(std::ranges::any_of(mapped.timings,[](const auto& row) { return row.start == 0 && row.end == 3; }) &&
            std::ranges::any_of(mapped.timings,[](const auto& row) { return row.start == 3 && row.end == 5; }) &&
            std::ranges::any_of(mapped.timings,[](const auto& row) { return row.start == 6 && row.end == 15; }),"Technical spoken words must project to their actual source owners");
        require(std::ranges::none_of(mapped.timings,[&](const auto& row) { return row.start == 0 && row.end == technical.text.size(); }),"A technical token may not illuminate the whole paragraph"); ++groups;
        auto unmapped = technical; unmapped.spoken_ranges.clear(); unmapped.displayed_ranges.clear();
        const auto conservative = synthesize_system_speech(unmapped,voice.language,voice.id,root);
        validate(conservative,unmapped.text.size(),false);
        require(conservative.timings.empty() && conservative.key != mapped.key,"Changed spoken text without source mapping must not invent timings or reuse another projection"); ++groups;

        std::stop_source stopped; stopped.request_stop();
        const auto cancelled_root = root / "cancel-before";
        rejects([&] { (void)synthesize_system_speech(basic,voice.language,voice.id,cancelled_root,stopped.get_token()); });
        require(!std::filesystem::exists(cancelled_root),"Cancellation before synthesis must not create files"); ++groups;
        NarrationSegment long_text; for (unsigned i = 0; i < 1500; ++i) long_text.text += "calculation "; long_text.spoken = long_text.text;
        const auto in_progress = root / "cancel-during"; std::atomic<bool> finished = false, was_cancelled = false;
        std::jthread task([&](std::stop_token stop) {
            try { (void)synthesize_system_speech(long_text,voice.language,voice.id,in_progress,stop); }
            catch (const std::exception&) { was_cancelled = stop.stop_requested(); }
            finished = true;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!finished && !has_pending(in_progress / "windows_tts") && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(!finished && has_pending(in_progress / "windows_tts"),"Cancellation fixture must enter a real synthesis operation");
        task.request_stop(); task.join();
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
