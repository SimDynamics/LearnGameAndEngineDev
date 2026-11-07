#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"
#include "core/narration_state.hpp"
#include "presentation/windows/narration_legacy_policy.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>

#include <filesystem>
#include <fstream>
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
std::string current_l17_report() {
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_aceptar_registros_y_detectar_duplicados.json";
    std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"Cannot read the actual L17 public report paragraph");
    std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.erase(0,3);
    const auto lesson=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(bytes));
    return winrt::to_string(lesson.GetNamedArray(L"pages").GetAt(3).GetObject().GetNamedArray(L"blocks").GetAt(2).GetObject().GetNamedString(L"text"));
}
struct ComponentSource {std::string lesson,source,owner,previous_mode,mode;std::size_t page=0;};
ComponentSource current_component_source(std::string_view relative,std::size_t page,std::size_t block_index,
        std::size_t response,std::string_view group,std::size_t item) {
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/relative;
    std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"Cannot read a real P1 nested component source");
    std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.erase(0,3);
    const auto lesson=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(bytes));
    const auto block=lesson.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(page)).GetObject()
        .GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(block_index)).GetObject();
    const auto component=block.GetNamedArray(L"responses").GetAt(static_cast<unsigned>(response)).GetObject()
        .GetNamedArray(winrt::to_hstring(group)).GetAt(static_cast<unsigned>(item)).GetObject()
        .GetNamedArray(L"content").GetAt(0).GetObject();
    const auto config=component.HasKey(L"config")?component.GetNamedObject(L"config"):component;
    const auto owner=winrt::to_string(lesson.GetNamedObject(L"stateLayout").GetNamedArray(L"current")
        .GetAt(static_cast<unsigned>(page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(block_index)).GetString());
    return {std::move(bytes),winrt::to_string(config.GetNamedString(L"text")),owner+"/responses/"+std::to_string(response)+"/"+
        std::string(group)+"/"+std::to_string(item)+"/content/0/text",winrt::to_string(block.GetNamedString(L"mathMode",L"auto")),
        winrt::to_string(config.GetNamedString(L"mathMode",L"auto")),page};
}
}

int main() try {
    ensure_winrt_apartment();
    const LaunchOptions options{ProfileId::Codex,L"narration-policy-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    NarrationWorker worker(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
    using Policy=NarrationOutputDataPolicy;
    const auto l17=current_l17_report();
    const std::string report="El informe empieza con `Guardados=2;Descartados=2`, sin punto y coma final.";
    require(l17.find(report)!=std::string::npos,"The current L17 report changed; inspect its exact public text");
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
    for(const std::string_view mode:{"auto","text"})for(const std::string_view context:{"auto","cpp","cpp_previous_parentheses"}) {
        const auto current=worker.plan(l17,"es",{},context,mode,Policy::current);
        const auto b42=worker.plan(l17,"es",{},context,mode,Policy::b42);
        const auto original=worker.plan(l17,"es",{},context,mode,Policy::original);
        owners(l17,current);owners(l17,b42);owners(l17,original);
        require(same(b42,original)&&same(original,worker.plan(l17,"es",{},context,mode,true)),
            "Native typed ORIGINAL or B42 failed to reconstruct the unchanged L17 historical plan");
        require(!same(current,b42)&&spoken(current).find("Guardados igual a dos")!=std::string::npos&&
            spoken(b42).find("Guardados asigna dos")!=std::string::npos,
            "The real L17 report did not distinguish current data equality from historical assignment speech");
        require(current.size()==b42.size(),"A report-policy correction changed sentence boundaries");
        for(std::size_t i=0;i<current.size();++i) {
            require(current[i].text==b42[i].text&&current[i].start==b42[i].start&&current[i].end==b42[i].end,
                "Native report migration changed the visible text or exact source owner");
            if(current[i].text.find("Guardados=2;Descartados=2")==std::string::npos)
                require(same(current[i],b42[i]),"A changed report invalidated an unaffected real L17 neighboring recording");
        }
        const auto modern_key=worker.identity("qa-real-l17-report","es","ef_dora",current);
        const auto b42_key=worker.identity("qa-real-l17-report","es","ef_dora",b42);
        require(modern_key!=b42_key&&b42_key==worker.identity("qa-real-l17-report","es","ef_dora",original),
            "The native L17 policy selector failed to preserve its authentic historical page key");

        const std::string mixed="𐐀 Antes conserva el dato. La salida es `N=1a|1b`. "+report+" Después comprueba el resultado.";
        const auto mixed_current=worker.plan(mixed,"es",{},context,mode,Policy::current);
        const auto mixed_b42=worker.plan(mixed,"es",{},context,mode,Policy::b42);
        const auto mixed_original=worker.plan(mixed,"es",{},context,mode,Policy::original);
        owners(mixed,mixed_current);owners(mixed,mixed_b42);owners(mixed,mixed_original);
        require(mixed_current.size()==4&&mixed_b42.size()==4&&mixed_original.size()==4,
            "The native three-generation page lost an original source sentence");
        for(const auto i:{0u,3u})require(same(mixed_current[i],mixed_b42[i])&&same(mixed_current[i],mixed_original[i]),
            "A policy transition changed an unaffected neighbor's prepared speech or word ranges");
        require(same(mixed_current[1],mixed_b42[1])&&same(mixed_b42[2],mixed_original[2]),
            "Native typed policies did not retain the two independently unchanged historical recordings");
        const auto a=worker.identity("qa-native-three-generations","es","ef_dora",mixed_current);
        const auto b=worker.identity("qa-native-three-generations","es","ef_dora",mixed_b42);
        const auto c=worker.identity("qa-native-three-generations","es","ef_dora",mixed_original);
        require(a.size()==64&&b.size()==64&&c.size()==64&&a!=b&&a!=c&&b!=c,
            "Native IPC collapsed three distinct authenticated normalization generations");
        require(same(mixed_current,worker.plan(mixed,"es",{},context,mode,false))&&
            same(mixed_original,worker.plan(mixed,"es",{},context,mode,true)),
            "A typed historical plan changed either original bool overload contract");

        const std::string protected_source="La instrucción `total = 3;` actualiza el valor. La cadena `\"Inicio: 7\"` conserva sus caracteres.";
        const auto protected_current=worker.plan(protected_source,"es",{},context,mode,Policy::current);
        for(const auto policy:{Policy::b42,Policy::original}) {
            const auto previous=worker.plan(protected_source,"es",{},context,mode,policy);
            owners(protected_source,previous);
            require(same(protected_current,previous),"A typed policy changed genuine assignment/string speech or UTF-16 owners");
            require(worker.identity("qa-native-source-protection","es","ef_dora",protected_current)==
                worker.identity("qa-native-source-protection","es","ef_dora",previous),
                "A typed policy invalidated a protected source recording identity");
        }
        ++groups;
    }
    bool rejected=false;
    try{(void)worker.plan(report,"es",{},"auto","auto",static_cast<Policy>(-1));}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"The native bridge silently coerced an unknown enum to current policy");
    const auto cpp=worker.plan("11/2=5","es",{},"cpp","auto");
    owners("11/2=5",cpp);
    require(spoken(cpp).find("once barra dos")!=std::string::npos&&spoken(cpp).find("igual")!=std::string::npos,
        "The native reading context did not reach the C++ numeric frontend");
    const std::string fraction="La fracción 3/4 conserva su valor.";
    const auto automatic=worker.plan(fraction,"es",{},"auto","auto");
    const auto text=worker.plan(fraction,"es",{},"auto","text");
    require(!same(automatic,text),"The native explicit mathematical mode was silently ignored");
    {
        using namespace lgaed::curriculum;
        const std::string source="𐐀 letras y palabras: y";
        const auto start=wide(source).find_last_of(L'y');
        TextSemantics technical{source,{{start,start+1,"y",SemanticRole::cpp,SemanticReading::automatic}}};
        const auto unmarked=worker.plan(source,"es",{},"cpp","auto");
        const auto marked=worker.plan(source,"es",{},"cpp","auto",Policy::current,technical);
        owners(source,marked);
        require(spoken(marked).find("letras y palabras")!=std::string::npos&&spoken(marked).find("griega")!=std::string::npos,
            "Native optional semantic ranges were lost or spread technical speech into adjacent prose");
        auto natural=technical;natural.spans.front().role=SemanticRole::output;
        const auto output=worker.plan(source,"es",{},"cpp","auto",Policy::current,natural);
        owners(source,output);require(spoken(output).find("griega")==std::string::npos,
            "Native explicit output prose retained an unrelated technical letter policy");
        require(worker.identity("qa-semantic-owner","es","ef_dora",marked)!=worker.identity("qa-semantic-owner","es","ef_dora",output),
            "Distinct semantic pronunciations acquired the same native page key");
        auto same_speech=technical;same_speech.spans.front().reading=SemanticReading::technical;
        const auto equivalent=worker.plan(source,"es",{},"cpp","auto",Policy::current,same_speech);
        require(same(marked,equivalent)&&worker.identity("qa-semantic-owner","es","ef_dora",marked)==
            worker.identity("qa-semantic-owner","es","ef_dora",equivalent),
            "Equivalent authored intent changed actual speech, word owners or an audio identity");
        require(same(unmarked,worker.plan(source,"es",{},"cpp","auto")),
            "Native annotated planning contaminated the subsequent unmarked legacy request");
        const auto rejected_descriptor=[&](const TextSemantics& value){bool caught=false;
            try{(void)worker.plan(source,"es",{},"cpp","auto",Policy::current,value);}catch(const std::invalid_argument&){caught=true;}
            require(caught,"Native IPC accepted a stale, malformed or split-surrogate semantic descriptor");};
        auto stale=technical;stale.source+=' ';rejected_descriptor(stale);
        stale=technical;stale.spans.front().text="another";rejected_descriptor(stale);
        stale=technical;stale.spans.front().start=1;stale.spans.front().end=2;stale.spans.front().text="x";rejected_descriptor(stale);
        for(const auto policy:{Policy::b42,Policy::original})owners(source,worker.plan(source,"es",{},"cpp","auto",policy));
        ++groups;
    }
    {
        using namespace lgaed::curriculum;using SemanticPolicy=NarrationSemanticPolicy;
        const std::string source="Antes conserva el dato. izquierda**derecha. Después conserva el dato.";
        const TextSemantics marked{source,{}};
        const auto current=worker.plan(source,"es",{},"auto","auto",Policy::current,marked);
        const auto original=worker.plan(source,"es",{},"auto","auto",Policy::current,marked,SemanticPolicy::original);
        owners(source,current);owners(source,original);
        require(current.size()==3&&original.size()==3&&same(current[0],original[0])&&same(current[2],original[2])&&
            current[1].prepared!=original[1].prepared,"Semantic V1 authentication changed neighboring speech or failed to distinguish the literal repair");
        const std::string expected_v1="Antes conserva el dato. izquierdaderecha. Después conserva el dato.";
        const auto reference=worker.plan(expected_v1,"es",{},"auto","auto",Policy::current,TextSemantics{expected_v1,{}});
        const auto previous_key=worker.identity("qa-semantic-frontend","es","ef_dora",original);
        const auto current_key=worker.identity("qa-semantic-frontend","es","ef_dora",current);
        require(previous_key!=current_key&&previous_key==worker.identity("qa-semantic-frontend","es","ef_dora",reference)&&
            same(original,worker.plan(source,"es",{},"auto","auto",Policy::current,marked,SemanticPolicy::original)),
            "The original semantic frontend no longer reconstructs its exact historical prepared-text key");
        require(same(current,worker.plan(source,"es",{},"auto","auto",Policy::current,marked)),
            "A private V1 authentication request contaminated current semantic planning");
        const auto unmarked=worker.plan(source,"es");
        require(same(unmarked,worker.plan(source,"es",{},"auto","auto",Policy::current,{},SemanticPolicy::original)),
            "An unannotated historical region changed its existing speech path");
        for(const auto slot:{0u,1u,2u})for(const auto at:{0u,1u,2u}){
            // State-only certificates below are labelled model fixtures; no
            // audio is generated, listened to or attributed to a real learner.
            narration::History history;for(unsigned i=0;i<3;++i)history.entries.push_back({"other-"+std::to_string(i),{},
                narration::Position{0,123+static_cast<std::int64_t>(i),0,false},"other-page-"+std::to_string(i),{}});
            auto& entry=history.entries[slot];entry.key=previous_key;entry.page="qa-semantic-frontend";
            entry.position={at,567,0,false,narration::HeardPrefix{std::string(64,'a'),12000,30000}};
            entry.semantic_plan=narration::SemanticPlanProof{"windows-semantic-v1",{{"exact-owner",source,"auto","auto",marked}}};
            const auto before=history;const bool unchanged=same(current[at],original[at]);
            const auto mapped=presentation::narration_legacy::changed_speech_position(entry.position,unchanged);
            require(presentation::narration_legacy::rekey_authenticated_bookmark(history,previous_key,current_key,mapped,entry.page),
                "An IPC-authenticated semantic history failed to rekey its existing slot");
            history.entries[slot].semantic_plan=narration::SemanticPlanProof{"windows-semantic-v2",{{"exact-owner",source,"auto","auto",marked}}};
            state::StateData data;narration::write_history(data,history);require(narration::read_history(data)==history&&history.entries.size()==3,
                "Native V1/V2 IPC migration lost its original three bounded slots");
            for(unsigned i=0;i<3;++i)if(i!=slot)require(history.entries[i]==before.entries[i],"IPC migration changed another page's saved position");
            if(unchanged)require(history.entries[slot].position==before.entries[slot].position,"Unchanged IPC speech lost a compatible offset/certificate");
            else require(history.entries[slot].position.milliseconds==0&&!history.entries[slot].position.heard_prefix&&
                !history.entries[slot].position.finished,"Changed IPC speech inherited an incompatible recording position");
        }
        bool invalid=false;try{(void)worker.plan(source,"es",{},"auto","auto",Policy::current,marked,static_cast<SemanticPolicy>(-1));}
        catch(const std::invalid_argument&){invalid=true;}require(invalid,"An unknown native semantic frontend policy was silently coerced");
        ++groups;
    }
    {
        // Native content, UI and selected speech languages have independent
        // owners. Planning another voice must not translate the visible verdict.
        const std::string lesson=R"({"id":"qa-native-region-languages","pages":[{"blocks":[{"type":"multiple_choice","question":"Elige.","options":["Uno","Dos"],"correctAnswer":0,"explanation":"Texto válido."},{"type":"math_puzzle","schemaVersion":1,"prompt":"Figura.","responses":[],"feedback":{"correct":"Figura válida."}}]}]})";
        const auto background=[&](std::string_view content){
            return winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(
                worker.curriculum_plan(lesson,0,"es","ef_dora","fr",{},"cpp",content))).GetNamedArray(L"segments");
        };
        const auto contains=[&](const auto& segments,std::string_view source){
            const auto plan=worker.plan(source,"es",{},"cpp");owners(source,plan);
            for(const auto& part:plan){bool found=false;
                for(const auto& candidate:segments)if(winrt::to_string(candidate.GetObject().GetNamedString(L"preparedText"))==part.prepared)found=true;
                require(found,"Native curriculum IPC lost the exact feedback region or its UI/content language");}
        };
        const auto spanish=background("es"),french=background("fr");
        contains(spanish,"✓ Correct\n\nTexto válido.");
        contains(spanish,"✓ Correcto\nFigura válida.");
        contains(french,"✓ Correct\n\nTexto válido.");
        contains(french,"✓ Correct\nFigura válida.");
        ++groups;
    }
    {
        using namespace lgaed::curriculum;
        const std::string source="𐐀 letras y palabras: LaGo!";
        const auto at=wide(source).find_last_of(L'!');
        const TextSemantics literal{source,{{at,at+1,"!",SemanticRole::data,SemanticReading::characters}}};
        auto natural=literal;natural.spans.front().reading=SemanticReading::natural;
        const auto before=worker.plan(source,"es",{},"cpp","auto",Policy::current,natural);
        const auto named=worker.plan(source,"es",{},"cpp","auto",Policy::current,literal);
        owners(source,before);owners(source,named);
        require(spoken(named).find("exclamación")!=std::string::npos&&spoken(named).find("negación")==std::string::npos&&
            spoken(named).find("letras y palabras")!=std::string::npos,
            "Native literal-character intent was lost or became a C++ operator outside its exact data span");
        require(worker.identity("qa-character-intent","es","ef_dora",before)!=worker.identity("qa-character-intent","es","ef_dora",named)&&
            same(before,worker.plan(source,"es",{},"cpp","auto",Policy::current,natural)),
            "Literal-character intent reused incompatible speech or contaminated the following natural request");
        auto same_intent=literal;same_intent.spans.front().role=SemanticRole::output;
        require(same(named,worker.plan(source,"es",{},"cpp","auto",Policy::current,same_intent)),
            "Literal-character intent depended on an arbitrary code/output color category");
        bool original_rejected=false;
        try{(void)worker.plan(source,"es",{},"cpp","auto",Policy::current,literal,NarrationSemanticPolicy::original);}
        catch(const std::exception&){original_rejected=true;}
        require(original_rejected,"A new character intent was falsely attributed to the historical V1 frontend");
        ++groups;
    }
    {
        const std::vector<ComponentSource> sources{
            current_component_source("archivos_y_programa_completo/lesson_precision_y_presentacion_de_double.json",1,2,0,"targets",1),
            current_component_source("repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json",5,4,0,"sources",0),
            current_component_source("archivos_y_programa_completo/lesson_aceptar_registros_y_detectar_duplicados.json",2,4,1,"options",0)};
        require(sources[0].source=="3.125"&&sources[1].source=="ultimo"&&sources[2].source.starts_with("Salida:\n"),
            "The real P1 component-mode sources changed; inspect their original public fields");
        Plan previous_script,current_script;
        narration::SemanticPlanProof previous_proof,current_proof;
        for(std::size_t i=0;i<sources.size();++i){const auto& source=sources[i];
            require(source.previous_mode=="auto"&&source.mode=="text","A nested P1 source no longer owns the tested native text mode");
            const auto previous=worker.plan(source.source,"es",{},"auto",source.previous_mode);
            const auto current=worker.plan(source.source,"es",{},"auto",source.mode);
            owners(source.source,previous);owners(source.source,current);
            require(previous.size()==current.size(),"A component mode changed the exact source segmentation");
            const auto background=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(
                worker.curriculum_plan(source.lesson,source.page,"es","ef_dora","es",{},"cpp","es"))).GetNamedArray(L"segments");
            for(const auto& part:current){bool found=false;
                for(const auto& candidate:background)if(winrt::to_string(candidate.GetObject().GetNamedString(L"preparedText"))==part.prepared)found=true;
                require(found,"The real native component mode lost its corresponding anticipated recording");}
            if(i==0)require(!same(previous,current)&&spoken(previous)=="tres coma uno dos cinco"&&spoken(current)=="3.125",
                "Native IPC lost the real decimal's distinct auto/text pronunciation");
            else require(same(previous,current),"The mode transport changed an unaffected real P1 control passage");
            previous_script.insert(previous_script.end(),previous.begin(),previous.end());
            current_script.insert(current_script.end(),current.begin(),current.end());
            previous_proof.regions.push_back({source.owner,source.source,"auto",source.previous_mode,{}});
            current_proof.regions.push_back({source.owner,source.source,"auto",source.mode,{}});
        }
        const std::string page="qa-component-mode-model";
        const auto previous_key=worker.identity(page,"es","ef_dora",previous_script);
        const auto current_key=worker.identity(page,"es","ef_dora",current_script);
        require(previous_key!=current_key&&previous_proof.policy=="windows-semantic-v2"&&current_proof.policy=="windows-semantic-v2",
            "A changed component mode lost its authenticated page-key/provenance distinction");
        const auto& old_decimal=previous_script.front();const auto& decimal=current_script.front();
        const auto alias=std::string(presentation::narration_legacy::component_mode_prefix)+"auto";
        const presentation::narration_legacy::Passage before{sources[0].owner,sources[0].source,alias,"auto",old_decimal.prepared,old_decimal.start,old_decimal.end};
        const presentation::narration_legacy::Passage after{sources[0].owner,sources[0].source,"auto","text",decimal.prepared,decimal.start,decimal.end};
        require(presentation::narration_legacy::same_previous_passage(before,after),"The authenticated component-mode alias lost its exact original decimal passage");
        auto invalid=before;invalid.context="auto";require(!presentation::narration_legacy::same_previous_passage(invalid,after),
            "A direct mode change borrowed historical compatibility without the authenticated alias");
        invalid=before;invalid.owner="other-owner";require(!presentation::narration_legacy::same_previous_passage(invalid,after),"A component-mode alias crossed owners");
        invalid=before;invalid.source="different";require(!presentation::narration_legacy::same_previous_passage(invalid,after),"A component-mode alias crossed source text");
        invalid=before;++invalid.end;require(!presentation::narration_legacy::same_previous_passage(invalid,after),"A component-mode alias crossed source ranges");
        for(unsigned slot=0;slot<3;++slot)for(std::size_t at=0;at<previous_script.size();++at){
            // This aggregate page and its certificates are explicit state-model
            // fixtures, not learner data or evidence of synthesized/heard audio.
            narration::History history;
            for(unsigned i=0;i<3;++i)history.entries.push_back({"unrelated-"+std::to_string(i),"stable-"+std::to_string(i),
                narration::Position{0,100+static_cast<std::int64_t>(i),0,false},"other-page-"+std::to_string(i),{}});
            auto& entry=history.entries[slot];entry.key=previous_key;entry.page=page;
            entry.position={static_cast<std::int64_t>(at),567,0,at+1==previous_script.size(),
                narration::HeardPrefix{std::string(64,'c'),12000,30000}};
            entry.semantic_plan=previous_proof;history.active_key=previous_key;history.active_owner="headless-model-owner";
            state::StateData data;narration::write_history(data,history);
            require(narration::read_history(data)==history,"The original V2 root-mode proof did not round-trip through the bounded model");
            const auto original=history;const bool compatible=same(previous_script[at],current_script[at]);
            const auto mapped=presentation::narration_legacy::changed_speech_position(entry.position,compatible);
            require(presentation::narration_legacy::rekey_authenticated_bookmark(history,previous_key,current_key,mapped,page),
                "An IPC-authenticated component-mode key failed to rekey its own original slot");
            history.entries[slot].semantic_plan=current_proof;narration::write_history(data,history);
            require(narration::read_history(data)==history&&history.entries.size()==3&&history.active_key==current_key&&
                history.active_owner==original.active_owner,"Component-mode recovery changed the three-slot owner or current V2 proof");
            for(unsigned i=0;i<3;++i)if(i!=slot)require(history.entries[i]==original.entries[i],"Component-mode recovery changed an unrelated bookmark");
            if(compatible)require(history.entries[slot].position==original.entries[slot].position,
                "An unchanged recording lost its existing position/certificate during mode recovery");
            else require(history.entries[slot].position.segment==static_cast<std::int64_t>(at)&&history.entries[slot].position.milliseconds==0&&
                history.entries[slot].position.heard_through==0&&!history.entries[slot].position.heard_prefix&&!history.entries[slot].position.finished,
                "A newly prepared decimal inherited an incompatible offset, terminal state or listening credit");
        }
        ++groups;
    }
    {
        using namespace lgaed::curriculum;
        // Generic QA guidance, planned by the real backend. These are not
        // authored answers, learner progress or evidence of heard recordings.
        const std::string old_source="✓ Correcto\n\nLa guía conserva el resultado.";
        const std::string new_source="✓ Correcto\n\nLa guía verifica cada resultado conservado.";
        const TextSemantics old_semantics{old_source,{}},new_semantics{new_source,{}};
        const narration::SemanticRegionProof saved_guide{"qa-earned-compiler-guide",old_source,"cpp","auto",old_semantics};
        struct LiveRegion {
            std::string anchor,key,source_text,reading_context,math_mode;
            std::optional<std::string> previous_math_mode;
            bool compiler_guidance=false,feedback=false,correct=false,required_compiler=false;
        };
        const LiveRegion live{saved_guide.owner,"qa-current-guide-key",new_source,"cpp","auto",{},true,true,true,true};
        require(presentation::narration_legacy::matches_saved_semantic_region(saved_guide,live),
            "A currently earned compiler guide lost its exact historical source owner");
        const auto old_guide=worker.plan(old_source,"es",{},"cpp","auto",Policy::current,old_semantics);
        const auto current_guide=worker.plan(new_source,"es",{},"cpp","auto",Policy::current,new_semantics);
        const auto replayed=worker.plan(saved_guide.source,"es",{},saved_guide.context,saved_guide.math_mode,Policy::current,saved_guide.semantics);
        owners(old_source,old_guide);owners(new_source,current_guide);owners(saved_guide.source,replayed);
        require(same(old_guide,replayed)&&!same(old_guide,current_guide),
            "Historical compiler guidance was reconstructed from today's replacement rather than its saved exact source");
        const std::string neighbor="Después revisa la salida.";
        const auto control=worker.plan(neighbor,"es",{},"cpp","auto");owners(neighbor,control);
        Plan old_page=old_guide,current_page=current_guide,replayed_page=replayed;
        for(auto* plan:{&old_page,&current_page,&replayed_page})plan->insert(plan->end(),control.begin(),control.end());
        const std::string page="qa-revised-compiler-guide-model";
        const auto old_key=worker.identity(page,"es","ef_dora",old_page);
        const auto replayed_key=worker.identity(page,"es","ef_dora",replayed_page);
        const auto current_key=worker.identity(page,"es","ef_dora",current_page);
        require(old_key==replayed_key&&old_key!=current_key,"A revised compiler guide bypassed full historical page-key authentication");
        const narration::SemanticRegionProof saved_neighbor{"qa-guide-neighbor",neighbor,"cpp","auto",{}};
        for(unsigned slot=0;slot<3;++slot){
            // Synthetic bounded-state certificates exercise preservation only;
            // no offset, certificate or credit comes from the plan-only IPC.
            narration::History history;
            for(unsigned i=0;i<3;++i)history.entries.push_back({"other-guide-model-"+std::to_string(i),"stable-"+std::to_string(i),
                narration::Position{0,200+static_cast<std::int64_t>(i),0,false},"other-page-"+std::to_string(i),{}});
            auto& saved=history.entries[slot];saved.key=old_key;saved.page=page;
            saved.position={static_cast<std::int64_t>(old_page.size()-1),432,static_cast<std::int64_t>(old_page.size()),true,
                narration::HeardPrefix{std::string(64,'d'),5000,9000}};
            saved.semantic_plan=narration::SemanticPlanProof{"windows-semantic-v2",{saved_guide,saved_neighbor}};
            history.active_key=old_key;history.active_owner="headless-guide-model";
            state::StateData data;narration::write_history(data,history);
            require(narration::read_history(data)==history,"Saved compiler-guide source provenance did not round-trip exactly");
            const auto before=history;
            auto mapped=presentation::narration_legacy::changed_speech_position(saved.position,false);
            // The typed revised guide is the first region of this model page.
            // Its existing revision boundary is revisited, even after finishing.
            mapped.segment=0;mapped.heard_through=0;
            require(presentation::narration_legacy::rekey_authenticated_bookmark(history,replayed_key,current_key,mapped,page),
                "Authenticated historical compiler guidance failed to rekey its own existing slot");
            history.entries[slot].semantic_plan=narration::SemanticPlanProof{"windows-semantic-v2",{
                {saved_guide.owner,new_source,"cpp","auto",new_semantics},saved_neighbor}};
            narration::write_history(data,history);
            require(narration::read_history(data)==history&&history.entries.size()==3&&history.active_key==current_key,
                "Revised compiler guidance lost its current source proof or created a fourth bookmark");
            for(unsigned i=0;i<3;++i)if(i!=slot)require(history.entries[i]==before.entries[i],"A revised guide changed another stored pause");
            const auto& restored=history.entries[slot].position;
            require(restored.segment==0&&restored.milliseconds==0&&restored.heard_through==0&&!restored.finished&&!restored.heard_prefix&&
                history.entries[slot].stable==before.entries[slot].stable&&history.active_owner==before.active_owner,
                "A replacement guide inherited old milliseconds, a recording certificate or listening credit");
        }
        ++groups;
    }
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
