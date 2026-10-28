#include "core/attempts.hpp"
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
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
#include <span>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
constexpr DWORD emphasis_effects=CFE_BOLD|CFE_ITALIC;
constexpr DWORD preserved_effects=CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE|CFE_STRIKEOUT|CFE_PROTECTED|CFE_AUTOBACKCOLOR;

void check(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
HWND descendant(HWND parent,int id) {
    struct Search{int id;HWND result=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{
        auto& value=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)==value.id){value.result=item;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.result;
}
CHARFORMAT2W character(HWND field,LONG offset) {
    CHARRANGE previous{},selected{offset,offset+1};
    SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous));
    SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    CHARFORMAT2W result{};result.cbSize=sizeof(result);
    SendMessageW(field,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&result));
    SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&previous));return result;
}
std::vector<CHARFORMAT2W> formats(HWND field) {
    const auto length=wide(edit_text(field)).size();
    check(length>0&&length<10000,"Unexpected size for a focal authored emphasis paragraph");
    std::vector<CHARFORMAT2W> result;result.reserve(length);
    for(std::size_t at=0;at<length;++at)result.push_back(character(field,static_cast<LONG>(at)));
    return result;
}
void same_format(const CHARFORMAT2W& expected,const CHARFORMAT2W& current,bool foreground) {
    check((expected.dwEffects&preserved_effects)==(current.dwEffects&preserved_effects),
        "Narration shading changed authored emphasis or another existing style");
    check(expected.yHeight==current.yHeight&&std::wstring_view(expected.szFaceName)==current.szFaceName,
        "Narration shading changed a native font or size");
    if(!(expected.dwEffects&CFE_AUTOBACKCOLOR))check(expected.crBackColor==current.crBackColor,
        "Narration shading changed the original technical background");
    if(foreground)check(expected.crTextColor==current.crTextColor
        &&(expected.dwEffects&CFE_AUTOCOLOR)==(current.dwEffects&CFE_AUTOCOLOR),
        "Restoring narration did not recover the exact original foreground");
}
struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::filesystem::path asset;
    std::string asset_hash;
    HWND host=nullptr,window=nullptr;
    explicit View(std::string_view relative):environment(resolve_windows_environment({ProfileId::Codex,
        L"curricular-emphasis-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),
        asset(std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/relative){
        check(!std::filesystem::exists(environment.paths.root),"Emphasis QA cannot reuse an existing profile");
        initialize_profile_directories(environment.paths);
        check(environment.paths.root.parent_path().filename()==L"qa","Emphasis QA must stay outside real student profiles");
        asset_hash=state::sha256(state::read_bytes(asset));lesson=windows::load_lesson(asset);
        state::StateStore store(environment.paths.root);(void)store.load();state::StateData initial;
        state::prepare_lesson_state(initial,lesson);state::save_current_page(initial,lesson,0);
        initial.preferences["native_windows"]["ui_language"]=std::string("es");
        initial.preferences["emphasis_qa_guard"]["opaque"]=std::string("original\n\t metadata");
        auto& reserve=initial.preferences["attempt_reserve_seconds_test"];
        reserve["reserve_programming"]=4.25;reserve["updated_at_programming"]=std::int64_t{4'000'000'000'000};
        reserve["recovery_interval_programming"]=attempts::recovery_millis;
        // Keep one genuine text-puzzle draft in its own stable address. The
        // formatting fixture never checks it or manufactures an achievement.
        bool seeded=false;
        for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
            if(!seeded&&(block.type==curriculum::BlockType::fill_code||block.type==curriculum::BlockType::output_prediction)){
                state::Response response;response.answer="Borrador conservado";response.raw_json=R"({"opaque":"original response"})";
                state::put_response(initial,lesson,block,response);seeded=true;
            }
        store.save(initial);
        host=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden curricular emphasis QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the hidden curricular emphasis host");open();
    }
    ~View(){if(window){(void)flush_lesson_view(window);DestroyWindow(window);}if(host)DestroyWindow(host);}
    void open(){
        window=create_lesson_view(host,environment,lesson);check(window!=nullptr,"Cannot mount the actual curricular emphasis lesson");
        MoveWindow(window,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Emphasis QA must not take the user's desktop");
        check(lesson_page_state(window).index==0,"The focal emphasis paragraph was redirected to a different page");
    }
    void reopen(){check(flush_lesson_view(window),"Cannot save emphasis QA");DestroyWindow(window);window=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(window),"Cannot flush emphasis QA");state::StateStore store(environment.paths.root);return store.load();}
};

void verify_paragraph(View& view,std::size_t index,std::string_view identity) {
    const auto position=curriculum::stable_block_position(view.lesson,identity);
    check(position&&position->page==0&&position->block==index,"The audited authored emphasis paragraph changed its stable identity");
    const auto& block=view.lesson.pages[0].blocks[index];
    check(block.type==curriculum::BlockType::paragraph&&!block.emphasis.empty(),"The current paragraph lost its authored emphasis contract");
    const auto target=descendant(view.window,1000+static_cast<int>(index)*100);
    check(target!=nullptr,"The actual emphasized paragraph control is missing");
    const auto displayed=edit_text(target);
    // Independent base rendering keeps automatic C++ language-name styling.
    // Only authored emphasis is absent, so automatic bold cannot make this
    // test pass while a missing curricular span goes unnoticed.
    const auto baseline=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_MULTILINE|ES_READONLY,
        0,0,1000,100,view.host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(baseline!=nullptr,"Cannot create the independent no-emphasis base control");
    struct Close{HWND value;~Close(){DestroyWindow(value);}} close{baseline};
    reader_theme::apply(baseline,reader_theme::Surface::canvas);
    set_rich_text(baseline,block.text,{},false,RichTextRole::body,view.lesson.locale);
    check(edit_text(baseline)==displayed,"Authored emphasis rewrote text, paragraph breaks or visible format delimiters");
    check(displayed.find('`')==displayed.npos&&displayed.find("**")==displayed.npos,
        "The mounted curricular paragraph exposes inline formatting markers");
    const auto native=wide(displayed);const auto plain=formats(baseline);const auto before=formats(target);
    check(before.size()==plain.size(),"Authored emphasis changed the source character count");
    std::vector<DWORD> authored(native.size());
    const auto ranges=curriculum::emphasis_ranges(block.text,block.emphasis);
    check(ranges.size()==block.emphasis.size(),"The audited exact authored emphasis fragment is missing or ambiguous");
    std::size_t bold_spans=0,italic_spans=0;
    for(const auto& range:ranges){
        const auto fragment=wide(std::string_view(block.text).substr(range.start,range.end-range.start));
        const auto begin=native.find(fragment);
        check(begin!=native.npos&&native.find(fragment,begin+1)==native.npos,
            "The focal authored fragment is not uniquely present in the mounted paragraph");
        const DWORD mask=(range.style=="bold"||range.style=="bold_italic"?CFE_BOLD:0)
            |(range.style=="italic"||range.style=="bold_italic"?CFE_ITALIC:0);
        check(mask!=0,"The current paragraph has an unsupported emphasis style");
        if(mask&CFE_BOLD)++bold_spans;if(mask&CFE_ITALIC)++italic_spans;
        for(auto at=begin;at<begin+fragment.size();++at)authored[at]|=mask;
    }
    std::size_t newly_styled=0,unchanged_plain=0;
    for(std::size_t at=0;at<before.size();++at){
        const auto expected=(plain[at].dwEffects&emphasis_effects)|authored[at];
        if((before[at].dwEffects&emphasis_effects)!=expected)
            throw std::runtime_error("Authored emphasis differs in "+view.lesson.id+"/"+std::string(identity)
                +" UTF16="+std::to_string(at)+" expected="+std::to_string(expected)
                +" actual="+std::to_string(before[at].dwEffects&emphasis_effects));
        if(authored[at]&&expected!=(plain[at].dwEffects&emphasis_effects))++newly_styled;
        if(!authored[at]&&!(plain[at].dwEffects&emphasis_effects))++unchanged_plain;
        check(before[at].crTextColor==plain[at].crTextColor&&before[at].yHeight==plain[at].yHeight
            &&std::wstring_view(before[at].szFaceName)==plain[at].szFaceName,
            "Adding authored emphasis changed native syntax ink, text size or typeface");
    }
    check(newly_styled>0&&unchanged_plain>0,"The paragraph did not retain both partial emphasis and ordinary body text");
    const auto initial=view.saved();
    CHARRANGE selected{3,9};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    const auto events=SendMessageW(target,EM_GETEVENTMASK,0,0),modified=SendMessageW(target,EM_GETMODIFY,0,0);
    POINT scroll{};RECT rectangle{};SendMessageW(target,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));GetWindowRect(target,&rectangle);
    const auto unchanged=[&](bool restored){
        check(edit_text(target)==displayed,"Narration shading changed authored paragraph text");
        const auto after=formats(target);check(after.size()==before.size(),"Narration changed source offsets");
        for(std::size_t at=0;at<after.size();++at)same_format(before[at],after[at],restored);
        CHARRANGE selection{};POINT current_scroll{};RECT current_rectangle{};
        SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(target,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&current_scroll));GetWindowRect(target,&current_rectangle);
        check(selection.cpMin==selected.cpMin&&selection.cpMax==selected.cpMax
            &&current_scroll.x==scroll.x&&current_scroll.y==scroll.y&&EqualRect(&rectangle,&current_rectangle),
            "Narration moved the paragraph's selection, scroll or rectangle");
        check(events==SendMessageW(target,EM_GETEVENTMASK,0,0)&&modified==SendMessageW(target,EM_GETMODIFY,0,0),
            "Narration changed event routing or the native modified flag");
    };
    {
        ReadingShade shade(target,block.text);check(shade.matches(target,block.text),"Narration cannot map the original authored paragraph to its actual display");
        const auto units=wide(block.text).size();
        for(const auto boundary:std::array<std::size_t,4>{0,units/2,units/4,units}){
            shade.read_through(boundary);unchanged(boundary==units);
            if(boundary<units){
                const auto shaded=formats(target);std::size_t changed=0,retained=0;
                for(std::size_t at=0;at<shaded.size();++at){
                    if(shaded[at].crTextColor!=before[at].crTextColor)++changed;else ++retained;
                }
                check(changed>0,"The mounted paragraph's unread suffix was never shaded");
                if(boundary>0)check(retained>0,"The mounted paragraph's read prefix did not recover its original ink");
            }
        }
        shade.restore();unchanged(true);
    }
    unchanged(true);
    const auto regions=lesson_reading_regions(view.window);
    check(std::ranges::any_of(regions,[&](const auto& region){return region.target==target&&region.source_text==block.text;}),
        "The actual reader lost or rewrote the original authored narration source");
    check(view.saved()==initial,"Emphasis or reversible narration changed progress, attempts, drafts or preferences");
    check(state::sha256(state::read_bytes(view.asset))==view.asset_hash,"Emphasis QA modified an authored lesson asset");
    std::cout<<"PASS real emphasis "<<view.lesson.id<<'/'<<identity<<": "<<bold_spans<<" bold, "<<italic_spans
        <<" italic spans; partial ordinary body, syntax ink, reversible narration, selection and state\n";
}
void real_paragraphs_and_references() {
    struct Plan{const char* path;std::size_t block;const char* identity;};
    const Plan paragraphs[]={
        {"programas_y_ejecucion/lesson_primer_programa_y_salida.json",3,"block_0000_0003"},
        {"objetos_y_estado/lesson_leer_una_entrada_conocida.json",1,"block_0000_0001"},
    };
    for(const auto& plan:paragraphs){View view(plan.path);verify_paragraph(view,plan.block,plan.identity);
        const auto saved=view.saved();view.reopen();verify_paragraph(view,plan.block,plan.identity);
        check(view.saved()==saved,"Reopening a focal emphasis paragraph changed its independent state");}
    View references("archivos_y_programa_completo/lesson_referencias_del_curso.json");
    check(references.lesson.pages.size()==1&&references.lesson.pages[0].blocks.size()==7,
        "The current APA fixture must retain its heading and six authored references");
    for(std::size_t index=1;index<=6;++index)verify_paragraph(references,index,references.lesson.pages[0].blocks[index].stable_id);
    const auto saved=references.saved();references.reopen();
    for(std::size_t index=1;index<=6;++index)verify_paragraph(references,index,references.lesson.pages[0].blocks[index].stable_id);
    check(references.saved()==saved,"Reopening the six APA references changed their independent state");
}
}
int main(){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto rich=LoadLibraryW(L"Msftedit.dll");int result=0;
    try{check(rich!=nullptr,"Cannot load RichEdit for the curricular emphasis regression");reader_theme::prepare_fonts();real_paragraphs_and_references();}
    catch(const std::exception& error){std::cerr<<"FAIL current curricular emphasis: "<<error.what()<<'\n';result=1;}
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}
