#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& value=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)==value.id){value.found=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.found;}
CHARFORMAT2W character(HWND target,LONG at){CHARRANGE before{},one{at,at+1};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&one));CHARFORMAT2W value{};value.cbSize=sizeof(value);
    SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&value));
    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return value;}
COLORREF ink(HWND target,std::wstring_view token){const auto source=wide(edit_text(target));const auto offset=source.find(token);
    check(offset!=source.npos,"The mounted control lost its authored technical mention");return character(target,static_cast<LONG>(offset)).crTextColor;}
struct View{
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,std::size_t page):environment(resolve_windows_environment({ProfileId::Codex,
        L"technical-context-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"Technical-context QA requires a new isolated profile");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();
        state::StateData initial;state::prepare_lesson_state(initial,lesson);state::save_current_page(initial,lesson,page);
        // Synthetic compiler receipts ONLY make the registered prose/choices
        // visible in this formatting test. They do not certify any execution.
        for(const auto& source_page:lesson.pages)for(const auto& block:source_page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            for(const auto& goal:block.output_check->cases){curriculum::FreshExecution result{1,true,true,0,goal.standard_input,goal.outputs.front()};
                result.executable=goal.executable;if(goal.standard_error==curriculum::StandardErrorExpectation::non_blank)result.standard_error="Synthetic prerequisite only";
                (void)windows::record_compiler_puzzle_case(initial,lesson,block,request,result,"Formatting QA prerequisite only");}}
        initial.preferences["native_windows"]["ui_language"]=std::string("es");store.save(initial);
        host=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden actual curricular color QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host,"Cannot create color QA host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view,"Cannot mount the actual curricular lesson");MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Color QA must not take the user's desktop");}
    void reopen(){check(flush_lesson_view(view),"Cannot save isolated color QA");DestroyWindow(view);open();}
    HWND item(std::size_t block,int part){auto result=descendant(view,1000+static_cast<int>(block)*100+part);
        if(!result)throw std::runtime_error("Missing real text control in "+lesson.id+" page="+std::to_string(lesson_page_state(view).index+1)+" block="+std::to_string(block+1)+" part="+std::to_string(part));return result;}
};
curriculum::Lesson load(std::string_view relative){return windows::load_lesson(std::filesystem::path(build::source_root)/
    "app/src/main/assets/content/es/programming/cpp_fundamentals"/relative);}
void escape_choice(){
    auto lesson=load("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json");
    const auto asset=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json";
    const auto hash=state::sha256(state::read_bytes(asset));View fixture(lesson,2);
    const auto verify=[&]{const auto question=fixture.item(6,0),choice=fixture.item(6,25);
        check(edit_text(choice)==lesson.pages[2].blocks[6].options[2],"Context changed the exact distractor text");
        const auto green=ink(question,L"\\n");const auto text=wide(edit_text(choice));const auto at=text.rfind(L"n.");check(at!=text.npos,"Missing the authored escape letter");
        check(character(choice,static_cast<LONG>(at)).crTextColor==green,"The actual escape distractor did not inherit the visible question's string category");
        check(edit_text(fixture.item(6,4)).empty(),"Coloring exposed hidden feedback before an answer");
        CHARRANGE selection{3,8};SendMessageW(choice,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        ReadingShade shade(choice,lesson.pages[2].blocks[6].options[2]);shade.read_through(0);shade.read_through(text.size());shade.restore();
        CHARRANGE after{};SendMessageW(choice,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        check(after.cpMin==3&&after.cpMax==8&&character(choice,static_cast<LONG>(at)).crTextColor==green,"Reader shading changed contextual ink or selection");
        for(const auto& region:lesson_reading_regions(fixture.view))if(region.target==choice)
            check(region.source_text==lesson.pages[2].blocks[6].options[2],"Formatting changed original narration source");
    };
    verify();fixture.reopen();verify();check(hash==state::sha256(state::read_bytes(asset)),"Color QA changed the curricular asset");
    std::cout<<"PASS real U1L3 escape question/isolated option, hidden feedback, shading, selection and reopening\n";
}
void constructor_question(){
    const auto lesson=load("datos_propios_y_estado_valido/lesson_construir_e_inicializar_miembros.json");View fixture(lesson,1);
    check(ink(fixture.item(1,0),L"int")==ink(fixture.item(1,25),L"int"),"The real constructor question and declaration use different int categories");
    check(edit_text(fixture.item(1,0))==lesson.pages[1].blocks[1].question,"Context rewrote the constructor question");
    check(edit_text(fixture.item(1,4)).empty(),"Contextual formatting exposed the constructor solution");
    std::cout<<"PASS actual U9L5 question matches its existing constructor declaration ink\n";
}
void existing_curricular_mentions(){
    struct Plan{const char* file;std::size_t page,block;int part;std::wstring_view token;};
    const Plan plans[]={
        {"datos_propios_y_estado_valido/lesson_construir_e_inicializar_miembros.json",0,1,0,L"void"},
        {"texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json",5,4,0,L"istringstream"},
        {"texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json",5,4,0,L"clear"},
        {"texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json",5,4,21,L"int"},
        {"texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json",5,4,25,L"false"},
        {"texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json",0,2,0,L"char"},
        {"texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json",0,4,0,L"'n'"},
        {"texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json",0,4,0,L"'f'"},
        {"texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json",1,2,0,L"runtime_error"},
        {"texto_interpretacion_y_flujos/lesson_un_lector_con_buffer_de_un_token.json",1,2,0,L"const"},
        {"texto_interpretacion_y_flujos/lesson_un_lector_con_buffer_de_un_token.json",1,2,0,L"explicit"},
        {"texto_interpretacion_y_flujos/lesson_un_lector_con_buffer_de_un_token.json",3,3,0,L"Token{'n', 14}"},
    };
    for(const auto& plan:plans){View fixture(load(plan.file),plan.page);const auto target=fixture.item(plan.block,plan.part);
        const auto reference=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE,0,0,500,100,fixture.host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(reference,"Cannot create the independent native source-color reference");reader_theme::apply(reference,reader_theme::Surface::card);
        set_rich_text(reference,utf8(plan.token),{},true);
        const auto source=wide(edit_text(target));const auto start=source.find(plan.token);
        check(start!=source.npos,"The mounted curricular token changed");
        // Braces/commas have the local prose/editor foreground, rather than a
        // lexical category. Compare every actual identifier/literal glyph.
        for(std::size_t offset=0;offset<plan.token.size();++offset)if(std::wstring_view(L" {}[],()").find(plan.token[offset])==std::wstring_view::npos&&
            character(target,static_cast<LONG>(start+offset)).crTextColor!=character(reference,static_cast<LONG>(offset)).crTextColor){
            std::cerr<<"Curricular color mismatch: "<<plan.file<<" page="<<plan.page+1<<" block="<<plan.block+1<<" token="<<utf8(plan.token)<<" offset="<<offset<<'\n';
            DestroyWindow(reference);check(false,"A registered real curricular mention differs from its native C++ category");}
        DestroyWindow(reference);
    }
    std::cout<<"PASS twelve actual U9/U11 prose/question/option mentions against native C++ categories\n";
}
}
int main(){const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto module=LoadLibraryW(L"Msftedit.dll");int result=0;
    try{check(module,"Cannot load RichEdit");reader_theme::prepare_fonts();escape_choice();constructor_question();existing_curricular_mentions();}
    catch(const std::exception& error){std::cerr<<"FAIL technical context: "<<error.what()<<'\n';result=1;}
    if(module)FreeLibrary(module);OleUninitialize();return result;
}
