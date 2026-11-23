// Revision-order regression through real LessonView / LessonReader lifetimes.
// The protocol fixture supplies deterministic PCM, not neural speech. No model
// downloads, private-member mutation, production profiles or manual CI runs.
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;

std::string phase="initialization";
std::size_t failures=0;
std::uint64_t profile_sequence=0;

std::string quoted(std::string_view value) {
    return winrt::to_string(JsonValue::CreateStringValue(wide(value)).Stringify());
}
void check(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(phase+": "+std::string(message));
}
void expect(bool value,std::string_view message) {
    if(value)return;
    ++failures;
    std::cerr<<"REVISION_ORDER_FAILURE stage="<<quoted(phase)<<" reason="<<quoted(message)<<'\n';
}
void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
void wait(const std::function<bool()>& done,std::string_view description,ULONGLONG timeout=30000) {
    const auto deadline=GetTickCount64()+timeout;
    do {
        pump();if(done())return;
        MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    } while(GetTickCount64()<deadline);
    throw std::runtime_error(phase+": timeout "+std::string(description));
}
HWND child(HWND parent,int id) {
    struct Search { int id;HWND result=nullptr; } search{id};
    EnumChildWindows(parent,[](HWND window,LPARAM context)->BOOL {
        auto& value=*reinterpret_cast<Search*>(context);
        if(GetDlgCtrlID(window)==value.id){value.result=window;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    check(search.result!=nullptr,"Missing actual Win32 control "+std::to_string(id));return search.result;
}
std::string bytes(const std::filesystem::path& path) {
    const auto raw=state::read_bytes(path);return std::string(raw.begin(),raw.end());
}
std::string field(const JsonObject& value,std::wstring_view key) {
    return winrt::to_string(value.GetNamedString(winrt::hstring(key)));
}

struct AuthoredBlock {
    std::string role,owner,type,text,question,feedback,answer;
    std::size_t storage_page=0,storage_block=0;
};
struct AuthoredPage { std::string id;std::vector<AuthoredBlock> blocks; };
struct Variant {
    std::string name,sha256;
    std::filesystem::path path;
    curriculum::Lesson lesson;
    std::vector<AuthoredPage> pages;
};

std::map<std::string,Variant> load_manifest(const std::filesystem::path& path) {
    const auto manifest=JsonObject::Parse(wide(bytes(path)));
    check(manifest.GetNamedNumber(L"version")==1,"Unsupported reader revision manifest");
    std::map<std::string,Variant> variants;
    for(const auto& item:manifest.GetNamedArray(L"fixtures")) {
        const auto record=item.GetObject();Variant value;
        value.name=field(record,L"name");value.sha256=field(record,L"sha256");
        const auto relative=std::filesystem::path(field(record,L"file"));
        check(!relative.empty()&&!relative.is_absolute()&&relative.filename()==relative,
            "Fixture paths must be filenames inside their own export directory");
        value.path=path.parent_path()/relative;
        check(state::sha256(state::read_bytes(value.path))==value.sha256,"Fixture hash differs from authored manifest");
        value.lesson=windows::load_lesson(value.path);
        check(value.lesson.id==field(manifest,L"lessonId")&&value.lesson.locale=="es"&&
            value.lesson.title=="READER_REVISION_TITLE"&&value.lesson.state_layout.version==2,
            "Fixture is not the expected authenticated v2 lesson");
        for(const auto& page_value:record.GetNamedArray(L"pages")) {
            const auto page_record=page_value.GetObject();AuthoredPage page;page.id=field(page_record,L"id");
            for(const auto& expected:page_record.GetNamedArray(L"expected")) {
                const auto block=expected.GetObject();AuthoredBlock authored;
                authored.role=field(block,L"role");authored.owner=field(block,L"owner");authored.type=field(block,L"type");
                const auto storage=block.GetNamedObject(L"storage");
                authored.storage_page=static_cast<std::size_t>(storage.GetNamedNumber(L"page"));
                authored.storage_block=static_cast<std::size_t>(storage.GetNamedNumber(L"block"));
                if(authored.type=="paragraph")authored.text=field(block,L"text");
                else {
                    check(authored.type=="fill_code","Unexpected test block type");
                    authored.question=field(block,L"question");authored.feedback=field(block,L"feedback");authored.answer=field(block,L"answer");
                }
                page.blocks.push_back(std::move(authored));
            }
            value.pages.push_back(std::move(page));
        }
        check(value.pages.size()==2&&value.lesson.pages.size()==value.pages.size(),"Expected two authored pages");
        for(std::size_t p=0;p<value.pages.size();++p) {
            const auto& model=value.lesson.pages[p];const auto& oracle=value.pages[p];
            check(model.stable_id==oracle.id&&model.blocks.size()==oracle.blocks.size(),"Loaded page order differs from independent authored oracle");
            for(std::size_t b=0;b<oracle.blocks.size();++b) {
                const auto& expected=oracle.blocks[b];const auto& block=model.blocks[b];
                check(block.stable_id==expected.owner&&block.address.page==expected.storage_page&&
                    block.address.block==expected.storage_block,"An authored owner lost its original storage identity");
                check(expected.type=="paragraph"?block.type==curriculum::BlockType::paragraph&&block.text==expected.text:
                    block.type==curriculum::BlockType::fill_code&&block.question==expected.question&&
                    block.explanation==expected.feedback&&block.answers==std::vector<std::string>{expected.answer},
                    "Loaded source differs from independent authored oracle");
            }
        }
        check(variants.emplace(value.name,std::move(value)).second,"Repeated fixture name");
    }
    check(variants.size()==6,"The reader regression requires all six source variants");
    for(const std::string name:{"baseline","insert","reorder","prose","other-page","retire-insert"})
        check(variants.contains(name),"Missing variant "+name);
    return variants;
}

struct ExpectedRegion {
    std::string owner,source;
    std::size_t block=std::numeric_limits<std::size_t>::max();
    int part=-1;
    bool feedback=false;
};
std::vector<ExpectedRegion> expected_regions(const Variant& variant,std::size_t page,
        const std::set<std::string>& solved) {
    // The authoring fixture has a literal role order. We never derive this
    // oracle by sorting storage keys, live regions or the reader's own plan.
    std::vector<ExpectedRegion> result;
    if(page==0)result.push_back({"@title","READER_REVISION_TITLE"});
    const auto& authored=variant.pages.at(page).blocks;
    for(std::size_t index=0;index<authored.size();++index) {
        const auto& block=authored[index];
        if(block.type=="paragraph")result.push_back({block.owner,block.text,index,0,false});
        else {
            result.push_back({block.owner,block.question,index,0,false});
            // This is the documented visible code-card caption, deliberately
            // repeated for different owners to catch index-based aliasing.
            result.push_back({block.owner,"Ejercicio de código C más más.",index,16,false});
            if(solved.contains(block.role))result.push_back({block.owner,"✓ Correcto\n\n"+block.feedback,index,4,true});
        }
    }
    return result;
}
std::size_t source_index(const std::vector<ExpectedRegion>& rows,std::string_view source) {
    const auto found=std::ranges::find(rows,source,&ExpectedRegion::source);
    check(found!=rows.end(),"Missing independent expected source "+std::string(source));
    return static_cast<std::size_t>(found-rows.begin());
}
std::vector<std::size_t> interval(std::size_t first,std::size_t last) {
    check(first<=last,"Invalid expected interval");std::vector<std::size_t> result;
    for(auto index=first;index<=last;++index)result.push_back(index);return result;
}

struct Fixture {
    WindowsEnvironment environment;
    std::shared_ptr<windows::NarrationWorker> backend;
    HWND host=nullptr,view=nullptr;
    std::unique_ptr<LessonReader> reader;
    const Variant* active=nullptr;

    Fixture(std::shared_ptr<windows::NarrationWorker> worker,const Variant& before):
        environment(resolve_windows_environment({ProfileId::Codex,L"reader-revision-"+
            std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+
            std::to_wstring(++profile_sequence),false})),backend(std::move(worker)) {
        check(!std::filesystem::exists(environment.paths.root),"Reader QA profile already exists");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root);auto data=store.load();
        state::prepare_lesson_state(data,before.lesson);
        auto& prefs=data.preferences["lesson_narration_settings"];
        prefs["engine"]=std::string("KOKORO");prefs["language"]=std::string("es");
        prefs["kokoro_voice:es"]=std::string("ef_dora");prefs["rate"]=1.;prefs["kokoro_advance_generation"]=true;
        data.preferences["reader_revision_qa_sentinel"]["opaque"]=std::string("Keep this private QA preference byte-for-byte.\n");
        store.save(data);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Reader revision sequencing QA",
            WS_OVERLAPPEDWINDOW,40,40,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr&&!IsWindowVisible(host),"Cannot create hidden isolated reader host");open(before);
    }
    ~Fixture() {
        reader.reset();if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);
    }
    LessonReaderSnapshot snapshot() const {
        auto result=reader->snapshot();check(result.error.empty(),"Actual reader error: "+result.error);return result;
    }
    void ready() {
        wait([&] {
            const auto value=snapshot();return value.planned&&!value.segments.empty()&&
                std::ranges::all_of(value.segments,[](const auto& segment){return segment.available;});
        },"actual reader plan and all deterministic recordings");
    }
    void open(const Variant& model) {
        check(!view&&!reader,"A revised view must have its own ordinary reader lifetime");active=&model;
        view=create_lesson_view(host,environment,model.lesson);check(view!=nullptr,"Cannot create actual LessonView");
        MoveWindow(view,0,0,980,720,TRUE);
        LessonViewHooks hooks;
        hooks.before_page_change=[this](HWND){if(reader)reader->pause();};
        hooks.content_changed=[this](HWND){if(reader)reader->content_changed();};
        hooks.feedback_changed=[this](HWND,std::size_t block,bool correct){if(reader)reader->feedback(block,correct);};
        set_lesson_view_hooks(view,std::move(hooks));
        reader=std::make_unique<LessonReader>(view,environment,backend,[]{});ready();
    }
    void close() {
        reader.reset();check(flush_lesson_view(view),"Cannot persist owned QA lesson state");DestroyWindow(view);view=nullptr;
    }
    const curriculum::Block& block(std::string_view role) const {
        for(const auto& page:active->pages)for(const auto& authored:page.blocks)if(authored.role==role) {
            const auto at=curriculum::stable_block_position(active->lesson,authored.owner);
            check(at.has_value(),"Expected owner missing from loaded lesson");return active->lesson.pages[at->page].blocks[at->block];
        }
        throw std::runtime_error(phase+": missing role "+std::string(role));
    }
    std::size_t display_index(std::string_view role) const {
        const auto at=curriculum::stable_block_position(active->lesson,block(role).stable_id);
        check(at&&at->page==lesson_page_state(view).index,"Answer role is not on the displayed page");return at->block;
    }
    void edit(std::string_view role,std::string_view text) {
        const auto index=display_index(role);const auto edit=child(view,1002+static_cast<int>(index)*100);
        const auto value=wide(text);CHARRANGE all{0,-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));
    }
    void answer(std::string_view role) {
        edit(role,"ok");const auto index=display_index(role);
        SendMessageW(child(view,1003+static_cast<int>(index)*100),BM_CLICK,0,0);
        reader->pause();ready();
        const auto response=state::get_response(read_lesson_state(view),active->lesson,block(role));
        check(response&&response->correct,"Actual fill control did not accept its authored answer");
        check(!snapshot().playing&&!snapshot().allocated,"Feedback preparation ignored explicit pause");
    }
    void page(std::size_t index) {
        reader->pause();check(navigate_lesson_view(view,index),"Cannot navigate actual lesson page");ready();
        check(lesson_page_state(view).index==index,"Lesson navigation did not reach requested page");
    }
};

void print_snapshot(const LessonReaderSnapshot& value) {
    std::cout<<"REVISION_POSITION stage="<<quoted(phase)<<" index="<<value.index<<" offset="<<value.offset
        <<" heard_through="<<value.heard_through<<" waiting="<<value.waiting_gate<<" finished="<<value.finished
        <<" playing="<<value.playing<<" allocated="<<value.allocated<<" key="<<quoted(value.key)<<'\n';
}
std::vector<ExpectedRegion> verify_plan(Fixture& fixture,const std::set<std::string>& solved) {
    const auto page=lesson_page_state(fixture.view).index;
    auto expected=expected_regions(*fixture.active,page,solved);
    const auto regions=lesson_reading_regions(fixture.view);const auto value=fixture.snapshot();
    expect(regions.size()==expected.size(),"Visible reading-region count differs from authored current order");
    expect(value.segments.size()==expected.size(),"Planned segment count differs from one-segment fixture oracle");
    const auto prefix=fixture.active->lesson.id+"/es/"+fixture.active->pages[page].id;
    for(std::size_t index=0;index<expected.size();++index) {
        const auto& row=expected[index];
        const auto control=row.part<0?990:1000+static_cast<int>(row.block)*100+row.part;
        const auto target=child(fixture.view,control);
        const auto key=row.part<0?prefix+"/title":prefix+'/'+row.owner+'/'+std::to_string(control);
        const auto anchor=row.part<0?std::string{}:prefix+'/'+row.owner+"/part/"+std::to_string(row.part);
        if(index<regions.size()) {
            const auto& actual=regions[index];
            expect(actual.key==key&&actual.anchor==anchor&&actual.source_text==row.source&&actual.target==target&&
                actual.block_index==row.block&&actual.feedback==row.feedback&&actual.correct==row.feedback,
                "Region order/owner/target/source mismatch at index "+std::to_string(index)+" owner "+row.owner);
        }
        if(index<value.segments.size()) {
            const auto& actual=value.segments[index];
            expect(actual.region==key&&actual.source==row.source&&actual.spoken==row.source&&actual.target==target&&
                actual.block==row.block&&actual.feedback==row.feedback&&actual.correct==row.feedback&&
                actual.start==0&&actual.end==wide(row.source).size()&&actual.available,
                "Segment order/owner/target/source mismatch at index "+std::to_string(index)+" owner "+row.owner);
            std::cout<<"REVISION_SEGMENT stage="<<quoted(phase)<<" index="<<index<<" owner="<<quoted(row.owner)
                <<" control="<<GetDlgCtrlID(actual.target)<<" source="<<quoted(actual.source)
                <<" expected="<<quoted(row.source)<<" feedback="<<actual.feedback<<" correct="<<actual.correct<<'\n';
        }
    }
    print_snapshot(value);return expected;
}

struct CachedRecording { std::filesystem::path path;std::vector<std::uint8_t> bytes;std::filesystem::file_time_type written; };
std::vector<CachedRecording> seal_cache(const LessonReaderSnapshot& snapshot) {
    std::vector<CachedRecording> result;std::set<std::filesystem::path> seen;
    for(const auto& part:snapshot.segments)if(seen.insert(part.recording).second) {
        check(!part.recording.empty()&&part.available,"Cannot seal a missing old recording");
        result.push_back({part.recording,state::read_bytes(part.recording),std::filesystem::last_write_time(part.recording)});
    }
    return result;
}
void verify_cache(const std::vector<CachedRecording>& sealed) {
    for(const auto& old:sealed)expect(std::filesystem::is_regular_file(old.path)&&
        state::read_bytes(old.path)==old.bytes&&std::filesystem::last_write_time(old.path)==old.written,
        "A lesson revision modified or replaced existing controlled audio "+utf8(old.path.native()));
}
void verify_existing_state(Fixture& fixture,const state::StateData& original,bool check_draft) {
    const auto current=read_lesson_state(fixture.view);
    expect(current.preferences.at("reader_revision_qa_sentinel")==original.preferences.at("reader_revision_qa_sentinel"),
        "Revision changed an unrelated existing preference");
    for(const std::string role:{"a","b"}) {
        if(role=="b"&&!check_draft)continue;
        const auto before=state::get_response(original,fixture.active->lesson,fixture.block(role));
        const auto after=state::get_response(current,fixture.active->lesson,fixture.block(role));
        expect(before&&after&&before->raw_json==after->raw_json&&before->answer==after->answer&&before->correct==after->correct,
            "Revision changed preserved response/draft for owner "+role);
    }
}

std::vector<std::size_t> trace_playback(Fixture& fixture,const std::vector<ExpectedRegion>& oracle,
        const std::function<bool(const LessonReaderSnapshot&)>& done,bool start=true) {
    std::vector<std::size_t> trace;
    if(start)fixture.reader->toggle();
    wait([&] {
        const auto value=fixture.snapshot();
        if(value.playing&&(trace.empty()||trace.back()!=value.index)) {
            trace.push_back(value.index);const auto& part=value.segments.at(value.index);
            std::cout<<"REVISION_PLAYED stage="<<quoted(phase)<<" sequence="<<trace.size()-1<<" index="<<value.index
                <<" owner="<<quoted(value.index<oracle.size()?oracle[value.index].owner:"@unexpected")
                <<" source="<<quoted(part.source)<<" heard_through="<<value.heard_through<<'\n';
        }
        return done(value);
    },"observed deterministic PCM segment sequence");
    print_snapshot(fixture.snapshot());return trace;
}
void compare_trace(const std::vector<std::size_t>& actual,const std::vector<std::size_t>& expected,
        const std::vector<ExpectedRegion>& oracle) {
    expect(actual==expected,"Playback order differs from authored current sequence");
    if(actual==expected)return;
    const auto count=std::max(actual.size(),expected.size());
    for(std::size_t index=0;index<count;++index) {
        const auto observed=index<actual.size()?static_cast<std::int64_t>(actual[index]):-1;
        const auto wanted=index<expected.size()?static_cast<std::int64_t>(expected[index]):-1;
        if(observed==wanted)continue;
        std::cerr<<"REVISION_ORDER_DIVERGENCE stage="<<quoted(phase)<<" sequence="<<index
            <<" actual_index="<<observed<<" expected_index="<<wanted
            <<" actual_owner="<<quoted(observed>=0&&static_cast<std::size_t>(observed)<oracle.size()?oracle[observed].owner:"@none")
            <<" expected_owner="<<quoted(wanted>=0&&static_cast<std::size_t>(wanted)<oracle.size()?oracle[wanted].owner:"@none")<<'\n';
    }
}

void run_revision(const std::shared_ptr<windows::NarrationWorker>& backend,const Variant& baseline,
        const Variant& revised,bool unheard_negative=false,bool insert_forward=false) {
    const auto scenario=unheard_negative?std::string("unheard-negative"):insert_forward?std::string("insert-forward"):revised.name;
    const bool insertion=revised.name=="insert"||revised.name=="retire-insert";
    phase=scenario+" / baseline";Fixture fixture(backend,baseline);
    verify_plan(fixture,{});fixture.edit("b","unfinished QA draft");fixture.answer("a");
    auto old_rows=verify_plan(fixture,{"a"});const auto a_feedback=source_index(old_rows,"✓ Correcto\n\nA_FEEDBACK");
    if(!unheard_negative) {
        fixture.reader->toggle();
        wait([&] {
            const auto value=fixture.snapshot();return value.playing&&value.index==source_index(old_rows,"P_BETWEEN")&&value.offset>=100;
        },"actual old A feedback playback followed by a partial paragraph");
        fixture.reader->pause();
        const auto heard=fixture.snapshot();
        check(heard.segments[a_feedback].feedback_fully_played&&heard.heard_through>static_cast<std::int64_t>(a_feedback),
            "Baseline did not actually hear A feedback before its saved pause");
    } else {
        check(!fixture.snapshot().segments[a_feedback].feedback_fully_played&&fixture.snapshot().heard_through<=static_cast<std::int64_t>(a_feedback),
            "Negative baseline accidentally earned A feedback credit");
    }
    const auto old=fixture.snapshot();const auto cached=seal_cache(old);
    check(flush_lesson_view(fixture.view),"Cannot save baseline UI data");
    const auto original=read_lesson_state(fixture.view);const auto old_history=narration::read_history(original);
    const auto bookmark=std::ranges::find(old_history.entries,old.key,&narration::Bookmark::key);
    check(bookmark!=old_history.entries.end()&&bookmark->semantic_plan.has_value()&&
        bookmark->position.segment==static_cast<std::int64_t>(old.index)&&bookmark->position.milliseconds==old.offset,
        "Baseline lacks a genuine reader-generated semantic bookmark");
    print_snapshot(old);fixture.close();

    phase=scenario+" / reopen revised";fixture.open(revised);
    auto rows=verify_plan(fixture,{"a"});verify_existing_state(fixture,original,true);verify_cache(cached);
    const auto check_resume=[&] {
        const auto value=fixture.snapshot();
        const std::string expected_source=unheard_negative?"✓ Correcto\n\nA_FEEDBACK":insertion?"P_NEW":
            revised.name=="reorder"?"B_QUESTION":revised.name=="prose"?"P_BETWEEN_REVISED":"P_BETWEEN";
        const auto expected_at=source_index(rows,expected_source);
        expect(value.index==expected_at,"Reopening selected the wrong current owner/source: expected "+expected_source);
        expect(!value.playing&&!value.allocated&&!value.finished,"Reopening started or completed unrequested playback");
        if(revised.name=="other-page")expect(value.offset==old.offset,"Editing another page lost the exact compatible paused offset");
        else expect(value.offset==0,"A new/changed/earlier pending region inherited an unrelated old offset");
        if(!unheard_negative&&revised.name!="reorder")expect(
            value.heard_through>static_cast<std::int64_t>(source_index(rows,"✓ Correcto\n\nA_FEEDBACK")),
            "A structural/editorial revision lost already-heard compatible A feedback credit");
        if(unheard_negative)expect(value.heard_through<=static_cast<std::int64_t>(source_index(rows,"✓ Correcto\n\nA_FEEDBACK")),
            "A merely correct answer acquired invented listening credit");
    };
    check_resume();fixture.close();
    phase=scenario+" / second reopen";fixture.open(revised);rows=verify_plan(fixture,{"a"});
    check_resume();verify_existing_state(fixture,original,true);

    if(insertion) {
        if(insert_forward||revised.name=="retire-insert") {
            phase=scenario+" / new prose before X";
            const auto first=source_index(rows,"P_NEW"),question=source_index(rows,"X_QUESTION");
            const auto played=trace_playback(fixture,rows,[](const auto& value){return value.waiting_gate;});
            compare_trace(played,interval(first,question+1),rows);
            expect(fixture.snapshot().index==question+1&&!fixture.snapshot().finished,
                "New prose did not lead to the actual first pending X gate");
        }
        // The other positive insertion scenario answers X immediately after
        // reopening. Replaying from a wrongly reset zero would hear A again
        // and mask the lost-credit X -> old A -> X regression we must expose.
        phase=scenario+" / new X feedback";fixture.answer("x");rows=verify_plan(fixture,{"a","x"});
        const auto x=source_index(rows,"✓ Correcto\n\nX_FEEDBACK");
        check(fixture.snapshot().index==x,"Fresh X answer did not select its actual feedback");
        if(unheard_negative) {
            const auto a=source_index(rows,"✓ Correcto\n\nA_FEEDBACK");
            const auto played=trace_playback(fixture,rows,[&](const auto& value){return value.playing&&value.index==a;});
            fixture.reader->pause();compare_trace(played,{x,a},rows);
            verify_existing_state(fixture,original,true);verify_cache(cached);
            std::cout<<"PASS negative revision fixture: an unheard prior correct answer still blocks continuation\n";return;
        }
        const auto b=source_index(rows,"B_QUESTION");
        const auto played=trace_playback(fixture,rows,[](const auto& value){return value.waiting_gate;});
        compare_trace(played,interval(x,b+1),rows);
        expect(fixture.snapshot().index==b+1&&!fixture.snapshot().finished,"X feedback did not stop at actual pending B");
    } else if(revised.name=="reorder") {
        phase=scenario+" / moved B feedback";fixture.answer("b");rows=verify_plan(fixture,{"a","b"});
        const auto played=trace_playback(fixture,rows,[](const auto& value){return value.finished;});
        compare_trace(played,interval(source_index(rows,"✓ Correcto\n\nB_FEEDBACK"),rows.size()-1),rows);
    } else {
        phase=scenario+" / resume current prose";
        const auto played=trace_playback(fixture,rows,[](const auto& value){return value.waiting_gate;});
        const auto first=source_index(rows,revised.name=="prose"?"P_BETWEEN_REVISED":"P_BETWEEN");
        const auto b=source_index(rows,"B_QUESTION");compare_trace(played,interval(first,b+1),rows);
        expect(fixture.snapshot().index==b+1&&!fixture.snapshot().finished,"Prose resume escaped pending B");
    }
    std::set<std::string> solved{"a","b"};if(insertion)solved.insert("x");
    if(revised.name!="reorder") {
        phase=scenario+" / finish B";fixture.answer("b");rows=verify_plan(fixture,solved);
        const auto played=trace_playback(fixture,rows,[](const auto& value){return value.finished;});
        compare_trace(played,interval(source_index(rows,"✓ Correcto\n\nB_FEEDBACK"),rows.size()-1),rows);
    }
    phase=scenario+" / restart whole current page";rows=verify_plan(fixture,solved);
    fixture.reader->restart();const auto restarted=trace_playback(fixture,rows,[](const auto& value){return value.finished;},false);
    compare_trace(restarted,interval(0,rows.size()-1),rows);
    expect(!fixture.snapshot().allocated,"Finished current page retained its player");
    verify_existing_state(fixture,original,false);verify_cache(cached);
    fixture.close();phase=scenario+" / reopen completed current page";fixture.open(revised);rows=verify_plan(fixture,solved);
    expect(fixture.snapshot().finished&&fixture.snapshot().index==rows.size()-1,
        "Reopening the complete revised sequence lost its terminal bookmark");
    phase=scenario+" / second page";fixture.page(1);rows=verify_plan(fixture,solved);
    if(revised.name=="other-page") {
        fixture.reader->restart();const auto gate=trace_playback(fixture,rows,[](const auto& value){return value.waiting_gate;},false);
        compare_trace(gate,interval(0,rows.size()-1),rows);
        fixture.answer("y");solved.insert("y");rows=verify_plan(fixture,solved);
    }
    fixture.reader->restart();const auto page_two=trace_playback(fixture,rows,[](const auto& value){return value.finished;},false);
    compare_trace(page_two,interval(0,rows.size()-1),rows);verify_cache(cached);
    std::cout<<"REVISION_SCENARIO_COMPLETE name="<<quoted(scenario)<<" controlled_pcm=true neural_audio_evidence=false\n";
}

std::shared_ptr<windows::NarrationWorker> controlled_worker(const std::filesystem::path& python,
        const std::filesystem::path& build_directory) {
    check(std::filesystem::is_regular_file(python)&&std::filesystem::is_directory(build_directory),
        "Controlled runtime needs an existing Python executable and QA build directory");
    const auto directory=build_directory/(L"reader-revision-runtime-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(directory),"Controlled runtime directory must be new");
    const auto runtime=directory/L"runtime.json";JsonObject config;JsonArray arguments;
    config.Insert(L"java",JsonValue::CreateStringValue(python.native()));
    for(const auto& value:{std::wstring(L"-u"),(std::filesystem::path(build::source_root)/
            L"native/presentation/windows/lesson_reader_edit_order_fixture.py").native(),
            (directory/L"transport.jsonl").native(),(directory/L"audio").native()})
        arguments.Append(JsonValue::CreateStringValue(value));
    config.Insert(L"arguments",arguments);
    {std::ofstream output(runtime);output<<winrt::to_string(config.Stringify());check(static_cast<bool>(output),"Cannot write owned fixture runtime configuration");}
    std::cout<<"REVISION_RUNTIME directory="<<quoted(utf8(directory.native()))<<'\n';
    return std::make_shared<windows::NarrationWorker>(runtime);
}
}

int main(int argc,char** argv) {
    if(argc!=4&&argc!=5) {
        std::cerr<<"Usage: lesson_reader_edit_order_tests <manifest.json> <python.exe> <QA-build-directory> [scenario]\n";return 2;
    }
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto rich=LoadLibraryW(L"Msftedit.dll");int result=0;
    try {
        check(rich!=nullptr,"Cannot load RichEdit controls");windows::ensure_winrt_apartment();reader_theme::prepare_fonts();
        const auto manifest_path=std::filesystem::path(argv[1]);const auto manifest_bytes=state::read_bytes(manifest_path);
        const auto variants=load_manifest(manifest_path);const auto backend=controlled_worker(argv[2],argv[3]);
        struct Shutdown { std::shared_ptr<windows::NarrationWorker> worker;~Shutdown(){worker->shutdown();} } shutdown{backend};
        const std::string filter=argc==5?argv[4]:"";std::size_t executed=0;
        for(const std::string scenario:{"insert","insert-forward","reorder","prose","other-page","unheard-negative","retire-insert"}) {
            if(!filter.empty()&&filter!=scenario)continue;++executed;
            try {run_revision(backend,variants.at("baseline"),variants.at(scenario=="unheard-negative"||scenario=="insert-forward"?"insert":scenario),
                scenario=="unheard-negative",scenario=="insert-forward");}
            catch(const std::exception& error) {++failures;std::cerr<<"REVISION_SCENARIO_ERROR stage="<<quoted(phase)<<" reason="<<quoted(error.what())<<'\n';}
        }
        check(executed>0,"Unknown requested reader revision scenario");
        for(const auto& [name,variant]:variants)check(state::sha256(state::read_bytes(variant.path))==variant.sha256,
            "Native reader QA changed an authored fixture "+name);
        check(state::read_bytes(manifest_path)==manifest_bytes,"Native reader QA changed its authored manifest");
        std::cout<<"REVISION_RESULT scenarios="<<executed<<" failures="<<failures
            <<" native_lifecycle=true deterministic_protocol=true neural_voice_acceptance=false\n";
        result=failures?1:0;
    } catch(const std::exception& error) {std::cerr<<"FAIL reader revision QA: "<<error.what()<<'\n';result=1;}
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}
