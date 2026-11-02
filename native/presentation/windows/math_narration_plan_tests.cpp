#include "platform/windows/narration_worker.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace presentation;
using namespace winrt::Windows::Data::Json;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string raw(const JsonObject& value){return winrt::to_string(value.Stringify());}
void put(JsonObject& value,const wchar_t* key,std::string_view text){value.Insert(key,JsonValue::CreateStringValue(wide(text)));}
std::string canonical(std::string_view text){
    std::string result;for(const unsigned char character:text){
        if(character>='A'&&character<='Z')result+=static_cast<char>(character-'A'+'a');
        else if((character>='a'&&character<='z')||(character>='0'&&character<='9'))result+=static_cast<char>(character);
    }return result;
}
void pump(){MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE))if(event.message!=WM_QUIT){TranslateMessage(&event);DispatchMessageW(&event);}}
struct Host {
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Isolated math narration plan QA",WS_OVERLAPPEDWINDOW,
        0,0,1150,1000,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ~Host(){if(window)DestroyWindow(window);}
};
JsonObject figure(std::string_view vertex,std::string_view first,std::string_view second){
    JsonObject result,diagram;JsonArray points,objects;
    const std::array<std::pair<double,double>,3> locations{{{0,0},{4,0},{2,3}}};
    const std::array<std::string_view,3> names{vertex,first,second};
    for(std::size_t at=0;at<names.size();++at){JsonObject point;put(point,L"id",names[at]);
        point.Insert(L"x",JsonValue::CreateNumberValue(locations[at].first));point.Insert(L"y",JsonValue::CreateNumberValue(locations[at].second));points.Append(point);}
    for(std::size_t at=0;at<2;++at){JsonObject ray;put(ray,L"id",at?"second":"first");put(ray,L"kind","ray");JsonArray through;
        through.Append(JsonValue::CreateStringValue(wide(vertex)));through.Append(JsonValue::CreateStringValue(wide(at?second:first)));
        ray.Insert(L"through",through);ray.Insert(L"contains",through);objects.Append(ray);}
    diagram.Insert(L"points",points);diagram.Insert(L"objects",objects);result.Insert(L"diagram",diagram);
    put(result,L"vertex",vertex);result.Insert(L"sides",JsonArray::Parse(LR"(["first","second"])"));put(result,L"region","counterclockwise");return result;
}
JsonObject puzzle(bool rigid){
    auto block=JsonObject::Parse(LR"({"type":"math_puzzle","id":"privadobloquezafiro","prompt":"Construye la figura indicada.","responses":[{"id":"privadorespuestazafiro"}],"feedback":{"correct":"La figura conserva las condiciones pedidas.","incorrect":"privadoerrorzafiro"}})");
    JsonObject config;
    if(rigid){
        config=JsonObject::Parse(LR"({"type":"rigid_angle_copy","relation":"congruent","initial":{"x":0.6,"y":-0.45,"rotation":1.1},"positionTolerance":0.071828,"directionTolerance":0.0314159})");
        config.Insert(L"reference",figure("O","A","B"));config.Insert(L"copy",figure("V","C","D"));
    }else config=JsonObject::Parse(LR"({"type":"segment_transport","mode":"chain","sources":[{"id":"ab","from":{"x":0,"y":3,"label":"A"},"to":{"x":4,"y":3,"label":"B"}}],"target":{"origin":{"x":0,"y":0,"label":"M"},"direction":{"x":1,"y":0}},"steps":["ab","ab"],"pointLabels":["N","P"],"positionTolerance":0.071828,"lengthTolerance":0.0314159})");
    auto response=block.GetNamedArray(L"responses").GetObjectAt(0);
    put(response,L"type",rigid?"math_rigid_angle_copy":"math_segment_transport");response.Insert(L"validator",config);return block;
}
JsonObject lesson(const JsonObject& block){
    JsonObject result,page;JsonArray blocks,pages;put(result,L"id","qa_math_narration_plan");put(result,L"title","");
    blocks.Append(block);page.Insert(L"blocks",blocks);pages.Append(page);result.Insert(L"pages",pages);return result;
}
struct Segment {
    std::string text,prepared,key;
    bool operator==(const Segment&)const=default;
};
std::vector<Segment> plan(windows::NarrationWorker& worker,const JsonObject& content,std::string_view language,
        std::string_view voice,std::string_view ui){
    const auto reply=JsonObject::Parse(wide(worker.curriculum_plan(raw(content),0,language,voice,ui)));
    std::vector<Segment> result;
    for(const auto& value:reply.GetNamedArray(L"segments")){const auto part=value.GetObject();
        result.push_back({winrt::to_string(part.GetNamedString(L"text")),winrt::to_string(part.GetNamedString(L"preparedText")),winrt::to_string(part.GetNamedString(L"key"))});
        require(!result.back().text.empty()&&!result.back().prepared.empty()&&result.back().key.size()==64,
            "The real curriculum planner returned an empty segment or missing cache key");}
    return result;
}
struct Locale{const char* ui;const char* speech;const char* voice;};
void contrast(HWND host,windows::NarrationWorker& worker,bool rigid,const Locale& locale){
    const auto block=puzzle(rigid);int changes=0,drafts=0,submissions=0;
    MathPuzzleCallbacks callbacks;callbacks.changed=[&]{++changes;};callbacks.draft_changed=[&]{++drafts;};callbacks.submit=[&](std::string){++submissions;};
    callbacks.expression_request=[&](std::string_view request){return worker.math_editor(request);};
    MathPuzzleView view(host,raw(block),locale.ui,"",callbacks);view.layout(0,0,1000);pump();
    const auto before=view.answers_json(),identity=view.answer_identity_json(),original_block=raw(block);
    const auto initial_changes=changes,initial_drafts=drafts,initial_submissions=submissions;
    const auto regions=view.reading_regions();const auto complete=plan(worker,lesson(block),locale.speech,locale.voice,locale.ui);
    const std::string context=std::string(rigid?"rigid_angle":"segment_transport")+"/"+locale.ui;
    require(!complete.empty(),context+": the full mathematics page has no narration");
    // Correct feedback is deliberately pregenerated. IDs, validation metadata
    // and the unavailable rejection guide must remain private. Alphabetic
    // probes avoid underscore pronunciation; canonicalization also detects
    // inserted spaces or changed case instead of hiding a leak behind G2P.
    for(const auto& part:complete)for(const auto* hidden:{"privadobloquezafiro","privadorespuestazafiro","privadoerrorzafiro","positionTolerance","directionTolerance","lengthTolerance","0.071828","0.0314159"})
        require(canonical(part.text).find(canonical(hidden))==std::string::npos&&canonical(part.prepared).find(canonical(hidden))==std::string::npos,
            context+": hidden validation data reached narration");
    std::size_t next=0,checked=0;
    for(const auto* suffix:{"/help","/legend"}){
        const auto region=std::ranges::find_if(regions,[&](const auto& value){return value.key.ends_with(suffix);});
        require(region!=regions.end()&&std::ranges::count_if(regions,[&](const auto& value){return value.key.ends_with(suffix);})==1,
            context+": expected one actual help/legend reading region");
        require(region->target&&IsWindow(region->target)&&IsChild(view.window(),region->target)&&!region->source_text.empty()
            &&edit_text(region->target)==region->source_text,context+": HWND text and actual reading source disagree");
        JsonObject paragraph;put(paragraph,L"type","paragraph");put(paragraph,L"text",region->source_text);
        const auto expected=plan(worker,lesson(paragraph),locale.speech,locale.voice,locale.ui);
        require(!expected.empty(),context+": actual visible help/legend produced no real speech segments");
        // The independent oracle is the live native HWND's exact source. No
        // translated worker constants, inferred SHA or synthesized answer is used.
        const auto found=std::search(complete.begin()+static_cast<std::ptrdiff_t>(next),complete.end(),expected.begin(),expected.end());
        if(found==complete.end()){
            std::cerr<<"Mismatch "<<context<<suffix<<": expected "<<expected.size()<<" visible segments, full page "<<complete.size()<<'\n';
            for(const auto& part:expected)std::cerr<<"  expected text="<<part.text<<" key="<<part.key<<'\n';
            for(const auto& part:complete)std::cerr<<"  actual text="<<part.text<<" key="<<part.key<<'\n';
        }
        require(found!=complete.end(),context+suffix+": advance plan differs from native help/legend text, prepared speech or cache keys");
        require(std::search(found+1,complete.end(),expected.begin(),expected.end())==complete.end(),context+": duplicated help/legend in advance plan");
        next=static_cast<std::size_t>(found-complete.begin())+expected.size();checked+=expected.size();
    }
    require(view.answers_json()==before&&view.answer_identity_json()==identity&&raw(block)==original_block
        &&changes==initial_changes&&drafts==initial_drafts&&submissions==initial_submissions,
        context+": read-only narration planning changed the real mathematics response or submitted an answer");
    std::cout<<"PASS "<<context<<": native help/legend match "<<checked<<" actual prepared segments and cache keys; hidden answers and draft untouched\n";
}
}
int main(){
    try{
        windows::ensure_winrt_apartment();reader_theme::prepare_fonts();INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};
        require(InitCommonControlsEx(&common)!=FALSE,"Cannot initialize mathematics QA controls");Host host;require(host.window,"Cannot create isolated mathematics narration host");
        windows::NarrationWorker worker(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
        for(const bool rigid:{false,true})for(const auto& locale:std::array<Locale,4>{{{"es","es","ef_dora"},{"en","en-US","af_heart"},{"fr","fr","ff_siwis"},{"en-GB","en-GB","bf_emma"}}})
            contrast(host.window,worker,rigid,locale);
        std::cout<<"PASS native math narration planning in four locales; no synthesis, personal profile or curricular acceptance claimed\n";return 0;
    }catch(const winrt::hresult_error& error){std::cerr<<"FAIL math narration plan: "<<winrt::to_string(error.message())<<'\n';}
     catch(const std::exception& error){std::cerr<<"FAIL math narration plan: "<<error.what()<<'\n';}
    return 1;
}
