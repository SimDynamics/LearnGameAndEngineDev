#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;using namespace presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}
void wait(const std::function<bool()>& done){const auto end=GetTickCount64()+30000;do{pump();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<end);throw std::runtime_error("Mathematical submission did not complete");}
HWND child(HWND parent,int id){struct Search{int id;HWND result=nullptr;} value{id};EnumChildWindows(parent,[](HWND item,LPARAM arg)->BOOL{auto& search=*reinterpret_cast<Search*>(arg);wchar_t owner[100]{};GetClassNameW(GetParent(item),owner,100);if(GetDlgCtrlID(item)==search.id&&std::wstring_view(owner)==L"LearnGameAndEngineDev.LessonPane"){search.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&value));check(value.result!=nullptr,"Native composition control is missing");return value.result;}
void replace(HWND item,std::string_view value){const auto text=wide(value);SendMessageW(item,EM_SETSEL,0,-1);SendMessageW(item,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));pump();}
HWND math_input(HWND view){HWND input=nullptr;EnumChildWindows(child(view,1005),[](HWND item,LPARAM arg)->BOOL{if(!(GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY)){wchar_t name[80]{};GetClassNameW(item,name,80);if(std::wstring_view(name).find(L"RICHEDIT")!=std::wstring_view::npos){*reinterpret_cast<HWND*>(arg)=item;return FALSE;}}return TRUE;},reinterpret_cast<LPARAM>(&input));check(input!=nullptr,"The numerical response has no editable field");return input;}
constexpr std::string_view content=R"json({"id":"qa_native_composition","title":"Composición.","pages":[
{"blocks":[{"type":"math_puzzle","schemaVersion":1,"prompt":"Completa la medida.","responses":[{"id":"number","type":"math_numeric_input","label":"Medida","unit":"cm","validator":{"type":"numeric","expected":6}}],"feedback":{"correct":"La medida es seis centímetros.","incorrect":"Revisa la medida."}}]},
{"blocks":[{"type":"inline_fill_code","question":"Completa la salida.","template":"std::cout [] [];","answers":["<<","\"Listo\""],"explanation":"Conserva el mensaje."}]},
{"blocks":[{"type":"drag_fill_code","question":"Coloca la función.","template":"int []() { return 0[] }","choices":["main",";","Main"],"correctOrder":[0,1],"explanation":"La función devuelve cero."}]}]})json";
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
        check(!navigate_lesson_view(view,1),"Wrong mathematics unlocked the next page");
        const auto wrong=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[0].blocks[0]));
        check(wrong&&!wrong->correct&&wrong->raw_json.find("5")!=std::string::npos,"Wrong mathematics lost its exact incorrect response");
        const auto wrong_feedback=edit_text(child(view,1004));close();open();
        wait([&]{return edit_text(child(view,1004))==wrong_feedback;});input=math_input(view);
        check(!lesson_page_state(view).can_next,"Reopening an incorrect answer unlocked the page");
        replace(input,"6");SendMessageW(input,WM_KEYDOWN,VK_RETURN,1);SendMessageW(input,WM_KEYUP,VK_RETURN,1);
        wait([&]{return IsWindowEnabled(child(view,1003));});
        check(edit_text(child(view,1004)).find("Correcto")!=std::string::npos&&lesson_page_state(view).index==0,"Enter did not check only the current math activity");
        auto before=read_lesson_state(view);const auto key=curriculum::exercise_key(lesson,lesson.pages[0].blocks[0]);
        auto response=state::get_response(before,key);check(response&&response->correct&&response->raw_json.find("contentFingerprint")!=std::string::npos,"Mathematical state lost its Android content identity");
        const auto raw=response->raw_json;close();open();
        wait([&]{return lesson_page_state(view).can_next;});
        check(state::get_response(read_lesson_state(view),key)->raw_json==raw,"Reopening rewrote the imported mathematical response");
        check(navigate_lesson_view(view,1),"A revalidated mathematical answer did not unlock navigation");pump();
        auto fill=child(view,1005);replace(GetDlgItem(fill,100),"<<");replace(GetDlgItem(fill,101),"\"Listo\"");
        SendMessageW(child(view,1003),BM_CLICK,0,0);pump();check(lesson_page_state(view).can_next,"Inline answers did not unlock their own page");
        const auto inline_response=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[1].blocks[0]));
        check(inline_response&&inline_response->raw_json.find("opaque")!=std::string::npos&&inline_response->raw_json.find("incorrectAttempts")!=std::string::npos,"Checking inline code removed imported metadata");
        close();open();check(lesson_page_state(view).index==1,"The stable current page was not restored");
        check(edit_text(GetDlgItem(child(view,1005),101))=="\"Listo\"","A native inline draft was not restored");
        check(navigate_lesson_view(view,2),"A restored inline answer lost correctness");pump();
        auto drag=child(view,1005);
        for(const auto pair:{std::pair{2000,100},std::pair{2001,101}}){SendMessageW(GetDlgItem(drag,pair.first),BM_CLICK,0,0);SendMessageW(GetDlgItem(drag,pair.second),WM_LBUTTONUP,0,0);}
        SendMessageW(child(view,1003),BM_CLICK,0,0);pump();
        if(!edit_text(child(view,1004)).starts_with("Correcto")){
            const auto actual=state::get_response(read_lesson_state(view),curriculum::exercise_key(lesson,lesson.pages[2].blocks[0]));
            throw std::runtime_error("Native drag cards: "+edit_text(child(view,1004))+"; state="+(actual?actual->raw_json:"missing"));
        }
        close();open();check(edit_text(child(view,1004)).starts_with("Correcto"),"Reopening lost a correctly placed code card");
        close();DestroyWindow(host);host=nullptr;std::cout<<"PASS real LessonView math validation/fingerprint/reopen/unit narration and inline/drag state/navigation\n";return 0;
    }catch(const std::exception& e){if(view)DestroyWindow(view);if(host)DestroyWindow(host);std::cerr<<"FAIL lesson compositions: "<<e.what()<<'\n';return 1;}
}
