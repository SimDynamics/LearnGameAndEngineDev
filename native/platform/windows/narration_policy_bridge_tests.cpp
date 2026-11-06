#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/base.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using Plan=std::vector<NarrationSegment>;
void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::wstring wide(std::string_view text) {
    const auto value=winrt::to_hstring(text);return {value.c_str(),value.size()};
}
std::string spoken(const Plan& plan) {
    std::string result;for(const auto& part:plan){if(!result.empty())result+=' ';result+=part.spoken;}return result;
}
bool same_ranges(const std::vector<SpeechRange>& a,const std::vector<SpeechRange>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i].start!=b[i].start||a[i].end!=b[i].end)return false;
    return true;
}
bool same(const NarrationSegment& a,const NarrationSegment& b) {
    return a.text==b.text&&a.prepared==b.prepared&&a.spoken==b.spoken&&a.start==b.start&&a.end==b.end&&
        same_ranges(a.spoken_ranges,b.spoken_ranges)&&same_ranges(a.displayed_ranges,b.displayed_ranges);
}
bool same(const Plan& a,const Plan& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(!same(a[i],b[i]))return false;
    return true;
}
void owners(std::string_view source,const Plan& plan) {
    require(!plan.empty(),"The native speech-policy request produced no plan");
    const auto original=wide(source);std::size_t previous=0;
    for(const auto& part:plan){
        const auto displayed=wide(part.text),speech=wide(part.spoken);
        require(previous<=part.start&&part.start<part.end&&part.end<=original.size()&&
            original.substr(part.start,part.end-part.start)==displayed,
            "Native IPC changed a source sentence or interpreted UTF-16 offsets as UTF-8 bytes");
        require(!part.spoken_ranges.empty()&&part.spoken_ranges.size()==part.displayed_ranges.size(),
            "Native IPC discarded paired spoken/displayed owners");
        for(std::size_t i=0;i<part.spoken_ranges.size();++i){
            const auto a=part.spoken_ranges[i],b=part.displayed_ranges[i];
            require(a.start<a.end&&a.end<=speech.size()&&b.start<b.end&&b.end<=displayed.size(),
                "A native speech-policy word owner exceeds its unchanged UTF-16 source");
        }
        previous=part.end;
    }
}
}

int main() try {
    ensure_winrt_apartment();
    const LaunchOptions options{ProfileId::Codex,L"narration-policy-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    NarrationWorker worker(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
    unsigned groups=0;
    for(const std::string_view mode:{"auto","text"})for(const std::string_view context:{"auto","cpp"}){
        const std::string source="𐐀 La salida es `N=26`.";
        const auto current=worker.plan(source,"es",{},context,mode);
        const auto historical=worker.plan(source,"es",{},context,mode,true);
        const auto after=worker.plan(source,"es",{},context,mode);
        owners(source,current);owners(source,historical);owners(source,after);
        require(spoken(current).find("igual")!=std::string::npos&&spoken(current).find("asigna")==std::string::npos,
            "The default native plan did not use current output-data speech");
        require(spoken(historical).find("asigna")!=std::string::npos,
            "The native bool was not serialized as the requested historical output-data policy");
        require(same(current,after)&&same(current,worker.plan(source,"es",{},context,mode,false)),
            "A historical native request contaminated a subsequent default or explicit-false plan");
        const auto identity=worker.identity("qa-native-output-policy","es","ef_dora",current);
        const auto previous_identity=worker.identity("qa-native-output-policy","es","ef_dora",historical);
        require(identity.size()==64&&previous_identity.size()==64&&identity!=previous_identity&&
            worker.identity("qa-native-output-policy","es","ef_dora",after)==identity&&
            worker.identity("qa-native-output-policy","es","ef_dora",worker.plan(source,"es",{},context,mode,true))==previous_identity,
            "Native IPC changed the authenticated historical/current plan identities");

        const std::string unchanged="La instrucción `total = 3;` actualiza el valor.";
        const auto source_now=worker.plan(unchanged,"es",{},context,mode);
        const auto source_before=worker.plan(unchanged,"es",{},context,mode,true);
        owners(unchanged,source_now);owners(unchanged,source_before);
        require(same(source_now,source_before)&&spoken(source_now).find("asigna")!=std::string::npos&&
            worker.identity("qa-native-unaffected","es","ef_dora",source_now)==
                worker.identity("qa-native-unaffected","es","ef_dora",source_before),
            "A compatibility request changed genuine C++ source or an unaffected recording identity");

        const std::string sequence="Antes conserva el dato. La salida es `N=26`. Después comprueba el resultado.";
        const auto sequence_now=worker.plan(sequence,"es",{},context,mode);
        const auto sequence_before=worker.plan(sequence,"es",{},context,mode,true);
        owners(sequence,sequence_now);owners(sequence,sequence_before);
        require(sequence_now.size()==3&&sequence_before.size()==3&&same(sequence_now[0],sequence_before[0])&&
            same(sequence_now[2],sequence_before[2]),"A native policy alias changed an unaffected neighboring passage");
        ++groups;
    }
    const auto cpp=worker.plan("11/2=5","es",{},"cpp","auto");
    owners("11/2=5",cpp);
    require(spoken(cpp).find("once barra dos")!=std::string::npos&&spoken(cpp).find("igual")!=std::string::npos,
        "The native reading context did not reach the C++ numeric frontend");
    const std::string fraction="La fracción 3/4 conserva su valor.";
    const auto automatic=worker.plan(fraction,"es",{},"auto","auto");
    const auto text=worker.plan(fraction,"es",{},"auto","text");
    require(!same(automatic,text),"The native explicit mathematical mode was silently ignored");
    worker.shutdown();worker.shutdown();
    std::cout<<"PASS "<<groups<<" real native/JVM output-policy groups, UTF-16 owners, context/mode and identity serialization. "
        "No synthesis, playback, windows or saved listening credit.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL native narration-policy bridge: "<<error.what()<<'\n';return 1;
} catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL native narration-policy Windows bridge: "<<error.code().value<<'\n';return 1;
} catch(...) {
    std::cerr<<"FAIL native narration-policy bridge: unexpected exception\n";return 1;
}
