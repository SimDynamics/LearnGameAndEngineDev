#pragma once
// Included beside the other focused reader tests, after the shared real-player
// helpers. This owns only a temporary profile and screenshots of native HWNDs.
namespace code_scope_reader_test {
constexpr std::string_view owner="u3l4_alcance_codigo_en_recuadros";
constexpr std::array<std::string_view,6> node_keys{"main","fichas","if","bolsas","salida","return"};
constexpr std::array<std::string_view,6> node_sources{"Cuerpo de main","const int fichas = 8;",
    "if (fichas >= 4)","const int bolsas = fichas / 4;","std::cout << bolsas;","return 0;"};

void reject_invalid_presentations(std::string_view source){
    using namespace winrt::Windows::Data::Json;
    const auto serialized=utf8(JsonObject::Parse(wide(source)).Stringify());
    // Prove the JSON serializer itself has not broken an existing curricular
    // contract before testing independent mutations of the new metadata.
    (void)windows::parse_lesson_json(serialized);(void)windows::parse_lesson_progress_metadata(serialized);
    const auto reject=[&](std::string_view name,const std::function<void(JsonObject)>& mutate,std::string_view reason){
        const auto root=JsonObject::Parse(wide(serialized));mutate(root.GetNamedObject(L"codeScopePresentations").GetNamedArray(L"entries").GetObjectAt(0));
        const auto bytes=utf8(root.Stringify());
        for(const bool metadata:{false,true}){bool rejected=false;
            try{if(metadata)(void)windows::parse_lesson_progress_metadata(bytes);else(void)windows::parse_lesson_json(bytes);}
            catch(const std::exception& error){rejected=std::string_view(error.what()).find(reason)!=std::string_view::npos;}
            check(rejected,"Invalid code-scope "+std::string(name)+(metadata?" survived the metadata loader":" survived the full loader"));
        }
    };
    reject("source fingerprint",[](JsonObject entry){entry.GetNamedObject(L"root").GetNamedArray(L"children").GetObjectAt(0)
        .Insert(L"sourceSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));},"Code scope source text fingerprint is stale");
    reject("foreign owner",[](JsonObject entry){entry.Insert(L"block",JsonValue::CreateStringValue(L"qa_foreign_scope_owner"));},"Code scope presentation requires an active composed puzzle owner");
    reject("pointer outside representation",[](JsonObject entry){entry.GetNamedObject(L"root").GetNamedArray(L"children").GetObjectAt(0)
        .Insert(L"source",JsonValue::CreateStringValue(L"/responses/0/options/0/label"));},"Code scope source field does not exist");
    reject("duplicate node",[](JsonObject entry){entry.GetNamedObject(L"root").GetNamedArray(L"children").GetObjectAt(1)
        .Insert(L"id",JsonValue::CreateStringValue(L"fichas"));},"Code scope node IDs must be unique stable identifiers");
    reject("multiline node",[](JsonObject entry){entry.GetNamedObject(L"root").Insert(L"title",JsonValue::CreateStringValue(L"Cuerpo de main\nSegunda línea"));},
        "Code scope titles and instructions must be complete nonblank physical lines");
    std::cout<<"PASS code-scope loader boundaries: 5 independent malformed presentations rejected by full and metadata loading\n";
}

std::wstring window_class(HWND window){wchar_t name[128]{};GetClassNameW(window,name,128);return name;}
std::size_t canvases(HWND parent){std::size_t result=0;EnumChildWindows(parent,[](HWND item,LPARAM data)->BOOL{
    if(window_class(item)==L"LearnGameAndEngineDev.MathCanvas")++*reinterpret_cast<std::size_t*>(data);return TRUE;
},reinterpret_cast<LPARAM>(&result));return result;}
RECT bounds_in(HWND child,HWND parent){RECT result{};check(GetWindowRect(child,&result)!=0,"Cannot measure native code-scope HWND");
    MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&result),2);return result;}

void capture_png(HWND window,const std::filesystem::path& destination){
    RECT area{};GetClientRect(window,&area);check(area.right>0&&area.bottom>0,"Cannot capture an empty code scope");
    const auto screen=GetDC(window),dc=CreateCompatibleDC(screen);BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(window,screen);
    check(dc&&bitmap&&pixels,"Cannot allocate the native code-scope capture");const auto old=SelectObject(dc,bitmap);
    struct Cleanup{HDC dc;HBITMAP bitmap;HGDIOBJ old;~Cleanup(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}} cleanup{dc,bitmap,old};
    FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    check(PrintWindow(window,dc,PW_CLIENTONLY)!=0,"PrintWindow failed for the real nested-code control");
    // Explicit child printing works for an off-screen hidden QA root as well;
    // the real RichEdit documents and container painters supply these pixels.
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    const auto stride=static_cast<UINT>(area.right)*4,bytes=stride*static_cast<UINT>(area.bottom);
    auto* values=static_cast<BYTE*>(pixels);for(UINT at=3;at<bytes;at+=4)values[at]=255;
    const auto colors=std::span(static_cast<const std::uint32_t*>(pixels),bytes/4);
    check(std::ranges::any_of(colors,[&](std::uint32_t value){return value!=colors.front();}),"Native code-scope capture is blank");
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())));
    winrt::check_hresult(factory->CreateStream(stream.GetAddressOf()));
    winrt::check_hresult(stream->InitializeFromFilename(destination.c_str(),GENERIC_WRITE));
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf()));
    winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    winrt::check_hresult(encoder->CreateNewFrame(frame.GetAddressOf(),nullptr));winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(area.right,area.bottom));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));check(IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA),"PNG encoder changed native capture format");
    winrt::check_hresult(frame->WritePixels(area.bottom,stride,bytes,values));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
}

std::vector<LessonReadingRegion> scope_regions(HWND view,std::size_t block){
    std::vector<LessonReadingRegion> result;for(const auto& region:lesson_reading_regions(view))
        if(region.block_index==block&&region.key.find("/code-scope/0/")!=std::string::npos)result.push_back(region);
    check(result.size()==7,"Nested code must expose exactly one description and six ordered source nodes");
    check(result.front().key.ends_with("/code-scope/0/description/source"),"The authentic grouping description must precede the code nodes");
    for(std::size_t i=0;i<node_keys.size();++i){const auto& region=result[i+1];
        check(region.key.ends_with(std::string("/code-scope/0/")+std::string(node_keys[i]))&&region.source_text==node_sources[i]
            &&edit_text(region.target)==node_sources[i],"Nested code source or its native reading order changed");
        check(region.math_mode=="text"&&region.text_semantics&&region.text_semantics->source==node_sources[i],
            "A literal code scope entered mathematical inference or lost explicit source semantics");
        const auto& spans=region.text_semantics->spans;
        if(i==0)check(spans.empty(),"The main caption stopped being ordinary prose");
        else check(spans.size()==1&&spans.front().start==0&&spans.front().end==wide(region.source_text).size()
            &&spans.front().text==node_sources[i]&&spans.front().role==curriculum::SemanticRole::cpp
            &&spans.front().reading==curriculum::SemanticReading::technical,"C++ scope nodes lost their complete technical spans");
    }
    return result;
}

void inspect_layout(HWND view,HWND host,const curriculum::Lesson& lesson,std::size_t block,
                    int width_dip,int height_dip,int scale,const std::filesystem::path& png){
    const auto dpi=GetDpiForWindow(host);check(dpi>0,"Windows returned no native DPI");
    const auto px=[&](int dip){return MulDiv(dip,static_cast<int>(dpi),96);};
    RECT outer{0,0,px(width_dip),px(height_dip)};
    check(AdjustWindowRectExForDpi(&outer,static_cast<DWORD>(GetWindowLongPtrW(host,GWL_STYLE)),FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(host,GWL_EXSTYLE)),dpi)!=0,"Cannot size the hidden native QA window");
    check(SetWindowPos(host,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=0,
        "Cannot resize the native QA window");
    RECT client{};GetClientRect(host,&client);check(client.right==px(width_dip)&&client.bottom==px(height_dip),"Native QA client has unexpected physical dimensions");
    const auto course=lesson.id.substr(0,lesson.id.find("__"));
    update_lesson_preferences(view,[&](state::StateData& data){appearance::save_course_text_scale(data,course,false,scale);});
    MoveWindow(view,0,0,client.right,client.bottom,TRUE);pump();
    const auto regions=scope_regions(view,block);const auto root=GetParent(regions[1].target),inner=GetParent(regions[3].target);
    check(window_class(root)==L"LearnGameAndEngineDev.MathContent"&&window_class(inner)==L"LearnGameAndEngineDev.MathContent"
        &&GetParent(inner)==root&&GetParent(regions[2].target)==root&&GetParent(regions[6].target)==root
        &&GetParent(regions[4].target)==inner&&GetParent(regions[5].target)==inner,
        "Native code HWNDs no longer express main/if containment without color");
    check(regions.front().target==root&&canvases(child(view,1005+static_cast<int>(block)*100))==0,
        "The explicit code scope retained a geometric canvas or detached its description");
    RECT root_area{};GetClientRect(root,&root_area);const auto in_view=bounds_in(root,view),inner_area=bounds_in(inner,root);
    check(in_view.left>=0&&in_view.right<=client.right,"The code scope extends beyond the minimum reader width");
    const auto first_outer=bounds_in(regions[2].target,root),last_outer=bounds_in(regions[6].target,root);
    check(first_outer.left==last_outer.left,"The instructions before and after if lost their common main-body alignment");
    if(width_dip==1536){RECT available{};GetClientRect(child(view,1005+static_cast<int>(block)*100),&available);
        check(root_area.right*2<available.right,"The short authored code scope still expands across the wide reader instead of fitting its content");
    }
    check(inner_area.left>0&&inner_area.top>0&&inner_area.right<root_area.right&&inner_area.bottom<root_area.bottom,
        "The inner code scope touches or escapes the outer border");
    LONG previous_bottom=0,total_text_height=0;
    for(std::size_t i=1;i<regions.size();++i){const auto& region=regions[i];const auto parent=GetParent(region.target);
        RECT parent_area{};GetClientRect(parent,&parent_area);const auto local=bounds_in(region.target,parent),absolute=bounds_in(region.target,root);
        check(local.left>0&&local.top>0&&local.right<parent_area.right&&local.bottom<parent_area.bottom,
            "A native scope source touches or clips its container border");
        check(absolute.top>=previous_bottom,"Nested code lines overlap or appear in another visual order");previous_bottom=absolute.bottom;
        total_text_height+=local.bottom-local.top;
        if(i>1){
            check(SendMessageW(region.target,EM_GETLINECOUNT,0,0)==1,"A requested code instruction is clipped or wraps in the required viewport");
            const auto format=character_format(region.target,0);check(std::wstring_view(format.szFaceName)==reader_theme::code_font_face(),
                "A code-scope line lost the code reader's monospace font");
            int numerator=0,denominator=0;SendMessageW(region.target,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            check((scale==100&&numerator==0&&denominator==0)||(denominator>0&&numerator*100==scale*denominator),
                "A nested source ignored the actual reader text-scale preference");
            const auto source=wide(region.source_text);POINTL last{},after{};SendMessageW(region.target,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&last),source.size()-1);
            SendMessageW(region.target,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&after),source.size());
            RECT text_area{};SendMessageW(region.target,EM_GETRECT,0,reinterpret_cast<LPARAM>(&text_area));
            check(last.x>=text_area.left&&last.x<text_area.right&&last.y>=text_area.top&&last.y<text_area.bottom&&after.x<=text_area.right,
                "The last character of a complete code instruction is outside its native text viewport");
        }
    }
    check(root_area.bottom<=total_text_height+px(160),"The code scope retained a large geometry-sized empty canvas");
    for(const auto container:{root,inner}){RECT area{};GetClientRect(container,&area);const auto screen=GetDC(container),dc=CreateCompatibleDC(screen);
        const auto bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);ReleaseDC(container,screen);check(dc&&bitmap,"Cannot measure native scope border ink");
        const auto old=SelectObject(dc,bitmap);SendMessageW(container,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        const auto border=GetPixel(dc,area.right/2,0);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        check(border==reader_theme::palette(container).outline,"Nested scope border no longer uses neutral reader outline ink");
    }
    capture_png(root,png);
    std::cout<<"CODE_SCOPE_LAYOUT logical="<<width_dip<<'x'<<height_dip<<" text_scale="<<scale<<" measured_dpi="<<dpi
        <<" client_pixels="<<client.right<<'x'<<client.bottom<<" scope_pixels="<<root_area.right<<'x'<<root_area.bottom
        <<" reference_4k_250_equivalent="<<(width_dip==1536)<<" physical_240dpi_observed="<<(dpi==240)
        <<" screenshot="<<png.string()<<" human_visual_acceptance=false\n";
}

void run(const std::shared_ptr<windows::NarrationWorker>& backend,const std::filesystem::path& baseline_path,
         const std::filesystem::path& artifacts){
    using namespace winrt::Windows::Data::Json;
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json";
    const auto original_bytes=state::read_bytes(asset),baseline_bytes=state::read_bytes(baseline_path);
    reject_invalid_presentations({reinterpret_cast<const char*>(original_bytes.data()),original_bytes.size()});
    const auto current=windows::load_lesson(asset),baseline=windows::load_lesson(baseline_path);
    const auto position=identity_position(current,owner),old_position=identity_position(baseline,owner);
    const auto& goal=current.pages[position.page].blocks[position.block];const auto& old_goal=baseline.pages[old_position.page].blocks[old_position.block];
    check(current.id==baseline.id&&position.page==1&&old_position.page==1&&old_position.block==13
        &&goal.address.page==7&&goal.address.block==30&&goal.address.page==old_goal.address.page&&goal.address.block==old_goal.address.block
        &&goal.address.addition_namespace==old_goal.address.addition_namespace&&goal.address.progress_page==old_goal.address.progress_page
        &&goal.raw_json==old_goal.raw_json&&curriculum::math_evaluation_source(goal)==curriculum::math_evaluation_source(old_goal)
        &&curriculum::exercise_key(current,goal)==curriculum::exercise_key(baseline,old_goal)
        &&state::response_storage_key(current,goal)==state::response_storage_key(baseline,old_goal)
        &&old_goal.code_scope_presentations.empty()&&goal.code_scope_presentations.size()==1,
        "Code-scope presentation changed the authentic owner, storage address, raw math contract or evaluation identity");
    const auto source=JsonObject::Parse(wide(goal.raw_json));const auto description=utf8(source.GetNamedArray(L"representations").GetObjectAt(0).GetNamedString(L"description"));
    std::filesystem::create_directories(artifacts);
    auto environment=resolve_windows_environment({ProfileId::Codex,L"code-scope-reader-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    check(!std::filesystem::exists(environment.paths.root),"Nested-code QA requires its own new profile");
    environment.paths.cache=std::filesystem::path(build::source_root)/"out/qa/lesson-reader-audio";initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);auto initial=store.load();state::prepare_lesson_state(initial,baseline);
    state::save_current_page(initial,baseline,0);initial.preferences["native_windows"]["ui_language"]=std::string("es");
    initial.preferences["code_scope_qa"]["opaque"]=std::string("Keep this unrelated value exactly.\n");
    auto& settings=initial.preferences["lesson_narration_settings"];settings["engine"]=std::string("KOKORO");settings["language"]=std::string("es");
    settings["kokoro_voice:es"]=std::string("ef_dora");settings["rate"]=3.;settings["kokoro_advance_generation"]=false;store.save(initial);
    const auto host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden native code-scope QA",
        WS_OVERLAPPEDWINDOW,0,0,1000,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host&&!IsWindowVisible(host),"Cannot create a hidden native code-scope host");HWND view=nullptr;std::unique_ptr<LessonReader> reader;
    struct Cleanup{HWND host;HWND& view;std::unique_ptr<LessonReader>& reader;
        ~Cleanup(){reader.reset();if(view){(void)flush_lesson_view(view);DestroyWindow(view);}DestroyWindow(host);}} cleanup{host,view,reader};
    const auto snapshot=[&]{auto result=reader->snapshot();check(result.error.empty(),"Real code-scope reader error: "+result.error);return result;};
    const auto ready=[&]{wait([&]{const auto value=snapshot();return value.planned&&!value.segments.empty()&&value.segments[value.index].available;},"actual code-scope current recording",180000);};
    const auto open=[&](const curriculum::Lesson& lesson,bool prepare_initial_entry=false){
        check(!view&&!reader,"A code-scope reopen must use a new ordinary view lifetime");
        const auto displayed=identity_position(lesson,owner);
        const auto& entry_goal=lesson.pages[displayed.page].blocks[displayed.block];
        LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(reader)reader->pause();};hooks.content_changed=[&](HWND){if(reader)reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(reader)reader->feedback(block,correct);};
        hooks.validate_math=[&](std::string_view raw,std::string_view answer,std::string_view locale,std::stop_token stop){return backend->validate_math(raw,answer,locale,stop);};
        hooks.math_editor_request=[&](std::string_view request){return backend->math_editor(request);};
        view=create_lesson_view(host,environment,lesson,std::move(hooks));check(view!=nullptr,"Cannot mount the complete authentic code-scope lesson");
        MoveWindow(view,0,0,980,720,TRUE);
        if(prepare_initial_entry){
            // Reach the target through the authentic first-page prerequisite.
            // Reopens never run this setup: their saved answer must suffice.
            const auto prerequisite_position=identity_position(lesson,"block_0000_0004");
            const auto& prerequisite=lesson.pages[prerequisite_position.page].blocks[prerequisite_position.block];
            check(prerequisite_position.page==0&&prerequisite_position.block==4
                &&prerequisite.type==curriculum::BlockType::multiple_choice&&prerequisite.correct_index
                &&*prerequisite.correct_index==0&&lesson_page_state(view).index==0
                &&!state::get_response(read_lesson_state(view),lesson,prerequisite),
                "The authentic initial code-scope prerequisite is absent, already answered or not on the first page");
            SendMessageW(child(view,1020+static_cast<int>(prerequisite_position.block)*100+
                static_cast<int>(*prerequisite.correct_index)*2),BM_CLICK,0,0);
            const auto prerequisite_check=child(view,1003+static_cast<int>(prerequisite_position.block)*100);
            check(IsWindowEnabled(prerequisite_check),"The actual first-page choice did not enable Check");
            SendMessageW(prerequisite_check,BM_CLICK,0,0);pump();
            const auto response=state::get_response(read_lesson_state(view),lesson,prerequisite);
            check(response&&response->correct&&response->selected==static_cast<std::int64_t>(*prerequisite.correct_index),
                "The authentic first-page choice was not checked through its native controls");
            check(navigate_lesson_view(view,displayed.page)&&lesson_page_state(view).index==displayed.page,
                "The checked first-page choice did not unlock the authentic code-scope page");
            std::cout<<"PASS code-scope initial prerequisite: native choice/Check and ordinary page navigation\n";
        }
        // Restored mathematics is canonically checked before the deferred
        // page entry is complete. Wait for that real UI state before starting
        // its reader, rather than planning a temporary page/feedback snapshot.
        wait([&]{if(lesson_page_state(view).index!=displayed.page)return false;
            const auto response=state::get_response(read_lesson_state(view),lesson,entry_goal);if(!response||!response->correct)return true;
            const auto visible=lesson_reading_regions(view);return std::ranges::any_of(visible,[&](const auto& item){return item.block_index==displayed.block&&item.solved;});
        },"canonical page entry for the complete code-scope lesson",180000);
        reader=std::make_unique<LessonReader>(view,environment,backend,[]{});ready();
    };
    const auto close=[&]{reader.reset();check(flush_lesson_view(view),"Cannot persist actual code-scope state");DestroyWindow(view);view=nullptr;};
    const auto selected=[&](const curriculum::Lesson& lesson){
        const auto displayed=identity_position(lesson,owner);const auto& item=lesson.pages[displayed.page].blocks[displayed.block];
        const auto response=state::get_response(read_lesson_state(view),lesson,item);
        const auto visible=lesson_reading_regions(view);
        check(response&&response->correct&&std::ranges::any_of(visible,[&](const auto& region){
            return region.block_index==displayed.block&&region.feedback&&region.correct&&region.solved;
        }),"The actual before-return answer lacks its canonically verified native feedback");return *response;};
    open(baseline,true);check(canvases(child(view,1005+static_cast<int>(old_position.block)*100))==1,
        "An ordinary Euclidean diagram without an overlay stopped using its existing renderer");
    auto regions=lesson_reading_regions(view);const auto option=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==old_position.block&&item.key.ends_with("/posicion/option/antes_de_return");});
    check(option!=regions.end(),"The authentic before-return response option is absent");
    SendMessageW(option->target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));SendMessageW(option->target,WM_LBUTTONUP,0,MAKELPARAM(4,4));pump();
    const auto action=child(view,1003+static_cast<int>(old_position.block)*100);check(IsWindowEnabled(action),"The real complete choice did not enable Check");
    SendMessageW(action,BM_CLICK,0,0);reader->pause();
    wait([&]{const auto response=state::get_response(read_lesson_state(view),baseline,old_goal);const auto value=snapshot();return response&&response->correct
        &&value.planned&&!value.segments.empty()&&value.segments[value.index].feedback
        &&value.segments[value.index].block==old_position.block&&value.segments[value.index].available;},"canonical validation of the original Euclidean answer",180000);
    const auto saved_response=selected(baseline);reader->toggle();wait([&]{const auto value=snapshot();return value.playing&&value.segments[value.index].feedback&&value.offset>180;},"real original-scope bookmark");
    reader->pause();const auto historical=snapshot();(void)verify_feedback_recording(historical.segments[historical.index]);
    const auto old_history=narration::read_history(read_lesson_state(view));const auto old_slot=std::ranges::find(old_history.entries,historical.key,&narration::Bookmark::key);
    check(old_slot!=old_history.entries.end()&&old_slot->semantic_plan&&old_slot->position.milliseconds==historical.offset,"Original code-scope playback did not persist its genuine bookmark");
    const auto page_slot=old_slot->page;close();open(current);
    wait([&]{const auto current_regions=scope_regions(view,position.block);return std::ranges::all_of(current_regions,[](const auto& item){return item.solved;});},"canonical verification of the pre-existing answer after adopting code scopes",180000);ready();
    const auto restored_answer=selected(current);check(restored_answer.raw_json==saved_response.raw_json&&restored_answer.reviewed_answer_identity==saved_response.reviewed_answer_identity,
        "Adding a presentation rewrote the user's existing answer or its reviewed identity");
    const auto migrated=snapshot();const auto adopted_history=narration::read_history(read_lesson_state(view));
    check(!migrated.playing&&!migrated.allocated&&!migrated.finished&&adopted_history.entries.size()==old_history.entries.size()
        &&std::ranges::count(adopted_history.entries,page_slot,&narration::Bookmark::page)==1
        &&std::ranges::find(adopted_history.entries,migrated.key,&narration::Bookmark::key)!=adopted_history.entries.end(),
        "The new presentation lost/duplicated the previous page bookmark or started playback");
    inspect_layout(view,host,current,position.block,1536,864,100,artifacts/"code-scope-4k-250-logical.png");
    inspect_layout(view,host,current,position.block,640,390,150,artifacts/"code-scope-minimum-150.png");
    ready();
    regions=scope_regions(view,position.block);check(regions.front().source_text==description,"Code-scope narration rewrote or duplicated its authentic grouping description");
    const auto planned=snapshot();std::size_t last_index=0,instructions=0;
    for(std::size_t node=0;node<node_keys.size();++node){const auto& region=regions[node+1];
        const auto expected=backend->plan(region.source_text,"es",{},region.reading_context,region.math_mode,windows::NarrationOutputDataPolicy::current,region.text_semantics);
        std::vector<std::size_t> indices;for(std::size_t i=0;i<planned.segments.size();++i)if(planned.segments[i].region==region.key)indices.push_back(i);
        check(!expected.empty()&&indices.size()==expected.size()&&(!node||indices.front()>last_index),"The real reader omitted or reordered a complete nested-code instruction");
        for(std::size_t i=0;i<indices.size();++i){const auto& have=planned.segments[indices[i]];const auto& want=expected[i];
            check(have.source==want.text&&have.spoken==want.spoken&&have.start==want.start&&have.end==want.end,"The actual scope reader ignored its typed semantic plan");
            if(node==1||node>=3){const auto audio=backend->prepare(want,"es","ef_dora",environment.paths.cache/L"kokoro",0);
                auto evidence=have;evidence.recording=audio.wav;evidence.duration_ms=audio.duration_ms;evidence.timings=audio.timings;
                (void)verify_feedback_recording(evidence);}
        }
        if(node==1||node>=3)++instructions;last_index=indices.back();
    }
    check(instructions==4,"The scope fixture did not cover all four exact authored instructions");
    // A current source-bound pause must reopen exactly; unlike a historical
    // plan transition it has no newly inserted region requiring new listening.
    reader->toggle();wait([&]{const auto value=snapshot();return value.playing&&value.offset>180;},"real current-presentation playback");reader->pause();
    const auto paused=snapshot();(void)verify_feedback_recording(paused.segments[paused.index]);const auto history=narration::read_history(read_lesson_state(view));
    close();open(current);wait([&]{const auto value=snapshot();return value.key==paused.key&&value.index==paused.index&&value.offset==paused.offset;},"exact current code-scope bookmark restoration",180000);
    const auto reopened=snapshot();check(!reopened.playing&&!reopened.allocated&&reopened.heard_through==paused.heard_through
        &&narration::read_history(read_lesson_state(view))==history&&selected(current).raw_json==saved_response.raw_json,
        "Current code-scope reopen lost the exact pause, listening boundary or preserved answer");
    const auto until=GetTickCount64()+150;wait([&]{const auto value=snapshot();check(!value.playing&&!value.allocated&&value.offset==paused.offset,"Code-scope reopen autoplays");return GetTickCount64()>=until;},"code-scope reopen remains paused",2000);
    check(state::string_value(read_lesson_state(view),"code_scope_qa","opaque")=="Keep this unrelated value exactly.\n"
        &&state::read_bytes(asset)==original_bytes&&state::read_bytes(baseline_path)==baseline_bytes,"Code-scope QA changed curriculum or unrelated profile state");
    std::cout<<"PASS native code scopes: authentic baseline/final raw math and owner 7,30 unchanged; ordinary geometry retained; description plus six ordered native nodes/four complete CPP instructions and real PCM; 1536x864 logical reference and 640x390 at 150% measured; genuine original answer and page bookmark migrate, current exact pause reopens without autoplay. Physical 250% only if logged DPI 240; no human visual/listening acceptance claimed.\n";
}
} // namespace code_scope_reader_test
