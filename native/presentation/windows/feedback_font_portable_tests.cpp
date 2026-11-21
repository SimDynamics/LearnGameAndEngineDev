// Read-only hosted Win32 regression of the ACTUAL lesson presentation.
// Focused synthetic curriculum lives only in this isolated QA process.
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <ole2.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;

void require(bool condition, std::string_view why) {
    if(!condition)throw std::runtime_error(std::string(why));
}
int id(int block,int part){return 1000+100*block+part;}
HWND control(HWND parent,int name){
    struct Lookup{int name;HWND found=nullptr;} lookup{name};
    EnumChildWindows(parent,[](HWND item,LPARAM argument)->BOOL {
        auto& result=*reinterpret_cast<Lookup*>(argument);
        if(GetDlgCtrlID(item)==result.name){result.found=item;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&lookup));
    require(lookup.found!=nullptr,"A real LessonView HWND is missing");
    return lookup.found;
}
void dispatch(HWND host){
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
        if(message.message==WM_QUIT)continue;
        if(!dispatch_lesson_enter(message)&&!IsDialogMessageW(host,&message)){
            TranslateMessage(&message);DispatchMessageW(&message);
        }
    }
}
void click(HWND host,HWND view,int block,int part){
    const auto target=control(view,id(block,part));
    require(IsWindowEnabled(target),"A real puzzle control is disabled");
    SendMessageW(target,BM_CLICK,0,0);dispatch(host);
}
void replace(HWND host,HWND view,int block,std::string_view source){
    const auto target=control(view,id(block,2));
    const auto wide_source=wide(source);
    CHARRANGE selection{0,-1};
    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    SendMessageW(target,EM_REPLACESEL,TRUE,
        reinterpret_cast<LPARAM>(wide_source.c_str()));
    dispatch(host);
    require(edit_text(target)==source,"The native answer editor changed its source text");
}
void replace_inline(HWND host,HWND view,int block,std::string_view first,std::string_view second){
    const std::array<std::string_view,2> sources{first,second};
    for(int i=0;i<2;++i){
        const auto panel=control(view,id(block,5));
        const auto field=GetDlgItem(panel,100+i);
        require(field!=nullptr,"A real inline-fill answer field is missing");
        const auto value=wide(sources[i]);
        SendMessageW(field,EM_SETSEL,0,-1);
        SendMessageW(field,EM_REPLACESEL,TRUE,
            reinterpret_cast<LPARAM>(value.c_str()));
    }
    dispatch(host);
    for(int i=0;i<2;++i){
        const auto panel=control(view,id(block,5));
        const auto field=GetDlgItem(panel,100+i);
        require(field&&edit_text(field)==sources[i],
            "A real inline-fill field changed the authored answer");
    }
}
LONG point_height(HWND edit,LONG at){
    CHARRANGE previous{},position{at,at+1};
    SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&position));
    CHARFORMAT2W style{};style.cbSize=sizeof(style);
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,
        reinterpret_cast<LPARAM>(&style));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));
    require((style.dwMask&CFM_SIZE)!=0,"Feedback has no definite font height");
    return style.yHeight;
}
int scale(HWND item){
    int n=0,d=0;
    SendMessageW(item,EM_GETZOOM,
        reinterpret_cast<WPARAM>(&n),reinterpret_cast<LPARAM>(&d));
    return d?MulDiv(n,100,d):100;
}
LRESULT CALLBACK host_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    return DefWindowProcW(hwnd,msg,wp,lp);
}
struct Fixture{
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    explicit Fixture():environment(resolve_windows_environment(
        {ProfileId::Codex,L"qa-feedback-font-"+std::to_wstring(GetCurrentProcessId())+
          L"-"+std::to_wstring(GetTickCount64()),false})){
        initialize_profile_directories(environment.paths);
        const auto file=R"json({
          "id":"cpp_fundamentals__qa__feedback_font_portable",
          "title":"Misma tipografía que el texto curricular",
          "pages":[{"blocks":[
            {"type":"paragraph","text":"La explicación curricular debe tener el mismo tamaño."},
            {"type":"fill_code","question":"Completa el valor.","code":"int n = [];",
             "answers":["7"],"explanation":"Siete es el valor esperado.",
             "reviewFeedback":"Revisa el valor de n."},
            {"type":"multiple_choice","question":"Selecciona la opción.",
             "options":["No","Sí"],"correctIndex":1,
             "explanation":"La segunda opción es correcta.",
             "reviewFeedback":"La primera no cumple la condición."},
            {"type":"output_prediction","question":"Predice la salida.",
             "code":"std::cout << 7;","answers":["7"],
             "explanation":"La salida es siete."},
            {"type":"order_code","question":"Ordena las líneas.",
             "fragments":["return 0;","int main() {","}"],
             "correctOrder":[1,0,2],"explanation":"La función empieza antes del retorno."},
            {"type":"inline_fill_code","question":"Completa las dos variables.",
             "language":"cpp","template":"int llaves = [];\nint cajas = [];",
             "answers":["7","2"],"explanation":"Ambos valores se conservan.",
             "reviewFeedback":"Comprueba las dos inicializaciones."}
          ]}]
        })json";
        lesson=windows::parse_lesson_json(file,"es");
        state::StateStore store(environment.paths.root);
        auto prefs=store.load();
        prefs.preferences["native_windows"]["ui_language"]=std::string("es");
        store.save(prefs);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"LgaedFeedbackFontHost",
            L"Isolated real LessonView feedback QA",WS_OVERLAPPEDWINDOW,
            0,0,1150,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(host!=nullptr,"Cannot create the disposable QA host window");
        open();
    }
    ~Fixture(){
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);}
        if(host)DestroyWindow(host);
    }
    void open(){
        require(view==nullptr,"The QA native lesson is already open");
        view=create_lesson_view(host,environment,lesson);
        require(view!=nullptr,"Cannot open the real Win32 lesson");
        RECT rect{};GetClientRect(host,&rect);
        MoveWindow(view,0,0,rect.right,rect.bottom,TRUE);
        dispatch(host);
    }
    void reopen(){
        require(flush_lesson_view(view),"The real lesson did not save");
        DestroyWindow(view);view=nullptr;dispatch(host);open();
    }
};

void exercise(){
    Fixture f;
    const auto prose=control(f.view,id(0,0));
    const auto base=point_height(prose,0);
    require(base==rich_text_height(),"Curricular text no longer uses 18sp body font");
    const auto verify=[&](int block,int expected_scale) {
        const auto feedback=control(f.view,id(block,4));
        const auto source=edit_text(feedback);
        require(!source.empty(),"The real puzzle has no visible verdict");
        const auto length=static_cast<LONG>(wide(source).size());
        const auto size_first=point_height(feedback,0);
        const auto size_last=point_height(feedback,length-1);
        // The view may replace prose HWNDs during a scale change. Requery
        // the CURRENT curricular control instead of reading a stale handle.
        const auto current_prose=control(f.view,id(0,0));
        const auto feedback_scale=scale(feedback),prose_scale=scale(current_prose);
        require(size_first==base&&size_last==base&&point_height(current_prose,0)==base&&
            feedback_scale==expected_scale&&prose_scale==expected_scale,
            "Font parity changed: block="+std::to_string(block)+
            ", requested="+std::to_string(expected_scale)+
            ", feedbackScale="+std::to_string(feedback_scale)+
            ", proseScale="+std::to_string(prose_scale)+
            ", feedbackFirst="+std::to_string(size_first)+
            ", feedbackLast="+std::to_string(size_last)+
            ", prose="+std::to_string(base));
    };
    replace(f.host,f.view,1,"9");click(f.host,f.view,1,3);
    require(edit_text(control(f.view,id(1,4))).starts_with("✗ Incorrecto"),
        "The real fill-code incorrect attempt was not reviewed");
    verify(1,100);
    click(f.host,f.view,2,20);click(f.host,f.view,2,3);
    require(edit_text(control(f.view,id(2,4))).starts_with("✗ Incorrecto"),
        "The real choice incorrect attempt was not reviewed");
    verify(2,100);
    replace_inline(f.host,f.view,5,"9","2");click(f.host,f.view,5,3);
    require(edit_text(control(f.view,id(5,4))).starts_with("✗ Incorrecto"),
        "The real inline-fill wrong attempt was not reviewed");
    verify(5,100);
    replace(f.host,f.view,1,"7");click(f.host,f.view,1,3);
    click(f.host,f.view,2,22);click(f.host,f.view,2,3);
    replace(f.host,f.view,3,"7");click(f.host,f.view,3,3);
    const auto order=control(f.view,id(4,5));
    auto line=GetDlgItem(order,100);
    require(line!=nullptr,"Cannot find the real reorderable code card");
    SendMessageW(line,WM_SYSKEYDOWN,VK_DOWN,LPARAM{1}<<29);
    dispatch(f.host);
    click(f.host,f.view,4,3);
    replace_inline(f.host,f.view,5,"7","2");click(f.host,f.view,5,3);
    std::array<std::string,5> original{};
    for(int i=0;i<5;++i){
        original[i]=edit_text(control(f.view,id(i+1,4)));
        require(original[i].starts_with("✓ Correcto"),
            "An original ordinary puzzle did not produce correct feedback");
        verify(i+1,100);
    }
    const auto original_source=lesson_reading_regions(f.view);
    const auto unchanged=[&]{
        const auto latest=lesson_reading_regions(f.view);
        require(latest.size()==original_source.size(),
            "Font zoom changed the narration region count");
        for(std::size_t index=0;index<latest.size();++index)
            require(latest[index].key==original_source[index].key&&
                latest[index].source_text==original_source[index].source_text,
                "Font zoom changed the narration content or identity");
    };
    for(const auto zoom:{75,100,125,150}){
        update_lesson_preferences(f.view,[zoom](state::StateData& data){
            data.preferences["lesson_text_size_settings"]["global_scale"]=
                static_cast<std::int64_t>(zoom);
        });
        dispatch(f.host);
        for(int i=0;i<5;++i){
            require(edit_text(control(f.view,id(i+1,4)))==original[i],
                "Changing font size changed the validated text");
            verify(i+1,zoom);
        }
        unchanged();
    }
    f.reopen();
    for(int i=0;i<5;++i){
        require(edit_text(control(f.view,id(i+1,4)))==original[i],
            "Reopening lost a correct puzzle response or guide");
        verify(i+1,150);
    }
    unchanged();
    std::cout<<"PASS real LessonView feedback: fill_code, choice, prediction, "
        "order_code, inline_fill_code, correct/incorrect and 75/100/125/150% shared 18sp; "
        "reopen and narration preserved.\n";
}
}
int main() try {
    const auto ole=OleInitialize(nullptr);
    if(FAILED(ole))throw std::runtime_error("Cannot initialize Windows OLE");
    struct OleGuard{~OleGuard(){OleUninitialize();}} guard;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};
    require(InitCommonControlsEx(&common)!=FALSE,"Cannot initialize native controls");
    WNDCLASSW type{};type.lpfnWndProc=host_proc;
    type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"LgaedFeedbackFontHost";
    require(RegisterClassW(&type)!=0,"Cannot register disposable QA window");
    exercise();return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL real LessonView feedback font parity: "<<error.what()<<"\n";
    return 1;
}
