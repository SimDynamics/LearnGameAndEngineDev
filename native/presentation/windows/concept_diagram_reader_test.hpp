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

// Measure the actual native RichEdit ink and the pixels painted by the
// existing owner-drawn STATIC. Do not toggle machine-wide accessibility or
// DPI settings; the test observes the palette and DPI of this QA window.
double contrast(COLORREF foreground,COLORREF background){
    const auto luminance=[](COLORREF value){
        const auto channel=[](BYTE byte){const double s=byte/255.;
            return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
        return .2126*channel(GetRValue(value))+.7152*channel(GetGValue(value))+.0722*channel(GetBValue(value));
    };
    const auto a=luminance(foreground),b=luminance(background);
    return (std::max(a,b)+.05)/(std::min(a,b)+.05);
}

void verify_text_ink(const MathReadingRegion& region){
    const auto native=wide(edit_text(region.target));
    RECT text_area{};SendMessageW(region.target,EM_GETRECT,0,reinterpret_cast<LPARAM>(&text_area));
    const auto surface=reader_theme::background(region.target);
    const auto palette=reader_theme::palette(region.target);
    for(std::size_t i=0;i<native.size();++i){
        if(std::iswspace(native[i])||IS_LOW_SURROGATE(native[i]))continue;
        POINTL point{};SendMessageW(region.target,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),i);
        check(point.x>=text_area.left&&point.x<text_area.right&&
              point.y>=text_area.top&&point.y<text_area.bottom,
              "An authored diagram character is outside its RichEdit viewport: "+region.key);
        const auto format=character_format(region.target,static_cast<LONG>(i));
        const auto ink=(format.dwEffects&CFE_AUTOCOLOR)?reader_theme::foreground(region.target):format.crTextColor;
        const auto background=(format.dwEffects&CFE_AUTOBACKCOLOR)?surface:format.crBackColor;
        if(palette.high_contrast)
            check(ink==palette.text&&background==surface,"Diagram text overrode system high-contrast ink or surface");
        else check(contrast(ink,background)>=4.5,"Diagram character lacks 4.5:1 contrast: "+region.key);
    }
}

void verify_arrow(HWND arrow,HWND view,bool horizontal){
    RECT area{};check(GetClientRect(arrow,&area)!=0&&area.right>0&&area.bottom>0,
                     "Diagram arrow has no drawable surface");
    const auto screen=GetDC(arrow);check(screen!=nullptr,"Cannot obtain arrow drawing context");
    const auto dc=CreateCompatibleDC(screen);BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;
    info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(arrow,screen);
    if(!dc||!bitmap||!pixels){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);
        throw std::runtime_error("Cannot allocate actual diagram arrow capture");}
    const auto previous=SelectObject(dc,bitmap);
    struct Cleanup{HDC dc;HBITMAP bitmap;HGDIOBJ previous;
        ~Cleanup(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}} cleanup{dc,bitmap,previous};
    FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_STATIC;draw.CtlID=static_cast<UINT>(GetDlgCtrlID(arrow));
    draw.itemAction=ODA_DRAWENTIRE;draw.hwndItem=arrow;draw.hDC=dc;draw.rcItem=area;
    check(SendMessageW(GetParent(arrow),WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw))!=0,
          "Native diagram container did not paint its actual arrow");
    GdiFlush();
    const auto palette=reader_theme::palette(view);
    const auto ink=palette.high_contrast?palette.text:palette.muted;
    const auto background=palette.card;
    if(!palette.high_contrast)check(contrast(ink,background)>=3.,"Diagram connection lacks 3:1 contrast");
    const auto dpi=static_cast<int>(GetDpiForWindow(arrow));
    const auto wing=std::max(1,MulDiv(2,dpi,96));
    const auto* colors=static_cast<const DWORD*>(pixels);
    const auto cx=area.right/2,cy=area.bottom/2;
    int toward=0,away=0,shaft=0;
    for(LONG y=0;y<area.bottom;++y)for(LONG x=0;x<area.right;++x){
        const auto pixel=colors[static_cast<std::size_t>(y)*static_cast<std::size_t>(area.right)+static_cast<std::size_t>(x)];
        if(RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255)!=ink)continue;
        const auto along=horizontal?x-cx:y-cy;
        const auto across=horizontal?y-cy:x-cx;
        if(std::abs(across)>wing){if(along>0)++toward;else if(along<0)++away;}
        else if(along<0)++shaft;
    }
    check(shaft>0&&toward>0&&away==0,
          "Diagram arrow lacks its shaft or points away from the next node");
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
                verify_text_ink(region);
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
            std::vector<HWND> arrows;
            EnumChildWindows(view.window(),[](HWND item,LPARAM data)->BOOL{
                if(code_scope_reader_test::window_class(item)==L"Static"&&
                    (GetWindowLongPtrW(item,GWL_STYLE)&SS_TYPEMASK)==SS_OWNERDRAW)
                    reinterpret_cast<std::vector<HWND>*>(data)->push_back(item);
                return TRUE;
            },reinterpret_cast<LPARAM>(&arrows));
            check(arrows.size()==block.concept_diagram->links.size(),"Missing or duplicate native diagram arrows");
            const bool horizontal=width==1120;
            std::ranges::sort(arrows,[&](HWND a,HWND b){
                const auto first=code_scope_reader_test::bounds_in(a,view.window());
                const auto second=code_scope_reader_test::bounds_in(b,view.window());
                return horizontal?first.left<second.left:first.top<second.top;
            });
            for(std::size_t i=0;i<arrows.size();++i){
                const auto source_key=block.concept_diagram->node_fields(i).front().key;
                const auto next_key=block.concept_diagram->node_fields(i+1).front().key;
                const auto source=std::ranges::find(regions,source_key,&MathReadingRegion::key);
                const auto next=std::ranges::find(regions,next_key,&MathReadingRegion::key);
                check(source!=regions.end()&&next!=regions.end(),"Connection lost an authored endpoint");
                const auto before=code_scope_reader_test::bounds_in(GetParent(source->target),view.window());
                const auto after=code_scope_reader_test::bounds_in(GetParent(next->target),view.window());
                const auto connection=code_scope_reader_test::bounds_in(arrows[i],view.window());
                check(horizontal?(connection.left>=before.right&&connection.right<=after.left):
                                 (connection.top>=before.bottom&&connection.bottom<=after.top),
                      "Native arrow does not lie between its consecutive authored nodes");
                verify_arrow(arrows[i],view.window(),horizontal);
            }
            check(JsonObject::Parse(wide(view.answers_json())).Size()==0,"Diagram created a response");
            if(scale==100)code_scope_reader_test::capture_png(view.window(),artifacts/
                (std::string(name)+"-"+std::to_string(width)+".png"));
        }
    }
    std::cout<<"PASS real diagram geometry, character bounds, contrast, directed arrows and read-only controls "
        <<name<<" at 75/100/150 percent, dpi="<<dpi<<" high_contrast="<<reader_theme::palette(parent).high_contrast<<"\n";
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
