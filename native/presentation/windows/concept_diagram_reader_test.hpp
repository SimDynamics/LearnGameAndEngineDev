#pragma once
// Actual native controls, loaders, navigation and isolated real reader.
namespace concept_diagram_reader_test {
using namespace winrt::Windows::Data::Json;

void rejects(const std::string& raw){
    for(const bool metadata:{false,true}){
        bool rejected=false;
        try{
            if(metadata)(void)windows::parse_lesson_progress_metadata(raw);
            else (void)windows::parse_lesson_json(raw);
        }catch(const std::exception&){rejected=true;}
        check(rejected,"A malformed conceptual diagram was accepted");
    }
}

void malformed(const std::string& example){
    const auto wrap=[](std::string_view block){
        return "{\"id\":\"cpp_fundamentals__qa__invalid_diagram\",\"title\":\"QA\",\"pages\":[{\"title\":\"QA\",\"blocks\":["+std::string(block)+"]}]}";
    };
    (void)windows::parse_lesson_json(wrap(example));
    (void)windows::parse_lesson_progress_metadata(wrap(example));
    for(const auto field:{L"explanation",L"reviewFeedback",L"responses"}){
        auto value=JsonObject::Parse(wide(example));
        value.Insert(field,JsonValue::CreateStringValue(L"forbidden"));
        rejects(wrap(utf8(value.Stringify())));
    }
    {
        auto value=JsonObject::Parse(wide(example));
        value.GetNamedArray(L"nodes").GetObjectAt(0).Insert(L"kind",JsonValue::CreateStringValue(L"unknown"));
        rejects(wrap(utf8(value.Stringify())));
    }
    {
        auto value=JsonObject::Parse(wide(example));
        value.GetNamedArray(L"nodes").GetObjectAt(1).Insert(L"id",JsonValue::CreateStringValue(L"tarifa"));
        rejects(wrap(utf8(value.Stringify())));
    }
    {
        auto value=JsonObject::Parse(wide(example));
        value.GetNamedArray(L"links").GetObjectAt(0).Insert(L"to",JsonValue::CreateStringValue(L"missing"));
        rejects(wrap(utf8(value.Stringify())));
    }
    {
        auto value=JsonObject::Parse(wide(example));
        value.GetNamedArray(L"nodes").GetObjectAt(0).Insert(L"value",JsonValue::CreateStringValue(L" "));
        rejects(wrap(utf8(value.Stringify())));
    }
    const auto start=example.find('{');
    check(start!=std::string::npos,"Missing example object");
    auto duplicate=example;
    duplicate.insert(start+1,"\"schemaVersion\":1,");
    rejects(wrap(duplicate));
    auto decimal=example;
    const auto version=decimal.find("\"schemaVersion\": 1");
    check(version!=std::string::npos,"Missing exact version fixture");
    decimal.insert(version+std::string_view("\"schemaVersion\": 1").size(),".0");
    rejects(wrap(decimal));
}

void visual(HWND parent,const curriculum::Block& block,const std::filesystem::path& artifacts,std::string_view name){
    check(block.concept_diagram.has_value(),"The full native loader lost its typed diagram");
    MathPuzzleView view(parent,block.raw_json,"es","{}",{},block.text_semantics,{},block.concept_diagram);
    const auto expected=block.concept_diagram->reading_fields();
    const auto count=block.concept_diagram->nodes.size();
    check(count>=2,"QA must exercise connected diagrams");
    const auto dpi=static_cast<int>(GetDpiForWindow(parent));
    const auto px=[&](int n){return MulDiv(n,dpi,96);};
    for(const auto scale:{75,100,150}){
        view.set_text_scale(scale);
        for(const auto width:{1120,280}){
            view.layout(0,0,px(width));
            const auto regions=view.reading_regions();
            check(regions.size()==expected.size(),"A diagram duplicated or omitted its authored reading fields");
            RECT bounds{};GetClientRect(view.window(),&bounds);
            for(std::size_t i=0;i<regions.size();++i){
                const auto& region=regions[i];
                check(region.key==expected[i].key&&region.source_text==expected[i].text&&
                      region.math_mode=="text"&&region.target&&IsWindow(region.target),
                      "Responsive layout changed the exact authored reading source");
                check(edit_text(region.target)==expected[i].text,
                      "Diagram display hid or replaced literal authored characters");
                check(region.text_semantics&&rich_text_semantics(region.target)==region.text_semantics,
                      "The visible control lost its explicit semantic source");
                const auto area=code_scope_reader_test::bounds_in(region.target,view.window());
                check(area.left>=0&&area.top>=0&&area.right<=bounds.right&&area.bottom<=bounds.bottom&&
                      area.right>area.left&&area.bottom>area.top,"Diagram text is clipped or lacks a native control");
                check((GetWindowLongPtrW(region.target,GWL_STYLE)&ES_READONLY)!=0,
                      "A reading-only diagram exposed an editable response");
            }
            const auto first=code_scope_reader_test::bounds_in(regions[1].target,view.window());
            const auto last_key=block.concept_diagram->node_fields(count-1).front().key;
            const auto last=std::ranges::find(regions,last_key,&MathReadingRegion::key);
            check(last!=regions.end(),"Missing last node");
            const auto end=code_scope_reader_test::bounds_in(last->target,view.window());
            if(width==1120)check(end.left>=first.right,"Wide diagram did not arrange its connected nodes horizontally");
            else check(end.top>=first.bottom,"Narrow diagram did not stack its connected nodes vertically");
            for(std::size_t node=0;node<count;++node){
                const auto surface=block.concept_diagram->nodes[node].kind=="console"
                    ?reader_theme::Surface::compiler_output:reader_theme::Surface::card;
                LOGBRUSH brush{};
                check(GetObjectW(reader_theme::brush(surface),sizeof(brush),&brush)==sizeof(brush),
                      "Cannot inspect native diagram surface");
                for(const auto& field:block.concept_diagram->node_fields(node)){
                    const auto item=std::ranges::find(regions,field.key,&MathReadingRegion::key);
                    check(item!=regions.end()&&reader_theme::background(item->target)==brush.lbColor,
                          "Diagram node did not use its authored native surface");
                }
            }
            check(JsonObject::Parse(wide(view.answers_json())).Size()==0,"Diagram created a response");
            if(scale==100)code_scope_reader_test::capture_png(view.window(),artifacts/
                (std::string(name)+"-"+std::to_string(width)+".png"));
        }
    }
    std::cout<<"PASS real diagram geometry, fields and read-only controls "<<name<<" at 75/100/150 percent\n";
}

void run(const std::shared_ptr<windows::NarrationWorker>& backend,const std::filesystem::path& fixture_path,
         const std::filesystem::path& artifacts){
    std::filesystem::create_directories(artifacts);
    const auto bytes=state::read_bytes(std::filesystem::path(build::source_root)/"docs/examples/concept_diagram_memory_console.json");
    malformed({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
    const auto lesson=windows::load_lesson(fixture_path);
    const auto metadata=windows::load_lesson_progress_metadata(fixture_path);
    check(lesson.pages.size()==3&&metadata.pages.size()==3,"QA diagram lesson has unexpected pages");
    for(std::size_t p=0;p<2;++p){
        const auto& block=lesson.pages[p].blocks.front();
        const auto& progress=metadata.pages[p].blocks.front();
        check(block.type==curriculum::BlockType::concept_diagram&&progress.type==block.type&&
              block.concept_diagram&&progress.concept_diagram==block.concept_diagram&&
              progress.stable_id==block.stable_id,"Full and progress loaders disagree on a diagram");
    }
    // Keep this test's window and state isolated from every published lesson.
    Fixture f(backend,false,true,true,"es",lesson,true,0,true);
    visual(f.host,lesson.pages[0].blocks[0],artifacts,"memoria");
    visual(f.host,lesson.pages[1].blocks[0],artifacts,"flujo");
    check(lesson_page_state(f.view).can_next,"The example is being treated as a required puzzle");
    const auto first_fields=lesson.pages[0].blocks[0].concept_diagram->reading_fields();
    auto regions=lesson_reading_regions(f.view);
    std::vector<std::string> actual;
    for(const auto& region:regions)if(region.key.find("/concept-diagram/")!=std::string::npos){
        check(!region.is_exercise&&region.solved&&region.target,"Diagram region incorrectly participates in a puzzle");
        actual.push_back(region.source_text);
    }
    std::vector<std::string> expected;
    for(const auto& field:first_fields)expected.push_back(field.text);
    check(actual==expected,"The lesson reader changed authored diagram order or repeated a decorative label");
    f.page(1);
    check(!lesson_page_state(f.view).can_next,"A diagram bypassed the real adjacent unanswered puzzle");
    SendMessageW(child(f.view,1120),BM_CLICK,0,0);
    SendMessageW(child(f.view,1103),BM_CLICK,0,0);
    const auto& puzzle=lesson.pages[1].blocks[1];
    wait([&]{const auto response=state::get_response(read_lesson_state(f.view),lesson,puzzle);
        return response&&response->correct;},"real adjacent choice answer");
    f.reader->pause();
    const auto saved=state::get_response(read_lesson_state(f.view),lesson,puzzle);
    check(saved&&saved->correct&&lesson_page_state(f.view).can_next,"The real choice did not unlock the page");
    f.page(0);
    f.reader->toggle();
    wait([&]{const auto s=f.snapshot();return s.playing&&s.index<s.segments.size()&&
        s.segments[s.index].region.find("/concept-diagram/")!=std::string::npos&&s.offset>80;},
        "real diagram narration playback",180000);
    f.reader->pause();
    const auto paused=f.snapshot();
    check(!paused.playing&&!paused.allocated,"Diagram reader pause did not release playback");
    f.reader.reset();
    check(flush_lesson_view(f.view),"Cannot flush isolated diagram lesson");
    DestroyWindow(f.view);f.view=nullptr;
    f.view=create_lesson_view(f.host,f.environment,lesson);
    check(f.view!=nullptr,"Cannot reopen isolated diagram lesson");
    MoveWindow(f.view,0,0,980,720,TRUE);
    f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});
    f.ready();
    const auto restored=f.snapshot();
    const auto retained=state::get_response(read_lesson_state(f.view),lesson,puzzle);
    check(!restored.playing&&!restored.allocated&&restored.key==paused.key&&
          restored.index==paused.index&&restored.offset==paused.offset,
          "Diagram reading bookmark was not restored without autoplay");
    check(retained&&retained->correct&&retained->raw_json==saved->raw_json&&
          retained->selected==saved->selected,"Reopening the diagram altered the adjacent real response");
    check(lesson_page_state(f.view).can_next,"The restored diagram blocked navigation");
    std::cout<<"PASS full/progress loader rejection, exact native reading order, real choice gate, audio pause/reopen and response preservation\n";
}
} // namespace concept_diagram_reader_test
