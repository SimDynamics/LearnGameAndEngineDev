#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
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
struct Fixture{
    WindowsEnvironment environment;HWND host=nullptr,view=nullptr;std::unique_ptr<LessonReader> reader;
    Fixture(std::shared_ptr<windows::NarrationWorker> backend,bool exercise=false,bool after=true,bool advance=true,std::string_view language="es",std::optional<curriculum::Lesson> supplied={},bool private_audio=false):
        environment(resolve_windows_environment({ProfileId::Codex,L"native-reader-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})){
        check(!std::filesystem::exists(environment.paths.root),"QA profile already exists");
        if(!private_audio)environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";initialize_profile_directories(environment.paths);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Native real Kokoro reader QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            40,40,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create reader QA window");
        view=create_lesson_view(host,environment,supplied?std::move(*supplied):model(exercise,after,language));check(view!=nullptr,"Cannot create actual lesson view");MoveWindow(view,0,0,980,720,TRUE);
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
};
}

int main(){std::cout<<std::unitbuf;const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    try{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        auto backend=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
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
            SendMessageW(child(f.view,116),BM_CLICK,0,0);pump();f.reader->content_changed();pump();
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
                std::cout<<"PASS compact reader: immediate real-mouse icon dragging, promoted-touch 90ms/rejected-wheel distinction and unchanged playback/reflow\n";
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
            const auto current=f.snapshot();const auto page=lesson_page_state(f.view);const auto legacy_regions=legacy_lesson_reading_regions(f.view);
            std::vector<windows::NarrationSegment> legacy;std::size_t paragraph=0,fragment=0;
            for(const auto& region:legacy_regions){const auto source=backend->plan(region.source_text,"es");
                if(region.block_index==0)paragraph=legacy.size();if(IsChild(card,region.target))fragment=legacy.size();
                legacy.insert(legacy.end(),source.begin(),source.end());}
            const auto old_key=backend->identity(page.narration_page_identity,"es","ef_dora",legacy);
            check(old_key!=current.key&&paragraph>0&&fragment>paragraph,"The migration fixture failed to reconstruct the exact old code-fragment script");
            for(int mode=0;mode<3;++mode){f.reader.reset();
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
            std::cout<<"PASS exact old order-code script migration: real paused/final offsets, removed-fragment provenance and the same three page slots\n";
        }
        {
            Fixture f(backend,true);f.answer("<<");f.reader->toggle();
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
