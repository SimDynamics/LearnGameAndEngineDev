#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "core/view_preferences.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <functional>
#include <array>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace lgaed;using namespace presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}
void wait(const std::function<bool()>& done){const auto end=GetTickCount64()+30000;do{pump();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<end);throw std::runtime_error("Mathematical submission did not complete");}
HWND child(HWND parent,int id){struct Search{int id;HWND result=nullptr;} value{id};EnumChildWindows(parent,[](HWND item,LPARAM arg)->BOOL{auto& search=*reinterpret_cast<Search*>(arg);wchar_t owner[100]{};GetClassNameW(GetParent(item),owner,100);if(GetDlgCtrlID(item)==search.id&&std::wstring_view(owner)==L"LearnGameAndEngineDev.LessonPane"){search.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&value));check(value.result!=nullptr,"Native composition control is missing");return value.result;}
void replace(HWND item,std::string_view value){const auto text=wide(value);SendMessageW(item,EM_SETSEL,0,-1);SendMessageW(item,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));pump();}
void swipe_forward(HWND item){SendMessageW(item,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,12));SendMessageW(item,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(20,12));SendMessageW(item,WM_LBUTTONUP,0,MAKELPARAM(20,12));pump();}
COLORREF first_ink(HWND item){SendMessageW(item,EM_SETSEL,0,1);CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(item,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(item,EM_SETSEL,0,0);return format.crTextColor;}
HWND math_input(HWND view){HWND input=nullptr;EnumChildWindows(child(view,1005),[](HWND item,LPARAM arg)->BOOL{if(!(GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY)){wchar_t name[80]{};GetClassNameW(item,name,80);if(std::wstring_view(name).find(L"RICHEDIT")!=std::wstring_view::npos){*reinterpret_cast<HWND*>(arg)=item;return FALSE;}}return TRUE;},reinterpret_cast<LPARAM>(&input));check(input!=nullptr,"The numerical response has no editable field");return input;}
constexpr std::string_view content=R"json({"id":"qa_native_composition","title":"Composición.","pages":[
{"blocks":[{"type":"math_puzzle","schemaVersion":1,"prompt":"Completa la medida.","responses":[{"id":"number","type":"math_numeric_input","label":"Medida","unit":"cm","validator":{"type":"numeric","expected":6}}],"feedback":{"correct":"La medida es seis centímetros.","incorrect":"Revisa la medida."}}]},
{"blocks":[{"type":"inline_fill_code","question":"Completa la salida.","template":"std::cout [] [];","answers":["<<","\"Listo\""],"explanation":"Conserva el mensaje."}]},
{"blocks":[{"type":"drag_fill_code","question":"Coloca la función.","template":"int []() { return 0[] }","choices":["main",";","Main"],"correctOrder":[0,1],"explanation":"La función devuelve cero."}]}]})json";
void refined_ordering_composition_tests(HWND host){
    using namespace winrt::Windows::Data::Json;
    const auto directory=std::filesystem::path(build::cpp_tool_host).parent_path().parent_path()/"test-fixtures/order-lines";
    const auto bytes=state::read_bytes(directory/"manifest.json");
    const auto manifest=JsonObject::Parse(wide(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
    JsonObject entry;
    for(const auto& item:manifest.GetNamedArray(L"entries"))if(item.GetObject().GetNamedString(L"path")==L"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json")entry=item.GetObject();
    check(entry.HasKey(L"refined"),"The native ordering UI fixture was not exported");
    const auto original=windows::load_lesson(directory/winrt::to_string(entry.GetNamedString(L"old")));
    auto lesson=windows::load_lesson(directory/winrt::to_string(entry.GetNamedString(L"refined")));
    const auto position=curriculum::stable_block_position(lesson,"block_0004_0002");
    check(position&&position->page==4&&position->block==2,"The real ordering UI fixture changed its historical page");
    const auto& block=lesson.pages[4].blocks[2];const auto& old=original.pages[4].blocks[2];
    check(block.order_refinement&&block.fragments.size()==6&&old.fragments.size()==4&&block.id=="orden_mensajes",
        "The UI test lost its actual six independent source lines");
    // Prerequisite execution is covered by the integrated compiler tests. This
    // projection isolates composition using the fully parsed real lesson, with
    // all pages, source contracts, state maps and historical addresses retained.
    lesson.activity_after.clear();lesson.compiler_puzzles_required=false;
    constexpr int order_id=1205,check_id=1203,feedback_id=1204,reset_id=1212;
    const auto visible_order=[&](HWND view,std::size_t count){
        const auto container=child(view,order_id);std::vector<std::pair<LONG,std::size_t>> rows;
        for(std::size_t index=0;index<count;++index){const auto row=GetDlgItem(container,100+static_cast<int>(index));
            check(row!=nullptr,"A source line was not an independent native card");RECT rect{};GetWindowRect(row,&rect);rows.emplace_back(rect.top,index);}
        std::ranges::sort(rows);std::vector<std::size_t> result;for(const auto& row:rows)result.push_back(row.second);return result;
    };
    const auto tap=[](HWND row){SendMessageW(row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));SendMessageW(row,WM_LBUTTONUP,0,MAKELPARAM(12,8));pump();};
    const auto ink=[](HWND row,LONG offset){SendMessageW(row,EM_SETSEL,offset,offset+1);CHARFORMAT2W format{};format.cbSize=sizeof(format);
        SendMessageW(row,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(row,EM_SETSEL,0,0);return format.crTextColor;};
    for(const std::string mode:{"correct","incorrect","draft"}){
        auto environment=resolve_windows_environment({ProfileId::Codex,L"order-lines-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+
            std::to_wstring(GetTickCount64())+L"-"+wide(mode),false});initialize_profile_directories(environment.paths);
        const auto key=curriculum::exercise_key(original,old);state::Response response;
        response.raw_json=R"({"incorrectAttempts":4,"opaque":{"caret":7,"selection":"return"}})";
        response.correct=mode=="correct";
        if(response.correct)response.order.assign(old.correct_order.begin(),old.correct_order.end());
        else response.order=mode=="incorrect"?std::vector<std::int64_t>{3,1,2,0}:std::vector<std::int64_t>{1,0,2,3};
        if(mode=="incorrect")state::mark_response_reviewed(response,old);
        std::string historical;
        {state::StateStore store(environment.paths.root);auto data=store.load();state::prepare_lesson_state(data,original);
            state::save_current_page(data,original,4);state::put_response(data,key,response);
            // The other exercise on the same real page is independent. Its
            // native accepted answers isolate the ordering's page-level gate.
            const auto& previous=original.pages[4].blocks[1];JsonArray answers;for(const auto& answer:previous.answers)answers.Append(JsonValue::CreateStringValue(wide(answer)));
            JsonObject raw;raw.Insert(L"answers",answers);state::Response completed;completed.correct=true;completed.raw_json=winrt::to_string(raw.Stringify());
            state::put_response(data,curriculum::exercise_key(original,previous),completed);historical=state::string_value(data,"learning_progress",key);store.save(data);}
        std::vector<std::size_t> expected;
        for(const auto group:response.order)for(const auto line:block.order_refinement->groups[static_cast<std::size_t>(group)])expected.push_back(line);
        struct OwnedView{HWND window=nullptr;~OwnedView(){if(window)DestroyWindow(window);}} owner;
        auto& view=owner.window;
        const auto unchanged=[&](const state::StateData& data){check(state::string_value(data,"learning_progress",key)==historical,
            "Opening, editing, checking or resetting refined cards overwrote the grouped historical response");};
        const auto open=[&]{view=create_lesson_view(host,environment,lesson);MoveWindow(view,0,0,960,690,TRUE);pump();
            check(lesson_page_state(view).index==4,"A refined composition lost its saved lesson page");unchanged(read_lesson_state(view));};
        const auto close=[&]{check(flush_lesson_view(view),"Cannot persist the independent ordering composition");DestroyWindow(view);view=nullptr;pump();};
        const auto row=[&](std::size_t index){return GetDlgItem(child(view,order_id),100+static_cast<int>(index));};
        const auto current=[&]{const auto saved=state::get_response(read_lesson_state(view),lesson,block);check(saved.has_value(),"The refined composition response disappeared");return *saved;};
        std::string question_key;
        const auto readable=[&](bool correct){bool prompt=false;for(const auto& region:lesson_reading_regions(view)){
            check(!IsChild(child(view,order_id),region.target),"The ordering reader began revealing source cards instead of the existing prompt");
            if(region.block_index==2&&region.source_text==block.question){prompt=true;check(region.is_exercise&&region.solved==correct,"The ordering reader lost its current completion boundary");
                if(question_key.empty())question_key=region.key;else check(question_key==region.key,"Ordering edits changed its stable reader key");}}
            check(prompt,"The refined ordering prompt disappeared from live reading regions");};
        open();check(visible_order(view,6)==expected&&current().correct==response.correct,"Opening refined cards reset an old correct, incorrect or partial arrangement");
        check(lesson_page_state(view).can_next==response.correct,"The projected ordering granted or removed completion");readable(response.correct);
        if(mode=="draft")check(edit_text(child(view,feedback_id)).empty(),"An unsubmitted old arrangement acquired incorrect feedback");
        else check(edit_text(child(view,feedback_id)).find(response.correct?"Correcto":"✗")!=std::string::npos,"The old ordering verdict was not restored");
        close();open();check(visible_order(view,6)==expected&&current().correct==response.correct,"Reopening lost the projected historical arrangement");
        SendMessageW(child(view,reset_id),BM_CLICK,0,0);pump();std::vector<std::size_t> initial(6);std::iota(initial.begin(),initial.end(),0);
        check(visible_order(view,6)==initial&&edit_text(child(view,feedback_id)).empty(),"Explicit Reset did not clear only the refined ordering");
        // Exercise every source row through the real accessible keyboard path.
        // In particular include/main and return/closing brace cannot move as a group.
        for(std::size_t index=0;index<6;++index){const auto before=visible_order(view,6);auto moved=before;
            const auto from=static_cast<std::size_t>(std::ranges::find(before,index)-before.begin());const auto to=from+1<before.size()?from+1:from-1;
            moved.erase(moved.begin()+from);moved.insert(moved.begin()+to,index);
            SendMessageW(row(index),WM_SYSKEYDOWN,to>from?VK_DOWN:VK_UP,0);pump();
            check(visible_order(view,6)==moved,"An individual include/main/return/brace source line moved together with another card");
            SendMessageW(row(index),WM_SYSKEYDOWN,to>from?VK_UP:VK_DOWN,0);pump();check(visible_order(view,6)==before,"Reversing a single-line keyboard move changed another group");}
        const auto origin=row(3),destination=row(4);RECT a{},b{};GetWindowRect(origin,&a);GetWindowRect(destination,&b);
        SendMessageW(origin,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));const auto ready=GetTickCount64()+20;while(GetTickCount64()<ready)Sleep(1);
        SendMessageW(origin,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(12,b.top-a.top+8));SendMessageW(origin,WM_LBUTTONUP,0,MAKELPARAM(12,b.top-a.top+8));pump();
        check(visible_order(view,6)==std::vector<std::size_t>{0,1,2,4,3,5},"Dragging include could not separate it from main");
        SendMessageW(child(view,reset_id),BM_CLICK,0,0);pump();for(const auto index:{5,4,3,2,1,0})tap(row(static_cast<std::size_t>(index)));
        check(visible_order(view,6)==std::vector<std::size_t>{5,4,3,2,1,0},"Native taps did not arrange independent source lines");
        SendMessageW(child(view,check_id),BM_CLICK,0,0);pump();const auto wrong_feedback=edit_text(child(view,feedback_id));
        check(!current().correct&&wrong_feedback.starts_with("✗")&&!lesson_page_state(view).can_next,"An incorrect reconstructed program was credited");readable(false);unchanged(read_lesson_state(view));
        close();open();check(visible_order(view,6)==std::vector<std::size_t>{5,4,3,2,1,0}&&edit_text(child(view,feedback_id))==wrong_feedback,
            "An incorrect new line arrangement or its reviewed feedback did not persist");
        SendMessageW(child(view,reset_id),BM_CLICK,0,0);pump();close();open();
        check(visible_order(view,6)==initial&&edit_text(child(view,feedback_id)).empty()&&!IsWindowEnabled(child(view,check_id)),"Reset revived an old grouped verdict on reopen");
        for(const auto index:block.correct_order)tap(row(index));SendMessageW(child(view,check_id),BM_CLICK,0,0);pump();
        check(current().correct&&edit_text(child(view,feedback_id)).starts_with("✓ Correcto")&&lesson_page_state(view).can_next,"The correct six-line reconstruction was rejected");readable(true);
        const auto include_ink=ink(row(3),0),return_ink=ink(row(1),4);
        check(reader_theme::palette(view).high_contrast||(include_ink==RGB(92,156,231)&&return_ink==RGB(231,155,186)),"Independent line cards lost their C++ syntax colors");
        update_lesson_preferences(view,[](state::StateData& data){appearance::save_global_text_scale(data,150);appearance::save_course_wrap(data,"cpp_fundamentals",true);});pump();
        for(std::size_t index=0;index<6;++index){int numerator=0,denominator=0;SendMessageW(row(index),EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            check(numerator*100==denominator*150&&denominator>0&&edit_text(row(index))==block.fragments[index],"Zoom or wrapping changed an independent line's original source");}
        check(ink(row(3),0)==include_ink&&ink(row(1),4)==return_ink,"Zoom replaced original syntax formatting");
        SendMessageW(row(3),WM_SYSKEYDOWN,VK_DOWN,0);tap(row(4));check(visible_order(view,6)==block.correct_order&&current().correct,"A completed ordering remained editable without Reset");
        unchanged(read_lesson_state(view));close();open();check(current().correct&&visible_order(view,6)==block.correct_order,"The correct refined achievement did not persist");close();
        state::StateStore store(environment.paths.root);const auto saved=store.load();unchanged(saved);
        // A translated exercise has its own untouched grouped source. Remove
        // only the independently seeded Spanish inline answer from this QA copy:
        // that different literal is unrelated to the ordering migration.
        auto translated=windows::load_lesson(directory/winrt::to_string(entry.GetNamedString(L"en")),"en");translated.activity_after.clear();translated.compiler_puzzles_required=false;
        const auto& translated_block=translated.pages[4].blocks[2];auto translation_data=saved;
        translation_data.preferences["learning_progress"].erase(curriculum::exercise_key(original,original.pages[4].blocks[1]));
        auto translated_environment=resolve_windows_environment({ProfileId::Codex,L"order-lines-ui-en-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+wide(mode),false});initialize_profile_directories(translated_environment.paths);
        {state::StateStore translated_store(translated_environment.paths.root);(void)translated_store.load();translated_store.save(translation_data);}
        view=create_lesson_view(host,translated_environment,translated);MoveWindow(view,0,0,960,690,TRUE);pump();
        std::vector<std::size_t> grouped;for(const auto index:response.order)grouped.push_back(static_cast<std::size_t>(index));
        check(lesson_page_state(view).index==4&&visible_order(view,4)==grouped,"A preserved translation received Spanish line indices instead of its original grouped attempt");
        for(std::size_t index=0;index<4;++index)check(edit_text(GetDlgItem(child(view,order_id),100+static_cast<int>(index)))==translated_block.fragments[index],"The original translated source was rewritten during Spanish refinement");
        unchanged(read_lesson_state(view));close();
    }
    std::cout<<"PASS actual U1L3 ordering UI: old correct/incorrect/draft, six individual keyboard rows, include/main drag, native taps, wrong/correct/Reset/reopen, original JSON, syntax/zoom/wrap/reader and four-group English preservation\n";
}
void pedido_editorial_state_tests(HWND host,const std::shared_ptr<windows::NarrationWorker>& backend){
    using namespace winrt::Windows::Data::Json;
    const auto bytes=state::read_bytes(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json");
    std::string source(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    const std::string before_label="Construcción que termina normalmente",after_label="Construcción desde código y unidades que termina normalmente";
    const auto old_at=source.find(before_label);
    if(old_at!=source.npos){
        source.replace(old_at,before_label.size(),after_label);
        // Append the full-block proof without reserializing mathematical JSON:
        // Android's historical response fingerprint keeps insertion order.
        const auto editorial=source.find("\"editorial\": [");
        check(editorial!=source.npos,"Pedido fixture has no published editorial manifest");
        const auto end=source.find(']',editorial);
        check(end!=source.npos,"Pedido fixture has no editorial array terminator");
        const auto count=JsonObject::Parse(wide(source)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Size();
        source.insert(end,(count?",":"")+std::string(R"({"id":"block_0007_0001","beforeSha256":"ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d","afterSha256":"20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b"})"));
    }
    auto lesson=windows::parse_lesson_json(source,"es");
    const auto position=curriculum::stable_block_position(lesson,"block_0007_0001");
    check(position&&position->page==7&&position->block==1,"Pedido matching historical position changed");
    const auto& block=lesson.pages[position->page].blocks[position->block];
    const auto key=curriculum::exercise_key(lesson,block);
    check(block.raw_json.find(after_label)!=std::string::npos,"Pedido fixture omitted the exact revised label");
    // This regression exercises the matching state independently of its already
    // covered compiler prerequisites. All block/family/storage metadata stays.
    lesson.activity_after.clear();lesson.compiler_puzzles_required=false;
    constexpr auto correct_answers=R"({"contratos":{"type":"matching","pairs":{"construccion":"objeto_valido","valor_invalido":"rechazo_construccion","transicion":"nuevo_estado","operacion_rechazada":"sin_cambios","consulta":"consulta_lectura","tipo_incorrecto":"tipo","asignacion":"valor_completo"}}})";
    std::string previous_reading_key;
    for(const bool correct:{false,true}){
        auto environment=resolve_windows_environment({ProfileId::Codex,L"pedido-label-"+std::to_wstring(GetCurrentProcessId())+L"-"+
            std::to_wstring(GetTickCount64())+(correct?L"-correct":L"-incorrect"),false});
        initialize_profile_directories(environment.paths);
        auto answers=JsonObject::Parse(wide(correct_answers));
        if(!correct){const auto pairs=answers.GetNamedObject(L"contratos").GetNamedObject(L"pairs");
            pairs.Insert(L"construccion",JsonValue::CreateStringValue(L"nuevo_estado"));
            pairs.Insert(L"transicion",JsonValue::CreateStringValue(L"objeto_valido"));}
        const auto reply=JsonObject::Parse(wide(backend->validate_math(block.raw_json,winrt::to_string(answers.Stringify()),"es")));
        check(reply.GetNamedBoolean(L"evaluable")&&reply.GetNamedBoolean(L"correct")==correct&&
            reply.GetNamedString(L"contentFingerprint")==L"978c6526c69d056658f4ee8455eaa4c37f5e87d97d9feebd5ecf93bac352ed13",
            "The revised matching did not retain its original Android response fingerprint");
        auto raw=JsonObject::Parse(reply.Stringify());raw.Insert(L"responses",answers);
        raw.Insert(L"incorrectAttempts",JsonValue::CreateNumberValue(4));
        raw.Insert(L"opaque",JsonObject::Parse(LR"({"selection":"construccion","caret":19})"));
        state::Response response;response.correct=correct;response.raw_json=winrt::to_string(raw.Stringify());
        if(!correct)state::mark_response_reviewed(response,block);
        {state::StateStore store(environment.paths.root);auto seed=store.load();state::prepare_lesson_state(seed,lesson);
            state::save_current_page(seed,lesson,position->page);state::put_response(seed,key,response);store.save(seed);}
        LessonViewHooks hooks;hooks.validate_math=[backend](auto b,auto a,auto locale,std::stop_token stop){return backend->validate_math(b,a,locale,stop);};
        for(unsigned reopen=0;reopen<2;++reopen){
            struct Owner{HWND window=nullptr;~Owner(){if(window)DestroyWindow(window);}} owner;
            owner.window=create_lesson_view(host,environment,lesson,hooks);MoveWindow(owner.window,0,0,960,690,TRUE);pump();
            check(lesson_page_state(owner.window).index==position->page,"Reopening the revised matching lost its saved page");
            wait([&]{return edit_text(child(owner.window,1104)).find(correct?"Correcto":"✗")!=std::string::npos;});
            const auto saved=state::get_response(read_lesson_state(owner.window),key);
            check(saved&&saved->correct==correct,"An old matching verdict was lost or incorrectly promoted on revision");
            const auto retained=JsonObject::Parse(wide(saved->raw_json));
            check(retained.GetNamedObject(L"responses").Stringify()==answers.Stringify()&&retained.GetNamedNumber(L"incorrectAttempts")==4&&
                retained.GetNamedObject(L"opaque").GetNamedString(L"selection")==L"construccion",
                "Reopening the revised matching changed selections, attempts or opaque imported state");
            std::string reading_key;bool old_spoken=false;
            for(const auto& region:lesson_reading_regions(owner.window)){
                if(region.source_text==after_label){reading_key=region.key;check(edit_text(region.target)==after_label,"The corrected matching label was not visibly rendered");}
                old_spoken=old_spoken||region.source_text==before_label;
            }
            // Mathematical regions already carry their semantic component in
            // key; the optional anchor field belongs to programming controls.
            check(reading_key==lesson.id+"/es/"+lesson.pages[position->page].stable_id+
                "/block_0007_0001/contratos/source/construccion"&&!old_spoken,
                "Reading retained the old ambiguous matching label or lost its semantic source key");
            if(previous_reading_key.empty())previous_reading_key=reading_key;
            else check(reading_key==previous_reading_key,"Reopening changed the matching card's stable reading key");
            check(flush_lesson_view(owner.window),"Cannot persist revised matching QA state");
            DestroyWindow(owner.window);owner.window=nullptr;pump();
        }
    }
    std::cout<<"PASS exact pedido label: legacy correct/incorrect fingerprint, matching pairs, attempts, opaque import state, saved page, visible/narratable label and stable reading key on reopen\n";
}
}
int main(){
    using namespace lgaed;using namespace presentation;
    HWND host=nullptr,view=nullptr;
    try{
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&common);
        auto environment=resolve_windows_environment({ProfileId::Codex,L"composition-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
        initialize_profile_directories(environment.paths);
        auto lesson=windows::parse_lesson_json(content);
        {state::StateStore store(environment.paths.root);auto seed=store.load();state::Response response;
            response.raw_json=R"({"answers":["",""],"opaque":{"caret":19},"incorrectAttempts":2})";
            state::put_response(seed,curriculum::exercise_key(lesson,lesson.pages[1].blocks[0]),response);store.save(seed);}
        auto backend=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Lesson composition QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create composition QA host");
        refined_ordering_composition_tests(host);
        pedido_editorial_state_tests(host,backend);
        LessonViewHooks hooks;hooks.validate_math=[backend](std::string_view b,std::string_view a,std::string_view lang,std::stop_token stop){return backend->validate_math(b,a,lang,stop);};
        auto open=[&]{view=create_lesson_view(host,environment,lesson);set_lesson_view_hooks(view,hooks);MoveWindow(view,0,0,960,690,TRUE);pump();};
        auto close=[&]{check(flush_lesson_view(view),"Cannot persist composition draft");DestroyWindow(view);view=nullptr;pump();};
        open();
        const auto regions=lesson_reading_regions(view);bool unit=false;
        for(const auto& r:regions)if(r.source_text.find("cm")!=std::string::npos&&r.source_text.find("Unidad")!=std::string::npos)unit=true;
        check(unit,"The complete unit label was not exposed to narration");
        HWND input=math_input(view);
        check(input!=nullptr,"The numerical response has no editable field");replace(input,"5");SendMessageW(child(view,1003),BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(child(view,1003));});
        check(!edit_text(child(view,1004)).empty(),"Wrong mathematics produced no visible feedback");
        check(reader_theme::palette(view).high_contrast||first_ink(child(view,1004))==RGB(240,166,166),"Mathematical incorrect feedback changed the original Android ink");
        check(!navigate_lesson_view(view,1),"Wrong mathematics unlocked the next page");
        const auto wrong=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[0].blocks[0]));
        check(wrong&&!wrong->correct&&wrong->raw_json.find("5")!=std::string::npos,"Wrong mathematics lost its exact incorrect response");
        const auto wrong_feedback=edit_text(child(view,1004));close();open();
        wait([&]{return edit_text(child(view,1004))==wrong_feedback;});input=math_input(view);
        check(!lesson_page_state(view).can_next,"Reopening an incorrect answer unlocked the page");
        replace(input,"6");SendMessageW(input,WM_KEYDOWN,VK_RETURN,1);SendMessageW(input,WM_KEYUP,VK_RETURN,1);
        wait([&]{const auto response=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[0].blocks[0]));return response&&response->correct;});
        check(!IsWindowEnabled(child(view,1003)),"A correct mathematical answer left the check button active");
        check(edit_text(child(view,1004)).find("Correcto")!=std::string::npos&&lesson_page_state(view).index==0,"Enter did not check only the current math activity");
        check(reader_theme::palette(view).high_contrast||first_ink(child(view,1004))==RGB(180,220,190),"Mathematical correct feedback changed the original Android ink");
        auto before=read_lesson_state(view);const auto key=curriculum::exercise_key(lesson,lesson.pages[0].blocks[0]);
        auto response=state::get_response(before,key);check(response&&response->correct&&response->raw_json.find("contentFingerprint")!=std::string::npos,"Mathematical state lost its Android content identity");
        const auto raw=response->raw_json;close();open();
        wait([&]{return lesson_page_state(view).can_next;});
        check(state::get_response(read_lesson_state(view),key)->raw_json==raw,"Reopening rewrote the imported mathematical response");
        HWND passive_prompt=nullptr;
        for(const auto& region:lesson_reading_regions(view))if(region.source_text=="Completa la medida.")passive_prompt=region.target;
        check(passive_prompt!=nullptr,"Mathematical prompt disappeared from its live reading targets");
        SendMessageW(passive_prompt,EM_SETSEL,0,0);swipe_forward(passive_prompt);
        check(lesson_page_state(view).index==1,"Dragging a passive mathematical instruction did not use the existing page gesture");
        check(navigate_lesson_view(view,0),"Cannot return after a passive mathematical text swipe");pump();
        swipe_forward(child(view,1005));
        check(lesson_page_state(view).index==1,"Dragging the passive mathematical background did not use the existing page gesture");
        check(navigate_lesson_view(view,0),"Cannot return after a passive mathematical background swipe");pump();
        check(navigate_lesson_view(view,1),"A revalidated mathematical answer did not unlock navigation");pump();
        auto fill=child(view,1005);replace(GetDlgItem(fill,100),"<<");replace(GetDlgItem(fill,101),"\"Listo\"");
        SendMessageW(child(view,1003),BM_CLICK,0,0);pump();check(lesson_page_state(view).can_next,"Inline answers did not unlock their own page");
        const auto inline_response=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[1].blocks[0]));
        check(inline_response&&inline_response->raw_json.find("opaque")!=std::string::npos&&inline_response->raw_json.find("incorrectAttempts")!=std::string::npos,"Checking inline code removed imported metadata");
        close();open();check(lesson_page_state(view).index==1,"The stable current page was not restored");
        check(edit_text(GetDlgItem(child(view,1005),101))=="\"Listo\"","A native inline draft was not restored");
        check(navigate_lesson_view(view,2),"A restored inline answer lost correctness");pump();
        auto drag=child(view,1005);const auto bank=GetDlgItem(drag,3000);
        check(bank&&GetParent(bank)==drag,"The real drag composition lost its original token-bank container");
        for(int at=0;at<2;++at){const auto card=GetDlgItem(bank,2000+at),slot=GetDlgItem(drag,100+at);
            check(card&&slot&&GetParent(card)==bank,"A stable drag choice or authored slot is missing from its actual container");
            // Android's bank tap fills the first empty slot directly. There is
            // no synthetic second click/up on the destination text control.
            SendMessageW(card,BM_CLICK,0,0);pump();
            check(edit_text(slot)==lesson.pages[2].blocks[0].choices[static_cast<std::size_t>(at)]&&
                !(GetWindowLongPtrW(card,GWL_STYLE)&WS_VISIBLE),"A real bank tap failed to move its exact token into the authored slot without duplication");}
        SendMessageW(child(view,1003),BM_CLICK,0,0);pump();
        if(!edit_text(child(view,1004)).starts_with("✓ Correcto")){
            const auto actual=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[2].blocks[0]));
            throw std::runtime_error("Native drag cards: "+edit_text(child(view,1004))+"; state="+(actual?actual->raw_json:"missing"));
        }
        close();open();check(edit_text(child(view,1004)).starts_with("✓ Correcto"),"Reopening lost a correctly placed code card");
        close();
        lesson=windows::parse_lesson_json(R"json({"id":"qa_structured_escape","title":"Edición.","pages":[{"blocks":[{"type":"math_puzzle","schemaVersion":1,"prompt":"Construye la fracción.","responses":[{"id":"formula","type":"math_expression_input","inputMode":"structured","keyboard":"ALGEBRA","validator":{"expected":"\\frac{12}{3}"}}]}]}]})json");
        unsigned backs=0;hooks.back_to_catalog=[&](HWND){++backs;};
        hooks.math_editor_request=[backend](std::string_view request){
            const auto command=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(request)).GetNamedString(L"command",L"");
            return command==L"math_keyboard"?backend->math_keyboard(request):backend->math_editor(request);
        };
        view=create_lesson_view(host,environment,lesson,hooks);MoveWindow(view,0,0,960,690,TRUE);ShowWindow(host,SW_SHOWNOACTIVATE);pump();
        HWND structured=nullptr;
        EnumChildWindows(view,[](HWND item,LPARAM found)->BOOL{wchar_t name[100]{};GetClassNameW(item,name,100);if(std::wstring_view(name)==L"LearnGameAndEngineDev.MathExpressionEditor"){*reinterpret_cast<HWND*>(found)=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&structured));
        check(structured!=nullptr,"Structured lesson editor is missing");SetWindowTextW(structured,LR"(\frac{12}{3})");SetFocus(structured);pump();
        const auto structured_before=inspect_math_expression_editor(structured);
        auto keyboard=math_keyboard_window(host);check(keyboard&&IsWindowVisible(keyboard),"A focused mathematical lesson did not show its canonical shortcut overlay");
        SendMessageW(structured,WM_KEYDOWN,VK_ESCAPE,0);pump();
        check(!IsWindowVisible(keyboard)&&backs==0,"Escape from the structured field skipped closing its mathematical keyboard");
        const auto structured_after=inspect_math_expression_editor(structured);
        check(structured_after.draft_json==structured_before.draft_json&&structured_after.cursor_json==structured_before.cursor_json,"Escape from the structured field altered its expression or navigation state");
        SendMessageW(structured,WM_KEYDOWN,VK_ESCAPE,0);check(backs==0,"Structured Escape synchronously destroyed its owning editor callback");pump();
        check(backs==1,"The next structured Escape did not reuse the normal lesson Back action");
        close();
        lesson=windows::load_lesson(std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json");
        check(lesson.pages[0].blocks.size()>5&&lesson.pages[0].blocks[4].type==curriculum::BlockType::math_puzzle&&lesson.pages[0].blocks[5].type==curriculum::BlockType::math_puzzle,"The real consecutive mathematical QA location changed");
        open();
        const auto verify_math_geometry=[&]{
            std::array<RECT,2> cards{};unsigned card_index=0;
            for(const auto index:{4,5}){
                const auto card=child(view,1000+index*100+5);RECT client{};GetClientRect(card,&client);check(IsWindowVisible(card)&&client.right>0&&client.bottom>0,"A real mathematical composition is hidden or has no viewport");
                std::vector<RECT> children;unsigned radios=0,labels=0,actions=0;
                for(auto item=GetWindow(card,GW_CHILD);item;item=GetWindow(item,GW_HWNDNEXT)){
                    wchar_t name[80]{};GetClassNameW(item,name,80);const bool button=_wcsicmp(name,L"BUTTON")==0;
                    const bool rich=_wcsnicmp(name,L"RichEdit",8)==0;
                    if(!button&&(!rich||GetWindowTextLengthW(item)==0))continue;
                    RECT bounds{};GetWindowRect(item,&bounds);MapWindowPoints(nullptr,card,reinterpret_cast<POINT*>(&bounds),2);
                    check(IsWindowVisible(item)&&bounds.left>=0&&bounds.top>0&&bounds.right>bounds.left&&bounds.bottom>bounds.top&&bounds.right<=client.right&&bounds.bottom<=client.bottom,
                        "A real mathematical label, radio or toolbar action remained unarranged/clipped inside its host");
                    for(const auto& previous:children){RECT overlap{};check(!IntersectRect(&overlap,&previous,&bounds),"Consecutive mathematical controls overlap after lesson layout");}
                    children.push_back(bounds);
                    if(button){const auto style=GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK;if(style==BS_RADIOBUTTON||style==BS_AUTORADIOBUTTON)++radios;else ++actions;}
                    else ++labels;
                }
                check(radios==3&&labels>=5&&actions==2,"A real mathematical choice lost its prompt, instruction, options or fullscreen/reset actions");
                GetWindowRect(card,&cards[card_index++]);
            }
            check(cards[1].top>=cards[0].bottom,"Two consecutive mathematical compositions occupy overlapping lesson rows");
        };
        for(const int width:{960,640,1120}){
            MoveWindow(view,0,0,width,690,TRUE);pump();verify_math_geometry();
            SendMessageW(view,WM_VSCROLL,SB_BOTTOM,0);pump();verify_math_geometry();
            SendMessageW(view,WM_VSCROLL,SB_TOP,0);pump();verify_math_geometry();
        }
        close();DestroyWindow(host);host=nullptr;std::cout<<"PASS real LessonView math validation/fingerprint/reopen/unit narration, passive gestures, structured Escape, consecutive authored math geometry/visibility/resize/scroll and inline/drag state/navigation\n";return 0;
    }catch(const std::exception& e){if(view)DestroyWindow(view);if(host)DestroyWindow(host);std::cerr<<"FAIL lesson compositions: "<<e.what()<<'\n';return 1;}
}
