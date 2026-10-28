#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/card_list.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "core/narration_speed.hpp"
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
curriculum::StatePosition identity_position(const curriculum::Lesson& lesson,std::string_view identity){
    const auto position=curriculum::stable_block_position(lesson,identity);
    check(position.has_value(),"Missing authored reader identity "+std::string(identity));return *position;
}
COLORREF color(HWND target,LONG offset){CHARRANGE before{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{offset,offset+1};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));CHARFORMAT2W format{};format.cbSize=sizeof(format);
    SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return format.crTextColor;
}
std::string verify_feedback_recording(const LessonReaderSegmentSnapshot& part) {
    const auto bytes = state::read_bytes(part.recording);
    const auto u16 = [&](std::size_t at) { return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8); };
    const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
    const auto marker = [&](std::size_t at, std::string_view expected) {
        return std::equal(expected.begin(), expected.end(), bytes.begin() + at);
    };
    check(bytes.size() > 44 && marker(0, "RIFF") && marker(8, "WAVE") && marker(12, "fmt ")
        && marker(36, "data") && u16(20) == 1 && u16(22) == 1 && u32(24) == 24000
        && u16(34) == 16 && u32(40) == bytes.size() - 44 && (bytes.size() - 44) % 2 == 0,
        "A feedback segment has no real mono 24 kHz PCM16 recording");
    check(std::ranges::any_of(bytes.begin() + 44, bytes.end(), [](auto sample) { return sample != 0; })
        && std::abs(part.duration_ms - (bytes.size() - 44) / 48.) <= 1 && !part.timings.empty(),
        "A feedback recording is silent, mistimed or lacks word alignment");
    double previous = 0;
    for (const auto& timing : part.timings) {
        check(timing.start < timing.end && timing.end <= wide(part.source).size()
            && std::isfinite(timing.start_ms) && std::isfinite(timing.end_ms)
            && timing.start_ms >= previous && timing.end_ms >= timing.start_ms
            && timing.end_ms <= part.duration_ms + 1,
            "A feedback word has invalid source or real-audio timing");
        previous = timing.start_ms;
    }
    return state::sha256(bytes);
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
curriculum::Lesson compiler_model(bool required=false){
    auto lesson=model();lesson.id="qa_native_reader_compiler";lesson.pages.resize(1);
    lesson.compiler_puzzles_required=required;
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
    Fixture(std::shared_ptr<windows::NarrationWorker> backend,bool exercise=false,bool after=true,bool advance=true,std::string_view language="es",std::optional<curriculum::Lesson> supplied={},bool private_audio=false,std::size_t initial_page=0,bool hidden_host=false):
        environment(resolve_windows_environment({ProfileId::Codex,L"native-reader-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})){
        check(!std::filesystem::exists(environment.paths.root),"QA profile already exists");
        if(!private_audio)environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";initialize_profile_directories(environment.paths);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|(hidden_host?WS_EX_NOACTIVATE:0),L"STATIC",L"Native real Kokoro reader QA",WS_OVERLAPPEDWINDOW|(hidden_host?0:WS_VISIBLE),
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
    void answer(std::string_view text,std::size_t block=1){const auto edit=child(view,1002+static_cast<int>(block)*100);const auto value=wide(text);CHARRANGE range{0,-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));
        SendMessageW(child(view,1003+static_cast<int>(block)*100),BM_CLICK,0,0);reader->pause();ready();
        check(!snapshot().playing&&!snapshot().allocated,"Pending feedback preparation overrode a subsequent pause");}
    void page(std::size_t index){reader->pause();check(navigate_lesson_view(view,index),"Cannot navigate QA page");reader->content_changed();ready();}
    void edit_program(std::string_view source,std::size_t block=1){reader->pause();const auto edit=child(view,1010+static_cast<int>(block)*100);const auto value=wide(source);CHARRANGE range{0,-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));}
    void run_program(std::size_t block=1,bool pause_after=true){const auto action=child(view,1011+static_cast<int>(block)*100);SendMessageW(action,BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(action);},"real checked compiler execution");if(pause_after)reader->pause();ready();
        if(pause_after)check(!snapshot().playing&&!snapshot().allocated,"Compiler feedback preparation overrode a subsequent pause");}
};
void runtime_stderr_reader_objective(const std::shared_ptr<windows::NarrationWorker>& backend) {
    auto lesson=compiler_model(true);lesson.id="qa_native_reader_runtime_stderr";
    auto& expected=lesson.pages[0].blocks[1].output_check->cases[0];
    expected.standard_error=curriculum::StandardErrorExpectation::non_blank;
    expected.explanation="Los dos canales cumplen el objetivo.";
    Fixture f(backend,false,true,true,"es",std::move(lesson));
    f.reader->restart();wait([&]{return f.snapshot().waiting_gate;},"reader reaches runtime stderr objective");
    f.run_program(1,false);
    wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&!value.preparing&&value.segments[value.index].feedback;},
        "partial stdout feedback is read and stops at the incomplete runtime stderr goal");
    const auto partial=f.snapshot();
    check(!partial.finished&&!partial.segments[partial.index].correct
        &&edit_text(child(f.view,1104)).find("La salida estándar es correcta; falta el diagnóstico solicitado en stderr.")!=std::string::npos,
        "Partial stdout did not keep its actual incomplete spoken feedback");
    check(std::ranges::none_of(partial.segments,[](const auto& segment){return segment.block==2;}),
        "The reader planned later prose before the current runtime stderr goal was complete");
    f.reader->seek_seconds(60);check(f.snapshot().segments[f.snapshot().index].feedback,
        "Forward seeking escaped the partial runtime-channel verdict");
    f.reader->pause();
    f.edit_program("#include <iostream>\nint main(){std::cout << \"Bien\";std::cerr << \"detalle variable\";}\n");f.ready();
    f.run_program(1,true);const auto correct=f.snapshot();
    check(!correct.playing&&!correct.allocated&&correct.segments[correct.index].feedback&&correct.segments[correct.index].correct,
        "The complete runtime-channel verdict lost its listening boundary or overrode manual pause");
    f.reader->toggle();wait([&]{return f.snapshot().finished;},"complete runtime stderr feedback is heard before later prose");
    const auto completed=f.snapshot();
    check(std::ranges::any_of(completed.segments,[](const auto& segment){return segment.block==2;}),
        "The completed runtime-channel objective did not allow subsequent narration");
    std::cout<<"PASS real Kokoro runtime stderr goal: partial stdout spoken and stopped, forward gate, manual pause and full-feedback continuation\n";
}

void dependent_puzzle_reader_bookmarks(const std::shared_ptr<windows::NarrationWorker>& backend) {
    for(const bool same_page:{false,true}) {
        auto lesson=compiler_model(false);lesson.id=same_page?"qa_reader_dependency_same":"qa_reader_dependency_later";
        lesson.pages.front().blocks.resize(2);
        curriculum::Block puzzle;puzzle.type=curriculum::BlockType::fill_code;puzzle.stable_id="recovered_program";
        puzzle.question="Recupera el programa construido y completa el símbolo.";
        puzzle.code="std::cout ___ \"Bien\";";puzzle.answers={"<<"};
        puzzle.explanation="El símbolo conserva la salida del programa.";
        if(!same_page) {
            auto tail=lesson.pages.front().blocks.front();tail.stable_id="after_free_compiler";tail.address={0,2,{},{}};
            tail.text="Una explicación posterior al editor.";lesson.pages.front().blocks.push_back(std::move(tail));
            curriculum::Page later;later.stable_id="later_page";
            auto paragraph=lesson.pages.front().blocks.front();paragraph.stable_id="later_intro";paragraph.address={1,0,{},{}};
            paragraph.text="Página de recuperación.";later.blocks.push_back(std::move(paragraph));
            lesson.pages.push_back(std::move(later));
        }
        const std::size_t target_page=same_page?0:1,target_block=same_page?2:1;
        puzzle.address={target_page,target_block,{},{}};lesson.pages[target_page].blocks.push_back(puzzle);
        lesson.state_layout.version=1;lesson.state_layout.locale="es";
        lesson.state_layout.lineage=state::sha256(lesson.id);lesson.state_layout.legacy_lineage=lesson.state_layout.lineage;
        for(const auto& page:lesson.pages){curriculum::StatePage layout;layout.id=page.stable_id;
            for(const auto& block:page.blocks)layout.blocks.push_back(block.stable_id);
            lesson.state_layout.origin.push_back(layout);lesson.state_layout.current.push_back(std::move(layout));}
        Fixture f(backend,false,true,true,"es",lesson,false,target_page);
        f.answer("<<",target_block);f.reader->pause();const auto old=f.snapshot();
        const auto old_part=std::ranges::find_if(old.segments,[&](const auto& part){return part.block==target_block&&!part.feedback;});
        check(old_part!=old.segments.end(),"The historical answered puzzle has no real spoken prompt");
        const auto old_at=static_cast<std::int64_t>(old_part-old.segments.begin());
        const auto old_page=lesson_page_state(f.view).narration_page_identity;
        const auto response_key=curriculum::exercise_key(lesson,lesson.pages[target_page].blocks[target_block]);
        const auto accepted=state::get_response(read_lesson_state(f.view),response_key);
        check(accepted&&accepted->correct,"The historical puzzle was not answered by its real control");
        const auto response_preserved=[&]{const auto current=state::get_response(read_lesson_state(f.view),response_key);
            return current&&current->answer==accepted->answer&&current->correct==accepted->correct
                &&current->raw_json==accepted->raw_json&&current->selected==accepted->selected
                &&current->order==accepted->order&&current->reviewed_answer_identity==accepted->reviewed_answer_identity;};
        std::string finished_key="other-finished",finished_page="other-page-b";
        std::int64_t finished_segment=0;
        if(!same_page){f.page(0);f.reader->pause();const auto source_history=f.snapshot();
            check(source_history.segments.back().block==2,"The old free compiler fixture has no later spoken paragraph");
            finished_key=source_history.key;finished_page=lesson_page_state(f.view).narration_page_identity;
            finished_segment=static_cast<std::int64_t>(source_history.segments.size()-1);f.page(target_page);}
        f.reader.reset();
        update_lesson_preferences(f.view,[&](state::StateData& data){
            auto history=narration::begin({},"other-paused","owner-a",{0,120,0,false},{},"other-page-a");
            history=narration::begin(std::move(history),finished_key,"owner-b",{finished_segment,200,0,false},{},finished_page);
            history=narration::checkpoint(std::move(history),finished_key,"owner-b",{finished_segment,200,0,true});
            history=narration::begin(std::move(history),old.key,"historical-puzzle",{old_at,231,0,false},{},old_page);
            narration::write_history(data,history);
        });
        check(flush_lesson_view(f.view),"Cannot preserve the old dependency bookmark");DestroyWindow(f.view);f.view=nullptr;
        lesson.activity_after.push_back({puzzle.stable_id,{lesson.pages[0].blocks[1].stable_id}});
        curriculum::validate_activity_relationships(lesson);
        f.view=create_lesson_view(f.host,f.environment,lesson);check(f.view!=nullptr,"Cannot reopen the protected puzzle model");
        MoveWindow(f.view,0,0,980,720,TRUE);
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        set_lesson_view_hooks(f.view,std::move(hooks));
        f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        const auto locked=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));
        check(lesson_page_state(f.view).index==0&&!locked.playing&&!locked.allocated&&!locked.finished,
            "A historical later bookmark bypassed its compiler or started playback");
        check(std::ranges::none_of(locked.segments,[&](const auto& part){return part.source==puzzle.question||part.source==puzzle.explanation;}),
            "A locked puzzle leaked its historical prompt or feedback into the new script");
        check(history.entries.size()==3&&history.entries.front().key==old.key&&history.entries.front().position.segment==old_at
            &&history.entries.front().position.milliseconds==231
            &&history.entries[1].key==finished_key&&history.entries[1].position.finished
            &&history.entries[2].key=="other-paused"&&history.entries[2].position.milliseconds==120,
            "Resolving a protected bookmark rewrote or evicted existing narration positions");
        check(response_preserved(),
            "Protecting a historical puzzle changed its valid response");
        f.reader->restart();wait([&]{return f.snapshot().waiting_gate;},"explicit puzzle prerequisite stops the reader without a global compiler flag");
        f.reader->seek_seconds(60);check(!f.snapshot().finished&&f.snapshot().segments[f.snapshot().index].block==1,
            "Seeking bypassed an explicit puzzle prerequisite");
        f.run_program(1,true);
        if(!same_page)f.page(target_page);
        const auto revealed=f.snapshot();
        check(std::ranges::any_of(revealed.segments,[&](const auto& part){return part.block==target_block&&part.feedback&&part.correct;}),
            "Completing the prerequisite lost the restored puzzle feedback");
        check(edit_text(child(f.view,1002+static_cast<int>(target_block)*100))=="<<"
            &&response_preserved(),
            "Revealing a protected puzzle lost or reevaluated its answer");
        if(!same_page)check(revealed.key==old.key&&revealed.index==static_cast<std::size_t>(old_at)&&revealed.offset==231&&!revealed.playing,
            "The exact old puzzle bookmark did not return after its prerequisite was completed");
        if(!same_page)f.page(0);f.reader->pause();SendMessageW(child(f.view,1112),BM_CLICK,0,0);f.ready();
        check(response_preserved()
            &&std::ranges::none_of(f.snapshot().segments,[&](const auto& part){return part.source==puzzle.question||part.source==puzzle.explanation;}),
            "Resetting the compiler erased the puzzle response or exposed locked narration");
        if(!same_page)check(!navigate_lesson_view(f.view,target_page),"Reset allowed a historical later puzzle to bypass its prerequisite");
        std::cout<<"PASS real reader protected puzzle "<<(same_page?"same page":"later bookmark")
            <<": old answer and three positions preserved, no hidden script, prerequisite gate, restore and Reset\n";
    }
}

void independent_feedback_does_not_complete_a_pending_compiler(const std::shared_ptr<windows::NarrationWorker>& backend) {
    auto lesson=compiler_model(false);lesson.id="qa_reader_independent_feedback";
    lesson.pages.front().blocks.resize(2);
    curriculum::Block puzzle;puzzle.type=curriculum::BlockType::fill_code;puzzle.stable_id="protected";
    puzzle.question="Recupera el programa construido.";puzzle.code="std::cout ___ \"Bien\";";puzzle.answers={"<<"};puzzle.address={0,2,{},{}};
    lesson.pages.front().blocks.push_back(puzzle);
    puzzle.stable_id="independent";puzzle.address.block=3;puzzle.question="Completa el símbolo independiente.";
    puzzle.explanation="El símbolo conserva la salida del programa. Esta respuesta no resuelve el compilador pendiente.";
    puzzle.review_feedback="Revisa el símbolo. Conserva los dos signos.";lesson.pages.front().blocks.push_back(puzzle);
    lesson.state_layout.version=1;lesson.state_layout.locale="es";
    lesson.state_layout.lineage=state::sha256(lesson.id);lesson.state_layout.legacy_lineage=lesson.state_layout.lineage;
    curriculum::StatePage page;page.id=lesson.pages[0].stable_id;
    for(const auto& block:lesson.pages[0].blocks)page.blocks.push_back(block.stable_id);
    lesson.state_layout.origin.push_back(page);lesson.state_layout.current.push_back(std::move(page));
    lesson.activity_after.push_back({"protected",{lesson.pages[0].blocks[1].stable_id}});
    Fixture f(backend,false,true,true,"es",lesson);
    const auto fresh_feedback=[&](bool correct){const auto snapshot=f.snapshot();
        check(snapshot.segments[snapshot.index].feedback&&snapshot.segments[snapshot.index].block==3
            &&snapshot.segments[snapshot.index].correct==correct&&!snapshot.playing&&!snapshot.allocated,
            "A pending compiler swallowed fresh feedback from an independent puzzle or overrode pause");};
    f.answer(">>",3);fresh_feedback(false);f.reader->toggle();
    wait([&]{const auto snapshot=f.snapshot();return !snapshot.playing&&!snapshot.allocated&&snapshot.segments[snapshot.index].feedback
        &&snapshot.segments[snapshot.index].block==3&&snapshot.offset>=snapshot.segments[snapshot.index].duration_ms-1;},
        "the independent incorrect guide is heard completely");
    check(!f.snapshot().finished,"The independent incorrect guide completed a pending compiler page");
    f.answer("<<",3);fresh_feedback(true);f.reader->toggle();
    wait([&]{const auto snapshot=f.snapshot();return !snapshot.playing&&!snapshot.allocated&&snapshot.waiting_gate;},
        "the independent correct feedback is heard completely and stops at the compiler requirement");
    const auto complete=f.snapshot();
    check(!complete.finished&&complete.index+1==complete.segments.size()&&complete.segments[complete.index].feedback
        &&complete.segments[complete.index].correct&&complete.segments[complete.index].block==3
        &&complete.offset>=complete.segments[complete.index].duration_ms-1&&!lesson_page_state(f.view).can_next,
        "Correct feedback was cut short or falsely finished a page with a required compiler pending");
    check(std::ranges::none_of(complete.segments,[](const auto& part){return part.block==2;}),
        "Independent feedback exposed the protected puzzle");
    f.run_program(1,true);check(std::ranges::any_of(f.snapshot().segments,[](const auto& part){return part.block==2;}),
        "Completing the compiler failed to reveal its own protected puzzle");
    std::cout<<"PASS independent real feedback: complete incorrect/correct explanations, manual pause and no false page completion across a required compiler\n";
}
void integrated_u1_reader_flow(const std::shared_ptr<windows::NarrationWorker>& backend){
    auto lesson=windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json");
    const auto prediction=identity_position(lesson,"block_0004_0003"),compiler=identity_position(lesson,"block_0004_0002"),
        following=identity_position(lesson,"block_0004_0004");
    check(lesson.compiler_puzzles_required&&curriculum::retired_block(lesson,"ejecucion_original_editor")
        &&!curriculum::stable_block_position(lesson,"ejecucion_original_editor"),
        "The current authored reader fixture must retain the original only in its archive");
    const auto& predicted=lesson.pages[prediction.page].blocks[prediction.block];
    const auto& goal=lesson.pages[compiler.page].blocks[compiler.block];
    const auto original_output=predicted.answers.at(0),expected=goal.output_check->cases.at(0).outputs.at(0),
        reviewed_wrong=goal.output_check->cases.at(0).review_feedback.at(0).outputs.at(0);
    auto correct_code=goal.initial_code;const auto literal=correct_code.find('"'+original_output+'"');
    check(literal!=std::string::npos,"The integrated compiler lost its authored initial output literal");
    auto wrong_code=correct_code;wrong_code.replace(literal+1,original_output.size(),reviewed_wrong);
    correct_code.replace(literal+1,original_output.size(),expected);
    Fixture f(backend,false,true,true,"es",lesson,false,compiler.page);
    check(lesson_page_state(f.view).index==compiler.page,"The current useful compiler did not restore its own page");
    f.answer(predicted.answers.at(0),prediction.block);f.reader->restart();f.reader->pause();
    check(f.snapshot().heard_through==0,"A pre-answered current prediction invented listening credit");
    const auto accepted_prediction = state::get_response(read_lesson_state(f.view), lesson, predicted);
    check(accepted_prediction && accepted_prediction->correct
        && !windows::compiler_puzzle_completed(read_lesson_state(f.view), lesson, goal),
        "The initial bare-verdict fixture lost its prediction or already completed the compiler");
    // The actual initial program prints Zona inicial. It matches neither the
    // requested Caja recibida nor the separately authored Zona segura guide.
    f.run_program(compiler.block);
    const auto bare = f.snapshot();
    const auto bare_part = bare.segments[bare.index];
    const auto bare_regions = lesson_reading_regions(f.view);
    const auto bare_region = std::ranges::find_if(bare_regions, [&](const auto& region) {
        return region.block_index == compiler.block && region.feedback;
    });
    check(bare_region != bare_regions.end() && bare_region->source_text == "✗ Incorrecto"
        && !bare_region->correct && !bare_region->solved && bare_part.feedback && !bare_part.correct
        && bare_part.block == compiler.block && bare_part.target == bare_region->target
        && bare_part.source == "✗ Incorrecto" && bare_part.start == 0
        && bare_part.end == wide(bare_region->source_text).size() && bare.offset == 0
        && edit_text(bare_part.target) == "✗ Incorrecto"
        && std::ranges::count_if(bare.segments, [&](const auto& part) {
            return part.block == compiler.block && part.feedback;
        }) == 1,
        "An unmatched current output omitted Incorrecto or attached an unrelated guide/success explanation");
    const auto bare_audio_hash = verify_feedback_recording(bare_part);
    f.reader->toggle();
    wait([&] { const auto value = f.snapshot();
        return value.playing && value.index == bare.index && value.offset > 30;
    }, "the current bare Incorrecto verdict really plays");
    wait([&] { const auto value = f.snapshot();
        return !value.playing && !value.allocated && !value.preparing && value.index == bare.index;
    }, "the current bare Incorrecto verdict ends and stops pending");
    const auto after_bare = f.snapshot();
    const auto preserved_prediction = state::get_response(read_lesson_state(f.view), lesson, predicted);
    check(!after_bare.finished && after_bare.offset + 1 >= bare_part.duration_ms
        && !windows::compiler_puzzle_completed(read_lesson_state(f.view), lesson, goal)
        && preserved_prediction && preserved_prediction->correct
        && preserved_prediction->answer == accepted_prediction->answer
        && preserved_prediction->raw_json == accepted_prediction->raw_json
        && std::ranges::none_of(after_bare.segments, [&](const auto& part) { return part.block == following.block; })
        && state::sha256(state::read_bytes(bare_part.recording)) == bare_audio_hash,
        "A bare incorrect verdict granted completion, lost the accepted prediction or changed its recording");
    std::cout << "PASS current U1L2 p5 initial execution: bare Incorrecto has real PCM/word timing, no invented explanation, stops pending and preserves prediction achievement\n";
    f.edit_program(wrong_code,compiler.block);f.ready();f.run_program(compiler.block);
    const auto wrong=f.snapshot();
    check(wrong.segments[wrong.index].feedback&&!wrong.segments[wrong.index].correct&&wrong.segments[wrong.index].block==compiler.block
        &&std::ranges::none_of(wrong.segments,[&](const auto& part){return part.block==following.block;}),
        "The integrated wrong goal failed to select its own guide or exposed the following puzzle");
    f.reader->toggle();wait([&]{const auto current=f.snapshot();return !current.playing&&!current.allocated
        &&current.segments[current.index].feedback&&current.segments[current.index].block==compiler.block;},
        "current compiler guide is heard and stops for a retry");
    check(!f.snapshot().finished,"The current compiler's wrong guide completed its page");
    f.reader->seek_seconds(-1);f.reader->toggle();wait([&]{const auto current=f.snapshot();return current.playing
        &&current.segments[current.index].feedback&&!current.segments[current.index].correct;},"current wrong guide replays");f.reader->pause();
    f.edit_program(correct_code,compiler.block);f.ready();f.run_program(compiler.block);const auto correct=f.snapshot();
    const auto earlier=std::ranges::find_if(correct.segments,[&](const auto& part){return part.block==prediction.block&&part.feedback&&part.correct;});
    check(earlier!=correct.segments.end()&&correct.segments[correct.index].block==compiler.block
        &&correct.segments[correct.index].feedback&&correct.segments[correct.index].correct&&correct.offset==0,
        "The useful compiler did not take fresh feedback priority over the pre-answered prediction");
    const auto unheard_edge=static_cast<std::int64_t>(earlier-correct.segments.begin());
    f.reader->toggle();wait([&]{const auto current=f.snapshot();return current.playing
        &&current.segments[current.index].block==following.block&&current.offset>150;},
        "the integrated correct explanation is heard before the following puzzle");
    f.reader->pause();const auto paused=f.snapshot();check(paused.heard_through<=unheard_edge,
        "The current useful compiler credited its earlier unheard prediction feedback");
    f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto restored=f.snapshot();
    check(restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset
        &&restored.heard_through==paused.heard_through&&!restored.playing&&!restored.allocated,
        "The integrated reader lost its exact paused position after retiring the redundant editor");
    f.edit_program(wrong_code,compiler.block);f.ready();const auto before_repeat=f.snapshot();f.run_program(compiler.block);const auto repeated=f.snapshot();
    check(repeated.key==before_repeat.key&&repeated.index==before_repeat.index&&repeated.offset==before_repeat.offset
        &&std::ranges::any_of(repeated.segments,[&](const auto& part){return part.block==compiler.block&&part.feedback&&part.correct;}),
        "A later compilation revoked the useful compiler's persistent achievement or moved its reader position");
    f.reader->pause();SendMessageW(child(f.view,1012+static_cast<int>(compiler.block)*100),BM_CLICK,0,0);f.ready();
    const auto retained_prediction=state::get_response(read_lesson_state(f.view),curriculum::exercise_key(lesson,predicted));
    check(std::ranges::none_of(f.snapshot().segments,[&](const auto& part){return part.block==following.block;})
        &&retained_prediction&&retained_prediction->correct,
        "Explicit compiler Reset lost the prediction answer or retained unavailable later narration");
    std::cout<<"PASS current integrated U1 reader: archived original absent, useful compiler required, native wrong/correct feedback, exact released restore, sticky achievement and scoped Reset\n";
}
void integrated_order_reader_flow(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto path = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json";
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    const auto position = identity_position(lesson, "block_0003_0002");
    const auto& goal = lesson.pages[position.page].blocks[position.block];
    const auto following = identity_position(lesson, "block_0003_0003");
    check(position.page == 3 && position.block == 2 && goal.type == curriculum::BlockType::order_code
        && goal.fragments.size() == 5 && !goal.review_feedback.empty() && !goal.explanation.empty(),
        "The current U1L2 order reader fixture lost its authored puzzle or feedback");
    Fixture f(backend, false, true, true, "es", lesson, false, 1, true);
    check(!IsWindowVisible(f.host), "The focal order narration QA exposed a desktop window");
    const auto reopen = [&](std::optional<std::size_t> initial_page = {}) {
        f.reader.reset();
        check(flush_lesson_view(f.view), "Cannot save the actual lesson before closing its view");
        DestroyWindow(f.view); f.view = nullptr;
        if (initial_page) {
            // This sets only the focal QA starting page, as Fixture does. It
            // never solves pending activities to bypass forward navigation.
            state::StateStore store(f.environment.paths.root);
            auto data = store.load();
            state::save_current_page(data, lesson, *initial_page);
            store.save(data);
        }
        f.view = create_lesson_view(f.host, f.environment, lesson);
        check(f.view != nullptr, "Cannot reopen the current order lesson in its isolated saved profile");
        MoveWindow(f.view, 0, 0, 980, 720, TRUE);
        LessonViewHooks hooks;
        hooks.before_page_change = [&](HWND) { if (f.reader) f.reader->pause(); };
        hooks.content_changed = [&](HWND) { if (f.reader) f.reader->content_changed(); };
        hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (f.reader) f.reader->feedback(block, correct); };
        set_lesson_view_hooks(f.view, std::move(hooks));
        f.reader = std::make_unique<LessonReader>(f.view, f.environment, backend, [] {});
        f.ready();
    };

    // These are two genuinely played positions on other current pages, not
    // fabricated listening credit or imported student history.
    std::vector<narration::Bookmark> other_positions;
    // Backward page selection is valid with unanswered activities; forward
    // selection would correctly require unrelated earlier puzzles to be solved.
    for (const std::size_t page : {1u, 0u}) {
        f.page(page);
        f.reader->toggle();
        wait([&] { const auto value = f.snapshot(); return value.playing && value.offset > 180; },
            "a real paused position on another current U1L2 page");
        f.reader->pause();
        const auto paused = f.snapshot();
        const auto history = narration::read_history(read_lesson_state(f.view));
        const auto saved = std::ranges::find(history.entries, paused.key, &narration::Bookmark::key);
        check(saved != history.entries.end() && saved->position.milliseconds == paused.offset
            && !saved->position.finished, "A real other-page pause was not saved exactly");
        other_positions.push_back(*saved);
    }
    reopen(position.page);
    const auto control = [&](int part) { return child(f.view, 1000 + static_cast<int>(position.block) * 100 + part); };
    const auto move_up = [&](std::size_t fragment) {
        const auto row = GetDlgItem(control(5), 100 + static_cast<int>(fragment));
        check(row != nullptr, "The authored order puzzle lost a real fragment card");
        SendMessageW(row, WM_SYSKEYDOWN, VK_UP, LPARAM{1} << 29);
        pump();
    };
    const auto submit = [&] {
        check(IsWindowEnabled(control(3)), "The real reordered puzzle did not enable Check");
        SendMessageW(control(3), BM_CLICK, 0, 0);
        f.reader->pause();
        f.ready();
        check(!f.snapshot().playing && !f.snapshot().allocated,
            "Order feedback generation overrode the following explicit pause");
    };
    struct Recording { std::filesystem::path path; std::string hash; };
    std::vector<Recording> recordings;
    const auto verify_audio = [&](const LessonReaderSegmentSnapshot& part) {
        recordings.push_back({part.recording, verify_feedback_recording(part)});
    };
    const auto verify_feedback = [&](bool correct, const std::string& body) {
        const auto regions = lesson_reading_regions(f.view);
        const auto region = std::ranges::find_if(regions, [&](const auto& item) {
            return item.block_index == position.block && item.feedback;
        });
        const auto source = std::string(correct ? "✓ Correcto\n\n" : "✗ Incorrecto\n\n") + body;
        check(region != regions.end() && region->source_text == source && region->target == control(4)
            && region->correct == correct && region->solved == correct,
            "The current order puzzle did not expose exactly its complete authored feedback");
        const auto plan = backend->plan(source, "es");
        const auto snapshot = f.snapshot();
        std::vector<std::size_t> indices;
        for (std::size_t index = 0; index < snapshot.segments.size(); ++index) {
            const auto& part = snapshot.segments[index];
            if (part.feedback && part.block == position.block) {
                const auto at = indices.size();
                check(at < plan.size() && part.region == region->key && part.correct == correct
                    && part.target == region->target && part.source == plan[at].text
                    && part.start == plan[at].start && part.end == plan[at].end
                    && wide(source).substr(part.start, part.end - part.start) == wide(part.source),
                    "The current order reader truncated or reordered an authored feedback sentence");
                verify_audio(part);
                indices.push_back(index);
            }
        }
        check(!indices.empty() && indices.size() == plan.size() && snapshot.index == indices.front()
            && snapshot.offset == 0, "Fresh order feedback did not start its complete original script at zero");
        return indices;
    };
    const auto preserve_other_positions = [&] {
        const auto history = narration::read_history(read_lesson_state(f.view));
        check(history.entries.size() == 3, "The current order page did not retain exactly three real page slots");
        for (const auto& before : other_positions) {
            const auto after = std::ranges::find(history.entries, before.key, &narration::Bookmark::key);
            check(after != history.entries.end() && *after == before,
                "Order feedback or reopening changed another actual page's saved position");
        }
    };

    move_up(1); // A real, changed but still incorrect order: 1,0,2,3,4.
    submit();
    const auto wrong_indices = verify_feedback(false, goal.review_feedback);
    auto response = state::get_response(read_lesson_state(f.view), lesson, goal);
    check(response && !response->correct && state::response_reviewed(*response, goal)
        && response->order == std::vector<std::int64_t>{1, 0, 2, 3, 4},
        "The real incorrect ordering did not retain its reviewed answer without achievement");
    const auto wrong_response = *response;
    std::vector<bool> wrong_heard(wrong_indices.size(), false);
    const auto observe_wrong = [&](const LessonReaderSnapshot& value) {
        for (std::size_t i = 0; i < wrong_indices.size(); ++i)
            if (value.playing && value.index == wrong_indices[i]) wrong_heard[i] = true;
    };
    f.reader->toggle();
    wait([&] {
        const auto value = f.snapshot(); observe_wrong(value);
        return value.playing && value.segments[value.index].feedback
            && value.segments[value.index].source.find("Distingue") != std::string::npos && value.offset > 200;
    }, "the actual authored order guide plays with real word timing");
    f.reader->pause();
    const auto paused = f.snapshot();
    preserve_other_positions();
    const auto history_before_reopen = narration::read_history(read_lesson_state(f.view));
    reopen(); // Actual acceptance: use the automatically saved page unchanged.
    const auto restored = f.snapshot();
    response = state::get_response(read_lesson_state(f.view), lesson, goal);
    check(lesson_page_state(f.view).index == position.page && restored.key == paused.key
        && restored.index == paused.index && restored.offset == paused.offset
        && restored.heard_through == paused.heard_through && !restored.finished
        && !restored.playing && !restored.allocated && response && !response->correct
        && response->order == wrong_response.order && response->raw_json == wrong_response.raw_json
        && response->reviewed_answer_identity == wrong_response.reviewed_answer_identity
        && narration::read_history(read_lesson_state(f.view)) == history_before_reopen,
        "Reopening the whole current lesson lost its exact paused guide, response or three-position history");
    const auto silence_until = GetTickCount64() + 200;
    wait([&] { const auto value = f.snapshot();
        check(!value.playing && !value.allocated && value.offset == paused.offset,
            "The reopened current order guide started playing without an explicit request");
        return GetTickCount64() >= silence_until;
    }, "a reopened authored guide remains paused", 2000);
    for (const auto& recording : recordings)
        check(state::sha256(state::read_bytes(recording.path)) == recording.hash,
            "Reopening the current order guide changed an existing valid recording");
    f.reader->toggle();
    check(f.snapshot().allocated && std::abs(f.snapshot().offset - paused.offset) <= 1,
        "Resuming the reopened actual guide restarted its recording");
    wait([&] { const auto value = f.snapshot(); observe_wrong(value);
        return !value.playing && !value.allocated && !value.preparing
            && value.index == wrong_indices.back();
    }, "the complete actual order guide finishes and stops for a retry");
    const auto stopped = f.snapshot();
    response = state::get_response(read_lesson_state(f.view), lesson, goal);
    check(std::ranges::all_of(wrong_heard, [](bool heard) { return heard; })
        && stopped.offset + 1 >= stopped.segments[stopped.index].duration_ms && !stopped.finished
        && response && !response->correct,
        "The authored wrong order guide was skipped, continued beyond the puzzle or granted achievement");

    auto order = wrong_response.order;
    for (std::size_t target = 0; target < goal.correct_order.size(); ++target) {
        auto at = static_cast<std::size_t>(std::ranges::find(order,
            static_cast<std::int64_t>(goal.correct_order[target])) - order.begin());
        check(at < order.size(), "The authored correct order no longer names a real fragment");
        while (at > target) { move_up(static_cast<std::size_t>(order[at])); std::swap(order[at], order[at - 1]); --at; }
    }
    submit();
    const auto correct_indices = verify_feedback(true, goal.explanation);
    response = state::get_response(read_lesson_state(f.view), lesson, goal);
    check(response && response->correct && response->reviewed_answer_identity.empty()
        && std::ranges::equal(response->order, goal.correct_order, [](std::int64_t actual, std::size_t expected) {
            return actual == static_cast<std::int64_t>(expected);
        }),
        "The real corrected order did not earn its authored achievement");
    std::vector<bool> correct_heard(correct_indices.size(), false);
    f.reader->toggle();
    wait([&] {
        const auto value = f.snapshot();
        for (std::size_t i = 0; i < correct_indices.size(); ++i)
            if (value.playing && value.index == correct_indices[i]) correct_heard[i] = true;
        return value.playing && value.segments[value.index].block == following.block && value.offset > 150;
    }, "the complete actual order explanation is heard before the next authored puzzle");
    f.reader->pause();
    const auto continued = f.snapshot();
    check(std::ranges::all_of(correct_heard, [](bool heard) { return heard; })
        && continued.heard_through > static_cast<std::int64_t>(correct_indices.back()) && !continued.finished,
        "The actual order reader continued before hearing every success sentence or completed an unanswered page");
    preserve_other_positions();
    for (const auto& recording : recordings)
        check(state::sha256(state::read_bytes(recording.path)) == recording.hash,
            "The current order feedback flow changed an existing valid recording");
    check(state::sha256(state::read_bytes(path)) == asset_hash && !IsWindowVisible(f.host),
        "The focal order reader QA modified its authored asset or exposed a desktop window");
    std::cout << "PASS current U1L2 p4 order reader: complete authored wrong guide and success explanation, real PCM/timings, retry stop, full-view exact silent restoration and three real page positions\n";
}

void extended_reader_playback_rates(const std::shared_ptr<windows::NarrationWorker>& backend){
    Fixture f(backend,false,true,true,"es",compiler_model());const auto initial=f.snapshot();
    std::vector<std::pair<std::filesystem::path,std::filesystem::file_time_type>> recordings;
    for(const auto& part:initial.segments)recordings.emplace_back(part.recording,std::filesystem::last_write_time(part.recording));
    const auto preserved=read_lesson_state(f.view);
    const auto unchanged=[&]{auto current=read_lesson_state(f.view),before=preserved;
        current.preferences["lesson_narration_settings"].erase("rate");before.preferences["lesson_narration_settings"].erase("rate");
        check(current==before,"The paused speed selector changed narration history, progress, drafts or generation settings");};
    for(const auto expected:{2.5,3.,.5,1.,1.5,2.}){
        const auto button=GetDlgItem(f.reader->bar(),6505);check(button!=nullptr,"The real playback speed button is missing");
        SendMessageW(button,BM_CLICK,0,0);pump();
        check(f.snapshot().rate==expected&&std::get<double>(*state::find(read_lesson_state(f.view),"lesson_narration_settings","rate"))==expected,
            "The real speed selector lost an existing rate or did not expose2.5X/3X in order");
        check(GetCapture()==nullptr,"The accessible playback speed action acquired mouse capture");unchanged();
    }
    const auto source=child(f.view,1110);const auto original_source=edit_text(source);CHARRANGE selection{4,9};
    SetFocus(source);SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump();
    KeyboardStateScope keyboard;
    const auto press=[&](UINT key,UINT kind=WM_KEYDOWN,LPARAM flags=1,HWND target=nullptr){MSG message{};message.hwnd=target?target:source;
        message.message=kind;message.wParam=key;message.lParam=flags;return f.reader->dispatch_shortcut(message);};
    check(press(VK_F10,WM_SYSKEYDOWN)&&f.snapshot().rate==2.5&&press(VK_F10,WM_SYSKEYUP)&&f.snapshot().rate==2.5,
        "F10 from the actual code editor failed to increase playback speed or leaked its menu-key release");
    check(press(VK_F10)&&f.snapshot().rate==3.,"F10 did not expose the new3X endpoint");
    for(int repeat=0;repeat<5;++repeat)check(press(VK_F10,WM_KEYDOWN,1LL|(1LL<<30))&&f.snapshot().rate==3.,
        "Holding F10 wrapped playback speed past its3X endpoint");
    bool first=true;for(const auto expected:{2.5,2.,1.5,1.,.5}){
        check(press(VK_F9,WM_KEYDOWN,first?1:1LL|(1LL<<30))&&f.snapshot().rate==expected,
            "F9 or its autorepeat skipped an existing playback speed");first=false;
    }
    check(press(VK_F9)&&f.snapshot().rate==.5&&press(VK_F9,WM_KEYUP),"F9 wrapped below0.5X or leaked its release");
    for(const auto modifier:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN}){
        auto modified=keyboard.clear;modified[modifier]=0x80;check(SetKeyboardState(modified.data())!=FALSE,"Cannot set the isolated speed-shortcut modifier");
        check(!press(VK_F10)&&f.snapshot().rate==.5,"A modified F10 shortcut bypassed its original system/editor action");
        check(SetKeyboardState(keyboard.clear.data())!=FALSE,"Cannot clear the isolated speed-shortcut modifier");
    }
    check(!press(VK_F10,WM_SYSKEYDOWN,1LL|(1LL<<29))&&!press(VK_F9,WM_KEYDOWN,1,GetDesktopWindow()),
        "Speed shortcuts consumed an Alt system key or a different application's target");
    CHARRANGE after{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(GetFocus()==source&&edit_text(source)==original_source&&after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&GetCapture()==nullptr,
        "Changing playback speed from the code editor changed focus, source, selection or capture");
    f.reader->set_controls_visible(false);check(!press(VK_F10)&&f.snapshot().rate==.5,"A hidden reader consumed F10");f.reader->set_controls_visible(true);
    for(const auto expected:{2.5,3.}){
        update_lesson_preferences(f.view,[expected](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=expected;});f.reader->settings_changed();
        const auto history=narration::read_history(read_lesson_state(f.view));
        f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        check(f.snapshot().rate==expected&&!f.snapshot().playing&&!f.snapshot().allocated&&
            narration::read_history(read_lesson_state(f.view))==history,"Recreating the reader clamped2.5X/3X or changed its paused bookmark");
        f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.playback_rate==expected;},"the selected fast rate reaches the actual media session");
        const auto before=f.snapshot();check(press(expected==2.5?VK_F10:VK_F9)&&
            f.snapshot().playback_rate==(expected==2.5?3.:2.5),"The focused-editor speed shortcut did not update live MediaPlaybackSession");
        check(press(expected==2.5?VK_F9:VK_F10)&&f.snapshot().playback_rate==expected&&f.snapshot().index==before.index&&f.snapshot().offset>=before.offset,
            "Changing a live speed through F9/F10 sought or restarted the current recording");
        f.reader->pause();check(!f.snapshot().allocated,"Pausing a fast rate retained the media player");
        state::StateStore saved(f.environment.paths.root);
        check(std::get<double>(*state::find(saved.load(),"lesson_narration_settings","rate"))==expected,"The selected fast playback rate was not persisted in its isolated profile");
    }
    for(const auto& [path,modified]:recordings)check(std::filesystem::is_regular_file(path)&&std::filesystem::last_write_time(path)==modified,
        "Changing only playback speed regenerated or replaced an existing QA recording");
    std::cout<<"PASS real reader playback rates: six existing/new choices,2.5X/3X persistence/restoration, focused-editor F9/F10 and held endpoints without menu/capture/seek, live media rates and unchanged recordings/protected state\n";
}
void checked_compilation_keeps_keyboard_reader_control(const std::shared_ptr<windows::NarrationWorker>& backend){
    auto lesson=compiler_model(true);lesson.id="qa_reader_compile_focus";
    auto& dependent=lesson.pages.front().blocks.back();dependent.type=curriculum::BlockType::multiple_choice;
    dependent.id=dependent.stable_id;dependent.text.clear();dependent.question="¿Qué salida produjo el programa?";
    dependent.options={"Bien","Mal"};dependent.correct_index=0;
    dependent.explanation="El programa mostró Bien en la consola.";
    lesson.activity_after.push_back({"after_compiler",{"checked_source"}});
    lesson.state_layout.version=1;lesson.state_layout.locale="es";
    lesson.state_layout.lineage=state::sha256(lesson.id);lesson.state_layout.legacy_lineage=lesson.state_layout.lineage;
    for(const auto& page:lesson.pages){curriculum::StatePage layout;layout.id=page.stable_id;
        for(const auto& block:page.blocks)layout.blocks.push_back(block.stable_id);
        lesson.state_layout.origin.push_back(layout);lesson.state_layout.current.push_back(std::move(layout));}
    curriculum::validate_activity_relationships(lesson);
    Fixture f(backend,false,true,true,"es",lesson);KeyboardStateScope keyboard;f.reader->pause();
    const auto source=child(f.view,1110),compile=child(f.view,1111),feedback=child(f.view,1104);
    const auto original_source=edit_text(source);CHARRANGE selection{4,9};
    SetForegroundWindow(f.host);SetActiveWindow(f.host);SetFocus(source);
    SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));SetFocus(compile);pump();
    check(GetForegroundWindow()==f.host&&GetActiveWindow()==f.host&&GetFocus()==compile,
        "Cannot establish actual foreground/focused compile-button preconditions");
    struct KeyResult{UINT key;HWND target;bool dispatched;};std::vector<KeyResult> keys;
    const auto log=[&](std::string_view phase){const auto state=f.snapshot();
        std::cout<<"compile-focus "<<phase<<": focus="<<GetFocus()<<" active="<<GetActiveWindow()<<" foreground="<<GetForegroundWindow()
            <<" planned="<<state.planned<<" preparing="<<state.preparing<<" playing="<<state.playing<<" allocated="<<state.allocated
            <<" finished="<<state.finished<<" gate="<<state.waiting_gate<<" index="<<state.index<<" offset="<<state.offset<<'\n';};
    const auto pump_reader=[&]{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){
        const bool handled=f.reader->dispatch_shortcut(message);
        if((message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN)&&
            (message.wParam==VK_F1||message.wParam==VK_F9||message.wParam==VK_F10)){
            keys.push_back({static_cast<UINT>(message.wParam),message.hwnd,handled});
            std::cout<<"compile-focus native key="<<message.wParam<<" message="<<message.message<<" lParam="<<message.lParam
                <<" alt-context="<<((message.lParam&(1LL<<29))!=0)<<" repeat="<<((message.lParam&(1LL<<30))!=0)
                <<" hwnd="<<message.hwnd<<" root="<<GetAncestor(message.hwnd,GA_ROOT)<<" dispatched="<<handled<<'\n';
        }
        if(!handled){TranslateMessage(&message);DispatchMessageW(&message);}
    }};
    const auto wait_reader=[&](const std::function<bool()>& done,std::string_view description,ULONGLONG timeout=60000){
        const auto deadline=GetTickCount64()+timeout;do{pump_reader();if(done())return;
            MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<deadline);
        throw std::runtime_error("Timeout: "+std::string(description));
    };
    const auto press=[&](WORD key){INPUT input[2]{};for(auto& event:input){event.type=INPUT_KEYBOARD;event.ki.wVk=key;}
        input[1].ki.dwFlags=KEYEVENTF_KEYUP;const auto before=keys.size();
        check(SendInput(2,input,sizeof(INPUT))==2,"Cannot inject the actual isolated QA reader key");
        wait_reader([&]{return keys.size()>before;},"native reader key delivered to the foreground QA thread",3000);
        return keys.back().dispatched;
    };
    log("before run");SendMessageW(compile,BM_CLICK,0,0);log("run disabled controls");
    wait_reader([&]{return IsWindowEnabled(compile);},"actual successful checked compiler execution");log("completed before any extra click");
    check(GetFocus()==source,"Disabling the focused compile button did not retain a healthy source-editor focus");
    const auto correct_feedback=edit_text(feedback);
    check(!correct_feedback.empty()&&std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){
        return region.feedback&&region.correct&&region.block_index==1;}),"Actual compilation did not retain its Correcto feedback");
    check(std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==2;}),
        "Actual successful compilation did not reveal its dependent puzzle/rebuild the reader script");
    const bool f1=press(VK_F1);log("after native F1");
    const auto old_rate=f.snapshot().rate;const bool f9=press(VK_F9);const auto lower=f.snapshot().rate;
    const bool f10=press(VK_F10);const auto restored=f.snapshot().rate;
    // A root-directed control is diagnostic only: distinguish lost keyboard
    // routing from a blocked feedback/bookmark replan before reporting failure.
    if(!f1){MSG control{};control.hwnd=f.host;control.message=WM_KEYDOWN;control.wParam=VK_F1;control.lParam=1;
        const auto handled=f.reader->dispatch_shortcut(control);std::cout<<"compile-focus root-directed F1 control="<<handled<<'\n';}
    wait_reader([&]{return f.snapshot().playing;},"F1 playback after the actual correct-feedback replan");log("playback confirmed");f.reader->pause();
    CHARRANGE after{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(edit_text(source)==original_source&&after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&edit_text(feedback)==correct_feedback,
        "Compile/reader shortcuts changed source, cursor selection or accepted feedback");
    check(f1&&f9&&f10&&lower<old_rate&&restored==old_rate,
        "After real checked compilation, F1/F9/F10 require another click despite the app remaining active");

    // Keep exercising USER's real no-focus SYS-key route even when the compile
    // action now transfers its button focus to the source before disabling it.
    SetFocus(nullptr);check(GetFocus()==nullptr&&GetActiveWindow()==f.host&&GetForegroundWindow()==f.host,
        "Cannot establish the actual active-root/no-focused-child keyboard route");
    const bool no_focus_f1=press(VK_F1),no_focus_f9=press(VK_F9);const auto no_focus_lower=f.snapshot().rate;
    const bool no_focus_f10=press(VK_F10);
    wait_reader([&]{return f.snapshot().playing;},"native no-focus SYS F1 plays without another click");f.reader->pause();
    check(no_focus_f1&&no_focus_f9&&no_focus_f10&&no_focus_lower<old_rate&&f.snapshot().rate==old_rate&&GetFocus()==nullptr,
        "Unmodified native SYS reader keys still fail or move focus when the active root has no focused child");
    SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(edit_text(source)==original_source&&after.cpMin==selection.cpMin&&after.cpMax==selection.cpMax&&edit_text(feedback)==correct_feedback,
        "The native no-focus SYS reader route changed source, cursor selection or accepted feedback");

    // Completion must not restore the launching app's focus after the student
    // has deliberately activated a different top-level window in the meantime.
    SetFocus(compile);SendMessageW(compile,BM_CLICK,0,0);
    const auto other=CreateWindowExW(0,L"STATIC",L"Other active QA window",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        80,80,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(other!=nullptr,"Cannot create the independent foreground QA window");
    const auto other_edit=CreateWindowExW(0,L"EDIT",L"Other task",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,200,30,other,nullptr,GetModuleHandleW(nullptr),nullptr);
    SetForegroundWindow(other);SetActiveWindow(other);SetFocus(other_edit);pump_reader();
    check(GetForegroundWindow()==other&&GetFocus()==other_edit,"Cannot activate the independent QA task during compilation");
    wait_reader([&]{return IsWindowEnabled(compile);},"background compilation completing without focus restoration");
    check(GetForegroundWindow()==other&&GetActiveWindow()==other&&GetFocus()==other_edit,
        "Finishing a background compilation stole focus from the newly active task");
    MSG thread_key{};thread_key.message=WM_KEYDOWN;thread_key.wParam=VK_F10;thread_key.lParam=1;
    check(!f.reader->dispatch_shortcut(thread_key),"A thread key belonging to another foreground task changed the reader rate");
    check(edit_text(feedback)==correct_feedback&&edit_text(source)==original_source,
        "Recompiling an accepted answer in the background revoked its feedback or changed the source");
    DestroyWindow(other);
    std::cout<<"PASS actual checked compilation: active compile-button focus, native F1/F9/F10 without extra clicks, feedback replan, source/selection/Correcto retained, no background focus steal\n";
}
void reader_arrow_aliases_preserve_control_keys(const std::shared_ptr<windows::NarrationWorker>& backend){
    Fixture f(backend,false,true,true,"es",compiler_model());KeyboardStateScope keyboard;ReaderCommandProbe probe(f.reader->bar());
    const auto press=[&](UINT key,HWND target,UINT kind=WM_KEYDOWN,LPARAM flags=1){MSG message{};message.hwnd=target;
        message.message=kind;message.wParam=key;message.lParam=flags;return f.reader->dispatch_shortcut(message);};
    const auto play=GetDlgItem(f.reader->bar(),6504);
    const std::array<std::pair<UINT,int>,8> mappings{{{VK_F5,6500},{VK_LEFT,6500},{VK_F6,6501},{VK_UP,6501},
        {VK_F7,6502},{VK_DOWN,6502},{VK_F8,6503},{VK_RIGHT,6503}}};
    for(const auto& [key,button]:mappings)for(const auto kind:{WM_KEYDOWN,WM_SYSKEYDOWN}){
        SetFocus(kind==WM_KEYDOWN?play:nullptr);const auto count=probe.commands.size();
        check(press(key,kind==WM_KEYDOWN?play:f.host,kind)&&probe.commands.size()==count+1&&probe.commands.back()==button,
            "A function/arrow seek alias did not activate its original direction/duration button from the reader/root");
        check(press(key,kind==WM_KEYDOWN?play:f.host,kind,1LL|(1LL<<30))&&probe.commands.size()==count+1,
            "Holding a seek arrow repeated an action that the original function key activates once");
        check(!press(key,f.host,kind==WM_SYSKEYDOWN?WM_SYSKEYUP:WM_KEYUP),"A seek-key release activated narration");f.reader->pause();
    }
    const auto source=child(f.view,1110),readonly=child(f.view,1000);
    check(source&&readonly,"The actual arrow-key fixture lost its source or selectable readonly text");
    const auto source_text=edit_text(source),readonly_text=edit_text(readonly);CHARRANGE selected{4,9};
    SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));SendMessageW(readonly,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    MathExpressionEditorAccess access;access.request=[backend](std::string_view request){return backend->math_editor(request);};
    const auto math=create_math_expression_editor(f.view,9100,std::move(access));
    check(math&&set_math_expression_latex(math,"1+2"),"Cannot initialize the actual structured math editor for arrow-key routing");
    MoveWindow(math,0,0,260,60,TRUE);const auto math_before=inspect_math_expression_editor(math);
    const auto list=create_card_list(f.view,9101,WM_APP+9101);check(list!=nullptr,"Cannot create an actual WANTARROWS card list");MoveWindow(list,0,65,260,60,TRUE);
    const auto before=f.snapshot();const auto commands=probe.commands.size();
    for(const auto target:{source,readonly,math,list}){
        SetFocus(target);check(GetFocus()==target,"Cannot focus the real native control that owns its arrow keys");
        for(const auto key:{VK_LEFT,VK_UP,VK_DOWN,VK_RIGHT}){
            check(!press(key,target)&&!press(key,f.host,WM_SYSKEYDOWN),
                "A seek-arrow alias stole native cursor/selection/control navigation from actual focus");
        }
        SetFocus(play);for(const auto key:{VK_LEFT,VK_UP,VK_DOWN,VK_RIGHT})
            check(!press(key,target),"A seek-arrow alias ignored the message's actual editor/control target");
    }
    CHARRANGE source_after{},readonly_after{};
    SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&source_after));SendMessageW(readonly,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&readonly_after));
    const auto math_after=inspect_math_expression_editor(math);const auto after=f.snapshot();
    check(source_after.cpMin==selected.cpMin&&source_after.cpMax==selected.cpMax&&readonly_after.cpMin==selected.cpMin&&readonly_after.cpMax==selected.cpMax&&
        edit_text(source)==source_text&&edit_text(readonly)==readonly_text&&math_after.cursor_json==math_before.cursor_json&&math_after.draft_json==math_before.draft_json&&
        probe.commands.size()==commands&&after.key==before.key&&after.index==before.index&&after.offset==before.offset&&after.rate==before.rate,
        "Yielding arrows changed document bytes, native selection, math cursor/draft or the reader's paused position/rate");
    SetFocus(source);check(!press(VK_RIGHT,source),"The source editor did not retain its actual Right key");SendMessageW(source,WM_KEYDOWN,VK_RIGHT,1);
    SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&source_after));
    check(source_after.cpMin==selected.cpMax&&source_after.cpMax==selected.cpMax&&edit_text(source)==source_text,
        "The original native Right key failed to collapse the source selection without editing code");
    SetFocus(play);for(const auto modifier:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN}){
        auto state=keyboard.clear;state[modifier]=0x80;check(SetKeyboardState(state.data())!=FALSE,"Cannot set the isolated arrow modifier");
        check(!press(VK_LEFT,play)&&!press(VK_RIGHT,f.host,WM_SYSKEYDOWN),"A modified arrow controlled narration");
        check(SetKeyboardState(keyboard.clear.data())!=FALSE,"Cannot restore the isolated arrow modifier");
    }
    check(!press(VK_LEFT,play,WM_SYSKEYDOWN,1LL|(1LL<<29)),"An Alt-context system arrow controlled narration");
    f.reader->set_controls_visible(false);check(!press(VK_RIGHT,play),"A hidden reader consumed a seek arrow");f.reader->set_controls_visible(true);
    // Normal reading commonly leaves focus on a paragraph or the header's
    // reader button, not on the floating playback controls.
    SendMessageW(readonly,EM_SETSEL,0,0);
    for(const auto target:{readonly,child(f.view,111),child(f.view,1111)}){
        SetFocus(target);check(GetFocus()==target,"Cannot focus an ordinary reading surface");
        for(const auto& [key,button]:mappings){const auto count=probe.commands.size();
            check(press(key,target)&&probe.commands.size()==count+1&&probe.commands.back()==button&&GetFocus()==target,
                "A real prose/header/compile-button focus blocked an existing seek action or changed focus");
            check(press(key,target,WM_KEYDOWN,1LL|(1LL<<30))&&probe.commands.size()==count+1,
                "An autorepeated prose seek activated the reader twice");
        }
    }
    check(edit_text(readonly)==readonly_text&&edit_text(source)==source_text,
        "Reading shortcuts rewrote curricular prose or source code");
    DestroyWindow(list);DestroyWindow(math);
    std::cout<<"PASS F5/Left -3, F6/Up -5, F7/Down +5, F8/Right +3: root/bar normal/SYS once, editor/readonly/math/card-list arrows preserve documents/cursors/selection and paused playback\n";
}
void missing_recording_recovery(const std::shared_ptr<windows::NarrationWorker>& backend) {
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
    // The timed word can be a technical fragment. Its pending ink must
    // contrast with that fragment's own background, just as ReadingShade does.
    CHARRANGE old_selection{};SendMessageW(old_part.target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&old_selection));
    CHARRANGE timed_word{character,character+1};SendMessageW(old_part.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&timed_word));
    CHARFORMAT2W word_style{};word_style.cbSize=sizeof(word_style);
    SendMessageW(old_part.target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&word_style));
    SendMessageW(old_part.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old_selection));
    const auto pending_ink=reader_theme::unread_color((word_style.dwEffects&CFE_AUTOBACKCOLOR)
        ?reader_theme::background(old_part.target):word_style.crBackColor);
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
}

int main(int argc,char** argv){const bool stderr_only=argc==2&&std::string_view(argv[1])=="--stderr-objective";
    const bool dependencies_only=argc==2&&std::string_view(argv[1])=="--dependent-puzzles";
    const bool integrated_only=argc==2&&std::string_view(argv[1])=="--integrated-u1";
    const bool integrated_order_only=argc==2&&std::string_view(argv[1])=="--integrated-order";
    const bool rates_only=argc==2&&std::string_view(argv[1])=="--rates";
    const bool compiler_focus_only=argc==2&&std::string_view(argv[1])=="--compiler-focus";
    const bool seek_keys_only=argc==2&&std::string_view(argv[1])=="--seek-keys";
    const bool missing_recording_only=argc==2&&std::string_view(argv[1])=="--missing-recording";
    if(argc>1&&!stderr_only&&!dependencies_only&&!integrated_only&&!integrated_order_only&&!rates_only&&!compiler_focus_only&&!seek_keys_only&&!missing_recording_only)return 2;
    std::cout<<std::unitbuf;const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    try{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        auto backend=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
        if(missing_recording_only){missing_recording_recovery(backend);return 0;}
        if(seek_keys_only){reader_arrow_aliases_preserve_control_keys(backend);return 0;}
        if(compiler_focus_only){checked_compilation_keeps_keyboard_reader_control(backend);return 0;}
        if(rates_only){extended_reader_playback_rates(backend);return 0;}
        if(integrated_only){integrated_u1_reader_flow(backend);return 0;}
        if(integrated_order_only){integrated_order_reader_flow(backend);return 0;}
        if(dependencies_only){dependent_puzzle_reader_bookmarks(backend);independent_feedback_does_not_complete_a_pending_compiler(backend);return 0;}
        runtime_stderr_reader_objective(backend);if(stderr_only)return 0;
        extended_reader_playback_rates(backend);
        checked_compilation_keeps_keyboard_reader_control(backend);
        reader_arrow_aliases_preserve_control_keys(backend);
        dependent_puzzle_reader_bookmarks(backend);
        independent_feedback_does_not_complete_a_pending_compiler(backend);
        integrated_u1_reader_flow(backend);
        integrated_order_reader_flow(backend);
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
            const std::array<std::pair<UINT,int>,6> system_mappings{{{VK_F1,6504},{VK_F2,6506},{VK_F5,6500},{VK_F6,6501},{VK_F7,6502},{VK_F8,6503}}};
            for(const auto& [key,button]:system_mappings){
                const auto count=probe.commands.size();SetFocus(nullptr);
                check(press(key,f.host,1,WM_SYSKEYDOWN)&&probe.commands.size()==count+1&&probe.commands.back()==button,
                    "An unmodified SYS reader shortcut did not activate its existing button from the active root");
                check(press(key,f.host,1LL|(1LL<<30),WM_SYSKEYDOWN)&&probe.commands.size()==count+1,
                    "Holding an unmodified SYS reader shortcut activated its button more than once");
                check(!press(key,f.host,1,WM_SYSKEYUP)&&probe.commands.size()==count+1,
                    "A SYS reader shortcut release activated its button again");f.reader->pause();
            }
            const auto count=probe.commands.size();
            for(const auto modifier:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN}){
                auto state=keyboard.clear;state[modifier]=0x80;check(SetKeyboardState(state.data())!=FALSE,"Cannot set QA modifier");
                check(!press(VK_F1,edit)&&!press(VK_F1,f.host,1,WM_SYSKEYDOWN)&&probe.commands.size()==count,
                    "A modified normal/SYS function key activated narration");
                check(SetKeyboardState(keyboard.clear.data())!=FALSE,"Cannot clear QA modifier");
            }
            check(!press(VK_F1,edit,1LL|(1LL<<29),WM_SYSKEYDOWN)&&!press(VK_F1,edit,1,WM_KEYUP)&&!press(VK_F3,edit),
                "The reader consumed an Alt-context system, release or unassigned key");
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
        for(const int scenario:{0,1,2}){
            const bool revised_prose=scenario==1,integrated=scenario==2;
            auto authored=test::load_pre_retirement_lesson("lesson_primer_programa_y_salida.json");
            const auto prediction=identity_position(authored,"block_0004_0003");
            const auto retiring_editor=identity_position(authored,"ejecucion_original_editor");
            Fixture f(backend,false,true,true,"es",authored,false,retiring_editor.page);
            if(revised_prose||integrated)f.answer(authored.pages[prediction.page].blocks[prediction.block].answers.at(0),prediction.block);
            const auto old=f.snapshot();const auto old_regions=lesson_reading_regions(f.view);const auto old_page=lesson_page_state(f.view);const auto archived=std::ranges::find_if(old.segments,[&](const auto& part){return part.block==retiring_editor.block&&!part.feedback;});
            check(archived!=old.segments.end(),"The real old compiler script has no removable editor anchor");
            check(std::ranges::none_of(old_regions,[](const auto& region){return region.key.find("/block_0004_0002/")!=std::string::npos;}),
                "The real pending original failed to hide its dependent compiler in the published lesson");
            const auto old_index=static_cast<std::size_t>(archived-old.segments.begin());
            f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush old retirement QA view");DestroyWindow(f.view);f.view=nullptr;
            const std::array<std::string,2> retiring{"ejecucion_original_consigna","ejecucion_original_editor"};
            auto active=integrated?windows::load_lesson(authored.source_path):test::retire_lesson(authored,retiring);
            if(revised_prose){
                // Presentation changes are supplied as a separate loaded-model
                // copy, as Work's validated integration will do. Historical
                // narration must use archived payloads, not today's prose.
                const auto current_prediction=identity_position(active,"block_0004_0003");
                const auto current_heading=identity_position(active,"modificar_mensaje_titulo");
                active.pages[current_prediction.page].blocks[current_prediction.block].question="Predice primero el mensaje del programa que vas a modificar. No ejecutes antes de responder.";
                active.pages[current_heading.page].blocks[current_heading.block].text="Ahora construye el mensaje solicitado en este editor independiente.";
            }
            const auto successor=curriculum::active_block_successor(active,"ejecucion_original_editor");
            check(successor.has_value(),"The retired compiler lost its active reading successor");
            {
                state::StateStore store(f.environment.paths.root);auto data=store.load();
                auto history=narration::begin({},"other-paused","paused-owner",{0,100,0,false},{},"other-page-a");
                history=narration::begin(std::move(history),"other-finished","finished-owner",{0,200,0,false},{},"other-page-b");
                history=narration::checkpoint(std::move(history),"other-finished","finished-owner",{0,200,0,true});
                history=narration::begin(std::move(history),old.key,"retirement-qa-owner",{static_cast<std::int64_t>(old_index),123,0,false},{},old_page.narration_page_identity);
                narration::write_history(data,history);store.save(data);
            }
            f.view=create_lesson_view(f.host,f.environment,std::move(active));MoveWindow(f.view,0,0,980,720,TRUE);
            LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
            hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
            hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};set_lesson_view_hooks(f.view,hooks);
            const auto archives=retired_lesson_reading_archives(f.view);
            const auto historical=std::ranges::find(archives,old_page.narration_page_identity,&LessonReadingArchive::page_identity);
            check(historical!=archives.end()&&historical->regions.size()==old_regions.size()&&
                std::ranges::equal(historical->regions,old_regions,[](const auto& a,const auto& b){return a.key==b.key&&a.source_text==b.source_text;}),
                "Retirement failed to retain the exact published page identity, original prose or dependency-filtered script");
            std::vector<windows::NarrationSegment> old_script;
            for(const auto& region:historical->regions){const auto sentences=backend->plan(region.source_text,"es");old_script.insert(old_script.end(),sentences.begin(),sentences.end());}
            check(backend->identity(historical->page_identity,"es","ef_dora",old_script)==old.key,
                "The archived script cannot prove the exact real pre-retirement bookmark key");
            f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto migrated=f.snapshot();
            check(migrated.key!=old.key&&migrated.offset==0&&!migrated.finished&&!migrated.playing&&migrated.heard_through==0,
                "Retiring an editor invented playback, listening credit or a timing offset");
            check(migrated.segments[migrated.index].block==successor->block&&std::ranges::none_of(migrated.segments,[](const auto& part){
                return part.region.find("ejecucion_original_")!=std::string::npos;}),
                "An archived editor bookmark failed to land on the active successor or retained retired narration");
            const auto history=narration::read_history(read_lesson_state(f.view));
            check(history.entries.size()==3&&history.entries.front().key==migrated.key&&history.entries.front().page==lesson_page_state(f.view).narration_page_identity
                &&history.entries[1].key=="other-finished"&&history.entries[1].position.finished&&history.entries[1].position.milliseconds==200
                &&history.entries[2].key=="other-paused"&&history.entries[2].position.milliseconds==100,
                "Retirement created parallel narration history or retained the obsolete page identity");
            std::cout<<"PASS real retired compiler bookmark"<<(integrated?" with integrated assets":revised_prose?" with revised active prose":"")<<": exact old identity/script, active successor, no retired narration, preserved three page slots and no invented timing\n";
        }
        {
            auto required=compiler_model(true);required.pages[0].blocks[1].initial_code=compiler_source("Mal");
            Fixture f(backend,false,true,true,"es",std::move(required));
            const auto pending=lesson_reading_regions(f.view);
            check(std::ranges::any_of(pending,[](const auto& region){return region.block_index==1&&region.is_exercise&&!region.solved;}),
                "A required compiler prompt is not an ordinary unanswered narration gate");
            check(std::ranges::none_of(pending,[](const auto& region){return region.block_index==2;}),
                "A required unanswered compiler exposed following prose to narration");
            f.reader->restart();wait([&]{return f.snapshot().waiting_gate;},"reader reaches required compiler prompt and stops");
            check(f.snapshot().segments[f.snapshot().index].block==1&&!f.snapshot().finished,"Required compiler narration skipped its own pending boundary");
            f.reader->seek_seconds(60);check(f.snapshot().segments[f.snapshot().index].block==1,"Seeking bypassed an unanswered compiler puzzle");
            f.run_program(1,false);
            wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&value.segments[value.index].feedback;},
                "wrong required compiler feedback reads its guide and stops");
            check(!f.snapshot().finished&&!f.snapshot().segments[f.snapshot().index].correct,"Wrong compiler feedback completed the page");
            f.reader->pause();f.edit_program(compiler_source("Bien"));f.ready();f.run_program(1,false);
            const auto paused=f.snapshot();check(!paused.playing&&!paused.allocated&&paused.segments[paused.index].feedback&&paused.segments[paused.index].correct,
                "A compiler success overrode a manual pause or failed to select its approved feedback");
            f.reader->toggle();wait([&]{return f.snapshot().finished;},"required successful compiler feedback continues to following prose");
            std::cout<<"PASS required compiler narration: pending prompt gate, wrong guide stop, manual pause and approved-feedback continuation\n";
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
            f.ready();f.run_program();
            check(f.snapshot().finished&&std::ranges::any_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==1&&region.feedback&&region.correct;}),
                "Editing/recompiling a solved compiler revoked its accepted feedback or restarted completed narration");
            SendMessageW(child(f.view,1112),BM_CLICK,0,0);f.edit_program(compiler_source("Mal"));f.ready();f.run_program();const auto wrong=f.snapshot();
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
            const auto before_repeat=f.snapshot();
            f.run_program();const auto repeated=f.snapshot();
            check(repeated.finished&&repeated.index==before_repeat.index&&repeated.offset==before_repeat.offset
                    &&color(child(f.view,1000),0)==previous_ink,"Recompiling a solved compiler restarted the approved narration or changed its position");
            check(std::ranges::equal(previously_recorded,repeated.segments,[](const auto& a,const auto& b){return a.recording==b.recording;}),
                "Recompiling a solved compiler invalidated compatible recording caches");
            std::cout<<"PASS real compiler output verdicts: technical word shading, released pause/exact resume, heard-feedback continuation, wrong stop/replay, corrected source and bounded history\n";
        }
        {
            // Preserve the verified pre-retirement P1/U1/L2/P5 ordering: an already solved
            // prediction precedes two independently checked C++ editors.
            // Only the saved page of this owned QA profile is seeded; no
            // historical text, expected output, answer or dependency is changed.
            // The integrated one-editor page is exercised separately below.
            auto authored=test::load_pre_retirement_lesson("lesson_primer_programa_y_salida.json");
            const auto predicted=identity_position(authored,"block_0004_0003");
            const auto original=identity_position(authored,"ejecucion_original_editor");
            const auto modification=identity_position(authored,"block_0004_0002");
            const auto instruction=identity_position(authored,"block_0004_0001");
            const auto following=identity_position(authored,"block_0004_0004");
            check(predicted.page==original.page&&original.page==modification.page&&modification.page==following.page,
                "The historical reader fixture no longer has its declared same-page activity sequence");
            const auto display_page=predicted.page;
            const auto& page=authored.pages.at(display_page);const auto prediction=page.blocks.at(predicted.block).answers.at(0);
            auto corrected_source=page.blocks.at(modification.block).initial_code;
            const auto original_output=page.blocks.at(original.block).output_check->cases.at(0).outputs.at(0);
            const auto expected_output=page.blocks.at(modification.block).output_check->cases.at(0).outputs.at(0);
            const auto reviewed_wrong_output=page.blocks.at(modification.block).output_check->cases.at(0).review_feedback.at(0).outputs.at(0);
            const auto literal=corrected_source.find('"'+original_output+'"');check(literal!=std::string::npos,"The authored P5 compiler fixture lost its original literal");
            auto reviewed_wrong_source=corrected_source;reviewed_wrong_source.replace(literal+1,original_output.size(),reviewed_wrong_output);
            corrected_source.replace(literal+1,original_output.size(),expected_output);
            Fixture f(backend,false,true,true,"es",std::move(authored),false,display_page);
            check(lesson_page_state(f.view).index==display_page,"The actual authored P5 was not restored in its independent QA profile");
            f.answer(prediction,predicted.block);f.reader->restart();f.reader->pause();
            check(f.snapshot().heard_through==0,"Restart falsely preserved earlier solved-prediction listening credit");
            f.run_program(original.block);const auto correct=f.snapshot();
            const auto earlier=std::ranges::find_if(correct.segments,[&](const auto& part){return part.block==predicted.block&&part.feedback&&part.correct;});
            check(earlier!=correct.segments.end()&&correct.segments[correct.index].block==original.block&&correct.segments[correct.index].feedback&&
                correct.segments[correct.index].correct&&correct.offset==0&&correct.heard_through==0,
                "The fresh original compiler check did not take priority independently of the unheard solved prediction");
            const auto unheard_edge=static_cast<std::int64_t>(earlier-correct.segments.begin());
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==instruction.block&&value.offset>150;},
                "authored P5 compiler feedback continues through Modificar into its following instructions despite an unheard solved prefix");
            f.reader->pause();const auto continued=f.snapshot();
            check(continued.heard_through<=unheard_edge,"Prioritized compiler continuation credited the skipped solved prediction as heard");
            f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();const auto restored=f.snapshot();
            check(restored.key==continued.key&&restored.index==continued.index&&restored.offset==continued.offset&&restored.heard_through==continued.heard_through&&
                !restored.playing&&!restored.allocated&&narration::read_history(read_lesson_state(f.view)).entries.size()<=3,
                "Reopening a prioritized position changed its exact pause, credited earlier feedback or created another bookmark");
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.index==restored.index&&value.offset>=restored.offset;},
                "restored priority resumes the exact real instruction recording");f.reader->pause();
            f.edit_program(reviewed_wrong_source,modification.block);f.ready();f.run_program(modification.block);const auto wrong=f.snapshot();
            check(wrong.segments[wrong.index].block==modification.block&&wrong.segments[wrong.index].feedback&&!wrong.segments[wrong.index].correct,
                "The authored second editor reused the original editor's correct verdict");
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&value.segments[value.index].block==modification.block&&value.segments[value.index].feedback;},
                "authored incorrect compiler feedback stops before the following unsolved multiple choice");
            const auto guide_end=f.snapshot();double guide_duration=0;auto guide_start=guide_end.index;
            for(std::size_t i=0;i<guide_end.segments.size();++i)if(guide_end.segments[i].block==modification.block&&guide_end.segments[i].feedback){
                guide_start=std::min(guide_start,i);guide_duration+=guide_end.segments[i].duration_ms;}
            const auto leave_seconds=static_cast<int>(std::ceil(guide_duration/1000.))+1;
            f.reader->seek_seconds(-leave_seconds);const auto outside=f.snapshot();
            check(outside.index<guide_start&&!outside.allocated&&outside.heard_through<=unheard_edge,
                "Rewinding across the incorrect guide's beginning failed to return to original page text without false hearing");
            f.reader->seek_seconds(leave_seconds);const auto returned=f.snapshot();
            check(returned.segments[returned.index].block==modification.block&&returned.segments[returned.index].feedback&&!returned.segments[returned.index].correct&&
                returned.heard_through==outside.heard_through&&!returned.allocated&&!returned.finished,
                "Returning to the incorrect guide lost the other correct block's original permission or escaped its own stop");
            f.reader->seek_seconds(-1);f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==modification.block&&value.segments[value.index].feedback;},
                "authored incorrect compiler feedback replays after actual rewind");f.reader->pause();
            f.edit_program(corrected_source,modification.block);f.ready();f.run_program(modification.block);const auto corrected=f.snapshot();
            check(corrected.segments[corrected.index].block==modification.block&&corrected.segments[corrected.index].feedback&&corrected.segments[corrected.index].correct&&corrected.offset==0,
                "Correcting the actual second editor did not select its own fresh correct feedback");
            const auto recording=corrected.segments[corrected.index].recording;
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==following.block;},
                "correct second-editor feedback continues normally and stops at the actual later unsolved puzzle");
            check(f.snapshot().heard_through<=unheard_edge&&!f.snapshot().finished,"Continuation invented prefix listening or crossed an unsolved later puzzle");
            const auto before_repeat=f.snapshot();f.run_program(modification.block);const auto repeated=f.snapshot();
            check(repeated.key==before_repeat.key&&repeated.index==before_repeat.index&&repeated.offset==before_repeat.offset
                    &&repeated.waiting_gate==before_repeat.waiting_gate&&repeated.heard_through==before_repeat.heard_through
                    &&repeated.segments[repeated.index].recording==before_repeat.segments[before_repeat.index].recording
                    &&std::ranges::any_of(repeated.segments,[&](const auto& part){return part.block==modification.block&&part.feedback&&part.correct&&part.recording==recording;}),
                "Recompiling an approved editor changed its pending narration position, granted listening credit or lost its compatible recording");
            f.reader->seek_seconds(-1);const auto back=f.snapshot();
            check(back.index<repeated.index||back.offset<repeated.offset,"Prioritized compiler rewind did not move the real paused position");
            f.reader->seek_seconds(60);const auto sought=f.snapshot();
            check(sought.segments[sought.index].block==following.block&&!sought.finished&&sought.heard_through==repeated.heard_through&&!sought.allocated,
                "Prioritized manual forward invented hearing, allocated paused audio or bypassed the later unanswered puzzle");
            f.reader->restart();f.reader->pause();f.reader->seek_seconds(60);const auto restarted=f.snapshot();
            check(restarted.segments[restarted.index].block==predicted.block&&restarted.segments[restarted.index].feedback&&restarted.heard_through==0&&!restarted.finished,
                "Restart kept answer priority instead of restoring the ordinary first unheard-feedback boundary");
            check(narration::read_history(read_lesson_state(f.view)).entries.size()<=3,"Answer priority introduced a parallel narration history");
            std::cout<<"PASS pinned historical P1/U1/L2/P5: unheard solved prediction, fresh real C++ priority/continuation, exact released restore, wrong replay, later puzzle bounds and cache-preserving seeks without false hearing\n";
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
        missing_recording_recovery(backend);
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
