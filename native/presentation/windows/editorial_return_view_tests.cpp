#include "core/editorial_return_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,std::string_view reason){if(!value)throw std::runtime_error(std::string(reason));}
void pump(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}}
void wait(const std::function<bool()>& done,std::string_view reason){
    const auto until=GetTickCount64()+90000;
    do{pump();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<until);
    throw std::runtime_error(std::string(reason));
}
COLORREF text_color(HWND target,LONG offset){
    CHARRANGE saved{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&saved));
    CHARRANGE point{offset,offset+1};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&point));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&saved));return format.crTextColor;
}
struct Recording{std::string hash;std::filesystem::file_time_type modified;};
struct View {
    std::shared_ptr<windows::NarrationWorker> worker;
    WindowsEnvironment environment;
    HWND host=nullptr,view=nullptr;
    std::unique_ptr<LessonReader> reader;
    std::map<std::filesystem::path,Recording> recordings;
    explicit View(std::shared_ptr<windows::NarrationWorker> backend,std::size_t index):worker(std::move(backend)){
        const auto token=L"editorial-return-"+std::to_wstring(index)+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        environment=resolve_windows_environment({ProfileId::Codex,token,false});
        environment.paths=make_profile_paths(std::filesystem::path(build::source_root)/"out/qa/editorial-return-view",ProfileId::Codex,token);
        environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";
        check(!std::filesystem::exists(environment.paths.root),"Return reader QA profile already exists");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root);auto data=store.load();
        auto& settings=data.preferences["lesson_narration_settings"];
        settings["engine"]=std::string("KOKORO");settings["language"]=std::string("es");settings["kokoro_voice:es"]=std::string("ef_dora");
        settings["rate"]=3.;settings["kokoro_advance_generation"]=false;
        data.preferences["editorial_return_opaque"]["keep"]=std::string(" \t preserved\r\n");store.save(data);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden editorial return reader QA",WS_OVERLAPPEDWINDOW,
            0,0,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create hidden editorial return reader host");
    }
    ~View(){close();if(host)DestroyWindow(host);}
    void close(){reader.reset();if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}}
    state::StateData saved(){if(view)check(flush_lesson_view(view),"Cannot flush return reader QA");return state::StateStore(environment.paths.root).load();}
    LessonReaderSnapshot snapshot(){const auto result=reader->snapshot();check(result.error.empty(),result.error);return result;}
    void ready(){wait([&]{const auto s=snapshot();return s.planned&&!s.segments.empty()&&s.index<s.segments.size()&&s.segments[s.index].available;},"Actual return reader audio was not prepared");}
    void open(const curriculum::Lesson& lesson,std::size_t page){
        check(!view&&!reader,"Close the old view before adopting another publication");
        state::StateStore store(environment.paths.root);auto data=store.load();state::prepare_lesson_state(data,lesson);
        state::save_current_page(data,lesson,page);store.save(data);
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(reader)reader->pause();};
        hooks.before_destroy=[&](HWND){reader.reset();};hooks.content_changed=[&](HWND){if(reader)reader->content_changed();};
        view=create_lesson_view(host,environment,lesson,std::move(hooks));check(view!=nullptr,"Cannot mount actual returned lesson");
        MoveWindow(view,0,0,980,720,TRUE);pump();
        check(!IsWindowVisible(host)&&lesson_page_state(view).index==page,"Return QA exposed a window or moved the stored page");
        reader=std::make_unique<LessonReader>(view,environment,worker,[]{});ready();
    }
    narration::Bookmark bookmark(std::string_view key){const auto history=narration::read_history(saved());
        const auto found=std::ranges::find(history.entries,key,&narration::Bookmark::key);check(found!=history.entries.end(),"Missing actual paused return bookmark");return *found;}
    void recording(const LessonReaderSegmentSnapshot& part){
        const auto bytes=state::read_bytes(part.recording);
        const auto u16=[&](std::size_t at){return unsigned(bytes[at])|(unsigned(bytes[at+1])<<8);};
        const auto u32=[&](std::size_t at){return u16(at)|(u16(at+2)<<16);};
        check(bytes.size()>44&&std::equal(bytes.begin(),bytes.begin()+4,"RIFF")&&std::equal(bytes.begin()+8,bytes.begin()+12,"WAVE")
            &&std::equal(bytes.begin()+12,bytes.begin()+16,"fmt ")&&std::equal(bytes.begin()+36,bytes.begin()+40,"data")
            &&u16(20)==1&&u16(22)==1&&u32(24)==24000&&u16(34)==16&&u32(40)==bytes.size()-44&&(bytes.size()-44)%2==0
            &&std::ranges::any_of(bytes.begin()+44,bytes.end(),[](auto byte){return byte!=0;})&&!part.timings.empty(),
            "Return reader requires real non-silent mono 24 kHz PCM16 and word timings");
        for(const auto& word:part.timings)check(word.start<word.end&&word.end<=wide(part.source).size()
            &&word.start_ms>=0&&word.end_ms>=word.start_ms&&word.end_ms<=part.duration_ms+1,"Returned source has invalid UTF-16/audio word alignment");
        const Recording actual{state::sha256(bytes),std::filesystem::last_write_time(part.recording)};
        if(const auto old=recordings.find(part.recording);old!=recordings.end())check(old->second.hash==actual.hash&&old->second.modified==actual.modified,"Compatible return audio was replaced");
        else recordings.emplace(part.recording,actual);
    }
    LessonReaderSnapshot play_pause(){const auto before=snapshot();const auto first_word=before.segments[before.index].timings.front().start_ms;reader->toggle();
        wait([&]{const auto now=snapshot();check(now.key==before.key&&now.index==before.index,"Real return playback selected another sentence");return now.playing&&now.allocated&&now.offset>before.offset+80&&now.offset>=first_word+5;},"Return PCM did not actually play");
        reader->pause();const auto after=snapshot();check(!after.playing&&!after.allocated,"Pausing kept the return player alive");recording(after.segments[after.index]);return after;
    }
    void preview(const curriculum::Lesson& lesson,std::size_t index){
        reader->seek_seconds(0);const auto before=snapshot();close();state::StateStore store(environment.paths.root);auto data=store.load();auto history=narration::read_history(data);
        const auto found=std::ranges::find(history.entries,before.key,&narration::Bookmark::key);check(found!=history.entries.end(),"Preview needs the actual planned page slot");
        // Explicit preview through the production state policy: no fabricated
        // listened prefix, feedback credit or compiler execution receipt.
        found->position=narration::seek(found->position,static_cast<std::int64_t>(index),0);narration::write_history(data,history);store.save(data);
        open(lesson,6);check(snapshot().index==index&&snapshot().offset==0,"Return preview selected a different source sentence");
    }
    static std::string copy_source(){return "#include <iostream>\nint main() {\n    int aforo = 5;\n    int anterior = aforo;\n    aforo += 3;\n    std::cout << \"Antes: \" << anterior << \"\\n\";\n    std::cout << \"Ahora: \" << aforo;\n    return 0;\n}\n";}
    void complete_first_copy(const curriculum::Lesson& lesson){
        const auto find_control=[&](int id){struct Search{int id;HWND result=nullptr;} search{id};
            EnumChildWindows(view,[](HWND item,LPARAM raw)->BOOL{auto& query=*reinterpret_cast<Search*>(raw);
                if(GetDlgCtrlID(item)!=query.id)return TRUE;query.result=item;return FALSE;},reinterpret_cast<LPARAM>(&search));
            check(search.result!=nullptr,"The actual prerequisite compiler control is missing");return search.result;};
        const auto editor=find_control(1210),button=find_control(1211);const auto source=wide(copy_source());
        check(IsWindowEnabled(editor)&&IsWindowEnabled(button),"The prerequisite compiler is locked");
        CHARRANGE all{0,-1};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        SendMessageW(editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(source.c_str()));pump();
        check(edit_text(editor)==copy_source(),"The prerequisite editor changed the supplied program");
        SendMessageW(button,BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(button)&&windows::compiler_puzzle_completed(saved(),lesson,lesson.pages[6].blocks[2]);},
            "The actual prerequisite copy program did not compile and pass its authored output check");
        reader->pause();ready();
    }
    void preserved_recordings(){for(const auto& [file,old]:recordings)check(state::sha256(state::read_bytes(file))==old.hash&&std::filesystem::last_write_time(file)==old.modified,"Return/reopen rewrote an existing QA WAV");}
};
void run(std::shared_ptr<windows::NarrationWorker> worker){
    const auto fixture=test::editorial_return_fixture();
    const auto original_bytes=state::read_bytes(fixture.original_archive),intermediate_bytes=state::read_bytes(fixture.intermediate_archive);
    check(fixture.returned.source_path==fixture.source&&fixture.returned.raw_json!=fixture.intermediate.raw_json,
        "Reader return QA must adopt the actual integrated live C from a distinct authenticated B");
    for(const bool historical:{true,false})for(std::size_t test_index=0;test_index<test::editorial_return_cases.size();++test_index){
        const auto& baseline=historical?fixture.original:fixture.intermediate;
        View qa(worker,test_index+(historical?0:2));std::vector<narration::Bookmark> other_slots;
        for(const auto page:{0u,2u}){qa.open(baseline,page);const auto paused=qa.play_pause();other_slots.push_back(qa.bookmark(paused.key));qa.close();}
        qa.open(baseline,6);if(test_index)qa.complete_first_copy(baseline);
        const auto plan=qa.snapshot();const auto block=test_index?5u:1u;
        const auto& paragraph=baseline.pages[6].blocks[block].text;
        const auto marker=paragraph.find(historical?test::editorial_return_cases[test_index].plain:test::editorial_return_cases[test_index].marked);
        check(marker!=paragraph.npos,"The authored output pair disappeared from its paragraph");
        const auto number=wide(paragraph.substr(0,marker+std::string_view("Antes: ").size()+(historical?0:1))).size();
        // Colons may split a label and its number into different sentences.
        // Locate the actual numeral by its original UTF-16 source coordinate.
        const auto target=std::ranges::find_if(plan.segments,[&](const auto& part){return part.block==block&&part.start<=number&&part.end>number;});
        check(target!=plan.segments.end(),"The actual marked output pair is absent from the reader");
        const auto index=static_cast<std::size_t>(target-plan.segments.begin());qa.preview(baseline,index);
        const auto paused=qa.play_pause();const auto old_part=paused.segments[paused.index];const auto old_slot=qa.bookmark(paused.key);
        check(historical||old_part.source.find('`')!=std::string::npos,"The intermediate source does not exercise inline delimiters");
        check(old_slot.position.heard_prefix&&old_slot.position.heard_prefix->microseconds>0,"Actual pre-return playback did not capture a listened-prefix certificate");
        qa.close();qa.open(fixture.returned,6);const auto returned=qa.snapshot();const auto& new_part=returned.segments[returned.index];
        const auto& current_paragraph=fixture.returned.pages[6].blocks[block].text;
        const auto current_marker=current_paragraph.find(test::editorial_return_cases[test_index].plain);
        check(current_marker!=current_paragraph.npos,"The returned output pair is missing");
        const auto current_number=wide(current_paragraph.substr(0,current_marker+std::string_view("Antes: ").size())).size();
        const auto current_target=std::ranges::find_if(returned.segments,[&](const auto& part){return part.block==block&&part.start<=current_number&&part.end>current_number;});
        check(current_target!=returned.segments.end(),"The returned numeral is absent from the current plan");
        check(returned.index==static_cast<std::size_t>(current_target-returned.segments.begin())&&returned.offset==paused.offset
            &&returned.heard_through==paused.heard_through&&!returned.playing&&!returned.allocated,
            "The legitimate editorial return moved or autoplayed the real paused position");
        check(new_part.source.find('`')==std::string::npos&&new_part.source.find(test_index?'8':'5')!=std::string::npos,
            "The current recording no longer describes the same visible output numeral");
        if(historical)check(returned.key==paused.key&&new_part.spoken==old_part.spoken&&new_part.recording==old_part.recording,
            "The identical genuine A passage failed to reuse its existing script and recording");
        if(returned.key!=paused.key){const auto history=narration::read_history(qa.saved());
            check(std::ranges::none_of(history.entries,[&](const auto& entry){return entry.key==paused.key;}),"Return migration kept a parallel stale narration slot");}
        qa.recording(new_part);
        qa.reader->pause(); // Opening a migrated, paused slot need not acquire ownership.
        const auto returned_slot=qa.bookmark(returned.key);
        if(historical)check(returned_slot.position.heard_prefix==old_slot.position.heard_prefix,"The identical genuine A recording lost its valid listened-prefix certificate");
        else check(!returned_slot.position.heard_prefix||(returned_slot.position.heard_prefix->fingerprint!=old_slot.position.heard_prefix->fingerprint
            &&returned_slot.position.heard_prefix->microseconds==0),"A changed source inherited unverified heard-prefix credit");
        const auto visible=edit_text(new_part.target);const auto visible_pair=visible.find(test::editorial_return_cases[test_index].plain);
        check(visible_pair!=visible.npos,"The native paragraph does not expose its exact visible output pair");
        const auto visible_number=wide(visible.substr(0,visible_pair+std::string_view("Antes: ").size())).size();
        check(text_color(new_part.target,static_cast<LONG>(visible_number))!=reader_theme::unread_color(reader_theme::background(new_part.target)),
            "The exact paused numeral was shaded using stale source coordinates");
        for(const auto& other:other_slots)check(qa.bookmark(other.key)==other,"Adopting a return changed one of the other two real paused slots");
        check(narration::read_history(qa.saved()).entries.size()==3,"Editorial return added or evicted a bounded narration slot");
        const auto second=qa.play_pause();
        if(!historical){const auto certificate=qa.bookmark(second.key).position.heard_prefix;
            check(certificate&&certificate->fingerprint!=old_slot.position.heard_prefix->fingerprint&&certificate->microseconds==0,
                "Resuming mid-recording falsely certified its previously unheard beginning");}
        qa.close();qa.open(fixture.returned,6);const auto reopened=qa.snapshot();
        check(reopened.key==second.key&&reopened.index==second.index&&reopened.offset==second.offset&&!reopened.playing&&!reopened.allocated,
            "The returned publication lost its real second pause on reopen");
        for(const auto& other:other_slots)check(qa.bookmark(other.key)==other,"Reopening the returned publication changed an unrelated slot");
        check(state::string_value(qa.saved(),"editorial_return_opaque","keep")==" \t preserved\r\n","Return playback changed an unrelated preference");
        if(test_index)check(windows::compiler_puzzle_completed(qa.saved(),fixture.returned,fixture.returned.pages[6].blocks[2])
            &&state::get_draft(qa.saved(),curriculum::draft_key("programming",fixture.returned,fixture.returned.pages[6].blocks[2]),"").source==View::copy_source(),
            "Return/reopen lost the actual prerequisite compilation or exact program draft");
        qa.preserved_recordings();check(state::read_bytes(fixture.source)==fixture.source_bytes,"Return QA wrote the live curriculum");
        check(state::read_bytes(fixture.original_archive)==original_bytes&&state::read_bytes(fixture.intermediate_archive)==intermediate_bytes,
            "Return playback/compilation modified an authenticated A/B historical document");
        std::cout<<"PASS actual returned numeral "<<test_index<<" from "<<(historical?"genuine A":"intermediate B")<<": real paused PCM, source position, three slots and preserved WAVs\n";
    }
    std::cout<<"PASS both actual U2L3 returns from genuine A and intermediate B: real Kokoro PCM, exact passage pause/return/resume/reopen, valid UTF-16 word shading, three bounded slots, immutable old WAVs and compatible reuse; two real MSVC prerequisite runs retain their proofs and drafts. Changed source/audio never inherits unverified listening credit. Neutral color/underline and human listening acceptance remain outside this test.\n";
}
}
int main(){std::cout<<std::unitbuf;const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);const auto rich=LoadLibraryW(L"Msftedit.dll");int result=0;
    try{check(rich!=nullptr,"Cannot load RichEdit for return QA");reader_theme::prepare_fonts();
        auto worker=std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");
        struct Shutdown{std::shared_ptr<windows::NarrationWorker> worker;~Shutdown(){worker->shutdown();}} cleanup{worker};run(worker);
    }catch(const std::exception& error){std::cerr<<"FAIL editorial return view: "<<error.what()<<'\n';result=1;}
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}
