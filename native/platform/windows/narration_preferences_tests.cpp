#include "platform/windows/narration_preferences.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        using namespace lgaed;
        const auto runtime = std::filesystem::path(build::source_root)/L"out/narration/runtime.json";
        for (const auto& [authored,expected] : std::map<std::string,std::string>{
            {" ES_mx ","es"},{"Español","es"},{"SPANISH","es"},{"en","en-US"},
            {"en-UK","en-GB"},{" EN_gb_scotland ","en-GB"},{"en-AU","en-US"},
            {"fr-CA","fr"},{"Français","fr"},{"\xC2\xA0" "FR_fr" "\xC2\xA0","fr"}})
            check(windows::narration_language_tag(authored)==expected,"Shared Android language aliases changed");
        check(!windows::narration_language_tag("cpp"),"A programming language was mistaken for narration metadata");
        const auto catalog = windows::kokoro_voice_catalog(runtime);
        check(!catalog.empty(),"The original runtime catalogue was not read");
        for (const auto& voice : catalog) {
            state::StateData data;
            data.preferences["lesson_narration_settings"]["kokoro_voice:"+voice.language]=voice.id;
            const auto original=data;
            check(windows::selected_kokoro_voice(data,voice.language,runtime)==voice.id,"An original voice identity was not preserved");
            check(data==original,"Selecting a compatible voice mutated preferences");
        }
        state::StateData data;
        data.preferences["lesson_narration_settings"]["kokoro_voice"]=std::string("em_santa");
        check(windows::selected_kokoro_voice(data,"es-MX",runtime)=="em_santa","A valid legacy Santa selection was lost");
        check(windows::selected_kokoro_voice(data,"fr-FR",runtime)=="ff_siwis","A Spanish legacy voice leaked into French");
        check(windows::selected_kokoro_voice(data,"en-UK",runtime)=="bf_emma","A Spanish legacy voice leaked into British English");
        data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("ef_dora");
        check(windows::selected_kokoro_voice(data,"es",runtime)=="ef_dora","The per-language voice did not override the legacy selection");
        data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("af_heart");
        check(windows::selected_kokoro_voice(data,"es",runtime)=="em_santa","An incompatible per-language voice hid a valid legacy selection");
        data.preferences["lesson_narration_settings"]["kokoro_voice"]=std::string("invented_voice");
        check(windows::selected_kokoro_voice(data,"es",runtime)=="ef_dora","An unknown voice replaced the original default");
        data.preferences["lesson_narration_settings"]["language"]=std::string(" FR_ca ");
        check(windows::selected_narration_language(data,"es")=="fr","Selected narration aliases did not share content normalization");
        data.preferences["lesson_narration_settings"]["language"]=std::string("cpp");
        check(windows::selected_narration_language(data,"EN_uk")=="en-GB","An invalid preference discarded a valid explicit fallback");
        const auto original=data;
        bool rejected=false;
        try {(void)windows::selected_kokoro_voice(data,"es",runtime.parent_path()/L"missing-narration-preferences-test-runtime.json");}
        catch(const std::exception& error){rejected=std::string(error.what()).find("Kokoro voice catalogue")!=std::string::npos;}
        check(rejected,"A missing runtime catalogue silently invented supported voice identities");
        check(data==original,"An unavailable catalogue reset saved preferences");
        std::cout<<"PASS shared language aliases, original voice catalogue, compatible legacy selection, invalid voice isolation and read-only failure\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
