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
#include "platform/windows/winrt_apartment.hpp"
#include "core/lesson.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/math_label_test_fixture.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "core/narration_speed.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <functional>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>

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
void await_resumed_player(const std::function<LessonReaderSnapshot()>& snapshot,
        const LessonReaderSnapshot& paused,const std::string& description){
    const auto requested=snapshot();
    check(requested.key==paused.key&&requested.index==paused.index&&requested.offset==paused.offset,
        description+": the explicit request changed its exact saved position");
    wait([&]{const auto value=snapshot();
        check(value.key==paused.key&&value.index==paused.index,description+": verification selected a different recording");
        if(!value.allocated)return false;
        check(value.offset>=paused.offset,description+": the allocated player moved behind its saved position");return true;
    },description);
}
HWND child(HWND parent,int id){struct Search{int id;HWND result=nullptr;} value{id};EnumChildWindows(parent,[](HWND item,LPARAM arg)->BOOL{
    auto& search=*reinterpret_cast<Search*>(arg);if(GetDlgCtrlID(item)==search.id){search.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&value));
    check(value.result!=nullptr,"Missing actual lesson control "+std::to_string(id));return value.result;
}
curriculum::StatePosition identity_position(const curriculum::Lesson& lesson,std::string_view identity){
    const auto position=curriculum::stable_block_position(lesson,identity);
    check(position.has_value(),"Missing authored reader identity "+std::string(identity));return *position;
}
CHARFORMAT2W character_format(HWND target,LONG offset){CHARRANGE before{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{offset,offset+1};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));CHARFORMAT2W format{};format.cbSize=sizeof(format);
    SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return format;
}
COLORREF color(HWND target,LONG offset){return character_format(target,offset).crTextColor;}
COLORREF pending_color(HWND target,LONG offset){
    const auto surface=reader_theme::background(target);
    const auto background=[&](LONG character){const auto format=character_format(target,character);
        return(format.dwEffects&CFE_AUTOBACKCOLOR)?surface:format.crBackColor;};
    const auto references=rich_text_reference_ranges(target);
    const auto semantic_surface=references.empty()?surface:background(static_cast<LONG>(references.front().first));
    const auto actual_surface=background(offset);const auto grays=reader_theme::unread_colors(surface,semantic_surface);
    return actual_surface==semantic_surface?grays.second:actual_surface==surface?grays.first:reader_theme::unread_color(actual_surface);
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
    Fixture(std::shared_ptr<windows::NarrationWorker> backend,bool exercise=false,bool after=true,bool advance=true,std::string_view language="es",std::optional<curriculum::Lesson> supplied={},bool private_audio=false,std::size_t initial_page=0,bool hidden_host=false,bool wait_initial=true,std::function<void(HWND)> before_reader={},std::function<void(state::StateData&)> initialize_state={},std::filesystem::path private_cache={},bool validate_math=false):
        environment(resolve_windows_environment({ProfileId::Codex,L"native-reader-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})){
        check(!std::filesystem::exists(environment.paths.root),"QA profile already exists");
        if(!private_cache.empty()){
            check(private_audio&&private_cache.is_absolute()&&!std::filesystem::exists(private_cache),"An owned narration cache must be a new absolute path");
            environment.paths.cache=std::move(private_cache);
        }else if(!private_audio)environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";
        initialize_profile_directories(environment.paths);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|(hidden_host?WS_EX_NOACTIVATE:0),L"STATIC",L"Native real Kokoro reader QA",WS_OVERLAPPEDWINDOW|(hidden_host?0:WS_VISIBLE),
            40,40,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create reader QA window");
        auto lesson=supplied?std::move(*supplied):model(exercise,after,language);
        if(initial_page||initialize_state){state::StateStore store(environment.paths.root);auto data=store.load();state::prepare_lesson_state(data,lesson);
            if(initial_page)state::save_current_page(data,lesson,initial_page);
            // Test-owned initial data belongs in the new QA store before the
            // view exists, never in its protected reader-preferences callback.
            if(initialize_state)initialize_state(data);store.save(data);}
        view=create_lesson_view(host,environment,std::move(lesson));check(view!=nullptr,"Cannot create actual lesson view");MoveWindow(view,0,0,980,720,TRUE);
        update_lesson_preferences(view,[advance](state::StateData& data){auto& prefs=data.preferences["lesson_narration_settings"];
            prefs["engine"]=std::string("KOKORO");prefs["language"]=std::string("es");prefs["kokoro_voice:es"]=std::string("ef_dora");prefs["rate"]=2.;prefs["kokoro_advance_generation"]=advance;});
        LessonViewHooks hooks;hooks.before_page_change=[this](HWND){if(reader)reader->pause();};
        hooks.content_changed=[this](HWND){if(reader)reader->content_changed();};
        hooks.feedback_changed=[this](HWND,std::size_t block,bool correct){if(reader)reader->feedback(block,correct);};
        if(validate_math)hooks.validate_math=[backend](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){return backend->validate_math(block,answers,locale,stop);};
        set_lesson_view_hooks(view,std::move(hooks));
        if(before_reader)before_reader(view);
        reader=std::make_unique<LessonReader>(view,environment,std::move(backend),[]{});
        if(wait_initial){if(advance)ready();else wait([&]{const auto s=snapshot();return s.planned&&!s.segments.empty()&&s.segments.front().available;},"current segment without advance preparation");}
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
#include "presentation/windows/u11_domain_reader_test.hpp"
#include "presentation/windows/u11_consult_reader_test.hpp"
#include "presentation/windows/u11_assignment_reader_test.hpp"
#include "presentation/windows/u11_literal_spacing_reader_test.hpp"
#include "presentation/windows/u12_file_format_reader_test.hpp"
#include "presentation/windows/u12_file_roundtrip_reader_test.hpp"
#include "presentation/windows/u12_append_hypothesis_reader_test.hpp"
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
void current_required_compiler_reader(const std::shared_ptr<windows::NarrationWorker>& backend){
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_enteros_y_aproximaciones.json";
    const auto asset_hash=state::sha256(state::read_bytes(path));
    const auto lesson=windows::load_lesson(path);
    const auto first=identity_position(lesson,"block_0006_0002");
    const auto second=identity_position(lesson,"block_0006_0006");
    const auto third=identity_position(lesson,"block_0007_0009");
    check(first.page==6&&first.block==2&&second.page==6&&second.block==6&&third.page==7&&third.block==9,
        "The reported U2L5 compiler chain moved from its authored identities");
    const auto& goal=lesson.pages[first.page].blocks[first.block];
    const auto& next_goal=lesson.pages[second.page].blocks[second.block];
    check(goal.question.empty()&&goal.output_check&&lesson.compiler_puzzles_required
        &&next_goal.question.empty()&&next_goal.output_check,
        "The current compiler regression no longer covers the actual questionless required objectives");
    Fixture f(backend,false,true,true,"es",lesson,false,first.page);
    check(lesson_page_state(f.view).index==first.page,"The reported compiler page was not reached in its isolated profile");
    check(!windows::compiler_puzzle_completed(read_lesson_state(f.view),lesson,goal),
        "The untouched reported compiler already has execution credit");
    const auto no_later_regions=[&](std::size_t boundary){
        const auto regions=lesson_reading_regions(f.view);
        check(std::ranges::any_of(regions,[&](const auto& region){
            return region.block_index==boundary&&region.required_compiler&&region.is_exercise&&!region.solved;
        }),"The actual U2L5 p7 compiler objective has no pending reader boundary");
        check(std::ranges::none_of(regions,[&](const auto& region){
            return region.block_index!=std::numeric_limits<std::size_t>::max()&&region.block_index>boundary;
        }),"An unchecked actual compiler exposed following prose or editor narration");
        check(!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,third.page),
            "The pending compiler allowed page navigation");
    };
    const auto wait_at_gate=[&](std::size_t boundary){
        wait([&]{const auto value=f.snapshot();
            check(value.segments[value.index].block==std::numeric_limits<std::size_t>::max()
                ||value.segments[value.index].block<=boundary,"The reader played beyond an unchecked actual compiler");
            return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==boundary;
        },"actual required compiler reader waits for its own check");
        check(!f.snapshot().finished,"A required compiler prompt falsely finished the page");
    };
    no_later_regions(first.block);
    f.reader->restart();f.reader->pause();f.reader->seek_seconds(100000);
    const auto sought=f.snapshot();
    check(sought.segments[sought.index].block==first.block&&!sought.finished&&!sought.allocated,
        "Forward seek bypassed the untouched actual required compiler");
    f.reader->seek_seconds(-1);f.reader->toggle();wait_at_gate(first.block);
    f.reader->seek_seconds(100000);check(f.snapshot().segments[f.snapshot().index].block==first.block,
        "A seek while waiting bypassed the actual compiler");
    f.reader->seek_seconds(-1);f.reader->toggle();wait_at_gate(first.block);
    // The untouched authored template compiles but prints nothing. Its actual
    // execution, not a synthetic receipt, must produce the ordinary retry stop.
    f.run_program(first.block,false);
    wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&!value.preparing
        &&value.segments[value.index].block==first.block&&value.segments[value.index].feedback;},
        "the actual required compiler's incorrect verdict finishes and stops");
    const auto rejected=f.snapshot();
    check(!rejected.finished&&!rejected.segments[rejected.index].correct
        &&!windows::compiler_puzzle_completed(read_lesson_state(f.view),lesson,goal),
        "An incorrect actual compiler attempt granted completion");
    const auto wrong_audio=verify_feedback_recording(rejected.segments[rejected.index]);
    no_later_regions(first.block);
    f.reader->seek_seconds(-1);f.reader->toggle();
    wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].feedback;},
        "the actual rejected compiler verdict replays after rewind");
    f.reader->pause();
    // The second authored program performs exactly the first task's nine-metre
    // division. Enter it through the first real editor; do not award it a proof.
    f.edit_program(next_goal.initial_code,first.block);f.ready();f.run_program(first.block,false);
    const auto accepted=f.snapshot();const auto accepted_part=accepted.segments[accepted.index];
    check(accepted_part.block==first.block&&accepted_part.feedback&&accepted_part.correct&&accepted.offset==0
        &&!accepted.playing&&!accepted.allocated&&!accepted.finished
        &&windows::compiler_puzzle_completed(read_lesson_state(f.view),lesson,goal)
        &&!windows::compiler_puzzle_completed(read_lesson_state(f.view),lesson,next_goal),
        "The actual success ignored manual pause, lost feedback priority or credited the next compiler");
    const auto accepted_audio=verify_feedback_recording(accepted_part);
    no_later_regions(second.block);
    // Close and reopen the whole lesson against only its new private QA profile.
    // Preserve the real source, selected correct explanation and exact offset.
    f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the isolated actual compiler lesson");
    DestroyWindow(f.view);f.view=create_lesson_view(f.host,f.environment,lesson);
    check(f.view!=nullptr,"Cannot reopen the isolated actual compiler lesson");MoveWindow(f.view,0,0,980,720,TRUE);
    LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
    hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
    hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
    set_lesson_view_hooks(f.view,std::move(hooks));
    f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
    const auto restored=f.snapshot();
    check(restored.key==accepted.key&&restored.index==accepted.index&&restored.offset==accepted.offset
        &&restored.heard_through==accepted.heard_through&&!restored.playing&&!restored.allocated
        &&edit_text(child(f.view,1010+static_cast<int>(first.block)*100))==next_goal.initial_code
        &&state::sha256(state::read_bytes(accepted_part.recording))==accepted_audio
        &&state::sha256(state::read_bytes(rejected.segments[rejected.index].recording))==wrong_audio,
        "Reopening lost the exact approved-feedback bookmark, source or compatible recording");
    f.reader->toggle();
    wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==first.block+1;},
        "the fully heard actual correct feedback continues into the following curricular paragraph");
    wait_at_gate(second.block);no_later_regions(second.block);
    f.reader->seek_seconds(100000);check(f.snapshot().segments[f.snapshot().index].block==second.block,
        "Completing the first compiler allowed seeking past the second pending objective");
    f.reader->restart();f.reader->pause();f.reader->seek_seconds(100000);
    check(f.snapshot().segments[f.snapshot().index].feedback&&f.snapshot().segments[f.snapshot().index].correct
        &&f.snapshot().heard_through==0&&!f.snapshot().finished,
        "Restart retained listening credit or bypassed the ordinary unheard correct-feedback gate");
    f.reader->pause();SendMessageW(child(f.view,1012+static_cast<int>(first.block)*100),BM_CLICK,0,0);f.ready();
    no_later_regions(first.block);
    check(!windows::compiler_puzzle_completed(read_lesson_state(f.view),lesson,goal)
        &&!windows::compiler_puzzle_completed(read_lesson_state(f.view),lesson,next_goal)
        &&edit_text(child(f.view,1010+static_cast<int>(first.block)*100))==goal.initial_code
        &&state::sha256(state::read_bytes(path))==asset_hash,
        "Reset failed to restore the actual pending goal, invented another achievement or modified the asset");
    std::cout<<"PASS current U2L5 p7: actual questionless required compiler waits; seeks/rewind/restart, real rejected and accepted PCM, paused whole-view restore, heard-feedback continuation, independent next gate and Reset\n";
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
    f.reader->toggle();wait([&]{const auto current=f.snapshot();return !current.playing&&!current.allocated&&!current.preparing
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
void integrated_matching_reader_bookmarks(const std::shared_ptr<windows::NarrationWorker>& backend) {
    using namespace winrt::Windows::Data::Json;
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json";
    const auto asset_hash=state::sha256(state::read_bytes(path));const auto lesson=windows::load_lesson(path);
    const auto position=identity_position(lesson,"block_0007_0001");
    const auto& goal=lesson.pages[position.page].blocks[position.block];
    check(position.page==7&&position.block==1&&goal.type==curriculum::BlockType::math_puzzle,
        "The actual editorial matching reader identity moved");
    const auto authored=JsonObject::Parse(wide(goal.raw_json));
    const auto spec=authored.GetNamedArray(L"responses").GetAt(0).GetObject();
    const auto expected=spec.GetNamedObject(L"validator").GetNamedObject(L"expected");
    const auto incorrect_body=utf8(authored.GetNamedObject(L"feedback").GetNamedString(L"incorrect"));
    const auto correct_body=utf8(authored.GetNamedObject(L"feedback").GetNamedString(L"correct"));
    const std::string revised="Construcción desde código y unidades que termina normalmente";
    const std::string source_suffix="contratos/source/construccion";
    auto environment=resolve_windows_environment({ProfileId::Codex,
        L"integrated-matching-reader-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    check(!std::filesystem::exists(environment.paths.root),"Matching reader QA requires a new private profile");
    // Keep the explicitly isolated QA state while using the same physical
    // technical audio cache as Fixture. A logical MSIX AppData alias is not a
    // valid path for the handle-bound playback verification.
    environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";
    initialize_profile_directories(environment.paths);
    state::StateData initial;state::prepare_lesson_state(initial,lesson);
    // Explicit compiler receipts isolate this later page only. No execution or
    // matching answer is invented, and later compiler objectives stay pending.
    std::set<std::string> direct_prerequisites;
    for(const auto entry:curriculum::activity_prerequisites(lesson,goal))
        direct_prerequisites.insert(lesson.pages[entry.page].blocks[entry.block].stable_id);
    check(direct_prerequisites==std::set<std::string>{"block_0006_0006","block_0006_0009"},
        "The current matching activity no longer has exactly its two authored direct owners");
    check(lesson.compiler_puzzles_required,"Matching reader QA must preserve the published global compiler gate");
    std::set<std::string> prerequisite_ids;
    for(std::size_t page=0;page<position.page;++page)for(const auto& block:lesson.pages[page].blocks)
        if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
            for(const auto& item:block.output_check->cases){
                curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
                execution.executable=item.executable;
                if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA prerequisite only";
                (void)windows::record_compiler_puzzle_case(initial,lesson,block,request,execution,"Synthetic QA prerequisite only; no execution");
            }
            check(windows::compiler_puzzle_completed(initial,lesson,block),"Cannot isolate the matching page's prior compiler objective");
            prerequisite_ids.insert(block.stable_id);
        }
    check(prerequisite_ids==std::set<std::string>{"block_0001_0002","block_0001_0007","block_0004_0002","block_0006_0006","block_0006_0009"}
        &&!state::get_response(initial,lesson,goal),"Matching QA must isolate exactly the five prior global compiler gates without an invented answer");
    state::save_current_page(initial,lesson,position.page);
    initial.preferences["matching_reader_qa"]["opaque"]=std::string(" \t preserved\n");
    auto& settings=initial.preferences["lesson_narration_settings"];
    settings["engine"]=std::string("KOKORO");settings["language"]=std::string("es");
    settings["kokoro_voice:es"]=std::string("ef_dora");settings["rate"]=3.;settings["kokoro_advance_generation"]=false;
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);
    const auto host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden actual matching reader QA",
        WS_OVERLAPPEDWINDOW,0,0,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host!=nullptr,"Cannot create hidden matching reader host");
    HWND view=nullptr;std::unique_ptr<LessonReader> reader;
    struct Cleanup{HWND host;HWND& view;std::unique_ptr<LessonReader>& reader;
        ~Cleanup(){reader.reset();if(view){(void)flush_lesson_view(view);DestroyWindow(view);}DestroyWindow(host);}} cleanup{host,view,reader};
    const auto snapshot=[&]{auto value=reader->snapshot();check(value.error.empty(),"Real matching reader error: "+value.error);return value;};
    const auto open=[&]{
        LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(reader)reader->pause();};
        hooks.content_changed=[&](HWND){if(reader)reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(reader)reader->feedback(block,correct);};
        hooks.validate_math=[&](std::string_view block,std::string_view answers,std::string_view language,std::stop_token stop){
            return backend->validate_math(block,answers,language,stop);};
        view=create_lesson_view(host,environment,lesson,std::move(hooks));
        check(view!=nullptr,"Cannot mount actual matching reader view");MoveWindow(view,0,0,980,720,TRUE);pump();
        check(lesson_page_state(view).index==position.page&&!IsWindowVisible(host),"Matching reader QA lost its authored page or exposed the desktop");
        reader=std::make_unique<LessonReader>(view,environment,backend,[]{});
        wait([&]{const auto value=snapshot();return value.planned&&!value.segments.empty()&&value.segments[value.index].available;},
            "the actual matching current sentence is synthesized");
    };
    const auto reopen=[&]{reader.reset();check(flush_lesson_view(view),"Cannot persist actual matching reader before close");
        DestroyWindow(view);view=nullptr;open();};
    const auto region=[&](std::string_view suffix){const auto regions=lesson_reading_regions(view);
        const auto found=std::ranges::find_if(regions,[&](const auto& value){return value.key.ends_with(suffix);});
        check(found!=regions.end(),"Missing actual matching narration card "+std::string(suffix));return *found;};
    const auto current_label=[&]{const auto label=region(source_suffix);
        check(label.source_text==revised&&edit_text(label.target)==revised,"Actual matching label retained its obsolete editorial wording");};
    struct Recording{std::filesystem::path path;std::string hash;};std::vector<Recording> recordings;
    const auto record=[&](const LessonReaderSegmentSnapshot& part){recordings.push_back({part.recording,verify_feedback_recording(part)});};
    const auto restore_pause=[&](const LessonReaderSnapshot& paused,const std::optional<state::Response>& response){
        const auto history=narration::read_history(read_lesson_state(view));
        const auto saved=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
        check(saved!=history.entries.end()&&saved->position.segment==static_cast<std::int64_t>(paused.index)&&saved->position.milliseconds==paused.offset
            &&saved->position.heard_through==paused.heard_through&&!saved->position.finished,
            "Actual matching playback failed to create an exact real bookmark");
        const auto recording=paused.segments[paused.index].recording;reopen();current_label();
        const auto restored=snapshot();const auto restored_response=state::get_response(read_lesson_state(view),lesson,goal);
        check(restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset
            &&restored.heard_through==paused.heard_through&&!restored.playing&&!restored.allocated&&!restored.finished
            &&restored.segments[restored.index].recording==recording&&narration::read_history(read_lesson_state(view))==history
            &&bool(restored_response)==bool(response)&&(!response||restored_response->raw_json==response->raw_json),
            "Reopening the entire matching view lost its current source-bound pause or response");
        const auto until=GetTickCount64()+150;
        wait([&]{const auto value=snapshot();check(!value.playing&&!value.allocated&&value.offset==paused.offset,
                "The restored matching bookmark started itself");return GetTickCount64()>=until;},"matching restoration stays paused",2000);
        reader->toggle();await_resumed_player(snapshot,paused,"Actual matching bookmark resume");
    };
    open();current_label();
    check(narration::read_history(read_lesson_state(view)).entries.empty(),"Matching narration began with fabricated listening history");
    reader->toggle();wait([&]{const auto value=snapshot();return value.playing&&value.segments[value.index].region.ends_with(source_suffix)
        &&value.offset>200;},"real narration reaches the revised construction label",90000);
    reader->pause();const auto label_pause=snapshot();record(label_pause.segments[label_pause.index]);
    check(label_pause.segments[label_pause.index].source==revised&&!label_pause.segments[label_pause.index].feedback,
        "The label bookmark belongs to another current sentence");
    restore_pause(label_pause,{});
    wait([&]{const auto value=snapshot();return value.playing&&value.index==label_pause.index&&value.offset>label_pause.offset+100;},
        "the restored current construction-label recording really resumes");reader->pause();

    // Exercise native source/target cards, not a prepared matching answer.
    const auto tap=[&](std::string_view key){const auto target=region(key).target;
        SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(4,4));pump();};
    auto wrong=JsonObject::Parse(expected.Stringify());const auto construction=wrong.GetNamedValue(L"construccion");
    wrong.Insert(L"construccion",wrong.GetNamedValue(L"transicion"));wrong.Insert(L"transicion",construction);
    for(const auto& pair:wrong){tap("contratos/source/"+utf8(pair.Key()));tap("contratos/target/"+utf8(pair.Value().GetString()));}
    check(IsWindowEnabled(child(view,1103)),"A complete actual matching attempt did not enable Check");
    SendMessageW(child(view,1103),BM_CLICK,0,0);reader->pause();
    wait([&]{const auto response=state::get_response(read_lesson_state(view),lesson,goal);const auto value=snapshot();
        return response&&!response->correct&&state::response_reviewed(*response,goal)&&value.planned&&!value.segments.empty()
            &&value.segments[value.index].feedback&&value.segments[value.index].available;},"native matching rejection and its real current recording");
    const auto wrong_response=state::get_response(read_lesson_state(view),lesson,goal);
    const auto guide=edit_text(child(view,1104));
    check(guide.find(incorrect_body)!=std::string::npos&&guide.find(correct_body)==std::string::npos,
        "The actual rejected matching control lost its complete guide or exposed the successful explanation");
    const auto feedback_regions=lesson_reading_regions(view);const auto feedback=std::ranges::find_if(feedback_regions,
        [&](const auto& value){return value.feedback&&value.block_index==position.block;});
    check(feedback!=feedback_regions.end()&&feedback->source_text==guide&&!feedback->correct,
        "The rejected matching reader has another source or verdict than the visible guide");
    const auto guide_plan=backend->plan(guide,"es");const auto guide_snapshot=snapshot();std::vector<std::size_t> guide_indices;
    for(std::size_t at=0;at<guide_snapshot.segments.size();++at)if(guide_snapshot.segments[at].feedback&&guide_snapshot.segments[at].block==position.block){
        const auto index=guide_indices.size();check(index<guide_plan.size()&&guide_snapshot.segments[at].source==guide_plan[index].text,
            "The real matching guide was truncated or reordered");guide_indices.push_back(at);}
    check(guide_indices.size()==guide_plan.size()&&!guide_indices.empty()&&!snapshot().playing&&!snapshot().allocated,
        "The native matching feedback did not preserve manual pause or all authored sentences");
    std::vector<bool> heard(guide_indices.size(),false);
    const auto observe=[&](const LessonReaderSnapshot& value){for(std::size_t at=0;at<guide_indices.size();++at)
        if(value.playing&&value.index==guide_indices[at])heard[at]=true;};
    reader->toggle();wait([&]{const auto value=snapshot();observe(value);return value.playing&&value.segments[value.index].feedback
        &&value.segments[value.index].source.find("Distingue")!=std::string::npos&&value.offset>200;},"actual matching guide plays before pause");
    reader->pause();const auto guide_pause=snapshot();record(guide_pause.segments[guide_pause.index]);
    restore_pause(guide_pause,wrong_response);
    wait([&]{const auto value=snapshot();observe(value);const auto& current=value.segments[value.index];
        // With advance generation disabled, arriving at the final sentence can
        // precede its synthesis. That transition is not the completed stop.
        return !value.playing&&!value.allocated&&!value.preparing&&value.index==guide_indices.back()
            &&current.available&&current.duration_ms>0&&value.offset+1>=current.duration_ms;},
        "restored matching guide finishes its available final recording and stops for retry");
    const auto stopped=snapshot();const auto response=state::get_response(read_lesson_state(view),lesson,goal);
    std::string heard_flags;for(const auto value:heard)heard_flags+=value?'1':'0';
    check(std::ranges::all_of(heard,[](bool value){return value;})&&!stopped.finished&&response&&!response->correct
        &&response->raw_json==wrong_response->raw_json&&stopped.offset+1>=stopped.segments[stopped.index].duration_ms,
        "Actual matching guide resume boundary: heard="+heard_flags+" finished="+std::to_string(stopped.finished)
        +" correct="+std::to_string(response&&response->correct)+" payload_equal="+std::to_string(response&&response->raw_json==wrong_response->raw_json)
        +" offset="+std::to_string(stopped.offset)+" duration="+std::to_string(stopped.segments[stopped.index].duration_ms)
        +" index="+std::to_string(stopped.index)+" last="+std::to_string(guide_indices.back()));
    for(const auto at:guide_indices)record(stopped.segments[at]);
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            const auto data=read_lesson_state(view);
            check(windows::compiler_puzzle_case_count(data,lesson,block)==windows::compiler_puzzle_case_count(initial,lesson,block)
                &&windows::compiler_puzzle_completed(data,lesson,block)==prerequisite_ids.contains(block.stable_id),
                "Matching playback changed a prior QA compiler receipt or credited another objective");
        }
    for(const auto& recording:recordings)check(state::sha256(state::read_bytes(recording.path))==recording.hash,
        "Matching restoration changed a compatible real recording");
    check(read_lesson_state(view).preferences.at("matching_reader_qa")==initial.preferences.at("matching_reader_qa")
        &&state::sha256(state::read_bytes(path))==asset_hash&&!IsWindowVisible(host),
        "Actual matching reader changed unrelated preferences, content or the desktop");
    std::cout<<"PASS current U9L13 matching reader: revised label and native rejected guide have actual PCM/word timing and real paused playback; whole-view bookmark restoration/resume, unchanged response/recordings and retry stop; five earlier global compiler receipts explicitly synthetic (two direct matching owners), no historical-label listening, bookmark-list UI or visual acceptance claimed\n";
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
    await_resumed_player([&]{return f.snapshot();},paused,"Resuming the reopened actual guide");
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

void current_inline_feedback_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        std::optional<std::size_t> selected) {
    struct Case { const char* file; std::size_t page,block; std::vector<std::string> incorrect; };
    const std::array<Case,9> cases{{
        {"objetos_y_estado/lesson_enteros_y_aproximaciones.json",7,6,{"int","longitud_metros","partes"}},
        {"objetos_y_estado/lesson_enteros_y_aproximaciones.json",3,3,{"double","int"}},
        {"objetos_y_estado/lesson_declarar_e_inicializar.json",2,4,{"12","3"}},
        {"objetos_y_estado/lesson_estado_asignacion_y_copia.json",5,4,{"+","-"}},
        {"objetos_y_estado/lesson_nombres_con_intencion.json",3,4,{"recibidos","sobres_recibidos"}},
        {"objetos_y_estado/lesson_conversiones_y_deduccion.json",3,5,{"int"}},
        {"objetos_y_estado/lesson_cadenas_y_texto.json",3,4,{"\"Sector\"","\"Este\""}},
        {"objetos_y_estado/lesson_caracteres_y_valores_logicos.json",2,3,{"'Y'","'Y'"}},
        {"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json",4,1,{"<<","\"Carga\""}},
    }};
    const auto visible=[](std::string text){std::erase(text,'`');return text;};
    for(std::size_t number=0;number<cases.size();++number){
        if(selected&&*selected!=number)continue;
        const auto& test=cases[number];
        const auto asset=std::filesystem::path(build::source_root)/
            "app/src/main/assets/content/es/programming/cpp_fundamentals"/test.file;
        const auto asset_hash=state::sha256(state::read_bytes(asset));
        const auto original=windows::load_lesson(asset);
        const auto goal=original.pages.at(test.page).blocks.at(test.block);
        const auto asset_bytes=state::read_bytes(asset);
        const auto authored=winrt::Windows::Data::Json::JsonObject::Parse(wide(std::string(
            reinterpret_cast<const char*>(asset_bytes.data()),asset_bytes.size())))
            .GetNamedArray(L"pages").GetObjectAt(static_cast<std::uint32_t>(test.page))
            .GetNamedArray(L"blocks").GetObjectAt(static_cast<std::uint32_t>(test.block));
        check(goal.type==curriculum::BlockType::inline_fill_code&&!goal.review_feedback.empty()
            &&!goal.explanation.empty()&&goal.answers.size()==test.incorrect.size()
            &&!curriculum::evaluate_hole_answers(goal,test.incorrect).correct
            &&goal.review_feedback==utf8(authored.GetNamedString(L"reviewFeedback"))
            &&goal.explanation==utf8(authored.GetNamedString(L"explanation"))
            &&goal.question==utf8(authored.GetNamedString(L"question"))
            &&goal.code_template==utf8(authored.GetNamedString(L"template")),
            "The registered current inline narration case no longer has its authored feedback/evaluator");

        // The complete original block is copied into a new QA lesson, without
        // changing any authored prompt/template/answer/alternative/feedback.
        // Only the QA address changes. This isolates the reader contract from
        // unrelated curricular gates without seeding compiler receipts or wins.
        auto lesson=model();lesson.id="qa_current_inline_reader_"+std::to_string(number+1);
        lesson.title="Retroalimentación.";lesson.pages.resize(1);
        auto& page=lesson.pages.front();page.blocks.front().text="Lectura previa de control.";
        auto target=goal;target.address={0,1,{},{}};page.blocks.push_back(std::move(target));
        auto tail=page.blocks.front();tail.stable_id="after_inline_feedback";tail.address={0,2,{},{}};
        tail.text="La lectura posterior continúa después de toda la explicación.";page.blocks.push_back(std::move(tail));
        Fixture f(backend,false,true,true,"es",lesson,false,0,true,true,[](HWND view){
            // Exercise the supported rate through the allowed preference API;
            // recordings/timings and complete listening still remain real.
            update_lesson_preferences(view,[](state::StateData& data){
                data.preferences["lesson_narration_settings"]["rate"]=3.;
            });
        },[](state::StateData& data){
            data.preferences["inline_audio_qa"]["unrelated"]=std::string(" \t preservar\r\n");
        });
        f.reader->pause();
        const auto unrelated=read_lesson_state(f.view).preferences.at("inline_audio_qa");
        const auto control=[&](int part){return child(f.view,1100+part);};
        const auto field=[&](std::size_t index){const auto result=GetDlgItem(control(5),100+static_cast<int>(index));
            check(result!=nullptr,"A current inline narration fixture lost its actual editable hole");return result;};
        const auto fields=[&](const std::vector<std::string>& values,bool enabled){
            for(std::size_t index=0;index<values.size();++index)
                check(edit_text(field(index))==values[index]&&(IsWindowEnabled(field(index))!=FALSE)==enabled,
                    "Actual inline narration lost its exact answers or completion lock");
        };
        const auto replace=[&](const std::vector<std::string>& values){
            for(std::size_t index=0;index<values.size();++index){
                check(IsWindowEnabled(field(index)),"The current inline retry field is locked");
                const auto text=wide(values[index]);SendMessageW(field(index),EM_SETSEL,0,-1);
                SendMessageW(field(index),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
            }
            f.reader->pause();f.ready();fields(values,true);
        };
        const auto privacy=[&]{
            check(edit_text(control(4)).empty(),"An unreviewed inline answer exposed feedback visually");
            std::string script;
            for(const auto& region:lesson_reading_regions(f.view)){
                check(!region.feedback&&region.target!=control(4),"An unreviewed inline answer exposed feedback to narration");
                script+=region.source_text+'\n';
            }
            check(script.find(goal.review_feedback)==std::string::npos&&script.find(goal.explanation)==std::string::npos,
                "The current inline reader exposed an unpublished guide or solution before Check");
            for(const auto& part:f.snapshot().segments)check(!part.feedback,
                "The prepared narration plan retained feedback for an unreviewed inline draft");
        };
        const auto reopen=[&](const std::function<void(state::StateData&)>& change_closed_qa={}){
            f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the isolated current inline lesson");
            DestroyWindow(f.view);f.view=nullptr;
            if(change_closed_qa){
                // Metadata compatibility fixtures edit only this new QA store
                // while no view owns it. Answers/hearing are never fabricated.
                state::StateStore store(f.environment.paths.root);auto data=store.load();change_closed_qa(data);store.save(data);
            }
            f.view=create_lesson_view(f.host,f.environment,lesson);
            check(f.view!=nullptr,"Cannot reopen the actual inline lesson and saved reader history");
            MoveWindow(f.view,0,0,980,720,TRUE);
            LessonViewHooks hooks;
            hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
            hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
            hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
            set_lesson_view_hooks(f.view,std::move(hooks));
            f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        };
        const auto submit=[&]{check(IsWindowEnabled(control(3)),"A completed inline draft cannot be checked");
            SendMessageW(control(3),BM_CLICK,0,0);f.reader->pause();f.ready();
            check(!f.snapshot().playing&&!f.snapshot().allocated,"Preparing inline feedback overrode an explicit pause");
        };
        struct Recording{std::filesystem::path path;std::string hash;std::filesystem::file_time_type modified;};
        std::vector<Recording> recordings;
        const auto verify_feedback=[&](bool correct,const std::string& body){
            const auto source=std::string(correct?"✓ Correcto\n\n":"✗ Incorrecto\n\n")+body;
            check(edit_text(control(4))==visible(source),"An inline reader fixture omitted or mixed its current full visible feedback");
            const auto regions=lesson_reading_regions(f.view);
            const auto region=std::ranges::find_if(regions,[](const auto& value){return value.block_index==1&&value.feedback;});
            check(region!=regions.end()&&region->source_text==source&&region->target==control(4)
                &&region->correct==correct&&region->solved==correct,
                "The actual inline feedback region lost its complete source, verdict or target");
            const auto plan=backend->plan(source,"es");const auto value=f.snapshot();std::vector<std::size_t> indices;
            double nominal_ms=0;
            for(std::size_t index=0;index<value.segments.size();++index){const auto& part=value.segments[index];
                if(part.block!=1||!part.feedback)continue;
                const auto at=indices.size();
                check(at<plan.size()&&part.region==region->key&&part.correct==correct&&part.target==region->target
                    &&part.source==plan[at].text&&part.start==plan[at].start&&part.end==plan[at].end
                    &&wide(source).substr(part.start,part.end-part.start)==wide(part.source),
                    "Actual inline narration truncated, replaced or reordered an authored feedback sentence");
                recordings.push_back({part.recording,verify_feedback_recording(part),std::filesystem::last_write_time(part.recording)});
                nominal_ms+=part.duration_ms;
                if(part.source.find("9 / 2")!=std::string::npos){
                    const auto slash=wide(part.source).find(L'/');
                    check(std::ranges::any_of(part.timings,[&](const auto& timing){return timing.start<=slash&&timing.end>slash;}),
                        "The current 9 / 2 explanation lost real word-timing ownership of its contextual slash");
                }
                std::cout<<"AUDIO inline="<<number+1<<" verdict="<<(correct?"correct":"retry")<<" part="<<at+1
                    <<" sha256="<<recordings.back().hash<<" path="<<part.recording.generic_string()<<'\n';
                indices.push_back(index);
            }
            check(!indices.empty()&&indices.size()==plan.size()&&value.index==indices.front()&&value.offset==0,
                "Fresh inline feedback failed to select its complete current script from zero");
            std::cout<<"DURATION inline="<<number+1<<" verdict="<<(correct?"correct":"retry")
                <<" parts="<<indices.size()<<" pcmMs="<<nominal_ms<<" rate="<<value.rate
                <<" nominalPlaybackMs="<<nominal_ms/value.rate<<'\n';
            return indices;
        };
        const auto preserve_recordings=[&]{for(const auto& recording:recordings)
            check(state::sha256(state::read_bytes(recording.path))==recording.hash
                &&std::filesystem::last_write_time(recording.path)==recording.modified,
                "Reopening or replaying current inline feedback rewrote an existing recording");};
        const auto response=[&]{return state::get_response(read_lesson_state(f.view),lesson,lesson.pages[0].blocks[1]);};
        const auto restore_pause=[&](const LessonReaderSnapshot& paused,const state::Response& answer){
            const auto history=narration::read_history(read_lesson_state(f.view));reopen();const auto restored=f.snapshot();
            const auto saved=response();
            check(saved&&saved->raw_json==answer.raw_json&&saved->correct==answer.correct
                &&saved->reviewed_answer_identity==answer.reviewed_answer_identity
                &&restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset
                &&restored.heard_through==paused.heard_through&&restored.finished==paused.finished
                &&!restored.playing&&!restored.allocated&&narration::read_history(read_lesson_state(f.view))==history,
                "Whole-view inline restoration changed the reviewed answer, exact real pause or listening history");
            const auto until=GetTickCount64()+200;
            wait([&]{const auto current=f.snapshot();check(!current.playing&&!current.allocated&&current.offset==paused.offset,
                "Restored inline feedback started without an explicit play action");return GetTickCount64()>=until;},
                "restored current inline feedback stays silent",2000);
            fields(answer.correct?goal.answers:test.incorrect,!answer.correct);preserve_recordings();
        };
        struct Observation{
            std::vector<bool> heard;
            std::vector<std::set<std::int64_t>> offsets;
            std::vector<std::set<std::vector<COLORREF>>> masks;
        };
        const auto observe=[&](Observation& evidence,const std::vector<std::size_t>& indices,const LessonReaderSnapshot& value){
            if(!value.playing)return;
            const auto found=std::ranges::find(indices,value.index);if(found==indices.end())return;
            const auto at=static_cast<std::size_t>(found-indices.begin());evidence.heard[at]=true;
            if(!evidence.offsets[at].insert(value.offset).second)return;
            const auto& part=value.segments[value.index];const auto displayed=wide(edit_text(part.target));
            const auto source=wide(std::string(part.correct?"✓ Correcto\n\n":"✗ Incorrecto\n\n")
                +(part.correct?goal.explanation:goal.review_feedback));
            std::vector<COLORREF> ink;
            for(const auto word:{std::size_t{0},part.timings.size()/2,part.timings.size()-1}){
                auto prefix=source.substr(0,part.start+part.timings[word].start);std::erase(prefix,L'`');
                check(prefix.size()<displayed.size(),"An inline real word timing maps outside its visible original feedback");
                ink.push_back(color(part.target,static_cast<LONG>(prefix.size())));
            }
            evidence.masks[at].insert(std::move(ink));
        };
        const auto make_observation=[](std::size_t count){return Observation{std::vector<bool>(count),
            std::vector<std::set<std::int64_t>>(count),std::vector<std::set<std::vector<COLORREF>>>(count)};};
        const auto diagnostic=[&](std::string_view stage,const Observation& evidence,const std::vector<std::size_t>& indices){
            const auto value=f.reader->snapshot();
            std::cerr<<"INLINE DIAGNOSTIC case="<<number+1<<" stage="<<stage<<" key="<<value.key
                <<" index="<<value.index<<" offsetMs="<<value.offset<<" heardThrough="<<value.heard_through
                <<" playing="<<value.playing<<" allocated="<<value.allocated<<" preparing="<<value.preparing
                <<" finished="<<value.finished<<" waitingGate="<<value.waiting_gate
                <<" rate="<<value.rate<<" playbackRate="<<value.playback_rate<<" parts="<<value.segments.size()
                <<" error="<<value.error<<'\n';
            if(value.index<value.segments.size()){const auto& part=value.segments[value.index];
                std::cerr<<"  current block="<<part.block<<" feedback="<<part.feedback<<" correct="<<part.correct
                    <<" available="<<part.available<<" durationMs="<<part.duration_ms<<" timings="<<part.timings.size()<<'\n';}
            for(std::size_t at=0;at<indices.size();++at){const auto index=indices[at];
                std::cerr<<"  feedbackPart="<<at+1<<" index="<<index<<" observedPlaying="<<evidence.heard[at]
                    <<" distinctOffsets="<<evidence.offsets[at].size()<<" distinctMasks="<<evidence.masks[at].size();
                if(!evidence.offsets[at].empty())std::cerr<<" firstObservedMs="<<*evidence.offsets[at].begin()
                    <<" lastObservedMs="<<*evidence.offsets[at].rbegin();
                if(index<value.segments.size())std::cerr<<" durationMs="<<value.segments[index].duration_ms;
                std::cerr<<'\n';
            }
        };
        const auto evidence_complete=[&](const Observation& evidence){
            check(std::ranges::all_of(evidence.heard,[](bool heard){return heard;})
                &&std::ranges::any_of(evidence.offsets,[](const auto& values){return values.size()>=3;}),
                "Current inline feedback skipped a real recording or lacked growing playback positions");
            if(!reader_theme::palette().high_contrast)
                check(std::ranges::any_of(evidence.masks,[](const auto& masks){return masks.size()>=2;}),
                    "Actual inline word timing did not produce different native masks within one recording");
        };
        const auto play_pause_restore=[&](const std::vector<std::size_t>& indices,Observation& evidence,const state::Response& answer){
            f.reader->toggle();
            wait([&]{const auto value=f.snapshot();observe(evidence,indices,value);
                if(!value.playing||!value.segments[value.index].feedback)return false;
                const auto& part=value.segments[value.index];return part.timings.size()>=3
                    &&value.offset>150&&value.offset<part.timings.back().start_ms;
            },"real current inline feedback reaches an internal word-aligned pause");
            f.reader->pause();const auto paused=f.snapshot();check(!paused.allocated,"Pausing inline feedback retained its player");
            std::cout<<"PAUSE inline="<<number+1<<" verdict="<<(answer.correct?"correct":"retry")
                <<" index="<<paused.index<<" offsetMs="<<paused.offset<<" heardThrough="<<paused.heard_through<<'\n';
            restore_pause(paused,answer);f.reader->toggle();const auto resumed=f.snapshot();
            std::cout<<"RESUME inline="<<number+1<<" key="<<resumed.key<<" index="<<resumed.index
                <<" offsetMs="<<resumed.offset<<" allocated="<<resumed.allocated<<" playing="<<resumed.playing
                <<" rate="<<resumed.rate<<" playbackRate="<<resumed.playback_rate<<'\n';
            try{
                check(resumed.key==paused.key&&resumed.index==paused.index&&resumed.offset==paused.offset&&resumed.rate==3.,
                    "Resuming restored inline feedback lost its selected recording, player or requested rate");
                // PlaybackRate is observable only after MediaOpened/seek have
                // completed. The exact paused bookmark was checked above;
                // now require real playback from that same point and part.
                auto previous=paused.offset;
                wait([&]{const auto current=f.snapshot();
                    check(current.key==paused.key&&current.index==paused.index,
                        "The restored inline recording advanced before its real resumed playback was observed");
                    if(!current.playing)return false;
                    check(current.playback_rate==3.&&current.offset>=paused.offset&&current.offset>=previous,
                        "Ready inline playback has the wrong rate or moved behind its real restored position");
                    observe(evidence,indices,current);previous=current.offset;
                    return current.offset>paused.offset;
                },"restored inline recording becomes ready at its exact position and real selected rate");
            }catch(...){diagnostic("resuming real playback after exact paused restoration",evidence,indices);throw;}
        };

        fields(std::vector<std::string>(goal.answers.size()),true);privacy();
        replace(test.incorrect);privacy();reopen();fields(test.incorrect,true);privacy();
        check(response()&&!response()->correct&&!state::response_reviewed(*response(),lesson.pages[0].blocks[1]),
            "Reopening an inline draft invented a submitted attempt");
        submit();const auto wrong_indices=verify_feedback(false,goal.review_feedback);const auto wrong=response();
        check(wrong&&!wrong->correct&&state::response_reviewed(*wrong,lesson.pages[0].blocks[1])
            &&edit_text(control(4)).find(visible(goal.explanation))==std::string::npos,
            "Current rejected inline feedback exposed success or lost its reviewed identity");
        auto wrong_evidence=make_observation(wrong_indices.size());play_pause_restore(wrong_indices,wrong_evidence,*wrong);
        try{wait([&]{const auto value=f.snapshot();observe(wrong_evidence,wrong_indices,value);
                return !value.playing&&!value.allocated&&!value.preparing&&value.index==wrong_indices.back();
            },"every actual inline guide part plays before stopping for a retry");
        }catch(...){diagnostic("complete retry guide",wrong_evidence,wrong_indices);throw;}
        const auto stopped=f.snapshot();evidence_complete(wrong_evidence);
        check(!stopped.finished&&stopped.offset+1>=stopped.segments[stopped.index].duration_ms&&response()&&!response()->correct,
            "The actual wrong inline guide skipped its end or granted completion");
        f.reader->seek_seconds(60);check(f.snapshot().segments[f.snapshot().index].feedback&&!f.snapshot().finished,
            "Forward seeking escaped an unresolved inline guide");
        replace(goal.answers);privacy();
        check(response()&&!response()->correct&&!state::response_reviewed(*response(),lesson.pages[0].blocks[1]),
            "Typing the correct inline draft credited an unchecked answer");
        submit();const auto correct_indices=verify_feedback(true,goal.explanation);const auto correct=response();
        check(correct&&correct->correct,"The actual corrected inline fields did not earn completion");fields(goal.answers,false);
        auto correct_evidence=make_observation(correct_indices.size());play_pause_restore(correct_indices,correct_evidence,*correct);
        if(number==0){
            // Start from a genuinely heard partial explanation. A manual
            // preview must not become proof or trigger legacy recovery when
            // its valid zero-prefix certificate is restored.
            f.reader->pause();const auto partial=f.snapshot();
            auto history=narration::read_history(read_lesson_state(f.view));
            const auto partial_entry=std::ranges::find(history.entries,partial.key,&narration::Bookmark::key);
            check(partial_entry!=history.entries.end()&&partial_entry->position.heard_prefix
                &&partial_entry->position.heard_prefix->microseconds>0,
                "The real partial inline recording has no persisted contiguous-prefix certificate");
            double explanation_ms=0;for(const auto index:correct_indices)explanation_ms+=partial.segments[index].duration_ms;
            const int preview_seconds=static_cast<int>(std::ceil(explanation_ms/1000.))+1;
            const auto preview_end=[&]{
                f.reader->seek_seconds(preview_seconds);const auto preview=f.snapshot();
                check(!preview.playing&&preview.index==correct_indices.back()
                    &&preview.offset+1>=preview.segments[preview.index].duration_ms
                    &&preview.heard_through==partial.heard_through,"Manual preview credited or escaped unheard inline feedback");
                return preview;
            };
            const auto preview=preview_end();reopen();const auto restored_preview=f.snapshot();
            check(restored_preview.index==preview.index&&restored_preview.offset==preview.offset
                &&restored_preview.heard_through==preview.heard_through&&!restored_preview.playing,
                "A zero-prefix preview failed exact silent restoration");
            f.reader->toggle();const auto preview_pending=f.snapshot();
            check(preview_pending.index==preview.index&&preview_pending.offset==preview.offset,
                "A pending zero-prefix preview changed its exact requested bookmark");
            wait([&]{const auto value=f.snapshot();return value.allocated||value.waiting_gate;},
                "the zero-credit preview finishes verification without legacy recovery");const auto preview_request=f.snapshot();
            // Native playback can finish the sub-millisecond tail between a
            // truncated seek offset and the rounded terminal checkpoint.
            check(preview_request.index==preview.index&&std::abs(preview_request.offset-preview.offset)<=1,
                "A valid zero-prefix manual preview unexpectedly restarted an explanation");
            f.reader->pause();check(f.snapshot().heard_through==partial.heard_through,
                "An unplayed preview request manufactured complete listening credit");

            for(const bool mismatch:{false,true}){
                if(mismatch)(void)preview_end();
                const auto before=f.snapshot();
                reopen([&](state::StateData& data){
                    auto old=narration::read_history(data);auto entry=std::ranges::find(old.entries,before.key,&narration::Bookmark::key);
                    check(entry!=old.entries.end(),"The closed QA compatibility fixture lost its real bookmark");
                    if(mismatch){check(entry->position.heard_prefix.has_value(),"The changed-recording fixture needs its existing zero certificate");
                        entry->position.heard_prefix->fingerprint=std::string(64,'0');}
                    else entry->position.heard_prefix.reset();
                    narration::write_history(data,old);
                });
                const auto compatible=f.snapshot();
                check(!compatible.playing&&!compatible.allocated&&compatible.index==before.index
                    &&compatible.offset==before.offset&&compatible.heard_through==before.heard_through,
                    "Legacy or mismatched evidence changed a paused bookmark or invented listening");
                f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.allocated&&value.index==correct_indices.front();},
                    "explicit compatibility recovery verifies its first uncredited recording");const auto recovered=f.snapshot();
                check(recovered.index==correct_indices.front()&&recovered.offset>=0
                    &&recovered.offset<recovered.segments[recovered.index].duration_ms
                    &&recovered.heard_through==partial.heard_through,
                    "Explicit legacy recovery failed to select only the first uncredited explanation sentence");
                if(!mismatch){f.reader->pause();check(f.snapshot().heard_through==partial.heard_through,
                    "Requesting legacy recovery granted unheard explanation credit");}
            }
            std::cout<<"PASS inline compatibility: genuine partial proof, restored zero-credit manual preview without recovery, old/mismatched trapped bookmarks remain exact while paused and explicit Play replays only the first uncredited explanation sentence\n";
        }
        try{wait([&]{const auto value=f.snapshot();observe(correct_evidence,correct_indices,value);
                if(!value.playing&&!value.preparing&&value.waiting_gate&&value.index==correct_indices.back()
                    &&value.offset+1>=value.segments[value.index].duration_ms)
                    throw std::runtime_error("Complete inline success playback stopped at the final listening gate instead of reaching following prose");
                return value.playing&&value.segments[value.index].block==2&&value.offset>150;
            },"all actual inline success sentences play before following prose");
        }catch(...){diagnostic("complete success before following prose",correct_evidence,correct_indices);throw;}
        f.reader->pause();evidence_complete(correct_evidence);
        check(f.snapshot().heard_through>static_cast<std::int64_t>(correct_indices.back()),
            "Inline success continued without real listening credit for every feedback part");
        f.reader->toggle();wait([&]{return f.snapshot().finished;},"the inline QA page reaches its real end");
        const auto finished=f.snapshot();restore_pause(finished,*correct);preserve_recordings();
        if(number==0){
            SendMessageW(control(12),BM_CLICK,0,0);f.reader->pause();f.ready();
            fields(std::vector<std::string>(goal.answers.size()),true);privacy();
            replace(goal.answers);submit();const auto fresh=f.snapshot();
            const auto history=narration::read_history(read_lesson_state(f.view));
            const auto entry=std::ranges::find(history.entries,fresh.key,&narration::Bookmark::key);
            check(entry!=history.entries.end()&&!entry->position.finished&&entry->position.milliseconds==0
                &&entry->position.heard_through<=static_cast<std::int64_t>(correct_indices.front())
                &&(!entry->position.heard_prefix||entry->position.heard_prefix->microseconds==0),
                "A fresh checked answer retained an older listening certificate while paused");
            reopen();const auto fresh_restored=f.snapshot();
            check(!fresh_restored.playing&&!fresh_restored.finished&&fresh_restored.offset==0
                &&fresh_restored.heard_through<=static_cast<std::int64_t>(correct_indices.front()),
                "Closing a paused fresh answer resurrected its previous completed reading");
            f.reader->restart();f.reader->pause();reopen();const auto restarted=f.snapshot();
            check(!restarted.playing&&!restarted.finished&&restarted.index==0&&restarted.offset==0&&restarted.heard_through==0,
                "Explicit Restart failed to persist cleared hearing evidence before closing");
            std::cout<<"PASS inline fresh answer and Restart persist cleared listening evidence while paused without rewriting recordings\n";
        }
        check(!IsWindowVisible(f.host)&&read_lesson_state(f.view).preferences.at("inline_audio_qa")==unrelated
            &&state::sha256(state::read_bytes(asset))==asset_hash,
            "Current inline narration changed its source asset, unrelated preferences or the desktop");
        std::cout<<"PASS current inline reader "<<number+1<<" "<<test.file<<" p"<<test.page+1<<" b"<<test.block+1
            <<": current complete retry/success, real Kokoro PCM/timings/playback/masks, unreviewed privacy, released exact silent reopen, retry stop/full-success continuation and unchanged recordings/assets; isolated copied block, no full curricular navigation or human listening claimed\n";
    }
}

void replaced_recording_is_verified_before_released_resume(const std::shared_ptr<windows::NarrationWorker>& backend){
    const auto cache=std::filesystem::path(build::source_root)/"out/qa"/
        (L"reader-replaced-wave-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto lesson=model(true);lesson.id="qa_replaced_recording";lesson.pages.resize(1);
    Fixture f(backend,true,true,true,"es",lesson,true,0,true,true,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },{},cache);
    f.answer("<<");f.reader->toggle();
    wait([&]{const auto value=f.snapshot();if(!value.playing||!value.segments[value.index].feedback)return false;
        const auto& part=value.segments[value.index];return part.timings.size()>3
            &&value.offset>=part.timings[part.timings.size()/2].start_ms&&value.offset<part.timings.back().start_ms;
    },"owned recording has a genuinely heard partial feedback prefix");
    f.reader->pause();const auto paused=f.snapshot();const auto old_part=paused.segments[paused.index];
    check(std::filesystem::equivalent(old_part.recording.parent_path(),cache/"kokoro"),
        "Replacement fixture attempted to modify a recording outside its new owned cache");
    const auto history=narration::read_history(read_lesson_state(f.view));
    const auto old_entry=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
    check(old_entry!=history.entries.end()&&old_entry->position.heard_prefix
        &&old_entry->position.heard_prefix->microseconds>0,"Owned PCM did not earn a real partial certificate");
    const auto old_certificate=*old_entry->position.heard_prefix;
    const auto response_before=state::get_response(read_lesson_state(f.view),lesson,lesson.pages[0].blocks[1]);
    check(response_before&&response_before->correct,"Replacement QA did not use an actually accepted answer");
    f.reader->suspend_preparation(true);
    struct File{HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} writer;
    wait([&]{writer.value=CreateFileW(state::native_io_path(old_part.recording).c_str(),GENERIC_READ|GENERIC_WRITE,
        0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);return writer.value!=INVALID_HANDLE_VALUE;},
        "Pause/suspension releases all reader-owned file leases before cache maintenance");
    CloseHandle(writer.value);writer.value=INVALID_HANDLE_VALUE;
    const auto original=state::read_bytes(old_part.recording);const auto original_hash=state::sha256(original);
    const auto sidecar=old_part.recording.parent_path()/(old_part.recording.stem().wstring()+L"-"
        +wide(state::sha256(old_part.source))+L".timings.json");
    const auto original_sidecar=state::read_bytes(sidecar);
    const auto backup=cache/"before-replacement";check(std::filesystem::create_directory(backup),"Owned recording backup already exists");
    check(std::filesystem::copy_file(old_part.recording,backup/"original.wav")
        &&std::filesystem::copy_file(sidecar,backup/"original.timings.json"),"Cannot preserve the original owned recording and alignment");
    auto replacement=original;check(replacement.size()>46,"The owned PCM fixture is too short");
    // Change one PCM bit, retaining the real format, duration, sample count,
    // non-silence and original word alignment. This is a valid new recording.
    const auto sample=44+2*((replacement.size()-44)/4);replacement[sample]^=1;
    const auto replacement_hash=state::sha256(replacement);check(replacement_hash!=original_hash,"The owned PCM replacement did not change bytes");
    writer.value=CreateFileW(state::native_io_path(old_part.recording).c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);check(writer.value!=INVALID_HANDLE_VALUE,"A released reader still pins its owned recording");
    DWORD written=0;check(WriteFile(writer.value,replacement.data(),static_cast<DWORD>(replacement.size()),&written,nullptr)
        &&written==replacement.size()&&FlushFileBuffers(writer.value),"Cannot replace only the test-owned PCM bytes");
    CloseHandle(writer.value);writer.value=INVALID_HANDLE_VALUE;
    auto metadata=winrt::Windows::Data::Json::JsonObject::Parse(wide(std::string(
        reinterpret_cast<const char*>(original_sidecar.data()),original_sidecar.size())));
    check(utf8(metadata.GetNamedString(L"audioSha256"))==original_hash,"Owned alignment did not describe its original real PCM");
    metadata.Insert(L"audioSha256",winrt::Windows::Data::Json::JsonValue::CreateStringValue(wide(replacement_hash)));
    {std::ofstream output(sidecar,std::ios::binary|std::ios::trunc);output<<winrt::to_string(metadata.Stringify());
        check(static_cast<bool>(output),"Cannot update the owned replacement's matching alignment SHA");}
    check(verify_feedback_recording(old_part)==replacement_hash,"The replacement lost valid real PCM/timings");
    f.reader->suspend_preparation(false);const auto still_paused=f.snapshot();
    check(still_paused.key==paused.key&&still_paused.index==paused.index&&still_paused.offset==paused.offset
        &&!still_paused.allocated&&!still_paused.playing&&still_paused.heard_through==paused.heard_through,
        "Returning from Options changed the released bookmark before explicit Play");
    f.reader->toggle();
    wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].feedback
        &&value.segments[value.index].correct&&value.offset>0;},"same-path replacement is revalidated and really replayed");
    f.reader->pause();const auto replayed=f.snapshot();
    const auto after=narration::read_history(read_lesson_state(f.view));
    const auto new_entry=std::ranges::find(after.entries,replayed.key,&narration::Bookmark::key);
    check(new_entry!=after.entries.end()&&new_entry->position.heard_prefix
        &&new_entry->position.heard_prefix->fingerprint!=old_certificate.fingerprint
        &&new_entry->position.heard_prefix->microseconds>0,
        "The replaced recording inherited old PCM credit instead of earning a new prefix from zero");
    const auto response_after=state::get_response(read_lesson_state(f.view),lesson,lesson.pages[0].blocks[1]);
    check(response_after&&response_after->raw_json==response_before->raw_json&&response_after->correct
        &&state::sha256(state::read_bytes(backup/"original.wav"))==original_hash
        &&state::read_bytes(backup/"original.timings.json")==original_sidecar
        &&state::sha256(state::read_bytes(old_part.recording))==replacement_hash,
        "Same-path recovery changed the accepted response, preserved original or valid replacement");
    f.reader->toggle();wait([&]{return f.snapshot().finished;},"actual replacement feedback completes without fabricated hearing");
    std::cout<<"PASS owned same-path PCM replacement after released Options: original WAV/alignment preserved, valid matching replacement, background byte verification and read-lease handoff, old certificate rejected, real replay from zero completes and accepted response unchanged\n";
}

void reader_preparation_suspension(const std::filesystem::path& python,const std::filesystem::path& build_directory){
    windows::ensure_winrt_apartment();using namespace winrt::Windows::Data::Json;
    check(std::filesystem::is_regular_file(python)&&std::filesystem::is_directory(build_directory),"Controlled suspension inputs are missing");
    const auto base=build_directory/(L"reader-suspend-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(base),"Controlled reader suspension requires a new QA directory");
    const auto log=base/L"transport.jsonl",cache=base/L"audio",ack=base/L"allow-cancel-ack",runtime=base/L"runtime.json";
    JsonObject config;config.Insert(L"java",JsonValue::CreateStringValue(python.native()));JsonArray arguments;
    for(const auto& value:{std::wstring(L"-u"),(std::filesystem::path(build::source_root)/L"native/presentation/windows/lesson_reader_suspend_fixture.py").native(),log.native(),cache.native(),ack.native()})
        arguments.Append(JsonValue::CreateStringValue(value));
    config.Insert(L"arguments",arguments);{std::ofstream output(runtime);output<<winrt::to_string(config.Stringify());check(static_cast<bool>(output),"Cannot save controlled reader transport");}
    auto backend=std::make_shared<windows::NarrationWorker>(runtime);
    std::unique_ptr<Fixture> fixture;std::future<std::string> background;
    struct ShutdownBeforeJoins{std::shared_ptr<windows::NarrationWorker> worker;~ShutdownBeforeJoins(){worker->shutdown();}} cleanup{backend};
    const std::wstring draft=L"Borrador parcial conservado";
    fixture=std::make_unique<Fixture>(backend,true,false,true,"es",std::nullopt,true,0,true,false,[&](HWND view){
        const auto answer=child(view,1102);CHARRANGE all{0,-1};SendMessageW(answer,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        SendMessageW(answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(draft.c_str()));
    });auto& f=*fixture;
    const auto records=[&]{std::vector<JsonObject> result;std::ifstream input(log);std::string line;
        while(std::getline(input,line))try{result.push_back(JsonObject::Parse(wide(line)));}catch(const winrt::hresult_error&){break;}
        return result;};
    const auto count=[&](const wchar_t* command){std::size_t result=0;for(const auto& row:records())if(row.GetNamedString(L"command")==command)++result;return result;};
    wait([&]{const auto value=f.snapshot();return value.planned&&value.segments.size()>1&&value.segments.front().available&&count(L"prepare_blocked")==1;},
        "controlled preparation blocked after one existing WAV");
    f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.offset>=100;},"controlled PCM playback before an exact paused bookmark");
    f.reader->pause();const auto before=f.snapshot();const auto protected_state=read_lesson_state(f.view);
    const auto recording=before.segments.front().recording;const auto recording_hash=state::sha256(state::read_bytes(recording));const auto modified=std::filesystem::last_write_time(recording);
    const auto started=std::chrono::steady_clock::now();f.reader->suspend_preparation(true);
    check(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(250),"Suspending preparation waited for the blocked host or ran its stop/join on the UI thread");
    f.reader->suspend_preparation(true);const auto paused=f.snapshot();
    check(paused.preparation_suspended&&paused.key==before.key&&paused.index==before.index&&paused.offset==before.offset
        &&paused.heard_through==before.heard_through&&paused.segments.size()==before.segments.size()&&!paused.playing&&!paused.allocated
        &&paused.segments.front().recording==recording&&read_lesson_state(f.view)==protected_state,
        "Suspending preparation changed the plan, exact bookmark, draft, history or existing cached part");
    wait([&]{return count(L"cancel")!=0;},"asynchronous cancellation reaches the controlled host");
    background=std::async(std::launch::async,[&]{return backend->curriculum_plan(R"({"id":"controlled-background"})",0,"es","ef_dora","es");});
    check(background.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready,
        "Background preparation bypassed an unacknowledged foreground operation");
    {std::ofstream output(ack);output<<"acknowledge this test-owned cancellation";check(static_cast<bool>(output),"Cannot release controlled cancellation acknowledgement");}
    wait([&]{return background.wait_for(std::chrono::seconds(0))==std::future_status::ready;},"background preparation continues after the original cancellation acknowledgement");
    check(background.get().find("background-after-ack")!=std::string::npos,"The admitted background request lost its independent reply");
    check(count(L"prepare")==2&&read_lesson_state(f.view)==protected_state,"A suspended reader continued foreground preparation or changed protected state");
    f.reader->settings_changed();f.reader->suspend_preparation(false);f.ready();const auto resumed=f.snapshot();
    check(!resumed.preparation_suspended&&!resumed.playing&&!resumed.allocated&&resumed.key==before.key&&resumed.index==before.index
        &&resumed.offset==before.offset&&resumed.heard_through==before.heard_through&&read_lesson_state(f.view)==protected_state
        &&edit_text(child(f.view,1102))==utf8(draft),"Resuming unchanged settings replanned/autoplayed or lost the exact bookmark, history or real draft");
    std::size_t existing_preparations=0;for(const auto& row:records())if(row.GetNamedString(L"command")==L"prepare"&&row.GetNamedString(L"text")==wide(before.segments.front().source))++existing_preparations;
    check(existing_preparations==1&&state::sha256(state::read_bytes(recording))==recording_hash&&std::filesystem::last_write_time(recording)==modified,
        "Resuming preparation regenerated, rewrote or replaced the already cached WAV");
    const auto prepared=count(L"prepare");f.reader->suspend_preparation(true);f.reader->suspend_preparation(false);pump();
    check(count(L"prepare")==prepared&&read_lesson_state(f.view)==protected_state,"Suspending an already cached page restarted synthesis or changed protected state");
    std::cout<<"PASS controlled reader suspension: UI returns before cancellation ACK, background proceeds after ACK, plan/bookmark/draft/history survive, and resume does not autoplay or regenerate existing PCM; no neural-audio claim\n";
}
void extended_reader_playback_rates(const std::shared_ptr<windows::NarrationWorker>& backend){
    Fixture f(backend,false,true,true,"es",compiler_model());const auto initial=f.snapshot();
    std::vector<std::pair<std::filesystem::path,std::filesystem::file_time_type>> recordings;
    for(const auto& part:initial.segments)recordings.emplace_back(part.recording,std::filesystem::last_write_time(part.recording));
    check(flush_lesson_view(f.view),"Cannot persist the initial paused speed-selector state");
    auto preserved=read_lesson_state(f.view);
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
    // Establish the deliberately changed cursor before testing shortcuts: a
    // preference update also persists the editor's current selection.
    check(flush_lesson_view(f.view),"Cannot persist the deliberate editor selection before speed shortcuts");
    preserved=read_lesson_state(f.view);
    KeyboardStateScope keyboard;
    const auto press=[&](UINT key,UINT kind=WM_KEYDOWN,LPARAM flags=1,HWND target=nullptr){MSG message{};message.hwnd=target?target:source;
        message.message=kind;message.wParam=key;message.lParam=flags;return f.reader->dispatch_shortcut(message);};
    check(press(VK_F10,WM_SYSKEYDOWN)&&f.snapshot().rate==2.5&&press(VK_F10,WM_SYSKEYUP)&&f.snapshot().rate==2.5,
        "F10 from the actual code editor failed to increase playback speed or leaked its menu-key release");
    check(press(VK_F10)&&f.snapshot().rate==3.,"F10 did not expose the new3X endpoint");
    for(const auto expected:{.5,1.,1.5,2.,2.5,3.,.5}){
        check(press(VK_F10,WM_KEYDOWN,1LL|(1LL<<30))&&f.snapshot().rate==expected,
            "F10 autorepeat did not follow the speed button's forward cycle through3X to0.5X");unchanged();
    }
    bool first=true;for(const auto expected:{3.,2.5,2.,1.5,1.,.5,3.}){
        check(press(VK_F9,WM_KEYDOWN,first?1:1LL|(1LL<<30))&&f.snapshot().rate==expected,
            "F9 or its autorepeat did not reverse the same speed cycle through0.5X to3X");first=false;unchanged();
    }
    check(press(VK_F10)&&f.snapshot().rate==.5&&press(VK_F9,WM_KEYUP)&&f.snapshot().rate==.5,
        "F10 did not wrap like the speed button or F9 release changed speed");
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
    for(const auto expected:{2.5,3.,.5}){
        update_lesson_preferences(f.view,[expected](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=expected;});f.reader->settings_changed();
        const auto history=narration::read_history(read_lesson_state(f.view));
        f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        check(f.snapshot().rate==expected&&!f.snapshot().playing&&!f.snapshot().allocated&&
            narration::read_history(read_lesson_state(f.view))==history,"Recreating the reader changed its selected cyclic speed or paused bookmark");
        f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.playback_rate==expected;},"the selected fast rate reaches the actual media session");
        const auto before=f.snapshot();const bool faster=expected!=.5;const auto next=expected==3.?.5:3.;
        check(press(faster?VK_F10:VK_F9)&&f.snapshot().playback_rate==next,
            "The focused-editor cyclic speed shortcut did not update live MediaPlaybackSession");
        check(press(faster?VK_F9:VK_F10)&&f.snapshot().playback_rate==expected&&f.snapshot().index==before.index&&f.snapshot().offset>=before.offset,
            "Cycling a live speed through F9/F10 sought or restarted the current recording");
        f.reader->pause();check(!f.snapshot().allocated,"Pausing a fast rate retained the media player");
        state::StateStore saved(f.environment.paths.root);
        check(std::get<double>(*state::find(saved.load(),"lesson_narration_settings","rate"))==expected,"The selected fast playback rate was not persisted in its isolated profile");
    }
    for(const auto& [path,modified]:recordings)check(std::filesystem::is_regular_file(path)&&std::filesystem::last_write_time(path)==modified,
        "Changing only playback speed regenerated or replaced an existing QA recording");
    std::cout<<"PASS real reader playback rates: the speed button and focused-editor F9/F10 share forward/reverse cyclic choices including held keys,3X/0.5X live wrap without menu/capture/seek, persisted/restored rates and unchanged recordings/protected state\n";
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
    const auto foreground_before=GetForegroundWindow();
    const auto foreground_requested=SetForegroundWindow(f.host);
    // Windows may deny a background test's initial activation request. Give
    // its visible QA window time to be activated; the exact focus assertion
    // below still has to hold before any native shortcut or compilation.
    const auto foreground_deadline=GetTickCount64()+30000;
    while(GetForegroundWindow()!=f.host&&GetTickCount64()<foreground_deadline){
        pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
    }
    // Establish child focus only after the real top-level activation.
    if(!foreground_requested&&GetForegroundWindow()==f.host){
        const auto activation_settled=GetTickCount64()+250;
        do{pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}
        while(GetTickCount64()<activation_settled);
    }
    const auto active_before=SetActiveWindow(f.host);SetFocus(source);
    SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));SetFocus(compile);pump();
    if(GetForegroundWindow()!=f.host||GetActiveWindow()!=f.host||GetFocus()!=compile){
        std::cout<<"compile-focus precondition: process="<<GetCurrentProcessId()<<" thread="<<GetCurrentThreadId()
            <<" SetForegroundWindow="<<foreground_requested<<" SetActiveWindow.previous="<<active_before<<'\n';
        const auto describe=[](std::string_view label,HWND window){std::array<wchar_t,256> name{};GetClassNameW(window,name.data(),static_cast<int>(name.size()));
            DWORD process=0;const auto thread=GetWindowThreadProcessId(window,&process);
            std::cout<<"compile-focus "<<label<<": hwnd="<<window<<" class="<<utf8(name.data())<<" process="<<process<<" thread="<<thread
                <<" visible="<<IsWindowVisible(window)<<" enabled="<<IsWindowEnabled(window)<<" iconic="<<IsIconic(window)<<'\n';};
        describe("foreground before request",foreground_before);describe("foreground after pump",GetForegroundWindow());
        describe("active after pump",GetActiveWindow());describe("focus after pump",GetFocus());
        describe("expected host",f.host);describe("expected compile",compile);
    }
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
void current_curricular_colors_reader(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_estado_asignacion_y_copia.json";
    const auto original=state::read_bytes(asset);const auto authored=windows::load_lesson(asset);
    struct Case{std::size_t page,block;bool explanation;const char* anchor;std::size_t letter;const char* ordinary;};
    const std::array<Case,3> cases{{{3,1,false,"int a = 2;",4,"las etapas son"},
        {5,1,false,"produce 15",8,"Una asignación como"},{5,3,true,"++turnos;",2,"aumenta"}}};
    const auto style=[](HWND target,LONG at){
        CHARRANGE selection{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        CHARRANGE one{at,at+1};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&one));
        CHARFORMAT2W value{};value.cbSize=sizeof(value);SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&value));
        SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));return value;
    };
    const auto background=[](HWND target,const CHARFORMAT2W& value){return (value.dwEffects&CFE_AUTOBACKCOLOR)
        ?reader_theme::background(target):value.crBackColor;};
    const auto same_shape=[](const CHARFORMAT2W& one,const CHARFORMAT2W& two){
        constexpr DWORD effects=CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE;
        return (one.dwEffects&effects)==(two.dwEffects&effects)&&one.bUnderlineType==two.bUnderlineType
            &&one.yHeight==two.yHeight&&std::wstring_view(one.szFaceName)==std::wstring_view(two.szFaceName);
    };
    for(std::size_t number=0;number<cases.size();++number){const auto& item=cases[number];const auto& source=authored.pages.at(item.page).blocks.at(item.block);
        curriculum::Lesson lesson;lesson.id="qa_current_curricular_colors_"+std::to_string(number);lesson.title="Prueba de referencias curriculares.";lesson.locale="es";
        curriculum::Page page;page.stable_id="qa_colors_page";curriculum::Block body;body.type=curriculum::BlockType::paragraph;
        body.stable_id="qa_colors_body";body.text=item.explanation?source.explanation:source.text;
        if(!item.explanation)body.emphasis=source.emphasis;
        check(!body.text.empty(),"The exact current curricular color source is missing");page.blocks.push_back(body);lesson.pages.push_back(page);
        // Exact current text/emphasis in a plain QA page isolates reader ink.
        // This does not claim to activate the original choice's feedback.
        const auto raw=wide(body.text);const auto anchor=raw.find(wide(item.anchor));
        check(anchor!=raw.npos,"The current selected technical/numeric reference changed");const auto raw_glyph=anchor+item.letter;
        struct Frame{HWND target=nullptr;LONG glyph=0,ordinary=0;CHARFORMAT2W ink{},plain{};std::string text;int zoom_n=0,zoom_d=0;LRESULT undo=0,modified=0;} frame;
        std::optional<Frame> first_frame;
        const auto before_reader=[&](HWND view){
            frame.target=child(view,1000);frame.text=edit_text(frame.target);const auto displayed=wide(frame.text);
            const auto at=displayed.find(wide(item.anchor)),plain=displayed.find(wide(item.ordinary));
            check(at!=displayed.npos&&plain!=displayed.npos,"The displayed reference/prose mapping is absent");
            frame.glyph=static_cast<LONG>(at+item.letter);frame.ordinary=static_cast<LONG>(plain);
            frame.ink=style(frame.target,frame.glyph);frame.plain=style(frame.target,frame.ordinary);
            const auto references=rich_text_reference_ranges(frame.target);
            check(std::ranges::any_of(references,[&](const auto& range){return range.first<=frame.glyph&&frame.glyph<range.second;})
                &&!(frame.ink.dwEffects&CFE_UNDERLINE)&&!(frame.plain.dwEffects&CFE_UNDERLINE),
                "The actual reference lacks background ownership or acquired an unwanted underline");
            if(!reader_theme::palette(frame.target).high_contrast)check(!(frame.ink.dwEffects&CFE_AUTOBACKCOLOR)
                &&frame.ink.crBackColor==reader_theme::reference_background(frame.target),"A reference lacks its exact migrated Android background");
            SendMessageW(frame.target,EM_GETZOOM,reinterpret_cast<WPARAM>(&frame.zoom_n),reinterpret_cast<LPARAM>(&frame.zoom_d));
            check(frame.zoom_d>0&&frame.zoom_n*100==frame.zoom_d*125,"The persisted QA text scale was not applied");
            frame.undo=SendMessageW(frame.target,EM_CANUNDO,0,0);frame.modified=SendMessageW(frame.target,EM_GETMODIFY,0,0);
            if(first_frame)check(frame.text==first_frame->text&&frame.ink.crTextColor==first_frame->ink.crTextColor
                &&background(frame.target,frame.ink)==background(frame.target,first_frame->ink)
                &&same_shape(frame.ink,first_frame->ink)&&same_shape(frame.plain,first_frame->plain)
                &&frame.zoom_n==first_frame->zoom_n&&frame.zoom_d==first_frame->zoom_d,
                "Reopening changed the original source's semantic ink, emphasis, font or scale before Reader attachment");
            else first_frame=frame;
            CHARRANGE selected{0,1};SendMessageW(frame.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
            update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=.5;});
        };
        const auto initialize=[](state::StateData& data){data.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{125};
            data.preferences["curricular_colors_opaque"]["text"]=std::string(" \t unchanged\r\n");
            data.preferences["curricular_colors_opaque"]["flag"]=true;
            data.preferences["curricular_colors_opaque"]["items"]=std::vector<std::string>{"one","two"};};
        Fixture f(backend,false,true,false,"es",lesson,false,0,true,true,before_reader,initialize);
        const auto opaque=read_lesson_state(f.view).preferences.at("curricular_colors_opaque");
        const auto invariant=[&]{
            const auto current=style(frame.target,frame.glyph),plain=style(frame.target,frame.ordinary);
            int numerator=0,denominator=0;SendMessageW(frame.target,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            CHARRANGE selected{};SendMessageW(frame.target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
            check(same_shape(current,frame.ink)&&same_shape(plain,frame.plain)
                &&frame.text==edit_text(frame.target)&&numerator==frame.zoom_n&&denominator==frame.zoom_d&&selected.cpMin==0&&selected.cpMax==1,
                "Real reader shading changed styles, face, zoom, text or selection");
            check(background(frame.target,current)==background(frame.target,frame.ink)
                &&(current.dwEffects&CFE_AUTOBACKCOLOR)==(frame.ink.dwEffects&CFE_AUTOBACKCOLOR),
                "Real reader shading changed the reference's exact original background");
            check(SendMessageW(frame.target,EM_CANUNDO,0,0)==frame.undo&&SendMessageW(frame.target,EM_GETMODIFY,0,0)==frame.modified,
                "Reader formatting changed the original document's Undo availability or modified flag");
            check(read_lesson_state(f.view).preferences.at("curricular_colors_opaque")==opaque,"The actual reader changed unrelated typed QA state");
        };
        const auto reopen=[&](std::optional<std::size_t> preview={}){
            const auto current_key=f.snapshot().key;
            f.reader.reset();check(flush_lesson_view(f.view),"Cannot preserve the actual color-reader bookmark");DestroyWindow(f.view);f.view=nullptr;
            if(preview){state::StateStore store(f.environment.paths.root);auto data=store.load();auto history=narration::read_history(data);
                const auto entry=std::ranges::find_if(history.entries,[&](const auto& value){return value.key==current_key;});
                check(entry!=history.entries.end(),"A preview needs this actual planned page's bounded slot");
                entry->position=narration::seek(entry->position,static_cast<std::int64_t>(*preview),0);
                narration::write_history(data,history);store.save(data);}
            f.view=create_lesson_view(f.host,f.environment,lesson);check(f.view!=nullptr,"Cannot reopen the exact QA curricular source");MoveWindow(f.view,0,0,980,720,TRUE);
            LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
            set_lesson_view_hooks(f.view,std::move(hooks));before_reader(f.view);
            f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});
            wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()&&value.segments[value.index].available;},"real selected curricular fragment preparation");
        };
        const auto plan=f.snapshot();const auto part=std::ranges::find_if(plan.segments,[&](const auto& value){return value.target==frame.target&&value.start<=raw_glyph&&raw_glyph<value.end;});
        check(part!=plan.segments.end(),"The selected raw UTF-16 glyph is not owned by an actual narrated part");
        const auto index=static_cast<std::size_t>(part-plan.segments.begin());f.reader->seek_seconds(0);reopen(index);
        const auto preview=f.snapshot();check(preview.index==index&&preview.offset==0&&!preview.playing&&!preview.allocated,"A positional-only preview autoplayed or moved");
        const auto& selected=preview.segments[index];const auto word=std::ranges::find_if(selected.timings,[&](const auto& value){return selected.start+value.start<=raw_glyph&&raw_glyph<selected.start+value.end;});
        check(word!=selected.timings.end()&&word->start_ms>=0&&word->end_ms>word->start_ms,"The actual selected word lacks a positive-duration real timing");
        const auto wave_hash=verify_feedback_recording(selected);const auto wave_stamp=std::filesystem::last_write_time(selected.recording);
        // At 0 ms a word whose real timing starts at zero is already visually
        // read. Preview the preceding part in that case; no heard evidence is
        // inferred, and real playback must enter the selected word's WAV.
        if(word->start_ms==0){check(index>0,"A zero-timed reference needs an earlier real part for a pending preview");reopen(index-1);}
        const auto base_background=background(frame.target,frame.ink);
        const auto pending_colors=reader_theme::unread_colors(reader_theme::background(frame.target),base_background);
        const auto pending=pending_colors.second;
        check(style(frame.target,frame.glyph).crTextColor==pending
            &&style(frame.target,frame.ordinary).crTextColor==pending_colors.first,
            "Pending technical/numeric references and prose do not use neutral gray ink");invariant();
        f.reader->toggle();wait([&]{const auto value=f.snapshot();check(value.index<=index,"Actual color-reader playback passed the selected part");
            return value.playing&&value.index==index&&value.playback_rate==.5&&value.offset>=word->start_ms+std::min(40.,(word->end_ms-word->start_ms)/3.)
                &&style(frame.target,frame.glyph).crTextColor==frame.ink.crTextColor;},"actual ef_dora word playback restores the reference's original ink");
        f.reader->pause();const auto paused=f.snapshot();invariant();
        check(paused.index==index&&!paused.playing&&!paused.allocated&&paused.offset>0
            &&style(frame.target,frame.glyph).crTextColor==frame.ink.crTextColor,"The actual read word lost its restored ink or exact paused recording");
        reopen();const auto restored=f.snapshot();invariant();
        check(restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset&&restored.heard_through==paused.heard_through
            &&!restored.playing&&!restored.allocated&&restored.segments[index].recording==selected.recording,"Reopening changed the exact real colored-word pause or recording");
        check(style(frame.target,frame.glyph).crTextColor==frame.ink.crTextColor,"A real read word lost its restored ink after a silent reopen");
        f.reader->toggle();await_resumed_player([&]{return f.snapshot();},restored,"Actual curricular color resume");
        wait([&]{const auto value=f.snapshot();return value.playing&&value.index==index&&value.offset>restored.offset+30;},"actual same-WAV colored-word continuation");
        f.reader->pause();invariant();check(style(frame.target,frame.glyph).crTextColor==frame.ink.crTextColor,
            "Pausing actual same-WAV continuation lost the reference's restored ink");
        f.reader->seek_seconds(-60);invariant();
        check(style(frame.target,frame.glyph).crTextColor==pending,"Rewinding did not restore pending gray for the reference");
        f.reader.reset();invariant();check(style(frame.target,frame.glyph).crTextColor==frame.ink.crTextColor,
            "Stopping the real reader did not restore the reference's original ink");
        check(state::sha256(state::read_bytes(selected.recording))==wave_hash&&std::filesystem::last_write_time(selected.recording)==wave_stamp
            &&state::read_bytes(asset)==original,"Color-reader playback/reopen rewrote actual audio or the curricular asset");
        std::cout<<"PASS current curricular source "<<number+1<<": actual ef_dora/.5 PCM and word timing, pending gray/read original ink, unchanged neutral backgrounds without underline, pause/reopen/resume/rewind/stop, exact QA text/emphasis/zoom/selection; explanation is a plain QA copy, not activated original feedback\n";
    }
}
struct LegacyAutoReadingProbe {
    HWND view=nullptr;
    std::string context;
    explicit LegacyAutoReadingProbe(HWND target,std::string previous_context="auto"):view(target),context(std::move(previous_context)) {
        check(SetWindowSubclass(view,proc,88,reinterpret_cast<DWORD_PTR>(this))!=FALSE,"Cannot install the isolated historical reading descriptor probe");
    }
    ~LegacyAutoReadingProbe(){detach();}
    void detach(){if(view){RemoveWindowSubclass(view,proc,88);view=nullptr;}}
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
        const auto result=DefSubclassProc(window,message,wp,lp);
        // QA emulates the previously published descriptor, not its audio or
        // listening history. The actual source, controls and answers stay real.
        if(message==lesson_reading_regions_message&&lp&&wp<4)
            for(auto& region:*reinterpret_cast<std::vector<LessonReadingRegion>*>(lp))
                if(region.reading_context=="cpp"&&region.math_mode=="auto")
                    region.reading_context=reinterpret_cast<LegacyAutoReadingProbe*>(data)->context;
        return result;
    }
};

void current_cpp_numeric_context_reader(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_terminos_prioridad_y_division_segura.json";
    const auto asset_bytes=state::read_bytes(asset);const auto lesson=windows::load_lesson(asset);
    const auto prediction=identity_position(lesson,"block_0006_0001"),choice=identity_position(lesson,"block_0006_0002");
    const auto owner=identity_position(lesson,"block_0004_0003");
    const auto& goal=lesson.pages[prediction.page].blocks[prediction.block];
    const auto initial_bookmark_page=lesson.pages.size()-1;
    check(prediction.page==6&&prediction.block==1&&choice.page==6&&choice.block==2
        &&goal.type==curriculum::BlockType::output_prediction&&goal.answers==std::vector<std::string>{"Resultado: 13"}
        &&goal.explanation.find("11/2")!=goal.explanation.npos&&lesson.compiler_puzzles_required
        &&initial_bookmark_page>=prediction.page+2,
        "The current U11L12 numeric narration source or its prerequisites changed");
    Fixture f(backend,false,true,true,"es",lesson,false,initial_bookmark_page,true,true,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=.5;});
    },[&](state::StateData& data){
        // The sole prerequisite receipt isolates this reader regression. It is
        // deliberately synthetic QA provenance, never a claimed C++ execution.
        const auto& block=lesson.pages[owner.page].blocks[owner.block];
        check(block.output_check&&block.output_check->cases.size()==1,"Numeric reader QA needs one aggregate compiler prerequisite");
        windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
        const auto& item=block.output_check->cases.front();
        curriculum::FreshExecution execution{request.revision,true,true,0,item.standard_input,item.outputs.front()};
        check(windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"Synthetic numeric-reader QA prerequisite; no execution"),
            "Cannot isolate the current numeric reader prerequisite");
        data.preferences["cpp_numeric_reader_qa"]["unrelated"]=std::string(" \t conservar\r\n");
    });
    check(lesson_page_state(f.view).index==initial_bookmark_page&&!IsWindowVisible(f.host),
        "The numeric reader QA opened another page or exposed the desktop");
    const auto unrelated=read_lesson_state(f.view).preferences.at("cpp_numeric_reader_qa");
    // These two slots are earned by playback on later authored pages, then
    // navigation goes backward to the focal puzzle. Forward navigation from
    // an unanswered earlier page must keep its real ordinary-puzzle gate.
    std::vector<narration::Bookmark> other_positions;
    for(const std::size_t page:{initial_bookmark_page,initial_bookmark_page-1}) {
        if(lesson_page_state(f.view).index!=page)f.page(page);
        f.reader->toggle();
        wait([&]{const auto value=f.snapshot();return value.playing&&value.offset>100;},"real other-page numeric-reader bookmark");
        f.reader->pause();const auto paused=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));
        const auto found=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
        check(found!=history.entries.end()&&found->position.milliseconds==paused.offset&&!found->position.finished,
            "Actual other-page playback did not persist its exact pause");other_positions.push_back(*found);
    }
    f.page(prediction.page);f.answer(goal.answers.front(),prediction.block);
    const auto response=state::get_response(read_lesson_state(f.view),lesson,goal);
    check(response&&response->correct&&response->answer==goal.answers.front(),
        "The numeric narration feedback was not activated by its real answer control");
    const auto regions=lesson_reading_regions(f.view);const auto planned=f.snapshot();
    std::vector<windows::NarrationSegment> all_expected;
    std::optional<std::size_t> selected;std::size_t selected_slash=0;
    std::size_t cpp_numeric_regions=0;
    for(const auto& region:regions) {
        const bool typed=region.block_index==prediction.block||region.block_index==choice.block;
        if(typed)check(region.reading_context=="cpp"&&region.math_mode=="auto",
            "A current C++ puzzle region lost its typed arithmetic context");
        if(region.block_index==0||region.block_index==std::numeric_limits<std::size_t>::max())
            check(region.reading_context=="auto","A lesson heading was forced into C++ arithmetic");
        auto expected=backend->plan(region.source_text,"es",{},region.reading_context,region.math_mode);
        all_expected.insert(all_expected.end(),expected.begin(),expected.end());
        std::vector<const LessonReaderSegmentSnapshot*> actual;
        for(const auto& part:planned.segments)if(part.region==region.key)actual.push_back(&part);
        check(actual.size()==expected.size(),"The actual numeric reader changed the worker's segment boundaries");
        for(std::size_t index=0;index<expected.size();++index) {
            const auto& want=expected[index];const auto& have=*actual[index];
            check(have.source==want.text&&have.spoken==want.spoken&&have.start==want.start&&have.end==want.end,
                "The actual reader ignored its region's contextual worker plan");
            check(want.end>=want.start&&wide(want.text).size()==want.end-want.start,
                "Contextual planning changed the curricular UTF-16 source range");
            if(!typed||have.source.find("11/2")==have.source.npos)continue;
            ++cpp_numeric_regions;
            check(have.spoken.find("medios")==have.spoken.npos&&have.spoken.find("barra")!=have.spoken.npos,
                "The C++ integer division was still narrated as a mathematical fraction");
            check(want.displayed_ranges.size()==want.spoken_ranges.size()&&!want.displayed_ranges.empty(),
                "The contextual C++ plan lost its paired spoken/source ownership");
            const auto source=wide(want.text),spoken=wide(want.spoken);const auto slash=source.find(L'/');
            check(slash!=source.npos,"A numeric source lost its literal slash");bool owned=false;
            for(std::size_t word=0;word<want.displayed_ranges.size();++word) {
                const auto& visible=want.displayed_ranges[word];const auto& audible=want.spoken_ranges[word];
                check(visible.start<visible.end&&visible.end<=source.size()&&audible.start<audible.end&&audible.end<=spoken.size(),
                    "The contextual numeric plan has an invalid UTF-16 ownership range");
                if(visible.start<=slash&&visible.end>slash) {
                    const auto words=spoken.substr(audible.start,audible.end-audible.start);
                    owned=owned||words.find(L"barra")!=words.npos;
                }
            }
            check(owned,"The spoken division operator does not belong to its displayed slash");
            check(std::ranges::any_of(have.timings,[&](const auto& timing){return timing.start<=slash&&timing.end>slash;}),
                "The real numeric recording lost the displayed slash's word timing");
            if(region.feedback&&!selected) {
                selected=static_cast<std::size_t>(actual[index]-planned.segments.data());selected_slash=slash;
            }
        }
    }
    check(cpp_numeric_regions>=3&&selected.has_value(),
        "The current feedback, question and choice did not all expose their real 11/2 source");
    const auto page_identity=lesson_page_state(f.view).narration_page_identity;
    check(backend->identity(page_identity,"es","ef_dora",all_expected)==planned.key,
        "The reader page key does not reflect its contextual prepared segments");
    const auto& focal=planned.segments[*selected];const auto hash=verify_feedback_recording(focal);
    const auto stamp=std::filesystem::last_write_time(focal.recording);
    const auto slash_word=std::ranges::find_if(focal.timings,[&](const auto& timing){return timing.start<=selected_slash&&timing.end>selected_slash;});
    check(slash_word!=focal.timings.end()&&planned.index<=*selected,"The selected current feedback has no reachable timed division word");
    f.reader->toggle();
    wait([&]{const auto value=f.snapshot();check(value.index<=*selected,"Playback skipped the selected contextual division recording");
        return value.playing&&value.index==*selected&&value.offset>=slash_word->start_ms
            &&value.offset<focal.duration_ms-20;},"the current C++ division word really plays");
    f.reader->pause();const auto paused=f.snapshot();
    check(!paused.playing&&!paused.allocated&&paused.index==*selected&&paused.offset>0&&!paused.finished,
        "Pausing the actual contextual division retained a player or finished the page");
    const auto history=narration::read_history(read_lesson_state(f.view));
    const auto preserve_other_slots=[&]{const auto current=narration::read_history(read_lesson_state(f.view));
        check(current.entries.size()==3,"Contextual reading lost the bounded three-page history");
        for(const auto& old:other_positions){const auto found=std::ranges::find(current.entries,old.key,&narration::Bookmark::key);
            check(found!=current.entries.end()&&*found==old,"Contextual playback rewrote another genuinely heard page position");}};
    preserve_other_slots();
    const auto reopen=[&]{
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the current numeric lesson");DestroyWindow(f.view);f.view=nullptr;
        f.view=create_lesson_view(f.host,f.environment,lesson);check(f.view!=nullptr,"Cannot reopen the actual numeric lesson");MoveWindow(f.view,0,0,980,720,TRUE);
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};set_lesson_view_hooks(f.view,std::move(hooks));
        f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
    };
    reopen();const auto restored=f.snapshot();
    check(restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset
        &&restored.heard_through==paused.heard_through&&!restored.playing&&!restored.allocated
        &&restored.segments[restored.index].recording==focal.recording&&narration::read_history(read_lesson_state(f.view))==history,
        "Reopening changed the genuine contextual division pause, certificate or recording");
    f.reader->toggle();await_resumed_player([&]{return f.snapshot();},restored,"same contextual division recording resumes");
    wait([&]{const auto value=f.snapshot();return value.playing&&value.index==restored.index&&value.offset>restored.offset+20;},
        "the resumed contextual division advances from its real saved offset");f.reader->pause();
    f.reader->seek_seconds(-60);const auto rewound=f.snapshot();
    check(!rewound.playing&&!rewound.allocated&&!rewound.finished
        &&(rewound.index<paused.index||(rewound.index==paused.index&&rewound.offset<paused.offset)),
        "Rewinding the contextual recording invented playback or did not restore an earlier position");preserve_other_slots();
    check(state::sha256(state::read_bytes(focal.recording))==hash&&std::filesystem::last_write_time(focal.recording)==stamp,
        "Reopening/resuming/rewinding regenerated the compatible contextual recording");

    // The same numeric bytes keep distinct semantic recordings. A genuine
    // mathematical puzzle below additionally checks the native auto-context
    // path rather than treating every route's slash as a C++ operator.
    const auto math_plan=backend->plan("11/2","es");const auto cpp_plan=backend->plan("11/2","es",{},"cpp");
    const auto chain=backend->plan("24/4/2","es",{},"cpp");
    const auto explicit_cpp_fraction=backend->plan("11/2","es",{},"cpp","inline");
    const auto explicit_auto_fraction=backend->plan("11/2","es",{},"auto","inline");
    check(math_plan.size()==1&&cpp_plan.size()==1&&!chain.empty()
        &&math_plan.front().spoken.find("medios")!=std::string::npos&&cpp_plan.front().spoken.find("barra")!=std::string::npos
        &&math_plan.front().prepared!=cpp_plan.front().prepared,"C++ division and genuine fractions share the wrong prepared identity");
    check(explicit_cpp_fraction.size()==1&&explicit_auto_fraction.size()==1
        &&explicit_cpp_fraction.front().prepared==explicit_auto_fraction.front().prepared
        &&explicit_cpp_fraction.front().spoken==explicit_auto_fraction.front().spoken
        &&explicit_cpp_fraction.front().spoken.find("medios")!=std::string::npos,
        "An explicit mathematical mode did not override the automatic C++ context");
    std::string chain_spoken;for(const auto& part:chain)chain_spoken+=part.spoken;
    const auto first_division=chain_spoken.find("barra");check(first_division!=chain_spoken.npos
        &&chain_spoken.find("barra",first_division+1)!=chain_spoken.npos,"Repeated C++ division lost one of its two operators");
    bool supplementary_owner=false;
    for(const auto& part:backend->plan("🧭 11/2=5","es",{},"cpp")) {
        const auto visible=wide(part.text),spoken=wide(part.spoken);const auto slash=visible.find(L'/');
        if(slash==visible.npos)continue;
        check(part.displayed_ranges.size()==part.spoken_ranges.size(),"The supplementary source lost paired ownership ranges");
        check(part.start+slash==5,"The source slash after a supplementary character did not retain its UTF-16 offset");
        for(std::size_t index=0;index<part.displayed_ranges.size();++index) {
            const auto& display=part.displayed_ranges[index];const auto& sound=part.spoken_ranges[index];
            if(display.start<=slash&&display.end>slash&&sound.end<=spoken.size())
                supplementary_owner=supplementary_owner||spoken.substr(sound.start,sound.end-sound.start).find(L"barra")!=spoken.npos;
        }
    }
    check(supplementary_owner,"A supplementary character displaced C++ operator ownership from its UTF-16 source");
    const auto math_audio=backend->prepare(math_plan.front(),"es","ef_dora",f.environment.paths.cache/L"kokoro",0);
    const auto cpp_audio=backend->prepare(cpp_plan.front(),"es","ef_dora",f.environment.paths.cache/L"kokoro",0);
    const auto cpp_hash=state::sha256(state::read_bytes(cpp_audio.wav));const auto cpp_stamp=std::filesystem::last_write_time(cpp_audio.wav);
    const auto cached=backend->prepare(cpp_plan.front(),"es","ef_dora",f.environment.paths.cache/L"kokoro",0);
    check(math_audio.key!=cpp_audio.key&&math_audio.wav!=cpp_audio.wav&&cached.cache_hit&&cached.key==cpp_audio.key
        &&cached.wav==cpp_audio.wav&&state::sha256(state::read_bytes(cached.wav))==cpp_hash&&std::filesystem::last_write_time(cached.wav)==cpp_stamp,
        "Contextual arithmetic aliased a fraction cache key or rewrote a reusable recording");

    const auto open_legacy_reader=[&]{
        f.reader.reset();auto probe=std::make_unique<LegacyAutoReadingProbe>(f.view);
        f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();return probe;
    };
    // A genuine paused AUTO recording whose prepared sentence did not change
    // keeps its exact position/file when a different passage changes the key.
    {
        auto probe=open_legacy_reader();f.reader->restart();
        wait([&]{const auto value=f.snapshot();return value.playing&&value.index==0&&value.offset>100;},
            "a genuinely played unchanged sentence in the historical AUTO script");
        f.reader->pause();const auto old=f.snapshot();const auto old_part=old.segments[old.index];
        const auto old_hash=verify_feedback_recording(old_part);const auto old_stamp=std::filesystem::last_write_time(old_part.recording);
        probe->detach();reopen();const auto migrated=f.snapshot();
        check(migrated.key!=old.key&&!migrated.playing&&!migrated.allocated&&!migrated.finished
            &&migrated.segments[migrated.index].region==old_part.region&&migrated.segments[migrated.index].source==old_part.source
            &&migrated.offset==old.offset&&migrated.heard_through<=old.heard_through
            &&migrated.segments[migrated.index].recording==old_part.recording,
            "AUTO-to-C++ migration lost an unchanged real pause or invented listening credit");
        check(state::sha256(state::read_bytes(old_part.recording))==old_hash&&std::filesystem::last_write_time(old_part.recording)==old_stamp,
            "Migrating another sentence rewrote the unchanged AUTO recording");preserve_other_slots();
    }
    // A genuinely heard fraction cannot donate its offset/certificate to the
    // corrected C++ pronunciation. Retry is performed through the real puzzle.
    {
        auto probe=open_legacy_reader();SendMessageW(child(f.view,1012+static_cast<int>(prediction.block)*100),BM_CLICK,0,0);
        f.reader->pause();f.ready();f.answer(goal.answers.front(),prediction.block);const auto old_plan=f.snapshot();
        const auto fraction=std::ranges::find_if(old_plan.segments,[&](const auto& part){return part.block==prediction.block
            &&part.feedback&&part.source.find("11/2")!=part.source.npos&&part.spoken.find("medios")!=part.spoken.npos;});
        check(fraction!=old_plan.segments.end(),"The historical AUTO descriptor did not reconstruct its actual fraction recording");
        const auto at=static_cast<std::size_t>(fraction-old_plan.segments.begin());
        const auto old_hash=verify_feedback_recording(*fraction);const auto old_stamp=std::filesystem::last_write_time(fraction->recording);
        f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.index==at&&value.offset>150
            &&value.offset<fraction->duration_ms-20;},"the historical AUTO fraction has a real partial playback offset");
        f.reader->pause();const auto old=f.snapshot();
        probe->detach();reopen();const auto migrated=f.snapshot();
        check(migrated.key!=old.key&&!migrated.playing&&!migrated.allocated&&!migrated.finished&&migrated.offset==0
            &&migrated.segments[migrated.index].region==fraction->region&&migrated.segments[migrated.index].source==fraction->source
            &&migrated.segments[migrated.index].spoken.find("barra")!=std::string::npos
            &&migrated.segments[migrated.index].recording!=fraction->recording
            &&migrated.heard_through<=static_cast<std::int64_t>(migrated.index),
            "A changed AUTO fraction donated its old offset, PCM or listening credit to corrected C++ speech");
        const auto migrated_history=narration::read_history(read_lesson_state(f.view));const auto entry=std::ranges::find(migrated_history.entries,migrated.key,&narration::Bookmark::key);
        check(entry!=migrated_history.entries.end()&&(!entry->position.heard_prefix||entry->position.heard_prefix->microseconds==0),
            "Migrating changed speech retained a certificate for the historical fraction");
        check(state::sha256(state::read_bytes(fraction->recording))==old_hash&&std::filesystem::last_write_time(fraction->recording)==old_stamp,
            "Corrected speech replaced the independently preserved historical fraction WAV");preserve_other_slots();
    }
    // Finish the old page by answering its remaining puzzles with their own
    // controls and really playing every segment. The changed earlier feedback
    // must still be replayed after migration, even if the final sentence matches.
    {
        auto probe=open_legacy_reader();const auto& multiple=lesson.pages[choice.page].blocks[choice.block];
        check(multiple.correct_index.has_value(),"The current numeric choice lost its authored answer");
        SendMessageW(child(f.view,1020+static_cast<int>(choice.block)*100+static_cast<int>(*multiple.correct_index)*2),BM_CLICK,0,0);
        SendMessageW(child(f.view,1003+static_cast<int>(choice.block)*100),BM_CLICK,0,0);f.reader->pause();f.ready();
        const auto last=identity_position(lesson,"block_0003_0003");
        check(last.page==prediction.page&&lesson.pages[last.page].blocks[last.block].answers.size()==1,
            "The current numeric page lost its final diagnostic prediction");
        f.answer(lesson.pages[last.page].blocks[last.block].answers.front(),last.block);
        update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
        f.reader->settings_changed();f.reader->restart();wait([&]{return f.snapshot().finished;},
            "the complete historical AUTO page is really played before its migration",120000);
        const auto old=f.snapshot();check(!old.allocated&&old.finished,"The complete AUTO page retained a decoder");
        const auto changed=std::ranges::find_if(old.segments,[&](const auto& part){return part.block==prediction.block&&part.feedback
            &&part.source.find("11/2")!=part.source.npos;});check(changed!=old.segments.end(),"The finished AUTO script lost the changed feedback");
        probe->detach();reopen();const auto migrated=f.snapshot();
        check(migrated.key!=old.key&&!migrated.finished&&!migrated.playing&&!migrated.allocated&&migrated.offset==0
            &&migrated.segments[migrated.index].region==changed->region&&migrated.segments[migrated.index].source==changed->source
            &&migrated.heard_through<=static_cast<std::int64_t>(migrated.index),
            "A finished AUTO page falsely credited changed earlier C++ feedback because its last sentence matched");
        preserve_other_slots();
    }

    const auto math_asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/math_physics/math_algebra_elemental/sistemas_lineales/lesson_dos_incognitas_y_soluciones_simultaneas.json";
    const auto math_bytes=state::read_bytes(math_asset);const auto math_lesson=windows::load_lesson(math_asset);
    auto math_goal=math_lesson.pages.at(2).blocks.at(3);
    check(math_goal.type==curriculum::BlockType::math_puzzle&&math_goal.raw_json.find("11/2")!=std::string::npos,
        "The genuine mathematical fraction control source changed");
    auto guard=model();guard.id="qa_current_cpp_numeric_math_guard";guard.title="Control matemático.";guard.pages.resize(1);
    guard.pages.front().blocks.front().text="Control matemático.";math_goal.address={0,1,{},{}};guard.pages.front().blocks.push_back(math_goal);
    Fixture mathematical(backend,false,true,true,"es",guard,false,0,true);
    bool genuine_fraction=false;
    for(const auto& region:lesson_reading_regions(mathematical.view))if(region.block_index==1) {
        check(region.reading_context=="auto","A genuine mathematics control was forced into C++ context");
        if(region.source_text.find("11/2")==region.source_text.npos)continue;
        for(const auto& part:backend->plan(region.source_text,"es",{},region.reading_context,region.math_mode))
            genuine_fraction=genuine_fraction||part.spoken.find("medios")!=std::string::npos;
    }
    check(genuine_fraction,"The mounted authentic mathematics control lost fraction pronunciation in auto mode");
    check(read_lesson_state(f.view).preferences.at("cpp_numeric_reader_qa")==unrelated&&state::read_bytes(asset)==asset_bytes
        &&state::read_bytes(math_asset)==math_bytes,"Contextual numeric QA changed assets or unrelated preferences");
    std::cout<<"PASS current U11L12 p7 numeric reader: typed feedback/question/choice plans, slash UTF-16 ownership and real PCM, distinct fraction/division cache keys, repeated division, released exact pause/reopen/resume/rewind and three genuinely heard page slots; real historical AUTO pauses/finished page migrate unchanged speech exactly and changed feedback without invented credit; authentic mathematical control stays auto. One compiler prerequisite is explicitly synthetic QA, no C++ execution, full curricular traversal or human listening claimed.\n";
}

void current_cpp_parenthesis_context_reader(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_signos_unarios_parentesis_y_progreso_recursivo.json";
    const auto asset_bytes=state::read_bytes(asset);auto lesson=windows::load_lesson(asset);
    const auto prediction=identity_position(lesson,"block_0003_0001"),following=identity_position(lesson,"block_0003_0003");
    const auto owner=identity_position(lesson,"block_0004_0004");
    check(prediction.page==4&&prediction.block==1&&following.page==prediction.page&&following.block==2
        &&lesson.pages[prediction.page].blocks[prediction.block].explanation.find("consume )")!=std::string::npos
        &&lesson.pages[prediction.page].blocks[prediction.block].answers==std::vector<std::string>{"Resultado: 21"}
        &&lesson.compiler_puzzles_required&&lesson.pages.size()>=prediction.page+3,
        "The current U11L13 parenthesis source or its real predictions changed");
    const auto original_goal=lesson.pages[prediction.page].blocks[prediction.block].raw_json;
    // Explicitly QA-only: this added visible puzzle makes the preceding C++
    // policy's numeric reading distinguishable from AUTO in the full page key.
    // Every real curricular field stays intact. This extra activity remains
    // unanswered; no source program is executed and no page is completed.
    curriculum::Block numeric_control;numeric_control.type=curriculum::BlockType::output_prediction;
    numeric_control.id=numeric_control.stable_id="qa_cpp_parenthesis_numeric_control";
    numeric_control.language="cpp";numeric_control.question="El código calcula 11/2.";numeric_control.answers={"5"};
    numeric_control.address={prediction.page,99,"cpp_parenthesis_reader_qa","qa_cpp_parenthesis_reader_page"};
    const auto numeric_index=lesson.pages[prediction.page].blocks.size();
    lesson.pages[prediction.page].blocks.push_back(numeric_control);
    const auto& goal=lesson.pages[prediction.page].blocks[prediction.block];
    const auto& second=lesson.pages[following.page].blocks[following.block];
    const auto initial_page=lesson.pages.size()-1;
    Fixture f(backend,false,true,true,"es",lesson,false,initial_page,true,true,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=2.;});
    },[&](state::StateData& data){
        // The compiler receipt isolates the prerequisite. It is synthetic QA,
        // separate from the genuine WAV playback asserted below.
        const auto& compiler=lesson.pages[owner.page].blocks[owner.block];
        check(compiler.output_check&&compiler.output_check->cases.size()==1,"Parenthesis reader QA needs its aggregate compiler prerequisite");
        windows::CppRunRequest request;request.revision=1;request.source_utf8=compiler.initial_code;
        const auto& expected=compiler.output_check->cases.front();
        curriculum::FreshExecution execution{1,true,true,0,expected.standard_input,expected.outputs.front()};
        check(windows::record_compiler_puzzle_case(data,lesson,compiler,request,execution,"Synthetic parenthesis-reader QA prerequisite; no execution"),
            "Cannot isolate the current parenthesis reader prerequisite");
        data.preferences["cpp_parenthesis_reader_qa"]["unrelated"]=std::string(" \t conservar\r\n");
    });
    const auto unrelated=read_lesson_state(f.view).preferences.at("cpp_parenthesis_reader_qa");
    std::vector<narration::Bookmark> other_positions;
    for(const std::size_t page:{initial_page,initial_page-1}){
        if(lesson_page_state(f.view).index!=page)f.page(page);
        f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.offset>100;},
            "actual other-page playback before the C++ parenthesis policy migration");
        f.reader->pause();const auto paused=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));
        const auto entry=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
        check(entry!=history.entries.end()&&entry->position.milliseconds==paused.offset&&!entry->position.finished
            &&entry->position.heard_prefix&&entry->position.heard_prefix->microseconds>0,
            "Another page's parenthesis-migration slot was not earned by actual playback");
        other_positions.push_back(*entry);
    }
    f.page(prediction.page);
    // Both actual predictions use their real answer/check controls. The
    // additional QA numeric question must remain pending after them.
    f.answer(goal.answers.front(),prediction.block);f.answer(second.answers.front(),following.block);
    const auto responses=read_lesson_state(f.view);
    const auto first_response=state::get_response(responses,lesson,goal),second_response=state::get_response(responses,lesson,second);
    check(first_response&&first_response->correct&&second_response&&second_response->correct
        &&!state::get_response(responses,lesson,numeric_control),"The actual predictions or unanswered QA control were not kept distinct");
    const auto preserve_other_slots=[&]{const auto history=narration::read_history(read_lesson_state(f.view));
        check(history.entries.size()==3,"Parenthesis policy migration changed the three-slot limit");
        for(const auto& old:other_positions){const auto found=std::ranges::find(history.entries,old.key,&narration::Bookmark::key);
            check(found!=history.entries.end()&&*found==old,"Parenthesis policy migration rewrote another genuinely heard page slot");}};
    const auto reopen=[&]{
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the current parenthesis lesson");DestroyWindow(f.view);f.view=nullptr;
        f.view=create_lesson_view(f.host,f.environment,lesson);check(f.view!=nullptr,"Cannot reopen the actual parenthesis lesson");MoveWindow(f.view,0,0,980,720,TRUE);
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};set_lesson_view_hooks(f.view,std::move(hooks));
        f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
    };
    const auto old_reader=[&]{f.reader.reset();auto probe=std::make_unique<LegacyAutoReadingProbe>(f.view,"cpp_previous_parentheses");
        f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();return probe;};
    struct RecordingEvidence{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};
    const auto recordings=[&](const LessonReaderSnapshot& snapshot,std::string_view phase){std::vector<RecordingEvidence> result;
        check(snapshot.index<snapshot.segments.size()&&!snapshot.playing&&!snapshot.allocated&&snapshot.offset>0,
            "Recording evidence requires a released genuine pause: "+std::string(phase));
        const auto& played=snapshot.segments[snapshot.index];
        try{check(played.available,"The actually played part is not available");(void)verify_feedback_recording(played);}
        catch(const std::exception& error){throw std::runtime_error(std::string(phase)+" played block="+std::to_string(played.block)
            +" index="+std::to_string(snapshot.index)+" WAV="+utf8(played.recording.native())+": "+error.what());}
        for(const auto& part:snapshot.segments)if(part.available&&!part.recording.empty()){
            // The code-announcement UI label has real PCM but no lexical
            // alignment. Preserve every available file; require the full
            // feedback contract for both actual explanations and, above, for
            // whichever sentence was genuinely played before this pause.
            if(part.feedback&&(part.block==prediction.block||part.block==following.block)){
                try{(void)verify_feedback_recording(part);}
                catch(const std::exception& error){throw std::runtime_error(std::string(phase)+" feedback block="+std::to_string(part.block)
                    +" duration="+std::to_string(part.duration_ms)+" timings="+std::to_string(part.timings.size())
                    +" source="+part.source+" WAV="+utf8(part.recording.native())+": "+error.what());}
            }
            const auto bytes=state::read_bytes(part.recording);
            check(!bytes.empty(),std::string(phase)+" available block="+std::to_string(part.block)
                +" has an empty WAV="+utf8(part.recording.native()));
            result.push_back({part,state::sha256(bytes),std::filesystem::last_write_time(part.recording)});
        }
        return result;};
    const auto preserve_recordings=[&](const std::vector<RecordingEvidence>& old,const LessonReaderSnapshot& current){
        std::size_t compatible=0;
        for(const auto& item:old){check(state::sha256(state::read_bytes(item.part.recording))==item.hash
                &&std::filesystem::last_write_time(item.part.recording)==item.stamp,"Parenthesis migration changed a historical WAV or timestamp");
            const auto match=std::ranges::find_if(current.segments,[&](const auto& part){return part.region==item.part.region
                &&part.source==item.part.source&&part.spoken==item.part.spoken&&part.start==item.part.start&&part.end==item.part.end;});
            if(match!=current.segments.end()){++compatible;check(match->recording==item.part.recording,
                "An unchanged parenthesis-policy sentence did not reuse its compatible WAV");}}
        check(compatible>=2,"The parenthesis migration did not exercise compatible recordings");};
    const auto assert_old_policy=[&](const LessonReaderSnapshot& snapshot){
        std::vector<windows::NarrationSegment> old,automatic;bool typed_numeric=false;
        for(const auto& region:lesson_reading_regions(f.view)){
            const auto previous=backend->plan(region.source_text,"es",{},region.reading_context,region.math_mode);
            old.insert(old.end(),previous.begin(),previous.end());
            const auto auto_context=region.reading_context=="cpp_previous_parentheses"?"auto":region.reading_context;
            const auto legacy=backend->plan(region.source_text,"es",{},auto_context,region.math_mode);
            automatic.insert(automatic.end(),legacy.begin(),legacy.end());
            if(region.block_index==numeric_index){check(region.reading_context=="cpp_previous_parentheses","The QA numeric control did not use the actual typed C++ descriptor");
                typed_numeric=std::ranges::any_of(previous,[](const auto& part){return part.spoken.find("barra")!=std::string::npos&&part.spoken.find("medios")==std::string::npos;});}
        }
        const auto page=lesson_page_state(f.view).narration_page_identity;
        check(typed_numeric&&backend->identity(page,"es","ef_dora",old)==snapshot.key
            &&backend->identity(page,"es","ef_dora",automatic)!=snapshot.key,
            "The historical C++ page key aliases AUTO and would not require the parenthesis-specific migration fallback");
    };
    const auto assert_same_slot=[&](const narration::History& before,std::string_view old_key,const LessonReaderSnapshot& current){
        const auto after=narration::read_history(read_lesson_state(f.view));
        const auto old=std::ranges::find(before.entries,old_key,&narration::Bookmark::key);
        const auto migrated=std::ranges::find(after.entries,current.key,&narration::Bookmark::key);
        check(before.entries.size()==3&&after.entries.size()==3&&old!=before.entries.end()&&migrated!=after.entries.end()
            &&old-before.entries.begin()==migrated-after.entries.begin()
            &&old->page==migrated->page&&std::ranges::none_of(after.entries,[&](const auto& item){return item.key==old_key;}),
            "The previous C++ bookmark did not migrate in place in its original page slot");preserve_other_slots();
    };
    {
        auto probe=old_reader();assert_old_policy(f.snapshot());f.reader->restart();
        wait([&]{const auto value=f.snapshot();return value.playing&&value.index==0&&value.offset>100;},
            "an unchanged U11L13 sentence really plays under the preceding C++ policy");
        f.reader->pause();const auto old=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));const auto evidence=recordings(old,"unchanged opening pause");
        probe->detach();reopen();const auto current=f.snapshot();
        check(current.key!=old.key&&!current.playing&&!current.allocated&&!current.finished&&current.offset==old.offset
            &&current.segments[current.index].region==old.segments[old.index].region
            &&current.segments[current.index].source==old.segments[old.index].source
            &&current.segments[current.index].recording==old.segments[old.index].recording,
            "The preceding C++ policy lost the exact compatible pause during parenthesis migration");
        const auto migrated_history=narration::read_history(read_lesson_state(f.view));
        const auto before=std::ranges::find(history.entries,old.key,&narration::Bookmark::key),after=std::ranges::find(migrated_history.entries,current.key,&narration::Bookmark::key);
        check(before!=history.entries.end()&&after!=migrated_history.entries.end()&&before->position.heard_prefix==after->position.heard_prefix,
            "An unchanged C++ recording lost its genuinely heard prefix certificate");
        assert_same_slot(history,old.key,current);preserve_recordings(evidence,current);
    }
    {
        auto probe=old_reader();assert_old_policy(f.snapshot());f.reader->restart();const auto planned=f.snapshot();
        const auto selected=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==prediction.block&&part.feedback
            &&part.source.find("consume )")!=std::string::npos;});
        check(selected!=planned.segments.end()&&selected->spoken.find(')')!=std::string::npos
            &&selected->spoken.find("cierra paréntesis")==std::string::npos,"The historical policy did not reconstruct the actual unlexicalized parenthesis feedback");
        const auto at=static_cast<std::size_t>(selected-planned.segments.begin());
        wait([&]{const auto value=f.snapshot();check(value.index<=at,"Historical playback skipped the selected U11L13 feedback");
            return value.playing&&value.index==at&&value.offset>150&&value.offset<selected->duration_ms-20;},
            "the historical U11L13 parenthesis feedback has a genuine partial playback offset");
        f.reader->pause();const auto old=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));const auto evidence=recordings(old,"changed parenthesis feedback pause");
        const auto old_entry=std::ranges::find(history.entries,old.key,&narration::Bookmark::key);
        check(old_entry!=history.entries.end()&&old_entry->position.heard_prefix&&old_entry->position.heard_prefix->microseconds>0,
            "The changed historical sentence had no genuine playback certificate");
        probe->detach();reopen();const auto current=f.snapshot();
        check(current.key!=old.key&&!current.playing&&!current.allocated&&!current.finished&&current.offset==0
            &&current.segments[current.index].region==selected->region&&current.segments[current.index].source==selected->source
            &&current.segments[current.index].spoken.find("cierra paréntesis")!=std::string::npos
            &&current.segments[current.index].recording!=selected->recording&&current.heard_through<=static_cast<std::int64_t>(current.index),
            "The changed parenthesis feedback inherited an old offset, recording or listening credit");
        const auto migrated_history=narration::read_history(read_lesson_state(f.view));
        const auto entry=std::ranges::find(migrated_history.entries,current.key,&narration::Bookmark::key);
        check(entry!=migrated_history.entries.end()&&(!entry->position.heard_prefix||entry->position.heard_prefix->microseconds==0),
            "The corrected parenthesis sentence inherited the old policy's heard-prefix certificate");
        assert_same_slot(history,old.key,current);preserve_recordings(evidence,current);
        reopen();const auto restored=f.snapshot();
        check(restored.key==current.key&&restored.index==current.index&&restored.offset==0&&restored.heard_through==current.heard_through
            &&!restored.playing&&!restored.allocated&&narration::read_history(read_lesson_state(f.view))==migrated_history,
            "Silent reopen changed the migrated parenthesis position, listening credit or slot");
        preserve_other_slots();preserve_recordings(evidence,restored);
    }
    {
        // A compatible pause AFTER changed successful feedback is different
        // from the unchanged opening pause above. Even an unfinished page
        // must revisit the new explanation before it can resume beyond it.
        auto probe=old_reader();assert_old_policy(f.snapshot());f.reader->restart();const auto planned=f.snapshot();
        const auto changed=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==prediction.block&&part.feedback
            &&part.source.find("consume )")!=std::string::npos;});
        const auto later=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==following.block&&part.feedback
            &&part.source.find("La unidad produce -9.")!=std::string::npos;});
        check(changed!=planned.segments.end()&&later!=planned.segments.end()&&changed<later,
            "The actual U11L13 page no longer has compatible feedback after the changed parenthesis sentence");
        const auto at=static_cast<std::size_t>(later-planned.segments.begin());
        wait([&]{const auto value=f.snapshot();check(value.index<=at,"Historical playback skipped the later compatible explanation");
            return value.playing&&value.index==at&&value.offset>150&&value.offset<later->duration_ms-20;},
            "the historical reader really reaches compatible feedback after the changed explanation");
        f.reader->pause();const auto old=f.snapshot();const auto history=narration::read_history(read_lesson_state(f.view));const auto evidence=recordings(old,"later compatible feedback pause");
        const auto old_entry=std::ranges::find(history.entries,old.key,&narration::Bookmark::key);
        check(!old.finished&&old.heard_through>static_cast<std::int64_t>(changed-planned.segments.begin())
            &&old_entry!=history.entries.end()&&old_entry->position.heard_prefix&&old_entry->position.heard_prefix->microseconds>0,
            "The later pause did not actually play through the old successful explanation");
        probe->detach();reopen();const auto current=f.snapshot();
        check(current.key!=old.key&&!current.playing&&!current.allocated&&!current.finished&&current.offset==0
            &&current.segments[current.index].region==changed->region&&current.segments[current.index].source==changed->source
            &&current.segments[current.index].spoken.find("cierra paréntesis")!=std::string::npos
            &&current.heard_through<=static_cast<std::int64_t>(current.index),
            "An unfinished later compatible pause skipped the corrected successful explanation");
        const auto migrated_history=narration::read_history(read_lesson_state(f.view));
        const auto entry=std::ranges::find(migrated_history.entries,current.key,&narration::Bookmark::key);
        check(entry!=migrated_history.entries.end()&&(!entry->position.heard_prefix||entry->position.heard_prefix->microseconds==0),
            "Revisiting changed earlier feedback retained the later recording's heard-prefix certificate");
        assert_same_slot(history,old.key,current);preserve_recordings(evidence,current);
        f.reader->toggle();await_resumed_player([&]{return f.snapshot();},current,"resuming migrated earlier parenthesis feedback");
        wait([&]{const auto value=f.snapshot();check(value.index==current.index,"Resuming after migration skipped the new parenthesis recording");
            return value.playing&&value.offset>80;},"the corrected explanation is actually played before later compatible prose");
        f.reader->pause();preserve_other_slots();
    }
    const auto saved=read_lesson_state(f.view);
    check(saved.preferences.at("cpp_parenthesis_reader_qa")==unrelated&&!state::get_response(saved,lesson,numeric_control)
        &&goal.raw_json==original_goal&&state::read_bytes(asset)==asset_bytes,
        "Parenthesis migration changed unrelated state, completed the QA control or modified the curricular source");
    std::cout<<"PASS actual U11L13 parenthesis feedback: preceding C++ policy with corrected numeric reading has its own non-AUTO key; genuine opening/changed/later-compatible pauses migrate in the same three heard page slots, retain compatible WAVs, clear new-speech credit and revisit corrected earlier feedback, with silent reopen. One visible numeric puzzle is QA-only and remains unanswered; one compiler prerequisite is synthetic, no compilation/full traversal/human listening claimed.\n";
}

void missing_recording_recovery(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto owned_cache=std::filesystem::path(build::source_root)/"out/qa"/
        (L"reader-missing-wave-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto lesson=model();lesson.pages.resize(1);Fixture f(backend,false,true,true,"es",std::move(lesson),true,0,false,true,{}, {},owned_cache);
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
    const auto original_background=(word_style.dwEffects&CFE_AUTOBACKCOLOR)
        ?reader_theme::background(old_part.target):word_style.crBackColor;
    const auto surface=reader_theme::background(old_part.target);auto semantic_surface=surface;
    const auto references=rich_text_reference_ranges(old_part.target);
    if(!references.empty()){
        CHARRANGE first_reference{static_cast<LONG>(references.front().first),static_cast<LONG>(references.front().first+1)};
        SendMessageW(old_part.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&first_reference));CHARFORMAT2W semantic{};semantic.cbSize=sizeof(semantic);
        SendMessageW(old_part.target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&semantic));
        SendMessageW(old_part.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old_selection));
        semantic_surface=(semantic.dwEffects&CFE_AUTOBACKCOLOR)?surface:semantic.crBackColor;
    }
    const auto grays=reader_theme::unread_colors(surface,semantic_surface);
    const auto pending_ink=original_background==semantic_surface?grays.second:original_background==surface?grays.first:reader_theme::unread_color(original_background);
    const auto word_is_pending=[&]{
        CHARRANGE selection{};SendMessageW(old_part.target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        SendMessageW(old_part.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&timed_word));CHARFORMAT2W current{};current.cbSize=sizeof(current);
        SendMessageW(old_part.target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&current));
        SendMessageW(old_part.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        check((current.dwEffects&CFE_AUTOBACKCOLOR)==(word_style.dwEffects&CFE_AUTOBACKCOLOR)
            &&((current.dwEffects&CFE_AUTOBACKCOLOR)||current.crBackColor==word_style.crBackColor),
            "Missing-audio recovery changed the timed word's original background");
        return current.crTextColor==pending_ink;
    };
    check(!word_is_pending(),"The real word was not read before deleting its private recording");
    check(old_part.recording==missing&&std::filesystem::remove(missing),"The controlled seek regression did not remove only the same private WAV");
    f.reader->toggle();wait([&]{const auto value=f.snapshot();return !value.segments[value.index].available;},
        "background byte validation observes the deleted recording before new timing delivery");
    f.reader->pause();const auto recovering=f.snapshot();
    check(!recovering.segments[recovering.index].available&&!recovering.allocated&&recovering.offset==before_seek.offset,
        "The recovery-seek fixture already delivered replacement timings or changed the paused offset");
    check(!word_is_pending(),"Preparing an unchanged missing recording lost the existing word shading");
    f.reader->seek_seconds(-1);const auto rewound=f.snapshot();
    check(rewound.index==before_seek.index&&rewound.offset==before_seek.offset-1000&&rewound.offset>0
            &&!rewound.allocated&&!rewound.playing&&rewound.heard_through==before_seek.heard_through,
        "Rewinding without an audio file changed segment identity, listening credit or the exact requested offset");
    check(word_is_pending(),
        "A seek reused an older visible word boundary while the current recording had no timing metadata");
    f.ready();check(f.snapshot().offset==rewound.offset&&!f.snapshot().playing,
        "Replacement timing delivery moved the new rewind position or overrode its pause");
    f.reader->toggle();await_resumed_player([&]{return f.snapshot();},rewound,"Playback after an unavailable-audio seek");
    f.reader->pause();check(digest(valid)==valid_digest&&std::filesystem::file_size(valid)==valid_size,
        "The controlled unavailable-audio seek changed a different valid recording");
    std::cout<<"PASS unavailable recording seek: unchanged offset preserves shading, real rewind restores pending words without invented timing or listening credit, and exact paused resume\n";
}
}

int main(int argc,char** argv){const bool stderr_only=argc==2&&std::string_view(argv[1])=="--stderr-objective";
    const bool suspend_only=argc==4&&std::string_view(argv[1])=="--suspend";
    const bool dependencies_only=argc==2&&std::string_view(argv[1])=="--dependent-puzzles";
    const bool integrated_only=argc==2&&std::string_view(argv[1])=="--integrated-u1";
    const bool integrated_order_only=argc==2&&std::string_view(argv[1])=="--integrated-order";
    const bool integrated_matching_only=argc==2&&std::string_view(argv[1])=="--integrated-matching";
    const bool u11_domain_revision_only=argc==2&&std::string_view(argv[1])=="--u11-domain-revision";
    const bool u11_domain_race_only=argc==2&&std::string_view(argv[1])=="--u11-domain-race";
    const bool u11_consult_revision_only=argc==2&&std::string_view(argv[1])=="--u11-consult-revision";
    const bool u11_assignment_revision_only=argc==5&&std::string_view(argv[1])=="--u11-assignment-revision";
    if(u11_assignment_revision_only&&std::string_view(argv[4])!="0"&&std::string_view(argv[4])!="1")return 2;
    const bool u11_literal_spacing_only=argc==5&&std::string_view(argv[1])=="--u11-literal-spacing-revision";
    if(u11_literal_spacing_only&&std::string_view(argv[4])!="0"&&std::string_view(argv[4])!="1")return 2;
    const bool u12_file_format_only=argc==3&&std::string_view(argv[1])=="--u12-file-format-reader";
    const bool u12_file_roundtrip_only=argc==2&&std::string_view(argv[1])=="--u12-file-roundtrip-reader";
    const bool u12_append_hypothesis_only=argc==5&&std::string_view(argv[1])=="--u12-append-hypothesis";
    if(u12_append_hypothesis_only&&std::string_view(argv[4])!="0"&&std::string_view(argv[4])!="1")return 2;
    if(u12_file_format_only&&(std::string_view(argv[2]).size()!=1||argv[2][0]<'0'||argv[2][0]>'2'))return 2;
    const bool current_inline_only=(argc==2||argc==3)&&std::string_view(argv[1])=="--current-inline-feedback";
    std::optional<std::size_t> current_inline_case;
    if(current_inline_only&&argc==3){const std::string_view value=argv[2];
        if(value.size()!=1||value.front()<'1'||value.front()>'9')return 2;
        current_inline_case=static_cast<std::size_t>(value.front()-'1');}
    const bool rates_only=argc==2&&std::string_view(argv[1])=="--rates";
    const bool compiler_focus_only=argc==2&&std::string_view(argv[1])=="--compiler-focus";
    const bool required_compiler_only=argc==2&&std::string_view(argv[1])=="--required-compiler";
    const bool seek_keys_only=argc==2&&std::string_view(argv[1])=="--seek-keys";
    const bool missing_recording_only=argc==2&&std::string_view(argv[1])=="--missing-recording";
    const bool changed_recording_only=argc==2&&std::string_view(argv[1])=="--changed-recording";
    const bool curricular_colors_only=argc==2&&std::string_view(argv[1])=="--curricular-colors";
    const bool cpp_numeric_context_only=argc==2&&std::string_view(argv[1])=="--cpp-numeric-context";
    const bool cpp_parenthesis_context_only=argc==2&&std::string_view(argv[1])=="--cpp-parenthesis-context";
    const bool state_compatibility_only=argc==2&&std::string_view(argv[1])=="--state-compatibility";
    if(argc>1&&!suspend_only&&!stderr_only&&!dependencies_only&&!integrated_only&&!integrated_order_only&&!integrated_matching_only&&!u11_domain_revision_only&&!u11_domain_race_only&&!u11_consult_revision_only&&!u11_assignment_revision_only&&!u11_literal_spacing_only&&!u12_file_format_only&&!u12_file_roundtrip_only&&!u12_append_hypothesis_only&&!current_inline_only&&!rates_only&&!compiler_focus_only&&!required_compiler_only&&!seek_keys_only&&!missing_recording_only&&!changed_recording_only&&!curricular_colors_only&&!cpp_numeric_context_only&&!cpp_parenthesis_context_only&&!state_compatibility_only)return 2;
    std::cout<<std::unitbuf;const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    try{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        if(suspend_only){reader_preparation_suspension(std::filesystem::path(argv[2]),std::filesystem::path(argv[3]));return 0;}
        auto backend=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
        if(required_compiler_only){current_required_compiler_reader(backend);return 0;}
        if(curricular_colors_only){current_curricular_colors_reader(backend);return 0;}
        if(cpp_numeric_context_only){current_cpp_numeric_context_reader(backend);return 0;}
        if(cpp_parenthesis_context_only){current_cpp_parenthesis_context_reader(backend);return 0;}
        if(missing_recording_only){missing_recording_recovery(backend);return 0;}
        if(changed_recording_only){replaced_recording_is_verified_before_released_resume(backend);return 0;}
        if(seek_keys_only){reader_arrow_aliases_preserve_control_keys(backend);return 0;}
        if(compiler_focus_only){checked_compilation_keeps_keyboard_reader_control(backend);return 0;}
        if(rates_only){extended_reader_playback_rates(backend);return 0;}
        if(integrated_only){integrated_u1_reader_flow(backend);return 0;}
        if(integrated_order_only){integrated_order_reader_flow(backend);return 0;}
        if(integrated_matching_only){integrated_matching_reader_bookmarks(backend);return 0;}
        if(u11_domain_revision_only){u11_domain_label_reader_revision(backend);return 0;}
        if(u11_domain_race_only){u11_domain_label_reader_revision(backend,true);return 0;}
        if(u11_consult_revision_only){u11_consult_choice_reader_revision(backend);return 0;}
        if(u11_assignment_revision_only){u11_assignment_editorial_reader(backend,std::filesystem::path(argv[2]),std::filesystem::path(argv[3]),static_cast<unsigned>(argv[4][0]-'0'));return 0;}
        if(u11_literal_spacing_only){u11_literal_spacing_reader(backend,std::filesystem::path(argv[2]),std::filesystem::path(argv[3]),static_cast<unsigned>(argv[4][0]-'0'));return 0;}
        if(u12_file_format_only){u12_file_format_reader(backend,static_cast<std::size_t>(argv[2][0]-'0'));return 0;}
        if(u12_file_roundtrip_only){current_u12_file_roundtrip_reader(backend);return 0;}
        if(u12_append_hypothesis_only){u12_append_hypothesis_reader(backend,std::filesystem::path(argv[2]),std::filesystem::path(argv[3]),static_cast<unsigned>(argv[4][0]-'0'));return 0;}
        if(current_inline_only){current_inline_feedback_reader(backend,current_inline_case);return 0;}
        if(dependencies_only){dependent_puzzle_reader_bookmarks(backend);independent_feedback_does_not_complete_a_pending_compiler(backend);return 0;}
        // These independent flows have their own selectors. State migration
        // checks can run without the separate real foreground-focus scenario;
        // the default suite keeps its existing coverage.
        if(!state_compatibility_only){
            runtime_stderr_reader_objective(backend);if(stderr_only)return 0;
            extended_reader_playback_rates(backend);
            checked_compilation_keeps_keyboard_reader_control(backend);
            reader_arrow_aliases_preserve_control_keys(backend);
            dependent_puzzle_reader_bookmarks(backend);
            independent_feedback_does_not_complete_a_pending_compiler(backend);
            integrated_u1_reader_flow(backend);
            integrated_order_reader_flow(backend);
        }
        {
            Fixture f(backend,true);KeyboardStateScope keyboard;ReaderCommandProbe probe(f.reader->bar());
            const auto edit=child(f.view,1102),play=GetDlgItem(f.reader->bar(),6504);SetFocus(edit);
            const auto press=[&](UINT key,HWND target,LPARAM flags=1,UINT kind=WM_KEYDOWN){
                MSG message{};message.hwnd=target;message.message=kind;message.wParam=key;message.lParam=flags;
                return f.reader->dispatch_shortcut(message);
            };
            check(press(VK_F1,edit)&&probe.commands==std::vector<int>{6504}&&GetFocus()==edit,
                "F1 from the real answer editor did not activate the existing play button without stealing focus");
            wait([&]{return f.snapshot().allocated;},"F1 allocates a real player after background file verification");
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
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},paused,"Playback after fullscreen");f.reader->pause();
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
                wait([&]{return f.snapshot().allocated;},"captured Play allocates after verifying the actual recording");
                wait([&]{return f.snapshot().playing;},"actual floating-icon playback");
                const auto icon_playing=f.snapshot();click_icon(play_button);const auto icon_paused=f.snapshot();
                check(!icon_paused.playing&&!icon_paused.allocated&&icon_paused.key==icon_playing.key&&icon_paused.index==icon_playing.index&&icon_paused.offset>=icon_playing.offset,
                    "A captured mouse click on Pause failed to release the real player at its current position");
                SendMessageW(play_button,BM_CLICK,0,0);
                wait([&]{return f.snapshot().allocated;},"accessible Play verifies and opens the actual recording");
                check(GetCapture()==nullptr,"Accessible Play retained mouse capture");
                SendMessageW(play_button,BM_CLICK,0,0);
                check(!f.snapshot().playing&&!f.snapshot().allocated&&GetCapture()==nullptr,
                    "Accessible Pause failed to release the actual player and capture");
                std::cout<<"PASS compact reader: immediate real-mouse icon dragging, promoted-touch 90ms/rejected-wheel distinction and unchanged playback/reflow\n";
                std::cout<<"PASS floating reader icons: captured mouse and accessible speed/play/pause activation, exact single dispatch and released media/capture\n";
            }
            f.reader->toggle();
            wait([&]{const auto s=f.snapshot();if(!s.playing||s.index!=1)return false;
                const auto& part=s.segments[s.index];return part.timings.size()>1
                    &&s.offset>=part.timings.front().end_ms&&s.offset<part.timings.back().start_ms;
            },"paragraph playback between its actual first and last word timings");
            const auto moving=f.snapshot();const auto& segment=moving.segments[moving.index];
            const auto read_offset=static_cast<LONG>(segment.start+segment.timings.front().start);
            const auto pending_offset=static_cast<LONG>(segment.start+segment.timings.back().start);
            const auto pending=pending_color(segment.target,pending_offset);
            check(color(segment.target,read_offset)!=pending&&color(segment.target,pending_offset)==pending,
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
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},hidden,"Explicit playback after showing the controls");
            std::cout<<"PASS header-only visibility, released exact pause, persisted hidden controls and explicit real-audio resume\n";
            f.reader->pause();const auto paused=f.snapshot();check(!paused.allocated&&!paused.playing&&paused.offset>=moving.offset,"Pause did not preserve position and release MediaPlayer");
            pump();check(f.snapshot().offset==paused.offset,"A released paused position kept advancing");
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},paused,"Explicit resume requests the exact saved offset");
            wait([&]{const auto s=f.snapshot();return s.playing&&s.offset>=paused.offset;},"real resumed media position");
            f.reader->pause();const auto before=f.snapshot();f.reader->seek_seconds(-3);const auto back=f.snapshot();
            check(back.index<before.index||(back.index==before.index&&back.offset<before.offset),"Rewind did not move actual reading position");
            check(color(segment.target,pending_offset)==pending,"Rewind left later words read");
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
            wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&!value.preparing&&value.segments[value.index].feedback;},
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
            const auto displayed=wide(edit_text(current.target));const auto technical_word=displayed.find(L"std::cout"),later_word=displayed.find(L"previsto");
            check(technical_word!=std::wstring::npos&&later_word!=std::wstring::npos
                    &&color(current.target,static_cast<LONG>(technical_word))!=pending_color(current.target,static_cast<LONG>(technical_word))
                    &&color(current.target,static_cast<LONG>(later_word))==pending_color(current.target,static_cast<LONG>(later_word)),
                "Compiler feedback did not keep read and pending technical words inside the same narration segment");
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},paused,"Resuming compiler feedback");
            wait([&]{const auto value=f.snapshot();return value.playing&&value.segments[value.index].block==2;},
                "fully heard correct compiler feedback continues into the following paragraph");
            wait([&]{return f.snapshot().finished;},"checked compiler narration reaches the real page end");
            const auto previous_prose=child(f.view,1000);const auto previous_ink=color(previous_prose,0);
            check(previous_ink!=pending_color(previous_prose,0),"The completed paragraph remained pending");
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
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&!value.preparing&&value.segments[value.index].block==modification.block&&value.segments[value.index].feedback;},
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
            f.reader->seek_seconds(60);wait([&]{const auto value=f.snapshot();return !value.playing&&!value.allocated&&!value.preparing;},"skipped feedback gate");const auto skipped=f.snapshot();
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
            const auto read=static_cast<LONG>(part.start+part.timings.front().start);
            const auto unread=static_cast<LONG>(part.start+part.timings.back().start);
            const auto pending=pending_color(part.target,unread);
            check(color(part.target,read)!=pending&&color(part.target,unread)==pending,
                "Real "+language+" playback did not show read and pending words inside one technical paragraph");
            check(!paused.allocated&&!paused.playing,"A language-specific pause kept MediaPlayer alive");
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},paused,"Language-specific exact resume");
            f.reader->pause();f.reader->seek_seconds(-60);
            check(color(part.target,read)==pending,"Rewinding did not restore pending technical words in "+language);
            std::cout<<"PASS real "<<language<<" Kokoro metadata, technical word shading, released pause, exact resume and rewind\n";
        }
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL native LessonReader: "<<failure.what()<<'\n';return 1;}
}
