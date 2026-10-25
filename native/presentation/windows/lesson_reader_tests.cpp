#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
void wait(const std::function<bool()>& done,const std::string& description,ULONGLONG timeout=60000){
    const auto deadline=GetTickCount64()+timeout;
    do{pump();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<deadline);
    throw std::runtime_error("Timeout: "+description);
}
HWND child(HWND parent,int id){struct Search{int id;HWND result=nullptr;} value{id};EnumChildWindows(parent,[](HWND item,LPARAM arg)->BOOL{
    auto& search=*reinterpret_cast<Search*>(arg);if(GetDlgCtrlID(item)==search.id){search.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&value));
    check(value.result!=nullptr,"Missing actual lesson control "+std::to_string(id));return value.result;
}
COLORREF color(HWND target,LONG offset){CHARRANGE before{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{offset,offset+1};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));CHARFORMAT2W format{};format.cbSize=sizeof(format);
    SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return format.crTextColor;
}
void pointer_message(HWND target,UINT message,WPARAM keys,LPARAM point,bool touch){
    const auto previous=SetMessageExtraInfo(touch?static_cast<LPARAM>(0xff515780u):0);
    SendMessageW(target,message,keys,point);SetMessageExtraInfo(previous);
}
struct KeyboardStateScope {
    std::array<BYTE,256> original{},clear{};
    KeyboardStateScope(){check(GetKeyboardState(original.data())!=FALSE,"Cannot retain QA keyboard state");
        check(SetKeyboardState(clear.data())!=FALSE,"Cannot initialize QA thread keyboard state");}
    ~KeyboardStateScope(){SetKeyboardState(original.data());}
};
struct ReaderCommandProbe {
    HWND bar;std::vector<int> commands;
    explicit ReaderCommandProbe(HWND target):bar(target){
        check(SetWindowSubclass(bar,proc,82,reinterpret_cast<DWORD_PTR>(this))!=FALSE,"Cannot observe actual reader button activation");}
    ~ReaderCommandProbe(){RemoveWindowSubclass(bar,proc,82);}
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
        if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED)
            reinterpret_cast<ReaderCommandProbe*>(context)->commands.push_back(LOWORD(wp));
        return DefSubclassProc(window,message,wp,lp);
    }
};
curriculum::Lesson model(bool exercise=false,bool after=true,std::string_view language="es"){
    curriculum::Lesson lesson;lesson.id="qa_native_reader";lesson.title="Prueba.";lesson.locale="es";
    if(language!="es"){lesson.locale=language=="fr"?"fr":"en";lesson.title=language=="fr"?"Lecture.":"Reading.";}
    for(std::size_t p=0;p<4;++p){curriculum::Page page;page.stable_id="page_"+std::to_string(p);
        if(language!="es")page.narration_language=std::string(language);
        curriculum::Block paragraph;paragraph.type=curriculum::BlockType::paragraph;paragraph.stable_id="prose";
        paragraph.text="La línea #include y std::cout muestran una salida.";paragraph.address={p,0,{},{}};page.blocks.push_back(paragraph);
        if(language!="es")page.blocks.back().text=language=="fr"?
            "La ligne #include prépare la bibliothèque et std::cout affiche le résultat du programme.":
            "The #include line prepares the library and std::cout displays the result of the program.";
        if(exercise&&p==0){curriculum::Block puzzle;puzzle.type=curriculum::BlockType::fill_code;puzzle.id="symbol";puzzle.stable_id="symbol";
            puzzle.question="Completa el símbolo.";puzzle.answers={"<<"};puzzle.explanation="La biblioteca prepara los recursos. La biblioteca prepara los recursos. La biblioteca prepara los recursos. La salida muestra el resultado.";
            puzzle.review_feedback="Revisa el símbolo. Conserva los dos signos.";puzzle.address={p,1,{},{}};page.blocks.push_back(puzzle);
            if(after){curriculum::Block tail=paragraph;tail.stable_id="tail";tail.text="La lectura continúa después.";tail.address={p,2,{},{}};page.blocks.push_back(tail);}}
        lesson.pages.push_back(std::move(page));}
    return lesson;
}
std::string compiler_source(std::string_view message){return "#include <iostream>\nint main() { std::cout << \""+std::string(message)+"\"; return 0; }\n";}
curriculum::Lesson compiler_model(){
    auto lesson=model();lesson.id="qa_native_reader_compiler";lesson.pages.resize(1);
    curriculum::Block compiler;compiler.type=curriculum::BlockType::cpp_compiler;compiler.stable_id="checked_source";
    compiler.question="Comprueba la salida.";compiler.initial_code=compiler_source("Bien");compiler.address={0,1,{},{}};
    curriculum::OutputCase expected;expected.outputs={"Bien"};
    expected.explanation="`std::cout` muestra el mensaje solicitado. La salida coincide con el resultado previsto.";
    expected.review_feedback.push_back({{"Mal"},"La salida es distinta. Cambia el mensaje de `std::cout`."});
    compiler.output_check=curriculum::OutputCheck{};compiler.output_check->cases.push_back(std::move(expected));
    lesson.pages.front().blocks.push_back(std::move(compiler));
    auto tail=lesson.pages.front().blocks.front();tail.stable_id="after_compiler";tail.text="La lectura continúa después.";tail.address={0,2,{},{}};
    lesson.pages.front().blocks.push_back(std::move(tail));return lesson;
}
struct Fixture{
    WindowsEnvironment environment;HWND host=nullptr,view=nullptr;std::unique_ptr<LessonReader> reader;
    Fixture(std::shared_ptr<windows::NarrationWorker> backend,bool exercise=false,bool after=true,bool advance=true,std::string_view language="es",std::optional<curriculum::Lesson> supplied={},bool private_audio=false,std::size_t initial_page=0):
        environment(resolve_windows_environment({ProfileId::Codex,L"native-reader-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})){
        check(!std::filesystem::exists(environment.paths.root),"QA profile already exists");
        if(!private_audio)environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";initialize_profile_directories(environment.paths);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Native real Kokoro reader QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            40,40,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create reader QA window");
        auto lesson=supplied?std::move(*supplied):model(exercise,after,language);
        if(initial_page){state::StateStore store(environment.paths.root);auto data=store.load();state::prepare_lesson_state(data,lesson);
            state::save_current_page(data,lesson,initial_page);store.save(data);}
        view=create_lesson_view(host,environment,std::move(lesson));check(view!=nullptr,"Cannot create actual lesson view");MoveWindow(view,0,0,980,720,TRUE);
        update_lesson_preferences(view,[advance](state::StateData& data){auto& prefs=data.preferences["lesson_narration_settings"];
            prefs["engine"]=std::string("KOKORO");prefs["language"]=std::string("es");prefs["kokoro_voice:es"]=std::string("ef_dora");prefs["rate"]=2.;prefs["kokoro_advance_generation"]=advance;});
        LessonViewHooks hooks;hooks.before_page_change=[this](HWND){if(reader)reader->pause();};
        hooks.content_changed=[this](HWND){if(reader)reader->content_changed();};
        hooks.feedback_changed=[this](HWND,std::size_t block,bool correct){if(reader)reader->feedback(block,correct);};set_lesson_view_hooks(view,std::move(hooks));
        reader=std::make_unique<LessonReader>(view,environment,std::move(backend),[]{});
        if(advance)ready();else wait([&]{const auto s=snapshot();return s.planned&&!s.segments.empty()&&s.segments.front().available;},"current segment without advance preparation");
    }
    ~Fixture(){reader.reset();if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    LessonReaderSnapshot snapshot(){auto result=reader->snapshot();check(result.error.empty(),"Real reader error: "+result.error);return result;}
    void ready(){wait([&]{const auto value=snapshot();return value.planned&&!value.segments.empty()&&std::ranges::all_of(value.segments,[](const auto& part){return part.available;});},"real page synthesis/word alignment");}
    void answer(std::string_view text){const auto edit=child(view,1102);const auto value=wide(text);CHARRANGE range{0,-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));
        SendMessageW(child(view,1103),BM_CLICK,0,0);reader->pause();ready();
        check(!snapshot().playing&&!snapshot().allocated,"Pending feedback preparation overrode a subsequent pause");}
    void page(std::size_t index){reader->pause();check(navigate_lesson_view(view,index),"Cannot navigate QA page");reader->content_changed();ready();}
    void edit_program(std::string_view source,std::size_t block=1){reader->pause();const auto edit=child(view,1010+static_cast<int>(block)*100);const auto value=wide(source);CHARRANGE range{0,-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));}
    void run_program(std::size_t block=1){const auto action=child(view,1011+static_cast<int>(block)*100);SendMessageW(action,BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(action);},"real checked compiler execution");reader->pause();ready();
        check(!snapshot().playing&&!snapshot().allocated,"Compiler feedback preparation overrode a subsequent pause");}
};
}

int main(){std::cout<<std::unitbuf;const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    try{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        auto backend=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
        {
            Fixture f(backend,true);KeyboardStateScope keyboard;ReaderCommandProbe probe(f.reader->bar());
            const auto edit=child(f.view,1102),play=GetDlgItem(f.reader->bar(),6504);SetFocus(edit);
            const auto press=[&](UINT key,HWND target,LPARAM flags=1,UINT kind=WM_KEYDOWN){
                MSG message{};message.hwnd=target;message.message=kind;message.wParam=key;message.lParam=flags;
                return f.reader->dispatch_shortcut(message);
            };
            check(press(VK_F1,edit)&&probe.commands==std::vector<int>{6504}&&f.snapshot().allocated&&GetFocus()==edit,
                "F1 from the real answer editor did not activate the existing play button without stealing focus");
            check(press(VK_F1,edit,1LL|(1LL<<30))&&probe.commands.size()==1&&f.snapshot().allocated,
                "Holding F1 toggled playback more than once");
            check(press(VK_F1,edit)&&probe.commands.size()==2&&!f.snapshot().allocated,
                "A second F1 press did not release playback through the existing pause button");
            const std::array<std::pair<UINT,int>,5> mappings{{{VK_F2,6506},{VK_F5,6500},{VK_F6,6501},{VK_F7,6502},{VK_F8,6503}}};
            for(const auto& [key,button]:mappings){
                const auto count=probe.commands.size();SetFocus(play);
                check(press(key,play)&&probe.commands.size()==count+1&&probe.commands.back()==button,
                    "A reader shortcut did not activate its corresponding existing button");
                check(press(key,play,1LL|(1LL<<30))&&probe.commands.size()==count+1,
                    "A held reader shortcut activated its button more than once");f.reader->pause();
            }
            const auto count=probe.commands.size();
            for(const auto modifier:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN}){
                auto state=keyboard.clear;state[modifier]=0x80;check(SetKeyboardState(state.data())!=FALSE,"Cannot set QA modifier");
                check(!press(VK_F1,edit)&&probe.commands.size()==count,"A modified function key activated narration");
                check(SetKeyboardState(keyboard.clear.data())!=FALSE,"Cannot clear QA modifier");
            }
            check(!press(VK_F1,edit,1,WM_SYSKEYDOWN)&&!press(VK_F1,edit,1,WM_KEYUP)&&!press(VK_F3,edit),
                "The reader consumed system, release or unassigned keys");
            EnableWindow(play,FALSE);check(press(VK_F1,edit)&&probe.commands.size()==count,
                "A shortcut bypassed its disabled reader button");EnableWindow(play,TRUE);
            EnableWindow(f.host,FALSE);const auto disabled=press(VK_F1,edit);EnableWindow(f.host,TRUE);
            check(!disabled&&probe.commands.size()==count,"A shortcut bypassed the disabled lesson owner");
            const auto auxiliary=CreateWindowExW(0,L"STATIC",L"QA auxiliary",WS_OVERLAPPEDWINDOW,
                0,0,100,100,f.host,nullptr,GetModuleHandleW(nullptr),nullptr);
            check(auxiliary!=nullptr,"Cannot create an independent owned QA window");
            const auto external=press(VK_F1,auxiliary);DestroyWindow(auxiliary);
            check(!external&&probe.commands.size()==count,"An auxiliary window controlled the lesson reader");
            f.reader->set_controls_visible(false);check(!press(VK_F1,edit)&&probe.commands.size()==count,
                "A shortcut activated hidden reader controls");f.reader->set_controls_visible(true);
            check(edit_text(edit).empty()&&!f.snapshot().allocated,
                "Reader shortcuts changed the puzzle answer or resumed hidden playback");
            std::cout<<"PASS F1/F2/F5-F8 activate existing reader buttons from editor/bar focus once, preserve answers and respect modifiers, disabled controls and auxiliary windows\n";
        }
        {
            auto lesson=windows::parse_lesson_json(R"({"id":"qa_reader_math_keyboard","title":"Prueba.","pages":[{"blocks":[{"type":"math_puzzle","prompt":"Escribe un número.","responses":[{"id":"value","type":"math_numeric_input"}]}]}]})");
            Fixture f(backend,false,true,true,"es",std::move(lesson));
            LessonViewHooks hooks;hooks.before_page_change=[&f](HWND){f.reader->pause();};
            hooks.content_changed=[&f](HWND){f.reader->content_changed();};
            hooks.feedback_changed=[&f](HWND,std::size_t block,bool correct){f.reader->feedback(block,correct);};
            hooks.math_editor_request=[backend](std::string_view request){
                const auto command=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(request)).GetNamedString(L"command",L"");
                return command==L"math_keyboard"?backend->math_keyboard(request):backend->math_editor(request);
            };set_lesson_view_hooks(f.view,std::move(hooks));
            HWND field=nullptr;EnumChildWindows(f.view,[](HWND candidate,LPARAM output)->BOOL{
                if(!math_field_keyboard_context(candidate).empty()){*reinterpret_cast<HWND*>(output)=candidate;return FALSE;}return TRUE;
            },reinterpret_cast<LPARAM>(&field));check(field!=nullptr,"The real mathematical puzzle has no editable field");
            const auto before=f.snapshot();SetFocus(f.view);SetFocus(field);pump();
            const auto keyboard=math_keyboard_window(GetAncestor(f.view,GA_ROOT)),bar=f.reader->bar();RECT keys{},initial{};
            check(keyboard&&IsWindowVisible(keyboard),"The real mathematical shortcut keyboard failed to open");
            GetWindowRect(keyboard,&keys);GetWindowRect(bar,&initial);
            check(initial.bottom<=keys.top,"The mathematical keyboard covers the floating reader controls");
            pointer_message(bar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,20),false);
            pointer_message(bar,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-61,-10),false);
            pointer_message(bar,WM_LBUTTONUP,0,MAKELPARAM(4,20),false);RECT moved{};GetWindowRect(bar,&moved);
            check(moved.left<initial.left&&moved.top<initial.top&&moved.bottom<=keys.top,
                "The reader stopped dragging on either axis above the mathematical keyboard");
            pointer_message(bar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,20),false);
            pointer_message(bar,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(4,1020),false);
            pointer_message(bar,WM_LBUTTONUP,0,MAKELPARAM(4,20),false);GetWindowRect(bar,&moved);
            check(moved.bottom<=keys.top,"Dragging the reader down bypassed the mathematical keyboard exclusion");
            check(navigate_lesson_back(f.view),"Back did not consume the original mathematical keyboard");pump();
            const auto after=f.snapshot();check(!IsWindowVisible(keyboard)&&GetFocus()==field&&!after.playing&&!after.allocated&&
                before.key==after.key&&before.index==after.index&&before.offset==after.offset&&edit_text(field).empty(),
                "Opening, dragging or closing mathematical shortcuts changed playback, focus or the answer");
            std::cout<<"PASS real mathematical shortcuts leave the reader fully visible and draggable, with bounded placement and unchanged released position/focus/answer\n";
        }
        {
            auto lesson=model();curriculum::Block source;source.type=curriculum::BlockType::code;
            source.stable_id="fullscreen_source";source.language="cpp";source.code="#include <iostream>\nint main() {}\n";
            source.address={0,1,{},{}};lesson.pages[0].blocks.push_back(source);
            Fixture f(backend,false,true,true,"es",std::move(lesson));f.reader->toggle();
            wait([&]{const auto current=f.snapshot();return current.playing&&current.index==1&&current.offset>200;},"existing real Kokoro recording before fullscreen");
            f.reader->pause();const auto paused=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));
            SendMessageW(child(f.view,1120),BM_CLICK,0,0);pump();f.reader->content_changed();pump();
            const auto expanded=f.snapshot();
            check(expanded.planned&&!expanded.playing&&!expanded.allocated&&expanded.key==paused.key&&expanded.index==paused.index&&expanded.offset==paused.offset&&
                expanded.segments.size()==paused.segments.size(),"Readonly fullscreen or a later content notification discarded the existing plan/position");
            SendMessageW(f.view,WM_KEYDOWN,VK_ESCAPE,0);pump();f.reader->content_changed();pump();
            const auto restored=f.snapshot();const auto after_history=narration::read_history(read_lesson_state(f.view));
            check(restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset&&restored.heard_through==paused.heard_through&&
                !restored.playing&&!restored.allocated&&history.entries.size()==after_history.entries.size(),
                "Leaving readonly fullscreen reset playback, listening evidence or the bounded position history");
            for(std::size_t i=0;i<paused.segments.size();++i)check(restored.segments[i].recording==paused.segments[i].recording,
                "A presentation-only fullscreen transition replaced a valid Kokoro recording");
            f.reader->toggle();const auto resuming=f.snapshot();
            check(resuming.allocated&&resuming.index==paused.index&&std::abs(resuming.offset-paused.offset)<=1,
                "Playback after fullscreen failed to resume the exact existing recording offset");f.reader->pause();
            std::cout<<"PASS readonly fullscreen and later notifications preserve the real Kokoro script/key/recordings, exact released pause and resume\n";
            const auto page=lesson_page_state(f.view);std::vector<windows::NarrationSegment> old_script;
            for(const auto& region:pre_caption_lesson_reading_regions(f.view)){const auto sentences=backend->plan(region.source_text,"es");old_script.insert(old_script.end(),sentences.begin(),sentences.end());}
            const auto old_key=backend->identity(page.narration_page_identity,"es","ef_dora",old_script);f.reader.reset();
            update_lesson_preferences(f.view,[&](state::StateData& data){
                auto history=narration::begin({},"other-paused","owner-a",{0,100,0,false},{},"other-page-a");
                history=narration::begin(std::move(history),"other-finished","owner-b",{0,200,1,false},{},"other-page-b");
                history=narration::checkpoint(std::move(history),"other-finished","owner-b",{0,200,1,true});
                const narration::Position terminal{static_cast<std::int64_t>(old_script.size()-1),321,0,true};
                history=narration::begin(std::move(history),old_key,"owner-c",terminal,{},page.narration_page_identity);
                history=narration::checkpoint(std::move(history),old_key,"owner-c",terminal);narration::write_history(data,history);
            });
            for(int reopen=0;reopen<2;++reopen){
                f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto terminal=f.snapshot();
                check(terminal.key==paused.key&&terminal.finished&&terminal.index+1==terminal.segments.size()&&!terminal.playing&&!terminal.allocated,
                    "A formerly completed page ending before its restored code label lost its valid final bookmark on migration/reopen");
                const auto terminal_history=narration::read_history(read_lesson_state(f.view));
                check(terminal_history.entries.size()==3&&narration::restore(terminal_history,terminal.key,terminal.segments.size()).has_value(),
                    "A completed heading migration stored an invalid final segment or changed the three-slot limit");
                for(std::size_t i=0;i<paused.segments.size();++i)check(terminal.segments[i].recording==paused.segments[i].recording,
                    "Migrating a completed page regenerated an existing valid recording");f.reader.reset();
            }
            std::cout<<"PASS appended original code heading preserves finished pages across migration and second restore, valid terminal position, existing audio and three slots\n";

        }
        {
            Fixture f(backend);
            {
                const auto bar=f.reader->bar();RECT initial{},parent{};GetWindowRect(bar,&initial);GetClientRect(f.view,&parent);
                check(initial.right-initial.left<parent.right/2,"The reader controls still form a full-width footer");
                const auto shape=CreateRectRgn(0,0,0,0);const auto kind=GetWindowRgn(bar,shape);
                check(kind!=ERROR&&!PtInRegion(shape,0,0)&&PtInRegion(shape,(initial.right-initial.left)/2,(initial.bottom-initial.top)/2),
                    "The compact reader has no actual rounded floating outline");DeleteObject(shape);
                const auto before=f.snapshot();
                const auto pane=GetParent(before.segments.front().target);RECT pane_bounds{},view_bounds{};
                GetWindowRect(pane,&pane_bounds);MapWindowPoints(nullptr,f.view,reinterpret_cast<POINT*>(&pane_bounds),2);GetClientRect(f.view,&view_bounds);
                check(pane_bounds.bottom==view_bounds.bottom&&!(GetWindowLongPtrW(pane,GWL_STYLE)&WS_VSCROLL),
                    "Floating reader reserved a footer or exposed the page scrollbar");
                for(int id=6500;id<6507;++id)UpdateWindow(GetDlgItem(bar,id));
                SendMessageW(bar,WM_TIMER,901,0);
                for(int id=6500;id<6507;++id)check(!GetUpdateRect(GetDlgItem(bar,id),nullptr,FALSE),
                    "An idle reader timer invalidated an unchanged icon");
                const auto early_down=GetTickCount64();pointer_message(bar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,30),true);
                pointer_message(bar,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-76,-10),true);
                RECT early{};GetWindowRect(bar,&early);check(EqualRect(&initial,&early),"Reader dragging ignored its 90ms distinction");
                wait([&]{return GetTickCount64()-early_down>=95;},"rejected touchscreen reader stream",1000);
                pointer_message(bar,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-76,-10),true);pointer_message(bar,WM_LBUTTONUP,0,MAKELPARAM(4,30),true);
                GetWindowRect(bar,&early);check(EqualRect(&initial,&early),"An early displaced touch stream armed a drag later instead of remaining rejected");
                const auto held_down=GetTickCount64();pointer_message(bar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,30),true);
                wait([&]{return GetTickCount64()-held_down>=95;},"stationary touchscreen reader hold",1000);
                pointer_message(bar,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-76,-10),true);pointer_message(bar,WM_LBUTTONUP,0,MAKELPARAM(4,30),true);
                RECT moved{};GetWindowRect(bar,&moved);check(moved.left<initial.left&&moved.top<initial.top,"The compact reader cannot move on both axes");
                SetWindowPos(f.view,nullptr,0,0,960,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();
                RECT reflow{};GetWindowRect(bar,&reflow);check(EqualRect(&moved,&reflow),"Reflow or resizing reset a manually moved reader position");
                SetWindowPos(f.view,nullptr,0,0,980,720,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();
                const auto after=f.snapshot();check(!after.allocated&&!after.playing&&before.index==after.index&&before.offset==after.offset&&before.key==after.key,
                    "Moving or laying out the reader changed playback state");
                const auto play_button=GetDlgItem(bar,6504);RECT button_bounds{},button_drag_before{};GetWindowRect(play_button,&button_bounds);GetWindowRect(bar,&button_drag_before);
                pointer_message(play_button,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,12),false);
                check(GetCapture()==bar,"A reader icon retained a private button capture instead of the floating drag owner");
                POINT start{12,12};MapWindowPoints(play_button,bar,&start,1);
                pointer_message(bar,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(start.x-65,start.y-30),false);
                pointer_message(bar,WM_LBUTTONUP,0,MAKELPARAM(start.x,start.y),false);RECT button_drag_after{};GetWindowRect(bar,&button_drag_after);
                check(button_drag_after.left<button_drag_before.left&&button_drag_after.top<button_drag_before.top,
                    "Dragging from a real reader icon did not move the whole floating bar");
                check(!f.snapshot().playing&&!f.snapshot().allocated&&f.snapshot().index==before.index,
                    "Dragging the playback icon also clicked it or changed the reading position");
                const auto click_icon=[bar](HWND button){
                    RECT bounds{};GetClientRect(button,&bounds);POINT point{bounds.right/2,bounds.bottom/2};
                    pointer_message(button,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y),false);
                    check(GetCapture()==bar,"Pressing a reader icon failed to transfer capture to its floating bar");
                    MapWindowPoints(button,bar,&point,1);
                    pointer_message(bar,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y),false);
                    check(GetCapture()==nullptr,"Activating a reader icon left mouse capture allocated");
                };
                update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=1.;});
                f.reader->settings_changed();const auto before_click=f.snapshot();
                const auto speed_button=GetDlgItem(bar,6505);click_icon(speed_button);
                check(std::get<double>(read_lesson_state(f.view).preferences.at("lesson_narration_settings").at("rate"))==1.5,
                    "A captured mouse click on the speed icon did not activate exactly once from 1X to 1.5X");
                const auto after_click=f.snapshot();
                check(!after_click.playing&&!after_click.allocated&&before_click.key==after_click.key&&before_click.index==after_click.index&&before_click.offset==after_click.offset,
                    "Changing speed through the actual icon changed the released playback position");
                SendMessageW(speed_button,BM_CLICK,0,0);
                check(std::get<double>(read_lesson_state(f.view).preferences.at("lesson_narration_settings").at("rate"))==2.&&GetCapture()==nullptr,
                    "Accessible BM_CLICK failed to activate the speed icon once without creating mouse capture");
                click_icon(play_button);
                check(f.snapshot().allocated,"A captured mouse click on Play failed to allocate the actual media player");
                wait([&]{return f.snapshot().playing;},"actual floating-icon playback");
                const auto icon_playing=f.snapshot();click_icon(play_button);const auto icon_paused=f.snapshot();
                check(!icon_paused.playing&&!icon_paused.allocated&&icon_paused.key==icon_playing.key&&icon_paused.index==icon_playing.index&&icon_paused.offset>=icon_playing.offset,
                    "A captured mouse click on Pause failed to release the real player at its current position");
                SendMessageW(play_button,BM_CLICK,0,0);
                check(f.snapshot().allocated&&GetCapture()==nullptr,"Accessible Play failed to resume the actual player without capture");
                SendMessageW(play_button,BM_CLICK,0,0);
                check(!f.snapshot().playing&&!f.snapshot().allocated&&GetCapture()==nullptr,
                    "Accessible Pause failed to release the actual player and capture");
                std::cout<<"PASS compact reader: immediate real-mouse icon dragging, promoted-touch 90ms/rejected-wheel distinction and unchanged playback/reflow\n";
                std::cout<<"PASS floating reader icons: captured mouse and accessible speed/play/pause activation, exact single dispatch and released media/capture\n";
            }
            f.reader->toggle();
            wait([&]{const auto s=f.snapshot();return s.playing&&s.index==1&&s.offset>700&&s.offset<2500;},"paragraph real playback");
            const auto moving=f.snapshot();const auto& segment=moving.segments[moving.index];
            const auto pending=reader_theme::unread_color(reader_theme::background(segment.target));
            check(color(segment.target,0)!=pending&&color(segment.target,static_cast<LONG>(wide(segment.source).size()-2))==pending,
                "Read and pending words do not coexist inside the real paragraph");
            auto expected_visibility_settings=read_lesson_state(f.view).preferences.at("lesson_narration_settings");
            expected_visibility_settings["controls_visible"]=false;
            f.reader->set_controls_visible(false);const auto hidden=f.snapshot();
            check(!f.reader->controls_visible()&&!IsWindowVisible(f.reader->bar())&&!hidden.allocated&&!hidden.playing
                &&hidden.key==moving.key&&hidden.index==moving.index&&hidden.offset>=moving.offset,
                "Hiding the controls did not use the existing exact-position pause/resource-release policy");
            const auto hidden_state=read_lesson_state(f.view);
            check(hidden_state.preferences.at("lesson_narration_settings")==expected_visibility_settings,
                "Hiding the controls failed to persist visibility or changed a voice/engine/rate/generation setting");
            f.reader->set_controls_visible(false);
            check(read_lesson_state(f.view)==hidden_state&&f.snapshot().offset==hidden.offset,
                "An idempotent visibility request rewrote state or advanced the paused recording");
            f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
            const auto restored_hidden=f.snapshot();
            check(!f.reader->controls_visible()&&!IsWindowVisible(f.reader->bar())&&!restored_hidden.allocated&&!restored_hidden.playing
                &&restored_hidden.key==hidden.key&&restored_hidden.index==hidden.index&&restored_hidden.offset==hidden.offset,
                "Recreating the reader lost the hidden preference or its bounded saved position");
            f.reader->toggle_controls();const auto revealed=f.snapshot();
            check(f.reader->controls_visible()&&IsWindowVisible(f.reader->bar())&&!revealed.allocated&&!revealed.playing
                &&revealed.key==hidden.key&&revealed.index==hidden.index&&revealed.offset==hidden.offset,
                "Showing the controls started/restarted playback or changed the exact paused recording");
            expected_visibility_settings["controls_visible"]=true;
            check(read_lesson_state(f.view).preferences.at("lesson_narration_settings")==expected_visibility_settings,
                "Showing the controls failed to persist visibility or changed another narration setting");
            f.reader->toggle();const auto visibility_resume=f.snapshot();
            check(visibility_resume.allocated&&visibility_resume.index==hidden.index&&std::abs(visibility_resume.offset-hidden.offset)<=1,
                "Explicit playback after showing the controls failed to resume the existing exact offset");
            std::cout<<"PASS header-only visibility, released exact pause, persisted hidden controls and explicit real-audio resume\n";
            f.reader->pause();const auto paused=f.snapshot();check(!paused.allocated&&!paused.playing&&paused.offset>=moving.offset,"Pause did not preserve position and release MediaPlayer");
            pump();check(f.snapshot().offset==paused.offset,"A released paused position kept advancing");
            f.reader->toggle();const auto opening=f.snapshot();check(opening.allocated&&std::abs(opening.offset-paused.offset)<=1,"Resume restarted instead of requesting the exact saved offset");
            wait([&]{const auto s=f.snapshot();return s.playing&&s.offset>=paused.offset;},"real resumed media position");
            f.reader->pause();const auto before=f.snapshot();f.reader->seek_seconds(-3);const auto back=f.snapshot();
            check(back.index<before.index||(back.index==before.index&&back.offset<before.offset),"Rewind did not move actual reading position");
            check(color(segment.target,static_cast<LONG>(wide(segment.source).size()-2))==pending,"Rewind left later words read");
            f.reader->seek_seconds(3);const auto forward=f.snapshot();check(forward.index>back.index||forward.offset>back.offset,"Manual forward did not advance visual position");
            f.reader->restart();check(f.snapshot().index==0&&f.snapshot().offset<100&&f.snapshot().heard_through==0,"Restart kept old playback or listening evidence");
            wait([&]{return f.snapshot().finished;},"natural page finish");const auto ended=f.snapshot();
            check(!ended.allocated&&!ended.playing&&ended.index+1==ended.segments.size(),"Finishing did not retain the final released position");
            f.reader->toggle();check(f.snapshot().finished&&!f.snapshot().allocated,"Finished page restarted automatically");
            const auto first_key=ended.key;
            f.page(1);f.reader->toggle();wait([&]{return f.snapshot().playing&&f.snapshot().offset>150;},"second page pause");f.reader->pause();const auto second_key=f.snapshot().key;
            f.page(2);f.reader->toggle();wait([&]{return f.snapshot().finished;},"third page finish");const auto third_key=f.snapshot().key;
            auto history=narration::read_history(read_lesson_state(f.view));check(history.entries.size()==3,"Finished pages did not occupy one of three slots");
            f.page(3);f.reader->toggle();wait([&]{return f.snapshot().playing&&f.snapshot().offset>150;},"fourth page pause");f.reader->pause();
            history=narration::read_history(read_lesson_state(f.view));check(history.entries.size()==3,"Fourth page exceeded the three-position limit");
            check(std::ranges::none_of(history.entries,[&](const auto& item){return item.key==first_key;}),"Oldest finished page escaped normal eviction");
            check(std::ranges::any_of(history.entries,[&](const auto& item){return item.key==second_key;})&&
                  std::ranges::any_of(history.entries,[&](const auto& item){return item.key==third_key&&item.position.finished;}),"Recent paused/finished positions were lost");
            const auto previous_voice=f.snapshot().key;
            update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_alex");});
            f.reader->settings_changed();f.ready();
            check(f.snapshot().key!=previous_voice&&f.snapshot().offset==0&&f.snapshot().index==0,
                "A changed recording inherited the old voice's position");
            f.reader->toggle();wait([&]{return f.snapshot().playing;},"second real Kokoro voice");f.reader->pause();
            history=narration::read_history(read_lesson_state(f.view));check(history.entries.size()==3,"Changing a voice allocated a fourth page slot");
            check(std::ranges::none_of(history.entries,[&](const auto& item){return item.key==previous_voice;}),"Both voices of one page consumed separate slots");
            check(std::ranges::any_of(history.entries,[&](const auto& item){return item.key==second_key;})&&
                  std::ranges::any_of(history.entries,[&](const auto& item){return item.key==third_key;}),"A voice change evicted another recent page");
            std::cout<<"PASS real Kokoro shading, pause/release, exact resume, seek, restart, finish and bounded three-page history\n";
        }
        {
            Fixture f(backend);f.page(1);const auto current=f.snapshot();const auto page=lesson_page_state(f.view);
            check(std::ranges::none_of(current.segments,[](const auto& segment){return segment.source=="Prueba.";}),
                "A later page still repeats the lesson title");
            const auto prefix=backend->plan(page.lesson_title,"es");auto legacy=prefix;
            for(const auto& region:lesson_reading_regions(f.view)){
                const auto planned=backend->plan(region.source_text,"es");legacy.insert(legacy.end(),planned.begin(),planned.end());
            }
            const auto old_key=backend->identity(page.narration_page_identity,"es","ef_dora",legacy);
            check(old_key!=current.key&&!prefix.empty(),"Legacy-title migration fixture did not describe the previous exact script");
            for(const bool finished:{false,true}){
                f.reader.reset();
                narration::Position position{static_cast<std::int64_t>(prefix.size()+current.segments.size()-1),300,
                    static_cast<std::int64_t>(prefix.size()),finished};
                update_lesson_preferences(f.view,[&](state::StateData& data){
                    auto history=narration::begin({},"other-paused","owner-a",{0,100,0,false},{},"other-page-a");
                    history=narration::begin(std::move(history),"other-finished","owner-b",{0,200,1,true},{},"other-page-b");
                    history=narration::checkpoint(std::move(history),"other-finished","owner-b",{0,200,1,true});
                    history=narration::begin(std::move(history),old_key,"owner-c",position,{},page.narration_page_identity);
                    if(finished)history=narration::checkpoint(std::move(history),old_key,"owner-c",position);
                    narration::write_history(data,history);
                });
                f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto restored=f.snapshot();
                check(restored.key==current.key&&restored.index+1==current.segments.size()&&restored.offset==300
                    &&restored.finished==finished&&!restored.allocated&&!restored.playing&&restored.heard_through==0,
                    "Removing only the old repeated title lost the exact paused/final position");
                const auto history=narration::read_history(read_lesson_state(f.view));
                check(history.entries.size()==3&&std::ranges::none_of(history.entries,[&](const auto& entry){return entry.key==old_key;})
                    &&std::ranges::any_of(history.entries,[](const auto& entry){return entry.key=="other-paused"&&entry.position.milliseconds==100;})
                    &&std::ranges::any_of(history.entries,[](const auto& entry){return entry.key=="other-finished"&&entry.position.finished;}),
                    "Title migration created a parallel bookmark or evicted another recent page");
            }
            std::cout<<"PASS exact legacy-title bookmark migration preserves offset, final state and the same three slots\n";
        }
        {
            auto lesson=model(true,false);auto& puzzle=lesson.pages[0].blocks[1];puzzle.type=curriculum::BlockType::order_code;
            puzzle.id=puzzle.stable_id="order";puzzle.answers.clear();puzzle.fragments={"return 0;","int main() {","}"};puzzle.correct_order={1,0,2};
            Fixture f(backend,false,true,true,"es",std::move(lesson));
            const auto card=child(f.view,1105),main_row=GetDlgItem(card,101);check(main_row!=nullptr,"Missing actual order row for legacy bookmark fixture");
            SendMessageW(main_row,WM_SYSKEYDOWN,VK_UP,LPARAM{1}<<29);SendMessageW(child(f.view,1103),BM_CLICK,0,0);f.reader->pause();f.ready();
            const auto current=f.snapshot();const auto page=lesson_page_state(f.view);
            for(const bool before_captions:{false,true}){
            const auto legacy_regions=before_captions?pre_caption_lesson_reading_regions(f.view):legacy_lesson_reading_regions(f.view);
            std::vector<windows::NarrationSegment> legacy;std::size_t paragraph=0,fragment=0;
            for(const auto& region:legacy_regions){const auto source=backend->plan(region.source_text,"es");
                if(region.block_index==0)paragraph=legacy.size();if(IsChild(card,region.target))fragment=legacy.size();
                legacy.insert(legacy.end(),source.begin(),source.end());}
            const auto old_key=backend->identity(page.narration_page_identity,"es","ef_dora",legacy);
            check(old_key!=current.key&&paragraph>0&&(before_captions||fragment>paragraph),"The migration fixture failed to reconstruct the exact previous source script");
            for(int mode=0;mode<(before_captions?2:3);++mode){f.reader.reset();
                narration::Position old{static_cast<std::int64_t>(mode==1?legacy.size()-1:mode==2?fragment:paragraph),321,mode==1?static_cast<std::int64_t>(legacy.size()):0,mode==1};
                update_lesson_preferences(f.view,[&](state::StateData& data){
                    auto history=narration::begin({},"other-paused","owner-a",{0,100,0,false},{},"other-page-a");
                    history=narration::begin(std::move(history),"other-finished","owner-b",{0,200,1,false},{},"other-page-b");
                    history=narration::checkpoint(std::move(history),"other-finished","owner-b",{0,200,1,true});
                    history=narration::begin(std::move(history),old_key,"owner-c",old,{},page.narration_page_identity);
                    if(old.finished)history=narration::checkpoint(std::move(history),old_key,"owner-c",old);
                    narration::write_history(data,history);
                });
                f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto restored=f.snapshot();
                check(restored.key==current.key&&!restored.playing&&!restored.allocated,"An exact legacy script migration started playback or failed to select the new script");
                check(restored.segments.size()==current.segments.size()&&std::ranges::equal(restored.segments,current.segments,[](const auto& a,const auto& b){
                    return a.recording==b.recording&&a.duration_ms==b.duration_ms&&a.source==b.source;
                }),"Adding only original code labels regenerated or replaced compatible existing audio");
                if(mode==0)check(restored.index==paragraph&&restored.offset==321&&!restored.finished,"An unchanged real segment lost its exact paused offset during instruction migration");
                if(mode==1)check(restored.index+1==restored.segments.size()&&restored.offset==321&&restored.finished,
                    "A finished page reset instead of retaining its real last segment and final bookmark");
                if(mode==2)check(restored.offset==0&&!restored.finished&&restored.heard_through==0&&restored.index>0
                    &&restored.segments[restored.index].source.starts_with("Mantén pulsado"),
                    "An excluded old code fragment reset the page, invented timing or credited unheard feedback");
                const auto history=narration::read_history(read_lesson_state(f.view));
                check(history.entries.size()==3&&history.entries.front().key==current.key
                    &&std::ranges::none_of(history.entries,[&](const auto& entry){return entry.key==old_key;})
                    &&std::ranges::any_of(history.entries,[](const auto& entry){return entry.key=="other-paused"&&entry.position.milliseconds==100;})
                    &&std::ranges::any_of(history.entries,[](const auto& entry){return entry.key=="other-finished"&&entry.position.finished;}),
                    "Instruction migration created a parallel history, changed recency or discarded either other recent page");
            }
            }
            std::cout<<"PASS exact old order-code and caption-free script migration: real paused/final offsets, removed-fragment provenance and the same three page slots\n";
        }
        {
            Fixture f(backend,false,true,true,"es",compiler_model());
            check(std::ranges::none_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==1&&region.is_exercise;}),
                "An editable compiler without a fresh verdict became an unanswered puzzle gate");
            f.run_program();const auto checked=f.snapshot();
            check(checked.index>0&&checked.segments[checked.index].feedback&&checked.segments[checked.index].correct&&checked.offset==0,
                "A freshly checked output did not select its own correct feedback");
            const auto regions=lesson_reading_regions(f.view);
            check(std::ranges::any_of(regions,[](const auto& region){return region.block_index==1&&region.feedback&&region.is_exercise&&region.solved;}),
                "Fresh compiler feedback lacks the ordinary solved-feedback listening boundary");
            f.reader->seek_seconds(60);const auto skipped=f.snapshot();
            check(skipped.segments[skipped.index].block==2&&skipped.heard_through<=static_cast<std::int64_t>(checked.index)&&!skipped.finished,
                "An explicit prioritized seek failed to move normally or falsely credited unheard compiler feedback");
            f.reader->seek_seconds(-60);f.reader->toggle();
            wait([&]{const auto value=f.snapshot();if(!value.playing)return false;const auto& part=value.segments[value.index];
                return part.feedback&&part.source.find("std::cout")!=std::string::npos&&part.timings.size()>3
                    &&value.offset>=part.timings[1].start_ms&&value.offset<part.timings.back().start_ms;
            },"real word-aligned compiler feedback playback");
            f.reader->pause();const auto paused=f.snapshot();const auto& current=paused.segments[paused.index];
            const auto pending=reader_theme::unread_color(reader_theme::background(current.target));
            const auto displayed=wide(edit_text(current.target));const auto technical_word=displayed.find(L"std::cout"),later_word=displayed.find(L"previsto");
            check(technical_word!=std::wstring::npos&&later_word!=std::wstring::npos
                    &&color(current.target,static_cast<LONG>(technical_word))!=pending&&color(current.target,static_cast<LONG>(later_word))==pending,
                "Compiler feedback did not keep read and pending technical words inside the same narration segment");
            f.reader->toggle();const auto resumed=f.snapshot();
            check(resumed.allocated&&std::abs(resumed.offset-paused.offset)<=1,"Resuming compiler feedback restarted its recording");
            wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==2;},
                "fully heard correct compiler feedback continues into the following paragraph");
            wait([&]{return f.snapshot().finished;},"checked compiler narration reaches the real page end");
            const auto previous_prose=child(f.view,1000);const auto previous_ink=color(previous_prose,0);
            check(previous_ink!=reader_theme::unread_color(reader_theme::background(previous_prose)),"The completed paragraph remained pending");
            f.edit_program(compiler_source("Mal"));
            check(color(previous_prose,0)==previous_ink,
                "Removing an old compiler verdict temporarily darkened previously read prose during replanning");
            f.ready();f.run_program();const auto wrong=f.snapshot();
            check(wrong.index>0&&wrong.segments[wrong.index].feedback&&!wrong.segments[wrong.index].correct&&wrong.offset==0,
                "An incorrect fresh output replayed the page beginning instead of its own feedback");
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&!value.preparing&&value.segments[value.index].feedback;},
                "incorrect compiler feedback stops before following prose");
            check(!f.snapshot().finished,"Incorrect compiler feedback completed the page");
            f.reader->seek_seconds(60);check(f.snapshot().segments[f.snapshot().index].feedback,
                "Manual forward escaped an incorrect compiler-feedback boundary");
            f.reader->seek_seconds(-3);f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].feedback;},
                "incorrect compiler feedback can be replayed after rewinding");
            f.reader->pause();f.edit_program(compiler_source("Bien"));f.ready();
            check(f.snapshot().index>0&&!f.snapshot().segments[f.snapshot().index].feedback,
                "Correcting a compiled source restarted the page or retained its old verdict");
            f.run_program();const auto corrected=f.snapshot();
            check(corrected.index>0&&corrected.offset==0&&corrected.segments[corrected.index].feedback&&corrected.segments[corrected.index].correct,
                "Corrected compiler output did not select the newly generated feedback");
            check(narration::read_history(read_lesson_state(f.view)).entries.size()<=3,
                "Compiler verdict replacements created an extra history outside the three page positions");
            f.reader->toggle();wait([&]{return f.snapshot().finished;},"corrected compiler feedback resumes through the remaining page");
            const auto previously_recorded=f.snapshot().segments;
            f.run_program();const auto repeated=f.snapshot();
            check(repeated.offset==0&&repeated.segments[repeated.index].feedback&&repeated.segments[repeated.index].correct
                    &&color(child(f.view,1000),0)==previous_ink,"An identical fresh verdict lost its own start or darkened previously read text");
            check(std::ranges::any_of(previously_recorded,[&](const auto& part){return part.recording==repeated.segments[repeated.index].recording;}),
                "An identical compiler verdict invalidated its compatible recording cache");
            f.reader->seek_seconds(60);const auto repeated_skip=f.snapshot();
            check(repeated_skip.segments[repeated_skip.index].block==2&&repeated_skip.heard_through<=static_cast<std::int64_t>(repeated.index),
                "An identical fresh verdict reused old listening credit to skip its narration");
            std::cout<<"PASS real compiler output verdicts: technical word shading, released pause/exact resume, heard-feedback continuation, wrong stop/replay, corrected source and bounded history\n";
        }
        {
            // Reproduce the authored P1/U1/L2/P5 ordering: an already solved
            // prediction precedes two independently checked C++ editors.
            // Only the saved page of this owned QA profile is seeded; no
            // authored text, expected output, answer or dependency is changed.
            auto authored=windows::load_lesson(std::filesystem::path(build::source_root)/
                "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json");
            const auto& page=authored.pages.at(4);const auto prediction=page.blocks.at(1).answers.at(0);
            auto corrected_source=page.blocks.at(6).initial_code;
            const auto original_output=page.blocks.at(3).output_check->cases.at(0).outputs.at(0);
            const auto expected_output=page.blocks.at(6).output_check->cases.at(0).outputs.at(0);
            const auto reviewed_wrong_output=page.blocks.at(6).output_check->cases.at(0).review_feedback.at(0).outputs.at(0);
            const auto literal=corrected_source.find('"'+original_output+'"');check(literal!=std::string::npos,"The authored P5 compiler fixture lost its original literal");
            auto reviewed_wrong_source=corrected_source;reviewed_wrong_source.replace(literal+1,original_output.size(),reviewed_wrong_output);
            corrected_source.replace(literal+1,original_output.size(),expected_output);
            Fixture f(backend,false,true,true,"es",std::move(authored),false,4);
            check(lesson_page_state(f.view).index==4,"The actual authored P5 was not restored in its independent QA profile");
            f.answer(prediction);f.reader->restart();f.reader->pause();
            check(f.snapshot().heard_through==0,"Restart falsely preserved earlier solved-prediction listening credit");
            f.run_program(3);const auto correct=f.snapshot();
            const auto earlier=std::ranges::find_if(correct.segments,[](const auto& part){return part.block==1&&part.feedback&&part.correct;});
            check(earlier!=correct.segments.end()&&correct.segments[correct.index].block==3&&correct.segments[correct.index].feedback&&
                correct.segments[correct.index].correct&&correct.offset==0&&correct.heard_through==0,
                "The fresh original compiler check did not take priority independently of the unheard solved prediction");
            const auto unheard_edge=static_cast<std::int64_t>(earlier-correct.segments.begin());
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==5&&value.offset>150;},
                "authored P5 compiler feedback continues through Modificar into its following instructions despite an unheard solved prefix");
            f.reader->pause();const auto continued=f.snapshot();
            check(continued.heard_through<=unheard_edge,"Prioritized compiler continuation credited the skipped solved prediction as heard");
            f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto restored=f.snapshot();
            check(restored.key==continued.key&&restored.index==continued.index&&restored.offset==continued.offset&&restored.heard_through==continued.heard_through&&
                !restored.playing&&!restored.allocated&&narration::read_history(read_lesson_state(f.view)).entries.size()<=3,
                "Reopening a prioritized position changed its exact pause, credited earlier feedback or created another bookmark");
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.index==restored.index&&value.offset>=restored.offset;},
                "restored priority resumes the exact real instruction recording");f.reader->pause();
            f.edit_program(reviewed_wrong_source,6);f.ready();f.run_program(6);const auto wrong=f.snapshot();
            check(wrong.segments[wrong.index].block==6&&wrong.segments[wrong.index].feedback&&!wrong.segments[wrong.index].correct,
                "The authored second editor reused the original editor's correct verdict");
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&value.segments[value.index].block==6&&value.segments[value.index].feedback;},
                "authored incorrect compiler feedback stops before the following unsolved multiple choice");
            const auto guide_end=f.snapshot();double guide_duration=0;auto guide_start=guide_end.index;
            for(std::size_t i=0;i<guide_end.segments.size();++i)if(guide_end.segments[i].block==6&&guide_end.segments[i].feedback){
                guide_start=std::min(guide_start,i);guide_duration+=guide_end.segments[i].duration_ms;}
            const auto leave_seconds=static_cast<int>(std::ceil(guide_duration/1000.))+1;
            f.reader->seek_seconds(-leave_seconds);const auto outside=f.snapshot();
            check(outside.index<guide_start&&!outside.allocated&&outside.heard_through<=unheard_edge,
                "Rewinding across the incorrect guide's beginning failed to return to original page text without false hearing");
            f.reader->seek_seconds(leave_seconds);const auto returned=f.snapshot();
            check(returned.segments[returned.index].block==6&&returned.segments[returned.index].feedback&&!returned.segments[returned.index].correct&&
                returned.heard_through==outside.heard_through&&!returned.allocated&&!returned.finished,
                "Returning to the incorrect guide lost the other correct block's original permission or escaped its own stop");
            f.reader->seek_seconds(-1);f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==6&&value.segments[value.index].feedback;},
                "authored incorrect compiler feedback replays after actual rewind");f.reader->pause();
            f.edit_program(corrected_source,6);f.ready();f.run_program(6);const auto corrected=f.snapshot();
            check(corrected.segments[corrected.index].block==6&&corrected.segments[corrected.index].feedback&&corrected.segments[corrected.index].correct&&corrected.offset==0,
                "Correcting the actual second editor did not select its own fresh correct feedback");
            const auto recording=corrected.segments[corrected.index].recording;
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==7;},
                "correct second-editor feedback continues normally and stops at the actual later unsolved puzzle");
            check(f.snapshot().heard_through<=unheard_edge&&!f.snapshot().finished,"Continuation invented prefix listening or crossed an unsolved later puzzle");
            f.run_program(6);const auto repeated=f.snapshot();
            check(repeated.offset==0&&repeated.segments[repeated.index].recording==recording&&repeated.heard_through<=unheard_edge,
                "Repeating an identical real output invalidated its recording or reused old listening credit");
            f.reader->seek_seconds(-1);const auto back=f.snapshot();
            check(back.index<repeated.index||back.offset<repeated.offset,"Prioritized compiler rewind did not move the real paused position");
            f.reader->seek_seconds(60);const auto sought=f.snapshot();
            check(sought.segments[sought.index].block==7&&!sought.finished&&sought.heard_through==repeated.heard_through&&!sought.allocated,
                "Prioritized manual forward invented hearing, allocated paused audio or bypassed the later unanswered puzzle");
            f.reader->restart();f.reader->pause();f.reader->seek_seconds(60);const auto restarted=f.snapshot();
            check(restarted.segments[restarted.index].block==1&&restarted.segments[restarted.index].feedback&&restarted.heard_through==0&&!restarted.finished,
                "Restart kept answer priority instead of restoring the ordinary first unheard-feedback boundary");
            check(narration::read_history(read_lesson_state(f.view)).entries.size()<=3,"Answer priority introduced a parallel narration history");
            std::cout<<"PASS authored P1/U1/L2/P5: unheard solved prediction, fresh real C++ priority/continuation, exact released restore, wrong replay, later puzzle bounds and cache-preserving seeks without false hearing\n";
        }
        {
            // Restart removes the explicit answer priority. Ordinary page
            // playback still requires truly heard feedback before proceeding.
            Fixture f(backend,true);f.answer("<<");f.reader->restart();f.reader->pause();f.reader->toggle();
            std::size_t repeated_index=0;
            wait([&]{const auto s=f.snapshot();if(!s.playing||!s.segments[s.index].feedback||s.offset<120)return false;
                const auto& current=s.segments[s.index];const bool repeated=s.index>0&&s.segments[s.index-1].feedback&&s.segments[s.index-1].source==current.source;
                if(repeated)repeated_index=s.index;return repeated;},"later repeated correct-feedback sentence actual playback");
            f.reader->seek_seconds(60);wait([&]{return !f.snapshot().playing&&!f.snapshot().allocated;},"skipped feedback gate");const auto skipped=f.snapshot();
            check(!skipped.finished&&skipped.waiting_gate&&skipped.heard_through<=static_cast<std::int64_t>(skipped.index),"Manual forward credited unheard correct feedback");
            check(skipped.heard_through<=static_cast<std::int64_t>(repeated_index),"An earlier identical sentence credited an unheard occurrence");
            check(skipped.segments[skipped.index].feedback,"A skip crossed feedback into restricted later text");
            f.reader->seek_seconds(-60);f.reader->toggle();wait([&]{return f.snapshot().finished;},"fully heard feedback unlocks later prose");
            check(f.snapshot().heard_through>static_cast<std::int64_t>(skipped.index),"Actually replayed feedback did not earn completion");
            f.reader->restart();f.reader->pause();check(f.snapshot().heard_through==0,"Restart retained completed feedback evidence");
            std::cout<<"PASS correct feedback manual seek does not earn listening; real full replay continues\n";
        }
        {
            Fixture f(backend,true,false);f.answer(">>");f.reader->toggle();
            wait([&]{const auto s=f.snapshot();return s.playing&&s.segments[s.index].feedback;},"incorrect feedback actual playback");
            wait([&]{const auto s=f.snapshot();return !s.playing&&!s.allocated&&!s.preparing&&s.segments[s.index].feedback;},"incorrect feedback stop");
            check(!f.snapshot().finished,"Incorrect final feedback marked the whole page finished");
            f.reader->seek_seconds(-3);f.reader->toggle();wait([&]{const auto s=f.snapshot();return s.playing&&s.segments[s.index].feedback;},"incorrect feedback replay after rewind");
            f.reader->pause();check(!f.snapshot().allocated,"Incorrect feedback pause retained MediaPlayer");
            std::cout<<"PASS incorrect feedback stops without finishing and replays after rewind\n";
        }
        {
            Fixture f(backend,true,false);f.answer(">>");f.reader->toggle();
            wait([&]{const auto s=f.snapshot();return s.playing&&s.segments[s.index].feedback;},"incorrect feedback before editing");
            f.reader->pause();const auto old=f.snapshot();check(old.index>0,"The feedback fixture unexpectedly started at the page beginning");
            const auto input=child(f.view,1102);CHARRANGE range{0,-1};SendMessageW(input,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
            SendMessageW(input,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"<<"));f.ready();
            const auto editing=f.snapshot();check(!editing.playing&&!editing.allocated&&editing.index>0&&!editing.segments[editing.index].feedback,
                "Removing stale incorrect feedback replayed the page beginning instead of preserving its surviving reading position");
            SendMessageW(child(f.view,1103),BM_CLICK,0,0);f.reader->pause();f.ready();const auto corrected=f.snapshot();
            check(corrected.index>0&&corrected.segments[corrected.index].feedback&&corrected.segments[corrected.index].correct&&corrected.offset==0,
                "A corrected answer restarted the page instead of selecting its own new feedback");
            check(narration::read_history(read_lesson_state(f.view)).entries.size()<=3,
                "Replacing incorrect feedback created parallel unbounded reading history");
            std::cout<<"PASS wrong-to-correct edits retain the nearest real segment and select only the new feedback, without page restart\n";
        }
        {
            auto lesson=model();lesson.pages.resize(1);Fixture f(backend,false,true,true,"es",std::move(lesson),true);
            update_lesson_preferences(f.view,[](state::StateData& data){
                auto history=narration::begin({},"retained-paused","owner-a",{0,120,0,false},{},"retained-page-a");
                history=narration::begin(std::move(history),"retained-finished","owner-b",{0,240,1,false},{},"retained-page-b");
                history=narration::checkpoint(std::move(history),"retained-finished","owner-b",{0,240,1,true});narration::write_history(data,history);
            });
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.index==1&&value.offset>300;},"real playback before deleting one private QA recording");
            f.reader->pause();const auto paused=f.snapshot();const auto missing=paused.segments[paused.index].recording;
            const auto valid=paused.segments.front().recording;
            const auto relative=missing.lexically_relative(f.environment.paths.cache);
            check(!relative.empty()&&relative.begin()->native()!=L".."&&missing!=valid&&std::filesystem::is_regular_file(missing),
                "Recording deletion regression is not confined to this test's independent audio cache");
            const auto digest=[](const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());check(!bytes.empty(),"Cannot inspect an independent cached recording");return state::sha256(bytes);};
            const auto valid_digest=digest(valid);const auto valid_size=std::filesystem::file_size(valid);
            check(std::filesystem::remove(missing),"Cannot remove the one private QA recording for the missing-file regression");
            f.reader->toggle();const auto pending=f.snapshot();
            check(pending.key==paused.key&&pending.index==paused.index&&pending.offset==paused.offset&&!pending.allocated,
                "A missing recording rebuilt the script, reset its exact position or opened an absent file");
            f.ready();wait([&]{const auto value=f.snapshot();return value.playing&&value.index==paused.index;},"regenerated missing recording resumes the real paused segment");
            const auto resumed=f.snapshot();check(std::filesystem::is_regular_file(missing)&&resumed.key==paused.key&&resumed.offset>=paused.offset,
                "Missing-file recovery failed to resume the same recording identity and offset");
            f.reader->pause();check(!f.snapshot().allocated&&digest(valid)==valid_digest&&std::filesystem::file_size(valid)==valid_size,
                "Missing-file recovery retained a paused player or changed another valid cached recording");
            const auto history=narration::read_history(read_lesson_state(f.view));
            check(history.entries.size()==3&&std::ranges::any_of(history.entries,[](const auto& entry){return entry.key=="retained-paused"&&entry.position.milliseconds==120;})
                &&std::ranges::any_of(history.entries,[](const auto& entry){return entry.key=="retained-finished"&&entry.position.finished&&entry.position.milliseconds==240;}),
                "Recovering a deleted audio file changed the other two recent page positions");
            std::cout<<"PASS explicitly deleted private recording: existing worker recovers it at the same offset, preserves valid cache and all three page positions\n";

            // A second, separate recovery keeps the original exact-resume
            // assertions above intact and exercises a seek before new timing
            // metadata is delivered to the UI thread.
            f.reader->toggle();wait([&]{const auto value=f.snapshot();if(!value.playing||value.index!=paused.index||value.offset<=1500)return false;
                return std::ranges::any_of(value.segments[value.index].timings,[&](const auto& timing){return timing.start_ms>value.offset-900&&timing.start_ms<value.offset-100;});},
                "real playback before seeking in one unavailable recording");
            f.reader->pause();const auto before_seek=f.snapshot();const auto& old_part=before_seek.segments[before_seek.index];
            const auto old_word=std::ranges::find_if(old_part.timings,[&](const auto& timing){return timing.start_ms>before_seek.offset-1000&&timing.start_ms<=before_seek.offset;});
            check(old_word!=old_part.timings.end(),"The recovery-seek fixture has no actually read word after its destination");
            const auto character=static_cast<LONG>(old_part.start+old_word->start);
            const auto pending_ink=reader_theme::unread_color(reader_theme::background(old_part.target));
            check(color(old_part.target,character)!=pending_ink,"The real word was not read before deleting its private recording");
            check(old_part.recording==missing&&std::filesystem::remove(missing),"The controlled seek regression did not remove only the same private WAV");
            f.reader->toggle();f.reader->pause();const auto recovering=f.snapshot();
            check(!recovering.segments[recovering.index].available&&!recovering.allocated&&recovering.offset==before_seek.offset,
                "The recovery-seek fixture already delivered replacement timings or changed the paused offset");
            check(color(old_part.target,character)!=pending_ink,"Preparing an unchanged missing recording lost the existing word shading");
            f.reader->seek_seconds(-1);const auto rewound=f.snapshot();
            check(rewound.index==before_seek.index&&rewound.offset==before_seek.offset-1000&&rewound.offset>0
                    &&!rewound.allocated&&!rewound.playing&&rewound.heard_through==before_seek.heard_through,
                "Rewinding without an audio file changed segment identity, listening credit or the exact requested offset");
            check(color(old_part.target,character)==pending_ink,
                "A seek reused an older visible word boundary while the current recording had no timing metadata");
            f.ready();check(f.snapshot().offset==rewound.offset&&!f.snapshot().playing,
                "Replacement timing delivery moved the new rewind position or overrode its pause");
            f.reader->toggle();const auto final_resume=f.snapshot();
            check(final_resume.allocated&&std::abs(final_resume.offset-rewound.offset)<=1,
                "Playback after an unavailable-audio seek did not request the same exact recording offset");
            f.reader->pause();check(digest(valid)==valid_digest&&std::filesystem::file_size(valid)==valid_size,
                "The controlled unavailable-audio seek changed a different valid recording");
            std::cout<<"PASS unavailable recording seek: unchanged offset preserves shading, real rewind restores pending words without invented timing or listening credit, and exact paused resume\n";
        }
        {
            Fixture f(backend,false,true,false);const auto before=f.snapshot();
            check(before.segments.size()>1&&std::ranges::count_if(before.segments,[](const auto& part){return part.available;})==1,
                "Disabled advance generation still prepared all future segments");
            f.reader->toggle();wait([&]{const auto s=f.snapshot();return s.playing&&s.index==1&&s.segments[1].available;},"on-demand next segment after disabling advance generation");
            f.reader->pause();check(!f.snapshot().allocated,"On-demand generation changed pause resource release");
            std::cout<<"PASS advance-generation preference and on-demand reprioritization\n";
        }
        for(const std::string language:{"en-US","en-GB","fr"}){
            Fixture f(backend,false,true,true,language);
            check(lesson_page_state(f.view).narration_language==language,
                "The page's explicit narration language was not preserved");
            // The preference deliberately remains Spanish: the original Android
            // metadata must choose the real voice without translating the text.
            check(state::string_value(read_lesson_state(f.view),"lesson_narration_settings","language")=="es",
                "A page language rewrote the student's global narration preference");
            f.reader->toggle();
            wait([&]{const auto current=f.snapshot();if(!current.playing||current.index>=current.segments.size())return false;
                const auto& part=current.segments[current.index];
                return part.block==0&&part.timings.size()>5&&current.offset>=part.timings[1].start_ms&&
                    current.offset<part.timings.back().start_ms;},"real word-aligned "+language+" playback");
            f.reader->pause();const auto paused=f.snapshot();const auto& part=paused.segments[paused.index];
            const auto pending=reader_theme::unread_color(reader_theme::background(part.target));
            const auto read=static_cast<LONG>(part.start+part.timings.front().start);
            const auto unread=static_cast<LONG>(part.start+part.timings.back().start);
            check(color(part.target,read)!=pending&&color(part.target,unread)==pending,
                "Real "+language+" playback did not show read and pending words inside one technical paragraph");
            check(!paused.allocated&&!paused.playing,"A language-specific pause kept MediaPlayer alive");
            f.reader->toggle();const auto resumed=f.snapshot();
            check(resumed.allocated&&std::abs(resumed.offset-paused.offset)<=1,
                "A language-specific resume lost the exact saved offset");
            f.reader->pause();f.reader->seek_seconds(-60);
            check(color(part.target,read)==pending,"Rewinding did not restore pending technical words in "+language);
            std::cout<<"PASS real "<<language<<" Kokoro metadata, technical word shading, released pause, exact resume and rewind\n";
        }
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL native LessonReader: "<<failure.what()<<'\n';return 1;}
}
