#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

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
    View(curriculum::Lesson model,std::size_t page,bool role_state=false):environment(resolve_windows_environment({ProfileId::Codex,
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
        if(role_state){initial.preferences["color_underline_opaque_qa"]["retained"]=std::string("\t original\r\n保存");
            // Initialize storage-only history before mounting the lesson. The
            // preference-only live API must never mutate lesson progress.
            auto history=narration::begin({},"color-storage-only","color-storage-only",{0,137,0,false},"opaque source","other-page");
            narration::write_history(initial,history);}
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
        check(character(choice,static_cast<LONG>(at)).dwEffects&CFE_UNDERLINE,"The actual escape-letter reference has no typographic underline");
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
    for(const auto target:{fixture.item(1,0),fixture.item(1,25)}){const auto at=wide(edit_text(target)).find(L"int");
        check(character(target,static_cast<LONG>(at)).dwEffects&CFE_UNDERLINE,"An actual constructor type mention has no real underline");}
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
        {"texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json",2,3,0,L"false"},
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
        if(start==source.npos)std::cerr<<"Missing curricular token: file="<<plan.file<<" page="<<plan.page+1<<" block="<<plan.block+1
            <<" part="<<plan.part<<" token="<<utf8(plan.token)<<" visible="<<utf8(source)<<'\n';
        check(start!=source.npos,"The mounted curricular token changed");
        // Braces/commas have the local prose/editor foreground, rather than a
        // lexical category. Compare every actual identifier/literal glyph.
        for(std::size_t offset=0;offset<plan.token.size();++offset)if(std::wstring_view(L" {}[],()").find(plan.token[offset])==std::wstring_view::npos&&
            character(target,static_cast<LONG>(start+offset)).crTextColor!=character(reference,static_cast<LONG>(offset)).crTextColor){
            std::cerr<<"Curricular color mismatch: "<<plan.file<<" page="<<plan.page+1<<" block="<<plan.block+1<<" token="<<utf8(plan.token)<<" offset="<<offset<<'\n';
            DestroyWindow(reference);check(false,"A registered real curricular mention differs from its native C++ category");}
        for(std::size_t offset=0;offset<plan.token.size();++offset)if(std::wstring_view(L" {}[],()").find(plan.token[offset])==std::wstring_view::npos)
            check(character(target,static_cast<LONG>(start+offset)).dwEffects&CFE_UNDERLINE,"A registered U9/U11 technical mention lacks its actual underline");
        DestroyWindow(reference);
    }
    std::cout<<"PASS twelve actual U9/U11 prose/question/option mentions against native C++ categories\n";
}

void created_buffer_feedback(){
    const auto relative="texto_interpretacion_y_flujos/lesson_un_lector_con_buffer_de_un_token.json";
    const auto asset=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/relative;
    const auto hash=state::sha256(state::read_bytes(asset));const auto lesson=load(relative);
    const auto location=curriculum::stable_block_position(lesson,"block_0003_0003");
    check(location&&location->page==3&&location->block==3,"The current U11L9 buffer contract choice moved");
    const auto& block=lesson.pages[location->page].blocks[location->block];
    check(block.type==curriculum::BlockType::multiple_choice&&block.correct_index==2&&block.options.size()==3
        &&block.review_feedback_options.size()==3&&!block.explanation.empty(),
        "The current U11L9 buffer contract lost its authored choices or feedback");
    View fixture(lesson,location->page);
    const auto saved=[&]{check(flush_lesson_view(fixture.view),"Cannot flush the actual buffer feedback QA");return read_lesson_state(fixture.view);};
    const auto visible=[](std::string value){std::erase(value,'`');return value;};
    const auto initial=saved();const auto identity=curriculum::exercise_key(lesson,block);
    const auto compare_code=[&](HWND target,std::wstring_view token){
        const auto reference=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE,0,0,500,100,
            fixture.host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(reference,"Cannot create an independent feedback C++ color reference");
        struct Reference{HWND value;~Reference(){DestroyWindow(value);}} cleanup{reference};
        reader_theme::apply(reference,reader_theme::Surface::card);set_rich_text(reference,utf8(token),{},true);
        const auto source=wide(edit_text(target));const auto start=source.find(token);
        check(start!=source.npos,"The actual buffer feedback lost its registered technical token");
        for(std::size_t offset=0;offset<token.size();++offset)if(std::wstring_view(L" {}[],()").find(token[offset])==std::wstring_view::npos){
            check(character(target,static_cast<LONG>(start+offset)).crTextColor==character(reference,static_cast<LONG>(offset)).crTextColor,
                "A newly created buffer feedback token differs from its native C++ category");
            check(character(target,static_cast<LONG>(start+offset)).dwEffects&CFE_UNDERLINE,"A newly created buffer feedback reference lost its underline");}
    };
    const auto question=[&]{
        check(edit_text(fixture.item(location->block,0))==visible(block.question),"Feedback creation rewrote the current buffer question");
        compare_code(fixture.item(location->block,0),L"Token{'n', 14}");
    };
    const auto empty=[&]{
        const auto target=fixture.item(location->block,4);
        check(edit_text(target).empty(),"An unreviewed buffer choice exposed feedback");
        for(const auto& region:lesson_reading_regions(fixture.view))
            check(region.target!=target,"An unreviewed buffer choice exposed a feedback narration source");
    };
    const auto verify=[&](bool correct){
        question();const auto target=fixture.item(location->block,4);
        const auto body=correct?block.explanation:block.review_feedback_options[0];
        const auto raw=std::string(correct?"✓ Correcto\n\n":"✗ Incorrecto\n\n")+body;
        const auto expected=visible(raw);
        check(edit_text(target)==expected,"The actual buffer choice omitted or mixed its full authored feedback");
        // The retry guide's int is an unambiguous type. The successful text's
        // member names retain their authored presentation; no arbitrary symbol
        // category is inferred merely from another block's source code.
        if(!correct)compare_code(target,L"int");
        bool found=false;
        for(const auto& region:lesson_reading_regions(fixture.view))if(region.target==target){
            found=true;check(region.feedback&&region.correct==correct&&region.source_text==raw,
                "The created buffer feedback lost its exact narration source or verdict");
        }
        check(found,"The created buffer feedback has no matching reader region");
        const auto text=wide(expected);std::vector<COLORREF> before;
        for(std::size_t at=0;at<text.size();++at)before.push_back(character(target,static_cast<LONG>(at)).crTextColor);
        CHARRANGE selection{3,8};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        ReadingShade shade(target,raw);shade.read_through(0);shade.read_through(wide(raw).size());shade.restore();
        CHARRANGE after{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        check(after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&edit_text(target)==expected,
            "Reader shading changed the created feedback text or selection");
        for(std::size_t at=0;at<text.size();++at)
            check(character(target,static_cast<LONG>(at)).crTextColor==before[at],"Reader shading changed an authored feedback ink");
        check(curriculum::exercise_key(fixture.lesson,fixture.lesson.pages[location->page].blocks[location->block])==identity
            &&saved().preferences.at("native_windows")==initial.preferences.at("native_windows")&&!IsWindowVisible(fixture.host),
            "Buffer feedback QA changed its identity, compiler receipts, preferences or the desktop");
    };
    question();empty();
    SendMessageW(fixture.item(location->block,20),BM_CLICK,0,0);pump();empty();
    SendMessageW(fixture.item(location->block,3),BM_CLICK,0,0);pump();verify(false);
    const auto rejected=state::get_response(saved(),lesson,block);
    check(rejected&&!rejected->correct&&state::response_reviewed(*rejected,block),"The native buffer choice did not save its reviewed rejection");
    fixture.reopen();verify(false);
    const auto reopened_rejected=state::get_response(saved(),lesson,block);
    check(reopened_rejected&&reopened_rejected->raw_json==rejected->raw_json,"Reopening rewrote the rejected buffer response");
    SendMessageW(fixture.item(location->block,24),BM_CLICK,0,0);pump();empty();
    SendMessageW(fixture.item(location->block,3),BM_CLICK,0,0);pump();verify(true);
    const auto correct=state::get_response(saved(),lesson,block);
    check(correct&&correct->correct&&!IsWindowEnabled(fixture.item(location->block,3)),"The native buffer success was not retained and locked");
    fixture.reopen();verify(true);
    const auto reopened_correct=state::get_response(saved(),lesson,block);
    check(reopened_correct&&reopened_correct->raw_json==correct->raw_json,"Reopening rewrote the correct buffer response");
    check(hash==state::sha256(state::read_bytes(asset)),"Created feedback QA changed the actual U11L9 asset");
    std::cout<<"PASS actual U11L9 p4b4 created retry/success feedback: native int and Token initializer categories, complete source/verdict, hidden-before-submit, selection/shading inks, raw rejection/correct reopen and untouched asset; no audio or desktop visual acceptance\n";
}

std::string visible_prose(std::string source){std::erase(source,'`');std::erase(source,'\r');return source;}
std::size_t passage_offset(HWND target,std::wstring_view context,std::wstring_view token){
    const auto text=wide(edit_text(target));const auto start=text.find(context),local=context.find(token);
    check(start!=text.npos&&local!=context.npos,"A registered actual curricular passage changed");return start+local;
}
void role_reference(View& fixture,HWND target,std::wstring_view context,std::wstring_view token,bool neutral=false,
        std::optional<std::size_t> exact_offset={}){
    const auto reference=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE,0,0,500,100,
        fixture.host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(reference,"Cannot create the independent actual C++/mathematics color reference");
    struct Reference{HWND value;~Reference(){DestroyWindow(value);}} cleanup{reference};
    reader_theme::apply(reference,reader_theme::Surface::card);reader_theme::set_text_color(reference,reader_theme::foreground(target));
    if(neutral){check(set_native_math_text(reference,"\\frac{"+utf8(token)+"}{1}"),"Cannot create the independent structural mathematical reference");
        const auto displayed=wide(edit_text(reference));const auto digit=displayed.find_first_of(L"0123456789");
        check(digit!=displayed.npos&&character(reference,static_cast<LONG>(digit)).crTextColor==reader_theme::foreground(reference),
            "The independent native mathematical numeral differs from its active neutral foreground");
    }else set_rich_text(reference,utf8(token),{},true);
    const auto start=exact_offset?*exact_offset:passage_offset(target,context,token);
    check(start+token.size()<=wide(edit_text(target)).size(),"An exact role oracle falls outside the actual visible control");
    for(std::size_t offset=0;offset<token.size();++offset){if(token[offset]==L' '||token[offset]==L'\t'||token[offset]==L'\n')continue;
        const auto actual=character(target,static_cast<LONG>(start+offset));
        auto expected=neutral?reader_theme::foreground(reference):character(reference,static_cast<LONG>(offset)).crTextColor;
        // Unclassified punctuation keeps its pre-existing technical neutral;
        // punctuation inside a categorized literal still uses that literal ink.
        if(!neutral&&!reader_theme::palette(target).high_contrast&&std::wstring_view(L";{}[],()").find(token[offset])!=std::wstring_view::npos
            &&expected==reader_theme::foreground(reference))expected=RGB(216,216,216);
        if(actual.crTextColor!=expected||!(actual.dwEffects&CFE_UNDERLINE))
            std::cerr<<"Actual role mismatch: lesson="<<fixture.lesson.id<<" page="<<lesson_page_state(fixture.view).index+1
                <<" context="<<utf8(context)<<" token="<<utf8(token)<<" offset="<<offset<<" neutral="<<neutral<<'\n';
        check(actual.crTextColor==expected&&(actual.dwEffects&CFE_UNDERLINE),"A current curricular reference differs from native code/math ink or lacks its real underline");
    }
}
void ordinary_span(HWND target,std::wstring_view context,std::wstring_view token){
    const auto start=passage_offset(target,context,token);const auto foreground=character(target,0).crTextColor;
    for(std::size_t offset=0;offset<token.size();++offset){const auto style=character(target,static_cast<LONG>(start+offset));
        check(style.crTextColor==foreground&&!(style.dwEffects&CFE_UNDERLINE),"A bibliographic citation or ordinary neighboring prose acquired technical color/underline");}
}
void native_differences(const state::Preferences& before,const state::Preferences& after){
    const auto describe=[](const state::Value* value){if(!value)return std::string("missing");
        if(const auto integer=std::get_if<std::int64_t>(value))return std::string("int:")+std::to_string(*integer);
        if(const auto text=std::get_if<std::string>(value))return std::string("string-sha256:")+state::sha256(*text);
        return std::string("type:")+std::to_string(value->index());};
    for(const auto& [key,value]:before){const auto current=after.find(key);if(current==after.end()||current->second!=value)
        std::cerr<<"Native QA state delta: "<<key<<" before="<<describe(&value)<<" after="<<describe(current==after.end()?nullptr:&current->second)<<'\n';}
    for(const auto& [key,value]:after)if(!before.contains(key))std::cerr<<"Native QA state delta: "<<key<<" before=missing after="<<describe(&value)<<'\n';
}
void source_and_shading(View& fixture,HWND target,std::string_view source,std::span<const curriculum::Emphasis> emphasis={}){
    check(flush_lesson_view(fixture.view),"Cannot snapshot state before an actual formatting-only transaction");
    const auto state_before=read_lesson_state(fixture.view);
    const auto displayed=visible_prose(std::string(source));check(edit_text(target)==displayed,"Formatting rewrote actual current curricular text");
    bool found=false;for(const auto& region:lesson_reading_regions(fixture.view))if(region.target==target){found=true;
        check(region.source_text==source,"Color/underline formatting changed an original narration source");}
    check(found,"A visible curricular source disappeared from narration");
    const auto shown=wide(displayed);std::vector<CHARFORMAT2W> before;
    for(std::size_t at=0;at<shown.size();++at)before.push_back(character(target,static_cast<LONG>(at)));
    for(const auto& range:curriculum::emphasis_ranges(source,emphasis)){
        const auto start=wide(visible_prose(std::string(source.substr(0,range.start)))).size();
        const auto end=wide(visible_prose(std::string(source.substr(0,range.end)))).size();
        const bool bold=range.style=="bold"||range.style=="bold_italic",italic=range.style=="italic"||range.style=="bold_italic";
        for(auto at=start;at<end;++at)check(((before.at(at).dwEffects&CFE_BOLD)!=0)==bold&&((before.at(at).dwEffects&CFE_ITALIC)!=0)==italic,
            "Technical references replaced the actual authored partial emphasis");
    }
    CHARRANGE selected{3,8};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    {ReadingShade shade(target,std::string(source));shade.read_through(0);
        for(std::size_t at=0;at<before.size();++at){const auto pending=character(target,static_cast<LONG>(at));
            check((pending.dwEffects&(CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE))==(before[at].dwEffects&(CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE)),
                "Actual pending narration changed underline or partial emphasis");
            if(before[at].dwEffects&CFE_UNDERLINE)check(pending.crTextColor==before[at].crTextColor,"An actual pending code/output reference lost its exact category ink");
            else check(pending.crTextColor==reader_theme::unread_color((before[at].dwEffects&CFE_AUTOBACKCOLOR)?reader_theme::background(target):before[at].crBackColor),
                "Ordinary pending curricular prose escaped the original gray reading policy");}
        shade.read_through(wide(source).size());shade.restore();}
    CHARRANGE after{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(after.cpMin==selected.cpMin&&after.cpMax==selected.cpMax&&edit_text(target)==displayed,"Actual reader shading changed selection or source text");
    for(std::size_t at=0;at<before.size();++at){const auto restored=character(target,static_cast<LONG>(at));
        check(restored.crTextColor==before[at].crTextColor&&restored.dwEffects==before[at].dwEffects
            &&((before[at].dwEffects&CFE_AUTOBACKCOLOR)||restored.crBackColor==before[at].crBackColor),
            "Actual reader restoration changed original category ink, underline or technical background");}
    check(flush_lesson_view(fixture.view),"Cannot snapshot state after an actual formatting-only transaction");
    const auto state_after=read_lesson_state(fixture.view);
    if(state_after!=state_before)native_differences(state_before.preferences.at("native_windows"),state_after.preferences.at("native_windows"));
    check(state_after==state_before,"A formatting-only transaction changed progress, attempts, compiler drafts, responses, preferences or narration");
}
void current_u2_roles(){
    const auto relative="objetos_y_estado/lesson_estado_asignacion_y_copia.json";const auto lesson=load(relative);
    const auto source=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/relative;
    const auto hash=state::sha256(state::read_bytes(source));
    const auto block_at=[&](std::string_view id,std::size_t page){const auto at=curriculum::stable_block_position(lesson,id);
        check(at&&at->page==page,"A registered U2 stable identity moved to another page");return at->block;};
    for(const auto page:{3u,5u,6u,8u}){View fixture(lesson,page,true);
        check(flush_lesson_view(fixture.view),"Cannot snapshot actual color QA");const auto initial=read_lesson_state(fixture.view);
        const auto verify=[&]{
            if(page==3){const auto index=block_at("block_0003_0001",page);const auto target=fixture.item(index,0);
                for(const auto instruction:{L"int a = 2;",L"int b = a;",L"a = a + 3;",L"b = a;"})role_reference(fixture,target,instruction,instruction);
                for(const auto context:{L"a vale 2",L"a y b valen 2",L"a vale 5 y b conserva 2"}){
                    const std::wstring_view text=context;
                    for(std::size_t at=0;at<text.size();++at)if(text[at]==L'a'||text[at]==L'b'||text[at]==L'2'||text[at]==L'5'){
                        // A one-character name must be a whole word; the a/b
                        // letters inside vale/valen/conserva remain prose.
                        if((text[at]==L'a'||text[at]==L'b')&&((at&&text[at-1]!=L' ')||(at+1<text.size()&&text[at+1]!=L' ')))continue;
                        const auto begin=passage_offset(target,text,text)+at;
                        role_reference(fixture,target,text,text.substr(at,1),false,begin);
                    }
                }
                role_reference(fixture,target,L"ambas valen 5",L"5");ordinary_span(target,L"se debe a una nueva",L"a una nueva");
                ordinary_span(target,L"(Stroustrup, 2024)",L"(Stroustrup, 2024)");
                for(const auto token:{L"1.",L"2.",L"3.",L"4."})role_reference(fixture,target,token,std::wstring_view(token).substr(0,1),true);
                source_and_shading(fixture,target,lesson.pages[page].blocks[index].text,lesson.pages[page].blocks[index].emphasis);
                const auto order=block_at("block_0003_0002",page);const auto prompt=fixture.item(order,0);
                role_reference(fixture,prompt,L"valor 3 en anterior",L"anterior");role_reference(fixture,prompt,L"valor 3 en anterior",L"3",true);
                role_reference(fixture,prompt,L"imprimir 8 3",L"8",true);role_reference(fixture,prompt,L"imprimir 8 3",L"3",true);
                source_and_shading(fixture,prompt,lesson.pages[page].blocks[order].question);
            }else if(page==5){const auto index=block_at("block_0005_0001",page);const auto target=fixture.item(index,0);
                role_reference(fixture,target,L"3 * 5 produce 15",L"3 * 5");role_reference(fixture,target,L"produce 15",L"15",true);
                ordinary_span(target,L"inicializador y -= para restar",L"y");ordinary_span(target,L"-= para restar",L"para restar");
                role_reference(fixture,target,L"-= para restar",L"-=");
                ordinary_span(target,L"(Stroustrup, 2024)",L"(Stroustrup, 2024)");
                source_and_shading(fixture,target,lesson.pages[page].blocks[index].text,lesson.pages[page].blocks[index].emphasis);
                const auto choice=block_at("block_0005_0003",page);role_reference(fixture,fixture.item(choice,0),L"entero turnos",L"turnos");
                for(std::size_t option=0;option<3;++option){const auto control=fixture.item(choice,21+static_cast<int>(option)*2);
                    const auto text=wide(lesson.pages[page].blocks[choice].options[option]);role_reference(fixture,control,text,text);
                    source_and_shading(fixture,control,lesson.pages[page].blocks[choice].options[option]);}
                const auto fill=block_at("block_0005_0004",page);role_reference(fixture,fixture.item(fill,0),L"duplicar 4",L"4",true);
                check(edit_text(fixture.item(choice,4)).empty(),"Unanswered U2 choice exposed its solution for color inference");
            }else{
                const std::vector<std::string_view> ids=page==6?std::vector<std::string_view>{"block_0006_0001","block_0006_0005"}:std::vector<std::string_view>{"block_0008_0002"};
                for(const auto id:ids){const auto index=block_at(id,page);const auto target=fixture.item(index,0);const auto text=wide(edit_text(target));
                    std::size_t checked=0;for(const auto label:{L"Antes: ",L"Ahora: "})for(std::size_t at=0;(at=text.find(label,at))!=text.npos;){
                        const auto number=at+std::wstring_view(label).size();
                        // The current paragraph also discusses the literal
                        // label `Antes:`. That mention is not an output pair.
                        if(number>=text.size()||text[number]<L'0'||text[number]>L'9'){at=number;continue;}
                        role_reference(fixture,target,std::wstring_view(text).substr(at,number-at+1),std::wstring_view(text).substr(number,1),true,number);++checked;at=number+1;}
                    if(checked!=(page==6?2u:4u))std::cerr<<"Output-pair oracle mismatch: lesson="<<lesson.id<<" page="<<page+1
                        <<" block="<<id<<" numeric_pairs="<<checked<<" visible="<<utf8(text)<<'\n';
                    check(checked==(page==6?2u:4u),"The actual Before/After output pairs changed");
                    source_and_shading(fixture,target,lesson.pages[page].blocks[index].text,lesson.pages[page].blocks[index].emphasis);
                }
            }
        };
        verify();check(flush_lesson_view(fixture.view)&&read_lesson_state(fixture.view)==initial,"Color/underline/shading changed drafts, response addresses, progress, preferences or stored narration");
        fixture.reopen();verify();check(flush_lesson_view(fixture.view)&&read_lesson_state(fixture.view)==initial,"Reopening role-aware U2 prose changed existing student state");
    }
    // Create the real current feedback through the existing UI, then inspect
    // its three complete instructions and their exact adjacent prose bounds.
    const auto at=*curriculum::stable_block_position(lesson,"block_0005_0003");const auto& choice=lesson.pages[at.page].blocks[at.block];View fixture(lesson,at.page);
    check(flush_lesson_view(fixture.view),"Cannot snapshot the actual U2 state before answering");
    const auto initial=read_lesson_state(fixture.view);const auto key=curriculum::exercise_key(lesson,choice);
    SendMessageW(fixture.item(at.block,20),BM_CLICK,0,0);pump();check(edit_text(fixture.item(at.block,4)).empty(),"Selecting an option revealed feedback before checking");
    SendMessageW(fixture.item(at.block,3),BM_CLICK,0,0);pump();
    const auto rejected=state::get_response(read_lesson_state(fixture.view),lesson,choice);check(rejected&&!rejected->correct&&state::response_reviewed(*rejected,choice),"Actual U2 choice did not retain its rejected attempt");
    fixture.reopen();check(state::get_response(read_lesson_state(fixture.view),lesson,choice)->raw_json==rejected->raw_json,"Reopening rewrote the real rejected U2 response");
    SendMessageW(fixture.item(at.block,24),BM_CLICK,0,0);pump();SendMessageW(fixture.item(at.block,3),BM_CLICK,0,0);pump();
    const auto verify_feedback=[&]{const auto target=fixture.item(at.block,4);const auto raw=std::string("✓ Correcto\n\n")+choice.explanation;
        check(edit_text(target)==visible_prose(raw),"Actual U2 success feedback lost its complete current explanation");
        for(const auto instruction:{L"++turnos;",L"turnos = 1;",L"int turnos = 1;"})role_reference(fixture,target,instruction,instruction);
        for(const auto prose:{L"aumenta en una unidad el valor existente",L"lo reemplaza por uno",L"es una definición, no una actualización del objeto existente"})ordinary_span(target,prose,prose);
        ordinary_span(target,L"objeto existente",L"existente");
        source_and_shading(fixture,target,raw);
    };
    verify_feedback();check(flush_lesson_view(fixture.view),"Cannot persist actual U2 feedback");const auto correct=state::get_response(read_lesson_state(fixture.view),lesson,choice);
    check(correct&&correct->correct&&curriculum::exercise_key(lesson,choice)==key,"Formatting changed a current U2 response identity");
    fixture.reopen();verify_feedback();check(state::get_response(read_lesson_state(fixture.view),lesson,choice)->raw_json==correct->raw_json,"Reopening rewrote the actual correct U2 response");
    const auto prediction_at=*curriculum::stable_block_position(lesson,"block_0005_0002");const auto& prediction=lesson.pages[prediction_at.page].blocks[prediction_at.block];
    const auto prediction_code=fixture.item(prediction_at.block,1),prediction_input=fixture.item(prediction_at.block,2);
    const auto original_code=edit_text(prediction_code);
    check((GetWindowLongPtrW(prediction_code,GWL_STYLE)&ES_READONLY)&&!(GetWindowLongPtrW(prediction_input,GWL_STYLE)&ES_READONLY)
        &&original_code==prediction.code,"The actual prediction code/input control contract changed");
    set_edit_text(prediction_input,"5");
    check(edit_text(prediction_input)=="5"&&edit_text(prediction_code)==original_code,"Typing the prediction result changed its read-only C++ source");
    SendMessageW(fixture.item(prediction_at.block,3),BM_CLICK,0,0);pump();
    check(edit_text(prediction_code)==original_code,"Checking the prediction changed its read-only C++ source");
    const auto verify_prediction=[&]{const auto target=fixture.item(prediction_at.block,4);
        const auto raw=std::string("✓ Correcto\n\n")+prediction.explanation;check(edit_text(target)==visible_prose(raw),"The actual U2 trace feedback is incomplete");
        for(const auto token:{L"1",L"2",L"6",L"5"})role_reference(fixture,target,L"Los valores sucesivos son 1, 2, 6 y 5.",token,true);
        source_and_shading(fixture,target,raw);
    };
    verify_prediction();check(flush_lesson_view(fixture.view),"Cannot persist actual trace feedback");
    const auto predicted=state::get_response(read_lesson_state(fixture.view),lesson,prediction);check(predicted&&predicted->correct,"The independently supplied actual trace result was not accepted");
    fixture.reopen();verify_prediction();check(state::get_response(read_lesson_state(fixture.view),lesson,prediction)->raw_json==predicted->raw_json,"Reopening rewrote the actual trace response");
    const auto fill_at=*curriculum::stable_block_position(lesson,"block_0005_0004");const auto& fill=lesson.pages[fill_at.page].blocks[fill_at.block];
    const auto fill_host=fixture.item(fill_at.block,5);
    for(const auto& [id,value]:std::vector<std::pair<int,const wchar_t*>>{{100,L"*"},{101,L"-"}}){const auto field=GetDlgItem(fill_host,id);
        check(field,"The actual current inline exercise lost one of its two answer fields");SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value));}
    SendMessageW(fixture.item(fill_at.block,3),BM_CLICK,0,0);pump();
    const auto verify_fill=[&]{const auto target=fixture.item(fill_at.block,4);const auto raw=std::string("✓ Correcto\n\n")+fill.explanation;
        check(edit_text(target)==visible_prose(raw),"The actual inline feedback omitted its complete current explanation");
        role_reference(fixture,target,L"cantidad * 2",L"cantidad * 2");role_reference(fixture,target,L"La consola muestra 6",L"6",true);
        ordinary_span(target,L"(Stroustrup, 2024)",L"(Stroustrup, 2024)");source_and_shading(fixture,target,raw);
    };
    verify_fill();check(flush_lesson_view(fixture.view),"Cannot persist actual inline feedback");const auto filled=state::get_response(read_lesson_state(fixture.view),lesson,fill);
    check(filled&&filled->correct,"The independently supplied actual multiplication/subtraction operators were not accepted");
    fixture.reopen();verify_fill();check(state::get_response(read_lesson_state(fixture.view),lesson,fill)->raw_json==filled->raw_json,"Reopening rewrote the actual inline response");
    check(flush_lesson_view(fixture.view),"Cannot snapshot the actual U2 state after answering");
    const auto final=read_lesson_state(fixture.view);const auto& actual_native=final.preferences.at("native_windows");
    native_differences(initial.preferences.at("native_windows"),actual_native);
    auto expected_native=initial.preferences.at("native_windows");
    const auto page=lesson_page_state(fixture.view);const auto scroll_key="scroll/"+lesson.id+'/'+page.page_id;
    const auto displayed_scroll=static_cast<std::int64_t>(MulDiv(GetScrollPos(fixture.view,SB_VERT),96,static_cast<int>(GetDpiForWindow(fixture.view))));
    expected_native[scroll_key]=displayed_scroll;
    check(actual_native==expected_native,"Actual answer/reopen actions changed compiler receipts or native settings beyond their real page scroll");
    check(hash==state::sha256(state::read_bytes(source)),"Color/subrayado QA changed the current U2 asset");
    std::cout<<"PASS actual U2 trace, operands/result, current choices/created feedback, Before/After outputs, bibliography, underlines, source/partial emphasis, original narration, exact state and reopen; synthetic compiler prerequisites are formatting-only, not execution evidence\n";
}
void preview(unsigned page){
    View fixture(load("objetos_y_estado/lesson_estado_asignacion_y_copia.json"),page-1);
    const auto title=L"LGAED - Color y subrayado QA - p"+std::to_wstring(page);SetWindowTextW(fixture.host,title.c_str());
    check(SetWindowSubclass(fixture.host,[](HWND host,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw)->LRESULT{
        if(message==WM_CLOSE){const auto& view=*reinterpret_cast<const View*>(raw);if(IsWindow(view.view))(void)flush_lesson_view(view.view);}
        return DefSubclassProc(host,message,wp,lp);
    },0x434F4C4F,reinterpret_cast<DWORD_PTR>(&fixture)),"Cannot preserve preview state on ordinary Close");
    ShowWindow(fixture.host,SW_SHOWNOACTIVATE);UpdateWindow(fixture.host);
    std::cout<<"Formatting-only QA preview p"<<page<<": current JSON, isolated new profile, declared synthetic compiler prerequisites; no reader/audio or compiler execution claimed. Close the window normally to finish.\n";
    MSG message{};while(IsWindow(fixture.host)&&GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
}
}
int main(int argc,char** argv){const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto module=LoadLibraryW(L"Msftedit.dll");int result=0;
    try{check(module,"Cannot load RichEdit");reader_theme::prepare_fonts();
        if(argc==3&&std::string_view(argv[1])=="--preview"){
            const std::string_view page=argv[2];check(page=="4"||page=="6"||page=="7"||page=="9","Preview admits only the actual registered U2 pages 4, 6, 7 and 9");
            preview(static_cast<unsigned>(page[0]-'0'));
        }else{check(argc==1,"Unknown technical-context test arguments");escape_choice();constructor_question();existing_curricular_mentions();created_buffer_feedback();current_u2_roles();}}
    catch(const std::exception& error){std::cerr<<"FAIL technical context: "<<error.what()<<'\n';result=1;}
    if(module)FreeLibrary(module);OleUninitialize();return result;
}
