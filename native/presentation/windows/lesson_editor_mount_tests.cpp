#include "core/lesson.hpp"
#include "core/profile.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& value=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=value.id)return TRUE;value.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));return search.found;}
HWND lesson_pane(HWND view){struct Search{HWND found=nullptr;} search;
    EnumChildWindows(view,[](HWND item,LPARAM raw)->BOOL{wchar_t name[128]{};GetClassNameW(item,name,128);
        if(std::wstring_view(name)!=L"LearnGameAndEngineDev.LessonPane"||!GetWindowTextLengthW(item))return TRUE;
        reinterpret_cast<Search*>(raw)->found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));
    check(search.found!=nullptr,"The actual lesson viewport is missing");return search.found;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::uint32_t dib_color(COLORREF color){return (static_cast<std::uint32_t>(GetRValue(color))<<16)
    |(static_cast<std::uint32_t>(GetGValue(color))<<8)|GetBValue(color);}
struct Raster{
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
    int width=0,height=0;
    explicit Raster(HWND window){RECT client{};GetClientRect(window,&client);width=client.right;height=client.bottom;
        check(width>0&&height>0,"The actual editor has no client pixels");
        const auto screen=GetDC(nullptr);dc=CreateCompatibleDC(screen);ReleaseDC(nullptr,screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        check(dc&&bitmap&&pixels,"Cannot create the owned editor raster");previous=SelectObject(dc,bitmap);
        const auto fill=CreateSolidBrush(reader_theme::background(window));FillRect(dc,&client,fill);DeleteObject(fill);
    }
    ~Raster(){if(dc&&previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    std::uint32_t at(int x,int y)const{return pixels[static_cast<std::size_t>(y)*width+x]&0x00ffffffu;}
    void render(HWND window,bool native){
        if(native)CallWindowProcW(reinterpret_cast<WNDPROC>(GetClassLongPtrW(window,GCLP_WNDPROC)),window,WM_PRINTCLIENT,
            reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        else SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        GdiFlush();
    }
};
struct View{
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    int outer_width=902,outer_height=750;
    View(curriculum::Lesson model,const state::StateData& initial,bool logical_window=false):environment(resolve_windows_environment({ProfileId::Codex,
        L"lesson-editor-mount-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"The editor mount QA profile must be new");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);
        if(logical_window){const auto dpi=GetDpiForSystem();outer_width=MulDiv(902,dpi,96);outer_height=MulDiv(750,dpi,96);}
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden real U11L9 editor mount QA",
            WS_OVERLAPPEDWINDOW,30,30,outer_width,outer_height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the isolated editor mount");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create the actual U11L9 lesson");
        RECT client{};GetClientRect(host,&client);MoveWindow(view,0,0,client.right,client.bottom,TRUE);pump();
        RECT outer{};GetWindowRect(host,&outer);check(outer.right-outer.left==outer_width&&outer.bottom-outer.top==outer_height,
            "The real mount does not retain the reported 902 by 750 window");
        check(!IsWindowVisible(host),"The isolated mount must not expose a desktop window");}
    void reopen(){check(flush_lesson_view(view),"Cannot persist the editor mount");DestroyWindow(view);view=nullptr;open();}
    HWND control(std::size_t block,int part){const auto value=descendant(view,control_id(block,part));check(value!=nullptr,"A mounted lesson control is missing");return value;}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush the owned editor state");return read_lesson_state(view);}
    void reveal(HWND source){const auto pane=lesson_pane(view);
        for(unsigned attempt=0;attempt<100;++attempt){
            RECT editor{},viewport{},client{},pane_client{},mapped{},visible{};GetWindowRect(source,&editor);GetWindowRect(pane,&viewport);
            GetClientRect(source,&client);GetClientRect(pane,&pane_client);mapped=pane_client;
            SetLastError(ERROR_SUCCESS);const auto result=MapWindowPoints(pane,source,reinterpret_cast<POINT*>(&mapped),2);
            check(result||GetLastError()==ERROR_SUCCESS,"Cannot map the physical editor/pane mounting rectangles");
            // At 225% DPI this unchanged 320dp editor is taller than the
            // 902x750 lesson viewport. Reach a broad real portion instead of
            // oscillating while demanding that an oversized child fit whole.
            const auto minimum_height=std::max(1L,std::min(client.bottom,pane_client.bottom)*3/4);
            const auto minimum_width=std::max(1L,std::min(client.right,pane_client.right)*3/4);
            if(IntersectRect(&visible,&client,&mapped)&&visible.bottom-visible.top>=minimum_height&&visible.right-visible.left>=minimum_width)return;
            SendMessageW(view,WM_VSCROLL,editor.top<viewport.top?SB_LINEUP:SB_LINEDOWN,0);pump();
        }
        RECT editor{},viewport{};GetWindowRect(source,&editor);GetWindowRect(pane,&viewport);
        SCROLLINFO range{sizeof(range),SIF_ALL};GetScrollInfo(pane,SB_VERT,&range);
        throw std::runtime_error("The U11L9 editor has no broad reachable viewport: source-y="+std::to_string(editor.top)+","+std::to_string(editor.bottom)
            +" pane-y="+std::to_string(viewport.top)+","+std::to_string(viewport.bottom)+" scroll="+std::to_string(range.nPos)
            +" dpi="+std::to_string(GetDpiForWindow(source)));
    }
};
void seed_other_objectives(state::StateData& data,const curriculum::Lesson& lesson,std::string_view target){
    // Synthetic receipts isolate this mounting regression only. The target
    // stays pending; this test claims no execution of a curricular solution.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check&&block.stable_id!=target){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
            for(const auto& item:block.output_check->cases){curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
                execution.executable=item.executable;if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA prerequisite diagnostic";
                (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"QA prerequisite proof");}
        }
}
void prepare_visual_profile(std::string_view scenario,std::wstring_view token){
    check((scenario=="u1"||scenario=="u11")&&is_valid_qa_instance(token),"Invalid visual QA scenario or unsafe instance token");
    const auto relative=scenario=="u1"
        ?"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json"
        :"texto_interpretacion_y_flujos/lesson_un_lector_con_buffer_de_un_token.json";
    const std::string identity=scenario=="u1"?"block_0006_0002":"block_0004_0003";
    const std::size_t page=scenario=="u1"?6:4;
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/relative;
    const auto asset_hash=state::sha256(state::read_bytes(path));
    const auto lesson=windows::load_lesson(path);
    const auto position=curriculum::stable_block_position(lesson,identity);
    check(position&&position->page==page,"The selected visual QA editor moved outside its audited page");
    const auto& target=lesson.pages[position->page].blocks[position->block];
    check(target.type==curriculum::BlockType::cpp_compiler&&target.output_check,
        "The selected visual QA editor lost its published output contract");
    const auto environment=resolve_windows_environment({ProfileId::Codex,std::wstring(token),false});
    check(environment.paths.root.parent_path().filename()==L"qa"
        &&environment.paths.root.parent_path().parent_path().filename()==L"Codex"
        &&environment.paths.root.filename()==token,"Visual QA resolved outside the selected isolated Codex profile");
    state::require_safe_path(environment.paths.root);
    check(!std::filesystem::exists(environment.paths.root),"Visual QA refuses to reuse or overwrite an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    check(std::filesystem::create_directory(environment.paths.root),"Another process already created this visual QA profile");
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);const auto empty=store.load();
    check(empty.preferences.empty(),"The newly reserved visual QA profile unexpectedly contains prior state");
    state::StateData data;state::prepare_lesson_state(data,lesson);
    // Explicit synthetic compiler prerequisites from this lesson only. No
    // quiz response is seeded, and the selected editor stays uncredited.
    seed_other_objectives(data,lesson,identity);state::save_current_page(data,lesson,page);
    auto& preferences=data.preferences["native_windows"];
    preferences["ui_language"]=std::string("es");preferences["content_language"]=std::string("es");
    preferences["catalog/locale"]=std::string("es");preferences["catalog/route"]=std::string("programming");
    preferences["catalog/course"]=std::string("cpp_fundamentals");
    const auto unit_end=lesson.id.rfind("__");check(unit_end!=std::string::npos,"The selected lesson has no catalog unit identity");
    preferences["catalog/unit"]=lesson.id.substr(0,unit_end);preferences["navigation/lesson"]=lesson.id;
    state::Draft draft;draft.source=target.initial_code;draft.input=target.standard_input.value_or("");
    const auto key=curriculum::draft_key("programming",lesson,target);state::put_draft(data,key,draft);
    check(!windows::compiler_puzzle_completed(data,lesson,target)&&!state::get_response(data,lesson,target),
        "Preparing visual QA credited the selected compiler or invented a historical response");
    state::validate_lesson_state(data,lesson);store.save(data);
    const auto saved=store.load();const auto restored=state::get_draft(saved,key,"");
    check(saved==data&&state::current_page(saved,lesson)==page&&restored.source==target.initial_code
        &&restored.input==target.standard_input.value_or("")&&!windows::compiler_puzzle_completed(saved,lesson,target),
        "The prepared visual QA profile did not preserve the pending target and its original draft");
    check(state::sha256(state::read_bytes(path))==asset_hash,"Preparing visual QA changed an authored asset");
    std::cout<<"profile=Codex\nqa_instance="<<utf8(token)<<"\ndata_root="<<utf8(environment.paths.root.native())
        <<"\nlesson="<<lesson.id<<"\ntarget="<<identity<<"\npage="<<page+1<<"\npage_index="<<page
        <<"\nblock="<<position->block+1<<"\nsynthetic_prerequisites=other-compiler-objectives-in-this-lesson-only"
        <<"\nquiz_responses=none\ntarget_completed=0\nwindows_created=0\n";
}
RECT visible_source_rect(HWND editor,HWND pane){
    RECT client{},viewport{},visible{};GetClientRect(editor,&client);GetClientRect(pane,&viewport);
    SetLastError(ERROR_SUCCESS);
    const auto mapped=MapWindowPoints(pane,editor,reinterpret_cast<POINT*>(&viewport),2);
    check(mapped||GetLastError()==ERROR_SUCCESS,"Cannot map the actual lesson viewport to source-client coordinates");
    check(IntersectRect(&visible,&client,&viewport),"The actual source client has no physically reachable lesson pixels");return visible;
}
struct Verification{std::size_t frames=0;std::vector<bool> seen=std::vector<bool>(25);};
void verify_native_frame(View& fixture,HWND editor,std::string_view original,CHARRANGE selection,Verification& verification){
    check(edit_text(editor)==original,"Scrolling or drawing changed an original U11L9 source character");
    CHARRANGE current{};SendMessageW(editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&current));
    check(current.cpMin==selection.cpMin&&current.cpMax==selection.cpMax,"Scrolling or drawing changed source selection/caret");
    RECT client{},format{};GetClientRect(editor,&client);SendMessageW(editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    const auto visible=visible_source_rect(editor,lesson_pane(fixture.view));
    check(format.top==0&&format.bottom==client.bottom,"The mounted source retained permanent vertical formatting clipping");
    const auto snapshot=windows::code_gutter_snapshot(editor);check(snapshot.active&&snapshot.width>0,"The actual mounted source lost its fixed number band");
    const auto count=SendMessageW(editor,EM_GETLINECOUNT,0,0);check(count>24,"The authored source no longer covers the reported lines");
    std::vector<windows::CodeGutterLine> expected;
    for(LRESULT line=0;line<count;++line){const auto character=SendMessageW(editor,EM_LINEINDEX,line,0);check(character>=0,"Native source line has no character owner");
        POINTL point{};SendMessageW(editor,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),character);
        const auto neighbour=SendMessageW(editor,EM_LINEINDEX,line+1<count?line+1:line-1,0);POINTL adjacent{};
        SendMessageW(editor,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&adjacent),neighbour);
        const auto height=std::abs(adjacent.y-point.y);check(height>0,"Native rows have no physical height");
        if(point.y<client.bottom&&point.y+height>0)expected.push_back({static_cast<std::size_t>(line)+1,static_cast<LONG>(character),point});
    }
    check(snapshot.logical_lines==static_cast<std::size_t>(count)&&snapshot.visible.size()==expected.size(),
        "The fixed band omitted a native visible or partially visible source row");
    for(std::size_t index=0;index<expected.size();++index){const auto& actual=snapshot.visible[index];const auto& line=expected[index];
        check(actual.number==line.number&&actual.character==line.character&&actual.text_position.y==line.text_position.y,
            "The mounted gutter is one viewport behind the native source rows");
        const auto following=SendMessageW(editor,EM_LINEINDEX,static_cast<WPARAM>(line.number),0);POINTL below{};
        if(following>=0){SendMessageW(editor,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&below),following);
            if(line.number>=14&&line.number<=24&&line.text_position.y<visible.bottom&&below.y>visible.top)
                verification.seen[line.number]=true;}
    }
    Raster native(editor),composed(editor);native.render(editor,true);composed.render(editor,false);
    int numerator=0,denominator=0;SendMessageW(editor,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
    if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
    const auto left=std::max<LONG>(snapshot.width,static_cast<LONG>(static_cast<long long>(format.left)*numerator/denominator));
    const auto source_background=dib_color(reader_theme::background(editor));std::size_t ink=0,different=0;
    for(int y=visible.top;y<visible.bottom;++y)for(int x=std::max<LONG>(left,visible.left);x<visible.right;++x){
        const auto wanted=native.at(x,y);ink+=wanted!=source_background;different+=composed.at(x,y)!=wanted;
    }
    check(different==0,"Mounted composition erased or altered a legitimate native source pixel");
    check(ink>0,"The mounted raster comparison did not contain actual source glyphs");
    const auto band_background=dib_color(RGB(20,20,20));
    for(const auto& line:expected){const auto next=SendMessageW(editor,EM_LINEINDEX,static_cast<WPARAM>(line.number),0);
        if(next<0)continue;POINTL below{};SendMessageW(editor,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&below),next);
        if(line.text_position.y<visible.top||below.y>visible.bottom||visible.left>0||visible.right<snapshot.width)continue;
        bool number_ink=false;
        for(int y=line.text_position.y;y<below.y&&!number_ink;++y)
            for(int x=0;x<snapshot.width;++x)if(composed.at(x,y)!=band_background){number_ink=true;break;}
        check(number_ink,"A complete visible native row lost its drawn line number");
    }
    ++verification.frames;
}
struct CaptionMetrics{
    RECT client{},format{};
    POINT scroll{};
    POINTL first{};
    CHARFORMAT2W font{};
    int numerator=0,denominator=0;
    LRESULT lines=0;
};
CaptionMetrics caption_metrics(HWND caption,std::string_view phase){
    CaptionMetrics value;GetClientRect(caption,&value.client);
    SendMessageW(caption,EM_GETRECT,0,reinterpret_cast<LPARAM>(&value.format));
    SendMessageW(caption,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&value.scroll));
    SendMessageW(caption,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&value.first),0);
    value.font.cbSize=sizeof(value.font);
    SendMessageW(caption,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&value.font));
    SendMessageW(caption,EM_GETZOOM,reinterpret_cast<WPARAM>(&value.numerator),reinterpret_cast<LPARAM>(&value.denominator));
    value.lines=SendMessageW(caption,EM_GETLINECOUNT,0,0);
    RECT screen{};GetWindowRect(caption,&screen);
    std::cout<<"CAPTION "<<phase<<" id="<<GetDlgCtrlID(caption)<<" dpi="<<GetDpiForWindow(caption)
        <<" client="<<value.client.left<<','<<value.client.top<<','<<value.client.right<<','<<value.client.bottom
        <<" format="<<value.format.left<<','<<value.format.top<<','<<value.format.right<<','<<value.format.bottom
        <<" scroll="<<value.scroll.x<<','<<value.scroll.y<<" char0="<<value.first.x<<','<<value.first.y
        <<" yHeight="<<value.font.yHeight<<" face="<<utf8(value.font.szFaceName)
        <<" bold="<<bool(value.font.dwEffects&CFE_BOLD)<<" zoom="<<value.numerator<<'/'<<value.denominator
        <<" lines="<<value.lines<<" screen="<<screen.left<<','<<screen.top<<','<<screen.right<<','<<screen.bottom<<'\n';
    return value;
}
struct CaptionPixels{
    RECT ink{};
    std::size_t count=0,native_differences=0;
    std::vector<std::uint32_t> shape;
};
CaptionPixels caption_pixels(HWND caption,std::string_view phase){
    Raster composed(caption),native(caption);composed.render(caption,false);native.render(caption,true);
    CaptionPixels result;result.ink={composed.width,composed.height,0,0};
    const auto background=dib_color(reader_theme::background(caption));
    for(int y=0;y<composed.height;++y)for(int x=0;x<composed.width;++x){
        const auto pixel=composed.at(x,y);
        result.native_differences+=pixel!=native.at(x,y);
        if(pixel==background)continue;
        ++result.count;result.ink.left=std::min<LONG>(result.ink.left,x);result.ink.top=std::min<LONG>(result.ink.top,y);
        result.ink.right=std::max<LONG>(result.ink.right,x+1);result.ink.bottom=std::max<LONG>(result.ink.bottom,y+1);
    }
    if(result.count)for(int y=result.ink.top;y<result.ink.bottom;++y)for(int x=result.ink.left;x<result.ink.right;++x)
        result.shape.push_back(composed.at(x,y));
    std::cout<<"CAPTION_PIXELS "<<phase<<" id="<<GetDlgCtrlID(caption)<<" ink="<<result.count
        <<" bounds="<<result.ink.left<<','<<result.ink.top<<','<<result.ink.right<<','<<result.ink.bottom
        <<" native_differences="<<result.native_differences<<'\n';
    return result;
}
bool same_caption_ink(const CaptionPixels& first,const CaptionPixels& second){
    return first.count>0&&second.count>0&&first.ink.right-first.ink.left==second.ink.right-second.ink.left&&
        first.ink.bottom-first.ink.top==second.ink.bottom-second.ink.top&&first.shape==second.shape;
}
struct CaptionChange{HWND answer=nullptr;unsigned notifications=0;};
LRESULT CALLBACK caption_change_probe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
    auto& probe=*reinterpret_cast<CaptionChange*>(raw);
    if(message==WM_COMMAND&&HIWORD(wp)==EN_CHANGE&&reinterpret_cast<HWND>(lp)==probe.answer)++probe.notifications;
    return DefSubclassProc(window,message,wp,lp);
}
void authored_caption_first(){
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json";
    const auto digest=state::sha256(state::read_bytes(path));const auto lesson=windows::load_lesson(path);
    const auto position=curriculum::stable_block_position(lesson,"block_0006_0003");
    check(position&&position->page==6&&position->block==1,"The current U1L3 prediction moved outside page7 block2");
    const auto& prediction=lesson.pages[position->page].blocks[position->block];
    check(prediction.type==curriculum::BlockType::output_prediction&&prediction.id=="prediccion_informe_inicial",
        "The reported toolbar is no longer the current output prediction");
    state::StateData data;state::prepare_lesson_state(data,lesson);
    // Same prerequisite isolation as visual U1 QA: the current page's compiler
    // and the prediction remain pending, with no fabricated quiz responses.
    seed_other_objectives(data,lesson,"block_0006_0002");state::save_current_page(data,lesson,position->page);
    data.preferences["native_windows"]["ui_language"]=std::string("es");
    check(!state::get_response(data,lesson,prediction),"Caption QA must start without a target response");
    View fixture(lesson,data,true);
    check(lesson_page_state(fixture.view).index==6,"The initial mount did not restore the actual seventh page");
    const auto readonly=fixture.control(1,16),response=fixture.control(1,38),answer=fixture.control(1,2);
    const auto source=fixture.control(1,1);const auto source_text=edit_text(source);
    check(edit_text(readonly)==edit_text(response)&&edit_text(readonly)=="</> C++",
        "The two reported captions no longer have identical authored labels");
    check(edit_text(answer).empty(),"Caption QA seeded a response before the first frame");
    check(!reader_theme::palette(readonly).high_contrast,"The reported caption requires the normal-contrast reader surface");
    std::cout<<"CAPTION_FIRST dpi="<<GetDpiForWindow(fixture.host)<<" logical_outer=902,750 pixel_outer="<<fixture.outer_width<<','<<fixture.outer_height
        <<" root="<<fixture.environment.paths.root.string()<<" profile=Codex/qa lesson="<<lesson.id<<" page=7 block=2\n";
    // Query the untouched first mounted state before any paint probe. No
    // reveal, scroll, resize, format rectangle, font or zoom setter intervenes.
    const auto initial_readonly=caption_metrics(readonly,"initial");
    const auto initial_response=caption_metrics(response,"initial");
    const auto initial_readonly_ink=caption_pixels(readonly,"initial");
    const auto initial_response_ink=caption_pixels(response,"initial");
    (void)caption_metrics(readonly,"after-first-paint");
    (void)caption_metrics(response,"after-first-paint");
    CaptionChange probe{answer};const auto parent=GetParent(answer);
    check(SetWindowSubclass(parent,caption_change_probe,0x43415054,reinterpret_cast<DWORD_PTR>(&probe)),
        "Cannot observe the actual native answer-change notification");
    const std::wstring draft=L"x";
    SendMessageW(answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(draft.c_str()));pump();
    RemoveWindowSubclass(parent,caption_change_probe,0x43415054);
    const auto after_readonly=caption_metrics(readonly,"after-answer-EN_CHANGE");
    const auto after_response=caption_metrics(response,"after-answer-EN_CHANGE");
    const auto after_readonly_ink=caption_pixels(readonly,"after-answer-EN_CHANGE");
    const auto after_response_ink=caption_pixels(response,"after-answer-EN_CHANGE");
    const auto saved=fixture.saved();const auto stored=state::get_response(saved,lesson,prediction);
    check(probe.notifications>0&&edit_text(answer)=="x"&&stored&&stored->answer=="x"&&!stored->correct&&
        !state::response_reviewed(*stored,prediction),"The focal input did not traverse EN_CHANGE as an unreviewed draft");
    check(edit_text(source)==source_text,"Typing the answer changed the independent readonly source");
    check(edit_text(readonly)=="</> C++"&&edit_text(response)=="</> C++","Typing changed a static caption's text");
    check(state::sha256(state::read_bytes(path))==digest,"Caption QA changed authored lesson bytes");
    unsigned failures=0;
    const auto expect=[&](bool condition,std::string_view message){if(!condition){++failures;std::cout<<"FAIL "<<message<<'\n';}};
    expect(initial_readonly.format.bottom==initial_readonly.client.bottom&&
        initial_response.format.bottom==initial_response.client.bottom,
        "An initial caption's format rectangle stops before its actual client bottom");
    expect(initial_readonly.scroll.x==0&&initial_readonly.scroll.y==0&&
        initial_response.scroll.x==0&&initial_response.scroll.y==0,
        "An initial caption has an unexpected native scroll offset");
    expect(initial_readonly.lines==1&&initial_response.lines==1,
        "An initial caption does not retain its single native text line");
    const auto same_font=[](const CaptionMetrics& first,const CaptionMetrics& second){
        return first.font.yHeight==second.font.yHeight&&std::wstring_view(first.font.szFaceName)==second.font.szFaceName&&
            (first.font.dwEffects&(CFE_BOLD|CFE_ITALIC))==(second.font.dwEffects&(CFE_BOLD|CFE_ITALIC))&&
            first.numerator==second.numerator&&first.denominator==second.denominator;
    };
    expect(same_font(initial_readonly,initial_response),"The identical initial captions differ in font or zoom");
    if(same_font(initial_readonly,initial_response)&&initial_readonly.lines==1&&initial_response.lines==1)
        expect(same_caption_ink(initial_readonly_ink,initial_response_ink),
            "The initial readonly caption loses glyph pixels compared with the identical response caption");
    expect(same_font(initial_readonly,after_readonly)&&same_font(initial_response,after_response),
        "An unrelated answer edit changed a static caption's font or zoom");
    expect(same_caption_ink(initial_readonly_ink,after_readonly_ink),
        "An unrelated answer edit changed the readonly caption's intrinsic painted glyphs");
    expect(same_caption_ink(initial_response_ink,after_response_ink),
        "An unrelated answer edit changed the response caption's intrinsic painted glyphs");
    std::cout<<"CAPTION_FIRST observed_EN_CHANGE="<<probe.notifications<<" failures="<<failures
        <<" windows_visible="<<IsWindowVisible(fixture.host)<<'\n';
    check(failures==0,"The reported first-frame caption differs from its identical peer or changes after unrelated input; inspect the recorded native metrics and pixel bounds");
    std::cout<<"PASS current U1L3 first captions: identical native glyphs before/after answer EN_CHANGE, raw metrics and offscreen paint; no desktop acceptance\n";
}
void authored_prediction_newline(){
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_representar_procesar_y_recuperar.json";
    const auto source_hash=state::sha256(state::read_bytes(path));
    const auto lesson=windows::load_lesson(path);
    check(lesson.pages.size()==11 && lesson.pages[1].blocks.size()>=2,
        "The reported U2L10 page 2/11 no longer exists");
    const auto& prediction=lesson.pages[1].blocks[1];
    check(prediction.type==curriculum::BlockType::output_prediction &&
        prediction.code.find("al_inicio")!=std::string::npos,
        "The reported multiline output prediction changed identity");
    state::StateData data;state::prepare_lesson_state(data,lesson);
    state::save_current_page(data,lesson,1);
    data.preferences["native_windows"]["ui_language"]=std::string("es");
    data.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{150};
    View fixture(lesson,data,true);
    check(lesson_page_state(fixture.view).index==1,"The multiline output prediction was not mounted on page 2");
    const auto answer=fixture.control(1,2);
    check(edit_text(answer).empty(),"The regression must begin with an empty answer");
    fixture.reveal(answer);
    SetFocus(answer);
    const auto type=[&](const wchar_t* value){
        SendMessageW(answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value));pump();
    };
    const auto visible_lines=[&](const char* stage){
        RECT format{},client{};GetClientRect(answer,&client);
        SendMessageW(answer,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        POINT scroll{};SendMessageW(answer,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        POINTL first{},second{};
        SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
        const auto second_index=SendMessageW(answer,EM_LINEINDEX,1,0);
        check(second_index>=0,"Enter did not create a second RichEdit line");
        SendMessageW(answer,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second),second_index);
        const auto line_height=second.y-first.y;
        std::cout<<"PREDICTION "<<stage<<" client="<<client.bottom<<" format="<<format.top<<','<<format.bottom
            <<" first="<<first.y<<" second="<<second.y<<" line_height="<<line_height
            <<" scroll="<<scroll.y<<'\n';
        check(scroll.y==0 && first.y>=format.top-2 && first.y<format.bottom,
            "Pressing Enter hid the first written answer line");
        check(line_height>0 && second.y+line_height<=format.bottom+3,
            "The multiline output field did not grow to show its second line");
    };
    type(L"Inicio: 8");
    MSG enter{};enter.hwnd=answer;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;enter.lParam=1;
    check(windows::CodeEditor::handle_key_message(enter),
        "The original editor did not handle physical Enter");
    enter.message=WM_KEYUP;(void)windows::CodeEditor::handle_key_message(enter);
    pump();visible_lines("after-enter");
    type(L"Ahora: 5");visible_lines("after-second-line");
    const auto saved=fixture.saved();
    const auto response=state::get_response(saved,lesson,prediction);
    check(response && response->answer=="Inicio: 8\nAhora: 5" && !response->correct,
        "Enter changed or silently completed the saved multiline answer");
    check(state::sha256(state::read_bytes(path))==source_hash,
        "The viewport regression changed curricular source data");
    std::cout<<"PASS U2L10 page 2 native multiline prediction: first and second lines visible after Enter, exact answer saved\n";
}
void authored_editor_mount(){
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_un_lector_con_buffer_de_un_token.json";
    const auto original_hash=state::sha256(state::read_bytes(path));const auto lesson=windows::load_lesson(path);
    const auto position=curriculum::stable_block_position(lesson,"block_0004_0003");check(position&&position->page==4&&position->block==3,"The reported U11L9 editor moved unexpectedly");
    const auto& target=lesson.pages[position->page].blocks[position->block];const auto original=target.initial_code;
    check(!reader_theme::palette().high_contrast,"The reported dark-theme regression requires a normal-contrast Windows test surface");
    state::StateData data;state::prepare_lesson_state(data,lesson);seed_other_objectives(data,lesson,target.stable_id);
    data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["code_wrap_settings"]["global_wrap"]=false;
    data.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{100};
    data.preferences["editor_mount_opaque"]["keep"]=std::string(" \t original metadata\r\n");
    state::Draft draft;draft.source=original;draft.input=" \t retained hidden input\r\n";draft.selection_start=5;draft.selection_end=9;
    const auto draft_key=curriculum::draft_key("programming",lesson,target);state::put_draft(data,draft_key,draft);state::save_current_page(data,lesson,4);
    View fixture(lesson,data);check(lesson_page_state(fixture.view).index==4,"The actual mounted bookmark did not restore page five");
    const auto& choice=lesson.pages[4].blocks[2];check(choice.type==curriculum::BlockType::multiple_choice&&choice.correct_index,"The preceding selection changed type");
    SendMessageW(fixture.control(2,20+static_cast<int>(*choice.correct_index)*2),BM_CLICK,0,0);pump();
    SendMessageW(fixture.control(2,3),BM_CLICK,0,0);pump();check(edit_text(fixture.control(2,4)).starts_with("✓ Correcto"),"The actual preceding selection was not answered correctly");
    auto source=fixture.control(position->block,10);fixture.reveal(source);
    const auto background=reader_theme::background(source);check(GetRValue(background)+GetGValue(background)+GetBValue(background)<384,
        "The real mounted editor did not use the reported dark surface");
    CHARRANGE selection{5,9};SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    Verification verification;verify_native_frame(fixture,source,original,selection,verification);
    // Visit the reported range with RichEdit's native line API, then drive
    // real wheel messages in both directions without widening the window.
    SendMessageW(source,EM_LINESCROLL,0,12);verify_native_frame(fixture,source,original,selection,verification);pump();
    verify_native_frame(fixture,source,original,selection,verification);
    const auto reachable=visible_source_rect(source,lesson_pane(fixture.view));
    POINT point{(reachable.left+reachable.right)/2,(reachable.top+reachable.bottom)/2};ClientToScreen(source,&point);
    POINT start{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&start));
    bool wheel_down=false,wheel_up=false;
    for(const int delta:{-120,-120,-120,120,120,120,-30,-30,-30,-30,30,30,30,30}){
        POINT before{},after{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
        SendMessageW(source,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),MAKELPARAM(point.x,point.y));
        SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));wheel_down|=after.y>before.y;wheel_up|=after.y<before.y;
        verify_native_frame(fixture,source,original,selection,verification);pump();verify_native_frame(fixture,source,original,selection,verification);
    }
    for(std::size_t number=14;number<=24;++number)check(verification.seen[number],"The mounted wheel regression did not visit every reported line 14 through 24");
    check(wheel_down&&wheel_up,"The actual editor did not consume wheel movement in both native directions");
    // Native wheel settings may reach either endpoint. Restore an observed
    // offset explicitly, then exercise a partially visible source/number row.
    SendMessageW(source,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&start));
    POINT finish{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&finish));
    check(finish.y==start.y,"Restoring a measured native source offset changed its position");
    for(const LONG delta:{5L,11L,0L}){POINT offset{0,start.y+delta};SendMessageW(source,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        verify_native_frame(fixture,source,original,selection,verification);pump();verify_native_frame(fixture,source,original,selection,verification);}
    bool horizontal=false;
    for(const LONG x:{40L,120L,0L}){POINT offset{x,finish.y};SendMessageW(source,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        POINT actual{};SendMessageW(source,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&actual));horizontal|=actual.x>0;
        verify_native_frame(fixture,source,original,selection,verification);pump();verify_native_frame(fixture,source,original,selection,verification);}
    check(horizontal,"The original mounted source did not exercise genuine horizontal overflow");
    const auto before=fixture.saved();check(!windows::compiler_puzzle_completed(before,lesson,target),"A repaint or prior selection credited the compiler goal");
    check(state::get_draft(before,draft_key,"").source==original,"Scroll persistence changed original source bytes");
    // A short owned edit verifies history after scrolling. No extra lines are
    // used to manufacture overflow; Undo restores the exact authored source.
    const auto insertion=original.rfind("    return 0;");check(insertion!=original.npos,"The original main lost its return line");
    const auto character=static_cast<LONG>(wide(std::string_view(original).substr(0,insertion)).size());
    CHARRANGE edit_selection{character,character};SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&edit_selection));
    const std::wstring comment=L"    // QA history\r\n";SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(comment.c_str()));pump();
    const auto edited=edit_text(source);check(edited!=original,"The owned history edit did not reach the actual source");
    SendMessageW(source,WM_UNDO,0,0);pump();check(edit_text(source)==original,"Native Undo after scrolling did not restore every original character");
    SendMessageW(source,EM_REDO,0,0);pump();check(edit_text(source)==edited,"Native Redo lost the owned history edit");
    SendMessageW(source,WM_UNDO,0,0);pump();check(edit_text(source)==original,"The final Undo did not restore original source bytes");
    SendMessageW(source,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump();fixture.reopen();source=fixture.control(position->block,10);fixture.reveal(source);
    const auto reopened=fixture.saved();const auto retained=state::get_draft(reopened,draft_key,"");
    check(retained.source==original&&retained.input==draft.input&&retained.selection_start==5&&retained.selection_end==9,
        "Reopening changed the exact source, hidden input draft or selection/caret");
    check(reopened.preferences.at("editor_mount_opaque")==data.preferences.at("editor_mount_opaque"),"The mounted regression changed unrelated preferences");
    check(!windows::compiler_puzzle_completed(reopened,lesson,target),"Reopening manufactured a compiler achievement");
    verify_native_frame(fixture,source,original,selection,verification);
    check(state::sha256(state::read_bytes(path))==original_hash,"The editor mounting regression changed authored content");
    std::cout<<"PASS real U11L9 page-five editor at 902x750, DPI="<<GetDpiForWindow(source)<<": "<<verification.frames
        <<" owned offscreen frames clipped to reachable source/pane pixels equal native RichEdit, wheel/horizontal viewports and line numbers, exact source/selection/Undo/Redo/draft/reopen; no desktop or curricular listening acceptance\n";
}
}
int main(int argc,char** argv){
    std::cout<<std::unitbuf;
    const bool caption_first=argc==2&&std::string_view(argv[1])=="--caption-first";
    const bool prediction_newline=argc==2&&std::string_view(argv[1])=="--prediction-newline";
    if(argc!=1&&!caption_first&&!prediction_newline){
        const bool shape=argc==4&&std::string_view(argv[1])=="--prepare-visual"
            &&(std::string_view(argv[2])=="u1"||std::string_view(argv[2])=="u11");
        const auto raw=shape?std::string_view(argv[3]):std::string_view{};
        const std::wstring token(raw.begin(),raw.end());
        if(!shape||!is_valid_qa_instance(token)){
            std::cerr<<"Usage: lesson_editor_mount_tests [--caption-first | --prediction-newline | --prepare-visual <u1|u11> <safe-new-QA-token>]\n";return 2;
        }
        // This branch performs JSON/state/file operations only. The original
        // no-argument regression owns every HWND/DPI/control initialization.
        try{prepare_visual_profile(argv[2],token);return 0;}
        catch(const std::exception& error){std::cerr<<"Visual QA preparation failed: "<<error.what()<<'\n';return 1;}
    }
    const auto previous_dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{if(caption_first)authored_caption_first();else if(prediction_newline)authored_prediction_newline();else authored_editor_mount();}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();if(previous_dpi)SetThreadDpiAwarenessContext(previous_dpi);return result;
}
