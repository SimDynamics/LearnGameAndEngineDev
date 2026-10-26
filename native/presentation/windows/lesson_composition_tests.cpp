#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/reader_theme.hpp"
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
#include <iostream>
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
