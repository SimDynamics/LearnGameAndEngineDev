#include "core/attempts.hpp"
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <numbers>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
constexpr double pi=std::numbers::pi;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string text(winrt::hstring value){return winrt::to_string(value);}
void put(JsonObject& object,const wchar_t* key,double value){object.SetNamedValue(key,JsonValue::CreateNumberValue(value));}
void put(JsonObject& object,const wchar_t* key,std::string_view value){object.SetNamedValue(key,JsonValue::CreateStringValue(wide(value)));}
void pump(){MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE))if(event.message!=WM_QUIT){TranslateMessage(&event);DispatchMessageW(&event);}}
template<class Predicate>void wait_for(Predicate predicate,const char* message){
    const auto until=GetTickCount64()+30000;
    do{pump();if(predicate())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<until);
    throw std::runtime_error(message);
}
std::vector<HWND> children(HWND parent){
    std::vector<HWND> result;EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{
        reinterpret_cast<std::vector<HWND>*>(raw)->push_back(item);return TRUE;
    },reinterpret_cast<LPARAM>(&result));return result;
}
std::wstring caption(HWND item){
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(item))+1,L'\0');
    value.resize(GetWindowTextW(item,value.data(),static_cast<int>(value.size())));return value;
}
HWND named(HWND parent,std::wstring_view name){for(const auto item:children(parent))if(caption(item)==name)return item;return nullptr;}
HWND by_id(HWND parent,int id){for(const auto item:children(parent))if(GetDlgCtrlID(item)==id)return item;return nullptr;}
HWND canvas(HWND parent){
    for(const auto item:children(parent)){wchar_t kind[100]{};GetClassNameW(item,kind,100);
        if(std::wstring_view(kind)==L"LearnGameAndEngineDev.MathCanvas")return item;}
    throw std::runtime_error("The rigid construction has no native canvas");
}
void click(HWND parent,std::wstring_view name){
    const auto item=named(parent,name);require(item&&IsWindowEnabled(item),"A required rigid-copy control is missing or disabled");
    SendMessageW(item,BM_CLICK,0,0);pump();
}
POINT pixel(const JsonObject& point){return{static_cast<LONG>(std::lround(point.GetNamedNumber(L"x"))),static_cast<LONG>(std::lround(point.GetNamedNumber(L"y")))};}
POINT marker(const JsonObject& drawing,std::string_view id){
    for(const auto& value:drawing.GetNamedArray(L"points")){const auto point=value.GetObject();
        if(point.GetNamedString(L"id")==wide(id))return pixel(point.GetNamedObject(L"center"));}
    throw std::runtime_error("A required given point disappeared: "+std::string(id));
}
void drag(HWND target,POINT from,POINT to){
    SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(from.x,from.y));
    SendMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(to.x,to.y));
    SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(to.x,to.y));pump();
}
std::vector<std::uint32_t> pixels(HWND canvas){
    RECT bounds{};GetClientRect(canvas,&bounds);require(bounds.right>0&&bounds.bottom>0,"Native figure has no paintable area");
    const auto screen=GetDC(canvas),buffer=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;
    info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* data=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&data,nullptr,0);
    ReleaseDC(canvas,screen);require(buffer&&bitmap&&data,"Cannot inspect the actual native rigid-copy pixels");
    const auto old=SelectObject(buffer,bitmap);SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT|PRF_ERASEBKGND);
    GdiFlush();const auto* begin=static_cast<const std::uint32_t*>(data);
    std::vector<std::uint32_t> result(begin,begin+static_cast<std::size_t>(bounds.right)*bounds.bottom);
    for(auto& color:result)color&=0xFFFFFF; // BI_RGB's unused high byte is not painted evidence.
    SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);return result;
}
JsonObject figure(std::string_view vertex,std::string_view first,std::string_view second,
        double angle,double sweep,double first_length,double second_length,bool reverse=false){
    JsonObject result,diagram;JsonArray points,objects;
    const auto point=[&](std::string_view id,double x,double y){JsonObject item;put(item,L"id",id);put(item,L"x",x);put(item,L"y",y);
        JsonArray labels;labels.Append(JsonValue::CreateStringValue(wide(id)));item.SetNamedValue(L"labels",labels);points.Append(item);};
    point(vertex,0,0);point(first,std::cos(angle)*first_length,std::sin(angle)*first_length);
    point(second,std::cos(angle+sweep)*second_length,std::sin(angle+sweep)*second_length);
    for(const auto& [id,end]:std::vector<std::pair<std::string,std::string>>{{"first",std::string(first)},{"second",std::string(second)}}){
        JsonObject item;put(item,L"id",id);put(item,L"kind","ray");JsonArray through;
        through.Append(JsonValue::CreateStringValue(wide(vertex)));through.Append(JsonValue::CreateStringValue(wide(end)));
        item.SetNamedValue(L"through",through);item.SetNamedValue(L"contains",through);objects.Append(item);
    }
    diagram.SetNamedValue(L"points",points);diagram.SetNamedValue(L"objects",objects);result.SetNamedValue(L"diagram",diagram);
    put(result,L"vertex",vertex);result.SetNamedValue(L"sides",JsonArray::Parse(reverse?LR"(["second","first"])":LR"(["first","second"])"));
    put(result,L"region",(sweep>0)!=reverse?"counterclockwise":"clockwise");return result;
}
struct Case {
    std::string name,relation;double reference_angle,reference_width,copy_width;bool clockwise=false,reverse=false;
    double copy_sweep()const{return(clockwise?-1.:1.)*(reverse?-copy_width:copy_width);}
    double expected_rotation()const{return reference_angle-(.65+(reverse?copy_sweep():0));}
    std::string first_copy_point()const{return reverse?"copy:D":"copy:C";}
    JsonObject block()const{
        JsonObject spec;put(spec,L"type","rigid_angle_copy");
        spec.SetNamedValue(L"reference",figure("O","A","B",reference_angle,(clockwise?-1.:1.)*reference_width,4,3.6));
        spec.SetNamedValue(L"copy",figure("V","C","D",.65,copy_sweep(),2.4,2.8,reverse));
        JsonObject initial;put(initial,L"x",.6);put(initial,L"y",-.45);put(initial,L"rotation",expected_rotation()+pi);
        spec.SetNamedValue(L"initial",initial);put(spec,L"relation",relation);put(spec,L"positionTolerance",.14);put(spec,L"directionTolerance",.045);
        auto result=JsonObject::Parse(LR"({"type":"math_puzzle","id":"qa_rigid_free_copy","prompt":"Traslada y gira la copia conservando sus lados y su región.","responses":[{"id":"copy","type":"math_rigid_angle_copy"}],"feedback":{"correct":"La copia conserva las condiciones geométricas pedidas.","incorrect":"Revisa la condición indicada."}})");
        result.GetNamedArray(L"responses").GetObjectAt(0).SetNamedValue(L"validator",spec);return result;
    }
};
const std::vector<Case> cases{
    {"inside-ccw","copy_inside_reference",.35,1.2,.55,false,false},
    {"inside-cw","copy_inside_reference",-.35,1.2,.55,true,false},
    {"contains-ccw","reference_inside_copy",.35,.55,1.2,false,false},
    {"contains-cw","reference_inside_copy",-.35,.55,1.2,true,false},
    {"reversed-ccw","congruent",.35,1.,1.,false,true},
    {"reversed-cw","congruent",-.35,1.,1.,true,true}
};
struct Bridge {
    windows::NarrationWorker worker{std::filesystem::path(build::source_root)/"out/narration/runtime.json"};
    JsonObject drawing;
    std::string request(std::string_view raw){
        const auto action=JsonObject::Parse(wide(raw)).GetNamedString(L"action",L"");
        const auto reply=worker.math_editor(raw);if(action==L"rigid_layout")drawing=JsonObject::Parse(wide(reply));return reply;
    }
    MathPuzzleCallbacks callbacks(int& changes){MathPuzzleCallbacks result;result.changed=[&changes]{++changes;};
        result.expression_request=[this](std::string_view raw){return request(raw);};return result;}
    JsonObject evaluate(const JsonObject& block,const MathPuzzleView& view){return JsonObject::Parse(wide(worker.validate_math(text(block.Stringify()),view.answers_json(),"es")));}
};
struct Host {
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Isolated native rigid-angle QA",WS_OVERLAPPEDWINDOW,
        0,0,1150,1000,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ~Host(){if(window)DestroyWindow(window);}
};
std::string reference_geometry(const JsonObject& drawing){
    JsonArray result;
    for(const auto key:{L"points",L"paths",L"arrows",L"regions"})for(const auto& value:drawing.GetNamedArray(key)){
        const auto part=value.GetObject();if(text(part.GetNamedString(L"id")).starts_with("reference:"))result.Append(part);
    }
    return text(result.Stringify());
}
void inspect_drawing(const JsonObject& drawing,const Case& task){
    require(drawing.GetNamedArray(L"arrows").Size()==4&&drawing.GetNamedArray(L"regions").Size()==2,
        "A rigid comparison lost one of its complete directed rays or selected regions");
    std::set<std::string> regions;
    for(const auto& value:drawing.GetNamedArray(L"regions")){
        const auto region=value.GetObject();const auto id=text(region.GetNamedString(L"id"));
        require(id=="reference:"||id=="copy:","A rigid figure returned an unknown selected region");regions.insert(id);
        const bool reference=id=="reference:";
        const auto expected_sweep=(task.clockwise?1.:-1.)*(reference?task.reference_width:task.copy_width)*180/pi;
        const auto expected_first=reference?task.reference_angle:
            .65+(task.reverse?task.copy_sweep():0)+drawing.GetNamedObject(L"placement").GetNamedNumber(L"rotation");
        require(std::abs(region.GetNamedNumber(L"sweepDegrees")-expected_sweep)<1e-6,
            "The native selected region reversed CW/CCW or changed its independently specified aperture");
        require(std::abs(std::remainder(region.GetNamedNumber(L"startDegrees")+expected_first*180/pi,360.))<1e-6,
            "The native region started from the wrong corresponding directed side");
    }
    require(regions.size()==2,"The native drawing duplicated one region and omitted the other");
}
void move_to_reference(HWND parent,Bridge& bridge){
    click(parent,L"Mover copia");drag(canvas(parent),pixel(bridge.drawing.GetNamedObject(L"copyCenter")),marker(bridge.drawing,"reference:O"));
}
void align_first_side(HWND parent,Bridge& bridge,const Case& task){
    click(parent,L"Girar copia");const auto center=bridge.drawing.GetNamedObject(L"copyCenter");
    const auto from=marker(bridge.drawing,task.first_copy_point());const auto origin=marker(bridge.drawing,"reference:O"),given=marker(bridge.drawing,"reference:A");
    const auto cx=center.GetNamedNumber(L"x"),cy=center.GetNamedNumber(L"y");
    const auto radius=std::hypot(from.x-cx,from.y-cy),length=std::hypot(given.x-origin.x,given.y-origin.y);
    require(radius>24&&length>24,"Native scale made the free rotation gesture unusable");
    const POINT to{static_cast<LONG>(std::lround(cx+radius*(given.x-origin.x)/length)),
        static_cast<LONG>(std::lround(cy+radius*(given.y-origin.y)/length))};
    drag(canvas(parent),from,to);
}
void no_answer_leak(Bridge& bridge,const JsonObject& block){
    const auto described=JsonObject::Parse(wide(bridge.worker.describe_math(text(block.Stringify()),"es")));
    const auto config=described.GetNamedArray(L"responses").GetObjectAt(0).GetNamedObject(L"config");
    for(const auto key:{L"validator",L"expected",L"solution",L"positionTolerance",L"directionTolerance"})
        require(!config.HasKey(key),"The visible response description exposed a private solution or evaluator");
}
void no_solution_coordinates(const IJsonValue& value){
    if(value.ValueType()==JsonValueType::Object){
        for(const auto& entry:value.GetObject()){
            for(const auto key:{L"expected",L"solution",L"placement",L"referencePlacement",L"rotation",L"x",L"y"})
                require(entry.Key()!=key,"Qualitative feedback exposed a solved transformation or coordinate");
            no_solution_coordinates(entry.Value());
        }
    }else if(value.ValueType()==JsonValueType::Array)for(const auto& entry:value.GetArray())no_solution_coordinates(entry);
}
void native_matrix(HWND host,Bridge& bridge){
    for(const auto& task:cases)for(const auto narrow:{false,true}){
        std::cout<<"Checking native "<<task.name<<(narrow?" 400px/150%":" 850px/100%")<<'\n';
        const auto block=task.block();const auto original=text(block.Stringify());no_answer_leak(bridge,block);int changes=0;
        MathPuzzleView view(host,original,"es","",bridge.callbacks(changes));view.set_text_scale(narrow?150:100);view.layout(0,0,narrow?400:850);
        const auto plot=canvas(view.window());inspect_drawing(bridge.drawing,task);
        require(bridge.drawing.GetNamedArray(L"points").Size()==6,"The initial free copy acquired extra axes/answer markers or lost its given points");
        std::set<std::string> names;for(const auto& value:bridge.drawing.GetNamedArray(L"labels"))names.insert(text(value.GetObject().GetNamedString(L"text")));
        for(const auto name:{"O","A","B","V","C","D"})require(names.contains(name),"An initial native figure lost a complete given point name");
        for(const auto item:children(view.window())){wchar_t kind[80]{};GetClassNameW(item,kind,80);
            if(std::wstring_view(kind).starts_with(L"RichEdit"))require((GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY)!=0,"A free rigid copy exposed coordinate editors");}
        const auto reference=reference_geometry(bridge.drawing),initial=view.answers_json();const auto before=pixels(plot);
        require(pixels(plot)==before,"Painting an unchanged rigid figure did not produce stable RGB pixels");
        require(changes==0,"Rendering or describing a new copy already changed its answer");
        move_to_reference(view.window(),bridge);require(view.answers_json()!=initial&&changes>0,"A real native drag did not move the copy freely");
        const auto opposite=bridge.evaluate(block,view);
        require(opposite.GetNamedBoolean(L"evaluable")&&!opposite.GetNamedBoolean(L"correct")&&
            opposite.GetNamedArray(L"results").GetObjectAt(0).GetNamedString(L"code")==L"math_rigid_review_direction",
            "Collinear opposite rays were accepted or received feedback for a different condition");
        no_solution_coordinates(opposite);require(opposite.GetNamedArray(L"results").GetObjectAt(0).GetNamedArray(L"arguments").Size()==0,
            "Direction feedback supplied a numerical solution instead of a qualitative condition");
        require(reference_geometry(bridge.drawing)==reference,"Moving a copy changed its fixed reference or selected reference region");
        const auto unreviewed=view.answers_json();
        {MathPuzzleView draft(host,original,"es",unreviewed,bridge.callbacks(changes));draft.set_text_scale(narrow?150:100);draft.layout(0,0,narrow?400:850);
            require(draft.answers_json()==unreviewed,"Recreating a partial opposite-ray attempt changed its exact placement");}
        align_first_side(view.window(),bridge,task);inspect_drawing(bridge.drawing,task);
        const auto accepted=bridge.evaluate(block,view);
        require(accepted.GetNamedBoolean(L"evaluable")&&accepted.GetNamedBoolean(L"correct"),"A freely aligned valid inclusion/reversed correspondence was rejected");
        const auto placement=JsonObject::Parse(wide(view.answers_json())).GetNamedObject(L"copy");
        require(std::hypot(placement.GetNamedNumber(L"x"),placement.GetNamedNumber(L"y"))<.14&&
            std::abs(std::remainder(placement.GetNamedNumber(L"rotation")-task.expected_rotation(),2*pi))<.045,
            "The native gestures did not independently reach the intended rigid placement");
        require(reference_geometry(bridge.drawing)==reference&&pixels(plot)!=before,"Rotation deformed/reframed the reference or failed to paint the moved copy");
        const auto solved=view.answers_json();click(view.window(),L"Vista");
        SendMessageW(plot,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(100,100));
        require(GetParent(plot)!=view.window(),"The native rigid figure did not expand with its controls");
        const auto expanded=pixels(plot);SendMessageW(plot,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(100,100));
        const auto zoomed=pixels(plot);require(zoomed!=expanded,"The existing expanded native zoom did not change presentation");
        drag(plot,{100,100},{120,110});require(view.answers_json()==solved,"Viewport navigation changed the geometric answer");
        require(pixels(plot)!=zoomed,"The native view-mode pan did not affect presentation");
        SendMessageW(plot,WM_KEYDOWN,VK_ESCAPE,0);require(GetParent(plot)==view.window()&&view.answers_json()==solved,
            "Closing the expanded view lost the native canvas or changed the placement");
        MathPuzzleView reopened(host,original,"es",solved,bridge.callbacks(changes));reopened.layout(0,0,narrow?400:850);
        require(reopened.answers_json()==solved&&bridge.evaluate(block,reopened).GetNamedBoolean(L"correct"),"Recreating the valid placement changed its answer or geometric verdict");
        click(view.window(),L"Restablecer copia inicial");require(view.answers_json()==initial,"Reset did not restore the authored unsolved copy");
        require(text(block.Stringify())==original,"Native integration QA mutated its source contract");
    }
    std::cout<<"PASS both inclusion directions and explicit reversed correspondence, CW/CCW regions, opposite directed rays, native gestures/rendering/view/reset and exact recreation\n";
}
void lesson_persistence(HWND host,Bridge& bridge,const Case& task){
    std::cout<<"Checking lesson persistence "<<task.name<<'\n';const auto block=task.block();
    auto document=JsonObject::Parse(LR"({"id":"qa_rigid__unit__lesson","title":"Copia rígida QA","pages":[{"blocks":[]}]})");
    document.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks").Append(block);
    auto lesson=windows::parse_lesson_json(text(document.Stringify()));
    lesson.source_path=std::filesystem::path(build::source_root)/"out/qa/math_physics/rigid-technical-only.json";
    const auto& activity=lesson.pages[0].blocks[0];
    const auto token=L"rigid-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::wstring(task.name.begin(),task.name.end());
    WindowsEnvironment environment{{ProfileId::Codex,token,false},
        make_profile_paths(std::filesystem::path(build::source_root)/"out/qa",ProfileId::Codex,token),false};
    require(!std::filesystem::exists(environment.paths.root),"Rigid QA requires a new isolated profile");initialize_profile_directories(environment.paths);
    {state::StateStore store(environment.paths.root);auto data=store.load();data.preferences["unrelated_rigid_qa"]["keep"]=std::string("original");store.save(data);}
    HWND view=nullptr;std::atomic<int> evaluations{0};std::mutex result_mutex;JsonObject last_result;
    const auto open=[&]{LessonViewHooks hooks;
        hooks.math_editor_request=[&](std::string_view raw){return bridge.request(raw);};
        hooks.validate_math=[&](std::string_view raw,std::string_view answers,std::string_view language,std::stop_token stop){
            const auto reply=bridge.worker.validate_math(raw,answers,language,stop);
            {const std::lock_guard lock(result_mutex);last_result=JsonObject::Parse(wide(reply));}++evaluations;return reply;
        };
        view=create_lesson_view(host,environment,lesson,std::move(hooks));require(view,"Cannot mount the isolated rigid-angle lesson");
        MoveWindow(view,0,0,1100,920,TRUE);pump();
    };
    const auto saved=[&]{require(flush_lesson_view(view),"Cannot save the isolated rigid-copy lesson");return read_lesson_state(view);};
    const auto response=[&]{const auto answer=state::get_response(saved(),lesson,activity);require(answer.has_value(),"The rigid placement was not saved");return *answer;};
    const auto reserve=[&]{return attempts::number(saved(),"reserve_math_physics",attempts::maximum("math_physics"));};
    const auto answers=[&]{return text(JsonObject::Parse(wide(response().raw_json)).GetNamedObject(L"responses").Stringify());};
    const auto close=[&]{require(flush_lesson_view(view),"Cannot flush the rigid QA before reopening");DestroyWindow(view);view=nullptr;pump();};
    try{
        open();move_to_reference(view,bridge);const auto draft=answers();
        require(!response().correct&&response().reviewed_answer_identity.empty(),"A native placement was automatically graded");
        close();open();require(answers()==draft&&response().reviewed_answer_identity.empty(),"Reopening graded or changed the unreviewed rigid placement");
        auto before=evaluations.load();SendMessageW(by_id(view,1003),BM_CLICK,0,0);
        wait_for([&]{return evaluations.load()>before&&!response().reviewed_answer_identity.empty();},"Opposite-ray checking did not produce a real reviewed response");
        {const std::lock_guard lock(result_mutex);require(last_result.GetNamedArray(L"results").GetObjectAt(0).GetNamedString(L"code")==L"math_rigid_review_direction", "The lesson did not preserve directed-ray feedback");}
        const auto rejected_feedback=edit_text(by_id(view,1004));const auto used=reserve();
        require(!response().correct&&used==attempts::maximum("math_physics")-1,"An incorrect rigid attempt did not charge exactly one ordinary attempt");
        click(view,L"Vista");drag(canvas(view),{120,100},{145,115});
        require(answers()==draft&&edit_text(by_id(view,1004))==rejected_feedback&&reserve()==used,"View navigation changed a reviewed rigid answer, feedback or attempts");
        close();open();wait_for([&]{return edit_text(by_id(view,1004))==rejected_feedback;},"Reopening lost the rigid rejection feedback");
        require(answers()==draft&&state::response_reviewed(response(),activity)&&reserve()==used,"Reopening changed the reviewed placement or charged again");
        align_first_side(view,bridge,task);require(response().reviewed_answer_identity.empty(),"Editing a rigid placement retained an obsolete review");
        SendMessageW(by_id(view,1003),BM_CLICK,0,0);
        wait_for([&]{return response().correct&&caption(by_id(view,1003)).starts_with(L"Correcto");},"The freely corrected inclusion/reversed placement was not accepted in the real lesson");
        const auto solved=answers(),feedback=edit_text(by_id(view,1004));require(reserve()==used,"Correcting the construction charged another attempt");
        close();open();wait_for([&]{return caption(by_id(view,1003)).starts_with(L"Correcto");},"Reopening failed to revalidate the rigid success");
        require(response().correct&&answers()==solved&&edit_text(by_id(view,1004))==feedback&&reserve()==used,"Reopening changed success, exact placement, feedback or attempts");
        require(state::string_value(saved(),"unrelated_rigid_qa","keep")=="original","Rigid QA changed unrelated preferences");
        click(view,L"Reiniciar");require(!response().correct&&!JsonObject::Parse(wide(response().raw_json)).HasKey(L"responses"),"Explicit reset retained the solved rigid answer");
        close();open();require(!response().correct&&!JsonObject::Parse(wide(response().raw_json)).HasKey(L"responses")&&reserve()==used,
            "Reopening after Reset resurrected the solved placement or changed the reserve");close();
    }catch(...){if(view)DestroyWindow(view);throw;}
    std::cout<<"PASS lesson "<<task.name<<": unreviewed/rejected/correct/reset exact placements, qualitative feedback, reopen and stable attempts\n";
}
}
int main(){
    std::cout<<std::unitbuf;
    try{
        windows::ensure_winrt_apartment();INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
        const auto rich=LoadLibraryW(L"Msftedit.dll");require(rich,"Cannot load the Windows RichEdit implementation");
        Host host;require(host.window,"Cannot create the private native rigid-copy QA host");Bridge bridge;
        native_matrix(host.window,bridge);
        for(const auto index:{0u,2u,4u})lesson_persistence(host.window,bridge,cases.at(index));
        std::cout<<"Native rigid-copy integration PASS; no curricular assets or personal profiles were modified, and human visual/listening acceptance remains separate.\n";return 0;
    }catch(const winrt::hresult_error& error){std::cerr<<"FAIL WinRT: "<<text(error.message())<<'\n';}
    catch(const std::exception& error){std::cerr<<"FAIL rigid copy: "<<error.what()<<'\n';}
    return 1;
}
