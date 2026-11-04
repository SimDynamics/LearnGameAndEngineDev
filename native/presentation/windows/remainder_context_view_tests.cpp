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
#include <algorithm>
#include <array>
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
CHARFORMAT2W character(HWND target,std::size_t at){CHARRANGE before{},one{static_cast<LONG>(at),static_cast<LONG>(at+1)};
    SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&one));
    CHARFORMAT2W value{};value.cbSize=sizeof(value);SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&value));
    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return value;}
bool reference_at(HWND target,std::size_t at){return std::ranges::any_of(rich_text_reference_ranges(target),[&](const auto& part){return part.first<=at&&at<part.second;});}
struct View{
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,std::size_t page):environment(resolve_windows_environment({ProfileId::Codex,
        L"remainder-context-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"Remainder formatting needs a fresh isolated QA profile");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();
        state::StateData data;state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,page);
        // Storage-only synthetic prerequisites expose the real public questions.
        // They do not certify compiler execution, listening or course completion.
        for(const auto& source_page:lesson.pages)for(const auto& block:source_page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            for(const auto& goal:block.output_check->cases){curriculum::FreshExecution result{1,true,true,0,goal.standard_input,goal.outputs.front()};
                result.executable=goal.executable;(void)windows::record_compiler_puzzle_case(data,lesson,block,request,result,"Formatting prerequisite only");}}
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["remainder_context_opaque"]["keep"]=std::string("\t intacto\r\n保存");store.save(data);
        host=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden U11L16 contextual question QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host,"Cannot create isolated formatting host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view,"Cannot mount actual remainder lesson");MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Formatting QA must not take the user's desktop");}
    void reopen(){check(flush_lesson_view(view),"Cannot save isolated formatting state");DestroyWindow(view);open();}
    HWND item(std::size_t block,int part){const auto target=descendant(view,1000+static_cast<int>(block)*100+part);check(target,"Missing native curricular control");return target;}
};
void reference_style(HWND target,std::size_t at){const auto style=character(target,at);
    check(!(style.dwEffects&CFE_UNDERLINE),"A contextual reference acquired an automatic underline");
    if(reader_theme::palette(target).high_contrast)return;
    check(reference_at(target,at)&&!(style.dwEffects&CFE_AUTOBACKCOLOR)&&style.crBackColor==reader_theme::reference_background(target),
        "A contextual reference lacks the shared neutral background");}
void compare_code(HWND target,std::wstring_view token,HWND code){const auto source=wide(edit_text(target));const auto offset=source.find(token);
    check(offset!=source.npos,"The original public question changed");
    const auto reference=wide(edit_text(code));const auto ref=reference.find(token);check(ref!=reference.npos,"The independent native code sample changed");
    for(std::size_t i=0;i<token.size();++i){const auto actual=character(target,offset+i),expected=character(code,ref+i);
        if(std::wstring_view(L" ()").find(token[i])==std::wstring_view::npos&&actual.crTextColor!=expected.crTextColor){
            std::cerr<<"Question ink mismatch: token="<<utf8(token)<<" offset="<<i<<" actual="<<actual.crTextColor<<" expected="<<expected.crTextColor<<'\n';
            check(false,"The actual U11L16 question does not match native C++ ink");}
        reference_style(target,offset+i);}
}
void source_shading(View& fixture,HWND target,std::string_view original){
    const auto source=wide(edit_text(target));std::vector<CHARFORMAT2W> before;for(std::size_t i=0;i<source.size();++i)before.push_back(character(target,i));
    bool region_found=false;for(const auto& region:lesson_reading_regions(fixture.view))if(region.target==target){
        check(region.source_text==original,"Context formatting changed the original narration source");region_found=true;}
    check(region_found,"The original question disappeared from narration");
    CHARRANGE selection{2,9};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    ReadingShade shade(target,std::string(original));shade.read_through(0);
    for(std::size_t i=0;i<source.size();++i){const auto pending=character(target,i);
        const auto gray=reader_theme::unread_colors(reader_theme::background(target),reader_theme::reference_background(target));
        check(pending.crTextColor==(reference_at(target,i)?gray.second:gray.first),"An unread technical phrase differs from the existing pending-ink policy");
        check(pending.crBackColor==before[i].crBackColor&&(pending.dwEffects&CFE_AUTOBACKCOLOR)==(before[i].dwEffects&CFE_AUTOBACKCOLOR),
            "Reading state changed the neutral reference background");}
    shade.read_through(wide(original).size());shade.restore();
    CHARRANGE after{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&edit_text(target)==original,"Reading shade changed selection or original text");
    for(std::size_t i=0;i<source.size();++i)check(character(target,i).crTextColor==before[i].crTextColor,"Reading did not restore native contextual ink");
}
void actual_question(const std::filesystem::path& asset,std::string_view identity,std::span<const std::wstring_view> tokens){
    const auto lesson=windows::load_lesson(asset);const auto position=curriculum::stable_block_position(lesson,identity);
    check(position.has_value(),"The registered U11L16 question lost its stable identity");const auto& block=lesson.pages[position->page].blocks[position->block];
    View fixture(lesson,position->page);
    const auto code=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE,0,0,500,200,fixture.host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(code,"Cannot create independent native source sample");reader_theme::apply(code,reader_theme::Surface::card);
    set_rich_text(code,"int izquierda, derecha; 23%7%3; 23%(7%3);",{},true);
    const auto verify=[&]{const auto target=fixture.item(position->block,0);check(edit_text(target)==block.question,"Formatting rewrote the authored question");
        for(const auto token:tokens)compare_code(target,token,code);
        check(edit_text(fixture.item(position->block,4)).empty(),"Formatting exposed hidden success or review feedback");
        source_shading(fixture,target,block.question);
    };
    check(flush_lesson_view(fixture.view),"Cannot flush the original formatting profile");const auto before=read_lesson_state(fixture.view);
    verify();fixture.reopen();verify();const auto after=read_lesson_state(fixture.view);
    check(before.preferences==after.preferences,"Formatting/reopening changed stored progress, prerequisites or preferences");
    DestroyWindow(code);std::cout<<"PASS actual "<<identity<<": native C++ categories, neutral background, grey pending, no underline, hidden feedback, source, selection and reopen\n";
}
void ordinary_and_hidden(){
    const auto host=CreateWindowExW(0,L"STATIC",L"Negative formatting QA",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host,"Cannot create negative QA host");const auto target=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE,0,0,600,300,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(target,"Cannot create negative QA text");reader_theme::apply(target,reader_theme::Surface::card);
    const std::string plain="Mira a izquierda y derecha de la pantalla. El descuento es 23%.";
    for(const auto context:{RichTextContext{},RichTextContext{{},"int izquierda, derecha;"}}){
        set_rich_text(target,plain,{},false,RichTextRole::body,"es",{},context);
        for(const auto token:{L"izquierda",L"derecha"}){const auto at=wide(plain).find(token);for(std::size_t i=0;i<std::wstring_view(token).size();++i){
            const auto style=character(target,at+i);check(style.crTextColor==reader_theme::foreground(target)&&!reference_at(target,at+i),
                "Merely visible code colored an ordinary directional phrase");}}
        const auto percent=wide(plain).find(L'%');check(!reference_at(target,percent),"A percentage acquired a remainder-operator role");
    }
    set_rich_text(target,"Supón izquierda y derecha en -100..100.");
    for(const auto token:{L"izquierda",L"derecha"}){const auto at=wide(edit_text(target)).find(token);check(!reference_at(target,at),"A hidden or absent code source supplied a name category");}
    const RichTextContext public_context{"Compara las expresiones del programa.",{}};
    for(const auto sample:{"23.5%7","23%7.5","23%7e2","x23%7","23%7x","23%(7%3","23%"}){
        set_rich_text(target,sample,{},false,RichTextRole::body,"es",{},public_context);
        const auto source=wide(sample);for(std::size_t at=0;at<source.size();++at)if(source[at]==L'%')
            check(!reference_at(target,at),"Malformed/non-integer remainder text acquired a partial code-expression role");
    }
    const std::string protected_text="Compara 23%7%3. (Stroustrup, 2024). https://example.test/23%7%3";
    const auto math_begin=wide(protected_text).find(L"23%7%3");const std::array<std::pair<std::size_t,std::size_t>,1> mathematics{{{math_begin,math_begin+6}}};
    set_rich_text(target,protected_text,{},false,RichTextRole::body,"es",mathematics,public_context);
    for(const auto token:{L"23%7%3",L"2024",L"https://example.test/23%7%3"}){const auto at=wide(protected_text).find(token);
        for(std::size_t i=0;i<std::wstring_view(token).size();++i)check(!reference_at(target,at+i),"Remainder inference escaped into protected mathematics, citations or URL");}
    DestroyWindow(target);DestroyWindow(host);std::cout<<"PASS ordinary directions, percentages and missing/hidden source do not acquire identifier categories\n";
}
void hidden_solution(const std::filesystem::path& asset){auto lesson=windows::load_lesson(asset);
    const auto position=curriculum::stable_block_position(lesson,"block_0002_0002");check(position.has_value(),"Missing ordering identity for hidden-source QA");
    auto& block=lesson.pages[position->page].blocks[position->block];
    // This in-memory negative changes only public/hidden QA strings. The real
    // code fragments remain visible and contain neither secret test name.
    block.question="Supón secreto y oculto en -100..100.";block.explanation="int secreto, oculto;";block.review_feedback="int secreto, oculto;";
    View fixture(lesson,position->page);const auto target=fixture.item(position->block,0);
    check(edit_text(fixture.item(position->block,4)).empty(),"The synthetic hidden-source negative revealed feedback");
    for(const auto token:{L"secreto",L"oculto"}){const auto first=wide(edit_text(target)).find(token);
        for(std::size_t i=0;i<std::wstring_view(token).size();++i)check(!reference_at(target,first+i),"An unshown solution supplied contextual identifier ink");}
    std::cout<<"PASS an actual hidden success/guide cannot supply names to the visible question\n";
}
}
int main(){const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto module=LoadLibraryW(L"Msftedit.dll");int result=0;try{check(module,"Cannot load native RichEdit");reader_theme::prepare_fonts();
        const auto asset=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json";
        const auto bytes=state::read_bytes(asset);const std::array<std::wstring_view,2> names{L"izquierda",L"derecha"},expressions{L"23%7%3",L"23%(7%3)"};
        actual_question(asset,"block_0002_0002",names);actual_question(asset,"block_0003_0003",expressions);ordinary_and_hidden();hidden_solution(asset);
        check(state::read_bytes(asset)==bytes,"Formatting QA modified the curricular JSON");
    }catch(const std::exception& error){std::cerr<<"FAIL remainder context: "<<error.what()<<'\n';result=1;}
    if(module)FreeLibrary(module);OleUninitialize();return result;
}
