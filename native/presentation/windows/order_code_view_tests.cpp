#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>
#include <ole2.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void click(HWND row){SendMessageW(row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));SendMessageW(row,WM_LBUTTONUP,0,MAKELPARAM(10,10));}
CHARFORMAT2W character(HWND row,LONG position){CHARRANGE old{},selected{position,position+1};SendMessageW(row,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&old));
    SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));CHARFORMAT2W result{};result.cbSize=sizeof(result);
    SendMessageW(row,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&result));SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old));return result;}
void pump(){MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){if(event.message==WM_QUIT)continue;TranslateMessage(&event);DispatchMessageW(&event);}}
void drag_commits_after_releasing_capture(HWND host){
    using namespace lgaed::presentation;
    const auto dpi=GetDpiForWindow(host);const auto px=[&](int value){return MulDiv(value,static_cast<int>(dpi),96);};
    for(const auto finish:{WM_LBUTTONUP,WM_CANCELMODE,WM_CAPTURECHANGED,WM_MOUSEWHEEL,WM_MOUSEHWHEEL}){
        const auto pane=CreateWindowExW(0,L"STATIC",L"Feedback reflow QA",WS_CHILD,px(10),px(10),px(720),px(500),host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(pane!=nullptr,"Cannot create the hidden feedback-reflow parent");
        int changes=0;bool published_with_capture=false,published_with_translucency=false;
        std::vector<std::size_t> published;OrderCodeView* active=nullptr;
        {
            OrderCodeView view(pane,{"line_0;","line_1;","line_2;","line_3;"},{},[&]{
                ++changes;published=active->order();
                const auto source=GetDlgItem(active->window(),100);
                published_with_capture=published_with_capture||GetCapture()==source;
                published_with_translucency=published_with_translucency||(GetWindowLongPtrW(source,GWL_EXSTYLE)&WS_EX_LAYERED)!=0;
                // Removing rejected feedback at the bottom clamps the lesson
                // scroll and moves the whole puzzle. Reproduce that response
                // to a change without relying on a particular lesson's height.
                SetWindowPos(pane,nullptr,px(10),px(90),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            });active=&view;
            MoveWindow(view.window(),0,0,px(700),view.height_for_width(px(700)),FALSE);pump();
            const auto source=GetDlgItem(view.window(),100);
            const auto at=[&](POINT point){ScreenToClient(source,&point);return MAKELPARAM(point.x,point.y);};
            RECT parent_before{};GetWindowRect(pane,&parent_before);
            SendMessageW(source,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(8)));
            SendMessageW(source,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(20),px(8)));
            check(changes==0&&view.order()==std::vector<std::size_t>({0,1,2,3}),
                "Dragging within the source card published a false change and removed reviewed feedback");
            SendMessageW(source,WM_LBUTTONUP,0,MAKELPARAM(px(20),px(8)));
            check(changes==0&&GetCapture()!=source,"A no-op drag published a change or retained capture");
            SendMessageW(source,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(8)));
            RECT target{};GetWindowRect(GetDlgItem(view.window(),101),&target);
            SendMessageW(source,WM_MOUSEMOVE,MK_LBUTTON,at({target.left+px(12),target.top+px(8)}));
            check(view.order()==std::vector<std::size_t>({1,0,2,3}),"A pending drag did not update its visible cards immediately");
            RECT during{};GetWindowRect(pane,&during);
            check(changes==0&&EqualRect(&during,&parent_before),"A drag published feedback and moved the parent before release");
            GetWindowRect(GetDlgItem(view.window(),103),&target);const POINT end{target.left+px(12),target.top+px(8)};
            SendMessageW(source,WM_MOUSEMOVE,MK_LBUTTON,at(end));
            check(changes==0&&view.order()==std::vector<std::size_t>({1,2,3,0}),"A second drag movement lost its original screen destination");
            const auto keys=finish==WM_MOUSEWHEEL||finish==WM_MOUSEHWHEEL?MAKEWPARAM(MK_LBUTTON,-WHEEL_DELTA):0;
            SendMessageW(source,finish,keys,finish==WM_LBUTTONUP?at(end):0);
            check(changes==1&&published==view.order()&&!published_with_capture&&!published_with_translucency,
                "A finished or cancelled drag did not publish its final arrangement once after restoring capture and style");
            GetWindowRect(pane,&during);
            check(during.top!=parent_before.top&&GetCapture()!=source&&!(GetWindowLongPtrW(source,GWL_EXSTYLE)&WS_EX_LAYERED),
                "Feedback reflow did not wait for a completed drag or left its capture/style behind");
            SendMessageW(source,WM_LBUTTONUP,0,at(end));check(changes==1,"An extra release became a second drag commit or a tap");
        }
        DestroyWindow(pane);
    }
    int changes=0;auto view=std::make_unique<OrderCodeView>(host,std::vector<std::string>{"first;","second;","third;"},std::vector<std::size_t>{},[&]{++changes;});
    MoveWindow(view->window(),px(10),px(10),px(700),view->height_for_width(px(700)),FALSE);pump();
    const auto begin_drag=[&]{const auto source=GetDlgItem(view->window(),100),target=GetDlgItem(view->window(),102);RECT a{},b{};
        GetWindowRect(source,&a);GetWindowRect(target,&b);SendMessageW(source,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(8)));
        SendMessageW(source,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(12),b.top-a.top+px(8)));return source;};
    auto source=begin_drag();check(changes==0,"An active drag published before a reset");view->reset();
    check(changes==1&&view->order()==std::vector<std::size_t>({0,1,2})&&GetCapture()!=source,
        "Reset published an obsolete drag draft or retained its pending capture");
    changes=0;source=begin_drag();view->set_enabled(false);
    check(changes==1&&view->order()==std::vector<std::size_t>({1,2,0})&&GetCapture()!=source,
        "Disabling a pending drag lost its final arrangement or published it twice");
    view->set_enabled(true);view->reset();changes=0;source=begin_drag();view.reset();
    check(changes==0&&GetCapture()!=source,"Destruction published a drag into its disappearing owner or retained capture");
    view=std::make_unique<OrderCodeView>(host,std::vector<std::string>{"first;","second;","third;"},std::vector<std::size_t>{},[&]{++changes;});
    MoveWindow(view->window(),0,0,px(700),view->height_for_width(px(700)),FALSE);changes=0;
    click(GetDlgItem(view->window(),100));
    check(changes==1&&view->order()==std::vector<std::size_t>({0,1,2}),"A deliberate first tap no longer starts a sequence at its existing position");
    SendMessageW(GetDlgItem(view->window(),102),WM_SYSKEYDOWN,VK_UP,0);
    check(changes==2&&view->order()==std::vector<std::size_t>({0,2,1}),"Keyboard ordering no longer publishes immediately");view.reset();
    for(const auto finish:{WM_LBUTTONUP,WM_CANCELMODE,WM_CAPTURECHANGED,WM_MOUSEWHEEL,WM_MOUSEHWHEEL}){
        changes=0;view=std::make_unique<OrderCodeView>(host,std::vector<std::string>{"first;","second;","third;"},std::vector<std::size_t>{},[&]{++changes;view.reset();});
        MoveWindow(view->window(),0,0,px(700),view->height_for_width(px(700)),FALSE);
        source=begin_drag();const auto keys=finish==WM_MOUSEWHEEL||finish==WM_MOUSEHWHEEL?MAKEWPARAM(MK_LBUTTON,-WHEEL_DELTA):0;
        SendMessageW(source,finish,keys,0);
        check(!view&&changes==1&&!IsWindow(source)&&GetCapture()!=source,
            "A release callback could not destroy its owner safely after restoring the gesture");
    }
    std::cout<<"PASS drag commit: stable parent until release, no-op, final draft, capture loss/cancel/wheel, Reset, disable and destruction\n";
}
void fast_drag_reaches_its_target(HWND host){
    using namespace lgaed::presentation;
    int changes=0;OrderCodeView view(host,{"line_0;","line_1;","line_2;","line_3;"},{},[&]{++changes;});
    const auto dpi=GetDpiForWindow(host);const auto px=[&](int value){return MulDiv(value,static_cast<int>(dpi),96);};
    MoveWindow(view.window(),px(10),px(10),px(700),view.height_for_width(px(700)),TRUE);pump();
    const auto origin=GetDlgItem(view.window(),100),target=GetDlgItem(view.window(),103);
    RECT destination{};GetWindowRect(target,&destination);
    const POINT end{destination.left+px(12),destination.top+px(8)};
    const auto at=[&](POINT point){ScreenToClient(origin,&point);return MAKELPARAM(point.x,point.y);};
    SetFocus(origin);const auto began=GetTickCount64();
    SendMessageW(origin,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(8)));
    check(GetCapture()==origin,"A fast native drag did not capture its source");
    // No hold or sleep: a full-card displacement is still a drag when its
    // first move arrives before the old20ms cutoff.
    const auto delay=GetTickCount64()-began;check(delay<20,"The fast-drag fixture exceeded its intended event boundary");
    SendMessageW(origin,WM_MOUSEMOVE,MK_LBUTTON,at(end));
    const auto after_move=view.order();
    BYTE opacity=0;DWORD flags=0;
    check(GetLayeredWindowAttributes(origin,nullptr,&opacity,&flags)&&opacity==140&&(flags&LWA_ALPHA),
        "A fast drag lost the existing source opacity");
    SendMessageW(origin,WM_LBUTTONUP,0,at(end));
    const auto duration=GetTickCount64()-began;
    std::cout<<"Fast drag first movement delay="<<delay<<"ms; complete gesture="<<duration<<"ms\n";
    check(after_move==std::vector<std::size_t>({1,2,3,0}),"A fast full-card drag was ignored instead of moving its source to the pointed row");
    check(view.order()==std::vector<std::size_t>({1,2,3,0})&&changes==1,
        "Releasing a fast drag performed an extra click-to-order");
    check(GetCapture()!=origin&&!(GetWindowLongPtrW(origin,GWL_EXSTYLE)&WS_EX_LAYERED),
        "A fast drag retained capture or source translucency");
    click(GetDlgItem(view.window(),102));
    check(view.order()==std::vector<std::size_t>({2,1,3,0}),
        "A fast drag advanced the next-tap sequence instead of preserving its first destination");
    view.reset();const auto jitter_row=GetDlgItem(view.window(),102);
    SendMessageW(jitter_row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(8)));
    SendMessageW(jitter_row,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(14),px(9)));
    SendMessageW(jitter_row,WM_LBUTTONUP,0,MAKELPARAM(px(14),px(9)));
    check(view.order()==std::vector<std::size_t>({2,0,1,3})&&!(GetWindowLongPtrW(jitter_row,GWL_EXSTYLE)&WS_EX_LAYERED),
        "Small pointer jitter stopped being a normal next-position tap");
    for(const auto cancel:{WM_CANCELMODE,WM_CAPTURECHANGED,WM_MOUSEWHEEL,WM_MOUSEHWHEEL}){
        view.reset();const auto pending=GetDlgItem(view.window(),102);const auto unchanged=view.order();
        SendMessageW(pending,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(8)));
        SendMessageW(pending,cancel,cancel==WM_MOUSEWHEEL||cancel==WM_MOUSEHWHEEL?MAKEWPARAM(MK_LBUTTON,-WHEEL_DELTA):0,0);
        SendMessageW(pending,WM_LBUTTONUP,0,MAKELPARAM(px(12),px(8)));
        check(view.order()==unchanged&&GetCapture()!=pending&&!(GetWindowLongPtrW(pending,GWL_EXSTYLE)&WS_EX_LAYERED),
            "Cancel, capture loss or wheel scrolling became a release-time tap or retained capture");
    }
}
struct Drawing {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    DWORD* pixels=nullptr;int width=1,height=1;
    Drawing(HWND host){RECT area{};GetClientRect(host,&area);width=std::max(1L,area.right);height=std::max(1L,area.bottom);const auto screen=GetDC(host);dc=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);ReleaseDC(host,screen);
        check(dc&&bitmap,"Cannot create native order-card raster");previous=SelectObject(dc,bitmap);SendMessageW(host,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
    }
    COLORREF pixel(int x,int y)const{const auto ink=pixels[static_cast<std::size_t>(y)*width+x];return RGB((ink>>16)&255,(ink>>8)&255,ink&255);}
    ~Drawing(){if(dc){SelectObject(dc,previous);DeleteDC(dc);}if(bitmap)DeleteObject(bitmap);}
};
struct ReentrantPaint {
    inline static thread_local ReentrantPaint* active=nullptr;
    HHOOK hook=nullptr;bool inside=false,dc_failed=false,layout_requested=false;
    int host_creations=0,row_creations=0,placements=0;
    ReentrantPaint(){check(active==nullptr,"A reentrant-construction hook is already installed");active=this;
        hook=SetWindowsHookExW(WH_CALLWNDPROCRET,observe,nullptr,GetCurrentThreadId());
        if(!hook){active=nullptr;throw std::runtime_error("Cannot observe synchronous native construction/layout paint");}}
    ~ReentrantPaint(){if(hook)UnhookWindowsHookEx(hook);active=nullptr;}
    static LRESULT CALLBACK observe(int code,WPARAM wp,LPARAM lp){auto* probe=active;
        if(code>=0&&probe&&!probe->inside){const auto& call=*reinterpret_cast<CWPRETSTRUCT*>(lp);wchar_t type[128]{};GetClassNameW(call.hwnd,type,128);
            HWND order=nullptr;
            if(std::wstring_view(type)==L"LearnGameAndEngineDev.OrderCode"&&call.message==WM_NCCREATE){order=call.hwnd;++probe->host_creations;}
            else if(_wcsicmp(type,MSFTEDIT_CLASS)==0&&(call.message==WM_NCCREATE||call.message==WM_WINDOWPOSCHANGED)){
                const auto parent=GetParent(call.hwnd);wchar_t owner[128]{};GetClassNameW(parent,owner,128);
                if(std::wstring_view(owner)==L"LearnGameAndEngineDev.OrderCode"){
                    order=parent;if(call.message==WM_NCCREATE)++probe->row_creations;else ++probe->placements;
                }
            }
            if(order){probe->inside=true;const auto dc=GetDC(order);if(!dc)probe->dc_failed=true;
                else{SendMessageW(order,WM_ERASEBKGND,reinterpret_cast<WPARAM>(dc),0);SendMessageW(order,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);ReleaseDC(order,dc);}
                // A child can cause its parent's layout before the constructor
                // has returned. Every published index must already be valid.
                if(call.message==WM_NCCREATE&&order!=call.hwnd&&!probe->layout_requested){probe->layout_requested=true;SendMessageW(order,WM_HSCROLL,SB_LINELEFT,0);}
                probe->inside=false;
            }
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
};
}
void optional_root_metadata_preserves_published_history(){
    using namespace lgaed;
    constexpr auto relative="app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "programas_y_ejecucion/lesson_primer_programa_y_salida.json";
    const auto original=windows::load_lesson(std::filesystem::path(build::source_root)/relative,"es");
    check(original.pages.size()>3&&original.pages[3].blocks.size()>2,
        "Original U1 code-order reader was not found");
    const auto& block=original.pages[3].blocks[2];
    check(block.stable_id=="block_0003_0002"&&block.type==curriculum::BlockType::order_code,
        "Historical code-order identity no longer points to the same block");
    auto bytes=original.raw_json;
    constexpr std::string_view field="\"orderVisualSeparators\"";
    const bool authored=bytes.find(field)!=bytes.npos;
    if(!authored){
        const auto location=bytes.find("  \"stateLayout\": {");
        check(location!=bytes.npos,"Historical v2 state map lacks its single root");
        bytes.insert(location,
            "  \"orderVisualSeparators\": [{\"activity\":\"block_0003_0002\",\"count\":2}],\n");
    }
    const auto projected=windows::parse_lesson_json(bytes,"es");
    const auto metadata=windows::parse_lesson_progress_metadata(bytes,"es");
    const auto& owner=projected.pages[3].blocks[2];
    const auto& progress=metadata.pages[3].blocks[2];
    check(owner.optional_order_separators>0&&
        progress.optional_order_separators==owner.optional_order_separators&&
        owner.fragments==block.fragments&&owner.correct_order==block.correct_order&&
        owner.raw_json==block.raw_json&&owner.address.page==block.address.page&&
        owner.address.block==block.address.block&&
        curriculum::order_representation_frame(owner)==curriculum::order_representation_frame(block)&&
        curriculum::exercise_key(projected,owner)==curriculum::exercise_key(original,block)&&
        curriculum::draft_key("programming",projected,owner)==curriculum::draft_key("programming",original,block),
        "Root-only optional cards changed original answers, progress, source or reference keys");
    if(!authored){
        auto invalid=bytes;
        const auto location=invalid.find("\"count\":2");
        check(location!=std::string::npos,"Invalid count negative fixture missing");
        invalid.replace(location,std::string_view("\"count\":2").size(),"\"count\":5");
        bool rejected=false;
        try{(void)windows::parse_lesson_json(invalid,"es");}
        catch(const std::exception&){rejected=true;}
        check(rejected,"An invalid root-only visual count bypassed loader validation");
    }
}

void source_bound_order_groups_preserve_history() {
    using namespace lgaed;
    constexpr auto relative="app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json";
    const auto original=windows::load_lesson(std::filesystem::path(build::source_root)/relative,"es");
    const auto before=curriculum::stable_block_position(original,"block_0004_0003");
    check(before.has_value(),"U12 independent declaration owner was moved");
    const auto& old=original.pages[before->page].blocks[before->block];
    check(old.type==curriculum::BlockType::order_code&&
          old.order_equivalence_groups==std::vector<std::vector<std::size_t>>{{2,3,4}},
        "The existing U12 source-reviewed three-card permutation is missing");
    auto bytes=original.raw_json;
    const auto anchor=bytes.find("\"stateLayout\":");
    check(anchor!=bytes.npos&&bytes.find("\"orderEquivalenceGroups\"")==bytes.npos,
        "The published U12 lesson has an unexpected root declaration");
    constexpr std::string_view entry=
        "\"orderEquivalenceGroups\": [{\"activity\":\"block_0004_0003\","
        "\"sourceSha256\":\"4380d9b5e34bdda15027e6d92939d391ad4b22864f3ded1dac00d22717111af6\","
        "\"groups\":[[2,3,4]]}],\n  ";
    bytes.insert(anchor,entry);
    const auto graded=windows::parse_lesson_json(bytes,"es");
    const auto metadata=windows::parse_lesson_progress_metadata(bytes,"es");
    const auto after=curriculum::stable_block_position(graded,"block_0004_0003");
    const auto read=curriculum::stable_block_position(metadata,"block_0004_0003");
    check(after&&read,"Authored root changed the historical code-order address");
    const auto& current=graded.pages[after->page].blocks[after->block];
    const auto& summary=metadata.pages[read->page].blocks[read->block];
    check(current.order_equivalence_groups==old.order_equivalence_groups&&
          summary.order_equivalence_groups==old.order_equivalence_groups&&
          old.fragments==current.fragments&&old.correct_order==current.correct_order&&
          current.raw_json==old.raw_json&&current.address.page==old.address.page&&
          current.address.block==old.address.block&&
          curriculum::order_representation_frame(old)==curriculum::order_representation_frame(current)&&
          curriculum::exercise_key(original,old)==curriculum::exercise_key(graded,current)&&
          curriculum::draft_key("programming",original,old)==
              curriculum::draft_key("programming",graded,current),
        "Root order declaration changed the stored response, source or draft identity");
    auto order=old.correct_order;
    auto group=std::vector<std::size_t>{2,3,4};
    std::vector<std::size_t> slots;
    for(std::size_t i=0;i<order.size();++i)
        if(std::ranges::find(group,order[i])!=group.end())slots.push_back(i);
    check(slots.size()==3,"U12 order no longer has three contiguous independent declarations");
    std::size_t combinations=0;
    do {
        for(std::size_t i=0;i<slots.size();++i)order[slots[i]]=group[i];
        check(curriculum::evaluate_order(current,order).correct&&
              curriculum::evaluate_order(summary,order).correct,
            "A source-authenticated equivalent order was rejected");
        ++combinations;
    }while(std::next_permutation(group.begin(),group.end()));
    check(combinations==6,"U12 did not preserve all six valid declaration orders");
    auto rejects=[](const std::string& source) {
        bool active=false,metadata=false;
        try{(void)lgaed::windows::parse_lesson_json(source,"es");}
        catch(const std::exception&){active=true;}
        try{(void)lgaed::windows::parse_lesson_progress_metadata(source,"es");}
        catch(const std::exception&){metadata=true;}
        check(active&&metadata,"A forged root order revision passed native load or catalog projection");
    };
    auto bad=bytes;
    auto position=bad.find("4380d9b5e34b");
    check(position!=bad.npos,"Missing source-hash negative fixture");
    bad.replace(position,1,"0");rejects(bad);
    bad=bytes;position=bad.find("[[2,3,4]]");
    check(position!=bad.npos,"Missing canonical-group negative fixture");
    bad.replace(position,std::string_view("[[2,3,4]]").size(),"[[2,3]]");rejects(bad);
    bad=bytes;position=bad.find("[[2,3,4]]");
    bad.replace(position,std::string_view("[[2,3,4]]").size(),"[[2,3],[3,4]]");rejects(bad);
    std::cout<<"PASS root-independent U12 code groups: 6 valid orders, original stored answer, "
        "archive, reader projection, invalid source and overlapping group rejection\n";
}

// This owner exists only in a disposable fixture. A genuine new review must
// work without adding another lesson/hash exception to the native loader.
void generic_order_groups_accept_new_owner() {
    using namespace lgaed;
    const std::string reviewed=R"lgaed({"id":"qa_order_generic","title":"Grupos independientes","pages":[{"blocks":[{"type":"order_code","id":"independent_order","question":"Ordena","fragments":["int a{1};","int b{2};","return a + b;"],"correctOrder":[0,1,2],"explanation":"Dos variables independientes."}]}],"stateLayout":{"version":2,"locale":"es","origin":[{"id":"page_0","blocks":[{"id":"qa_new_independent_order","sha256":"f18d4a1e05c3b696d68c217a74a3ab0428ef917011bcd56e0acb71507d24b149"}]}],"current":[{"id":"page_0","blocks":["qa_new_independent_order"]}],"family":{"es":{"sha256":"0000000000000000000000000000000000000000000000000000000000000000","origin":[{"id":"page_0","blocks":[{"id":"qa_new_independent_order","sha256":"f18d4a1e05c3b696d68c217a74a3ab0428ef917011bcd56e0acb71507d24b149"}]}]}},"additions":[],"editorial":[]},"orderEquivalenceGroups":[{"activity":"qa_new_independent_order","sourceSha256":"f18d4a1e05c3b696d68c217a74a3ab0428ef917011bcd56e0acb71507d24b149","groups":[[0,1]]}]})lgaed";
    const auto at=reviewed.find(",\"orderEquivalenceGroups\":");
    check(at!=reviewed.npos,"Generic order QA fixture lost its root declaration");
    const auto prior=reviewed.substr(0,at)+"}";
    const auto original=windows::parse_lesson_json(prior,"es");
    const auto active=windows::parse_lesson_json(reviewed,"es");
    const auto metadata=windows::parse_lesson_progress_metadata(reviewed,"es");
    const auto& before=original.pages.at(0).blocks.at(0);
    const auto& after=active.pages.at(0).blocks.at(0);
    const auto& summary=metadata.pages.at(0).blocks.at(0);
    check(before.stable_id=="qa_new_independent_order"&&
          before.order_equivalence_groups.empty()&&
          after.order_equivalence_groups==std::vector<std::vector<std::size_t>>{{0,1}}&&
          summary.order_equivalence_groups==after.order_equivalence_groups,
        "The generic root order reviewer still requires a hardcoded historical owner");
    const auto swapped=std::vector<std::size_t>{1,0,2};
    const auto invalid=std::vector<std::size_t>{2,1,0};
    check(!curriculum::evaluate_order(before,swapped).correct&&
          curriculum::evaluate_order(after,swapped).correct&&
          curriculum::evaluate_order(summary,swapped).correct&&
          !curriculum::evaluate_order(after,invalid).correct&&
          curriculum::evaluate_order(after,after.correct_order).correct,
        "Generic order groups changed canonical evaluation or accepted a dependency inversion");
    check(before.raw_json==after.raw_json&&
          before.correct_order==after.correct_order&&
          before.fragments==after.fragments&&
          curriculum::order_representation_frame(before)==
              curriculum::order_representation_frame(after)&&
          curriculum::exercise_key(original,before)==
              curriculum::exercise_key(active,after)&&
          curriculum::draft_key("programming",original,before)==
              curriculum::draft_key("programming",active,after),
        "A new order equivalence rewrote physical code or existing learner state");
    // An authored group must survive a subsequent authenticated, token-
    // equivalent source-format revision. The source digest remains bound to
    // its original physical cards, not replaced by the newly spaced bytes.
    using namespace winrt::Windows::Data::Json;
    const auto formatted_json=JsonObject::Parse(winrt::to_hstring(reviewed));
    const auto current_block=formatted_json.GetNamedArray(L"pages")
        .GetObjectAt(0).GetNamedArray(L"blocks").GetObjectAt(0);
    const auto previous_block=JsonObject::Parse(current_block.Stringify());
    current_block.GetNamedArray(L"fragments").SetAt(0,
        JsonValue::CreateStringValue(L"int a {1};"));
    const auto current_layout=formatted_json.GetNamedObject(L"stateLayout");
    JsonObject saved_archive;
    saved_archive.Insert(L"id",JsonValue::CreateStringValue(L"qa_new_independent_order"));
    saved_archive.Insert(L"block",previous_block);
    JsonArray archives;
    archives.Append(saved_archive);
    current_layout.Insert(L"sourceFormatRevisions",archives);
    JsonObject transition;
    transition.Insert(L"id",JsonValue::CreateStringValue(L"qa_new_independent_order"));
    transition.Insert(L"beforeSha256",JsonValue::CreateStringValue(
        L"ed142eb744cac81c8dcb146d29bbd2f200c837af3d06dda62fe2272b66a54201"));
    transition.Insert(L"afterSha256",JsonValue::CreateStringValue(
        L"0df9290ad0b4251aa67ea3327f1596055cf1c5d731afdaf224b27bd377281add"));
    current_layout.GetNamedArray(L"editorial").Append(transition);
    const auto formatted_source=winrt::to_string(formatted_json.Stringify());
    const auto formatted=windows::parse_lesson_json(formatted_source,"es");
    const auto formatted_catalog=windows::parse_lesson_progress_metadata(
        formatted_source,"es");
    const auto& spaced=formatted.pages.at(0).blocks.at(0);
    const auto& indexed=formatted_catalog.pages.at(0).blocks.at(0);
    check(spaced.fragments.at(0)=="int a {1};"&&
          spaced.source_format_original_fragments.has_value()&&
          *spaced.source_format_original_fragments==after.fragments&&
          spaced.order_equivalence_groups==after.order_equivalence_groups&&
          indexed.order_equivalence_groups==after.order_equivalence_groups&&
          curriculum::evaluate_order(spaced,swapped).correct&&
          curriculum::order_representation_frame(spaced)==
              curriculum::order_representation_frame(after)&&
          curriculum::exercise_key(formatted,spaced)==
              curriculum::exercise_key(active,after)&&
          curriculum::draft_key("programming",formatted,spaced)==
              curriculum::draft_key("programming",active,after),
        "Authenticated physical spacing lost an already authored independent order group");
    // An invalid token change cannot borrow a legitimate formatting archive.
    const auto forged_formatted=JsonObject::Parse(formatted_json.Stringify());
    forged_formatted.GetNamedArray(L"pages").GetObjectAt(0)
        .GetNamedArray(L"blocks").GetObjectAt(0)
        .GetNamedArray(L"fragments").SetAt(0,JsonValue::CreateStringValue(L"int a {2};"));
    bool rejected=false;
    try {
        (void)windows::parse_lesson_json(
            winrt::to_string(forged_formatted.Stringify()),"es");
    } catch(const std::exception&) {rejected=true;}
    check(rejected,"A token-changing source borrowed an old independent order digest");
    // Widening the set of accepted orders must NOT silently rewrite an
    // incorrect historical answer. Only the next explicit student check may
    // turn that stored attempt into a correct one.
    state::StateData previous_attempt;
    state::Response prior_answer;
    prior_answer.order={1,0,2};
    prior_answer.correct=false;
    state::put_response(previous_attempt,original,before,prior_answer);
    const auto preserved_blob=state::string_value(
        previous_attempt,"learning_progress",
        curriculum::exercise_key(original,before));
    const auto saved_before=previous_attempt;
    const auto restored=state::get_response(previous_attempt,active,after);
    const auto spaced_restored=state::get_response(
        previous_attempt,formatted,spaced);
    check(spaced_restored&&spaced_restored->order==prior_answer.order&&
          !spaced_restored->correct&&
          restored.has_value()&&restored->order==prior_answer.order&&
          !restored->correct&&
          curriculum::evaluate_order(after,std::vector<std::size_t>{1,0,2}).correct&&
          state::response_storage_key(original,before)==
              state::response_storage_key(active,after)&&
          state::string_value(previous_attempt,"learning_progress",
              curriculum::exercise_key(active,after))==preserved_blob&&
          previous_attempt==saved_before,
        "New equivalences silently promoted, regraded or rewrote a prior incorrect answer");
    // Previously earned canonical answers also survive unchanged.
    state::StateData earlier_success;
    state::Response successful;
    successful.order={0,1,2};
    successful.correct=true;
    state::put_response(earlier_success,original,before,successful);
    const auto reopened_success=state::get_response(earlier_success,active,after);
    check(reopened_success&&reopened_success->correct&&
          reopened_success->order==successful.order,
        "Root-only order equivalence revoked a valid historical achievement");
    const auto reject=[](std::string forged) {
        bool loader=false,progress=false;
        try{(void)lgaed::windows::parse_lesson_json(forged,"es");}
        catch(const std::exception&){loader=true;}
        try{(void)lgaed::windows::parse_lesson_progress_metadata(forged,"es");}
        catch(const std::exception&){progress=true;}
        check(loader&&progress,"Unverified generic order metadata passed one native loader");
    };
    auto forged=reviewed;
    auto marker=forged.find("\"groups\":[[0,1]]");
    check(marker!=forged.npos,"Generic group negative fixture is missing");
    forged.replace(marker,std::string_view("\"groups\":[[0,1]]").size(),
                   "\"groups\":[[0,2]]");
    reject(forged);
    forged=reviewed;
    marker=forged.find("\"sourceSha256\":\"f18d4a1e");
    check(marker!=forged.npos,"Generic source negative fixture is missing");
    marker+=std::string_view("\"sourceSha256\":\"").size();
    forged.replace(marker,1,"0");
    reject(forged);
    std::cout<<"PASS generic order root: new stable owner, swapped independent cards, "
        "original response, source digest and catalog state\n";
}

void optional_visual_chips_do_not_publish_answers(HWND host){
    using namespace lgaed::presentation;
    const auto dpi=static_cast<int>(GetDpiForWindow(host));
    const auto px=[&](int value){return MulDiv(value,dpi,96);};
    const std::vector<std::string> fragments{
        "    inicializar();", "    calcular();", "    devolver();"};
    int changes=0;
    OrderCodeView view(host,fragments,{0,1,2},[&]{++changes;},2);
    MoveWindow(view.window(),0,0,px(650),view.height_for_width(px(650)),TRUE);pump();
    const std::vector<std::size_t> original{0,1,2};

    // Enumerate the actual child positions. order() intentionally excludes
    // decorative cards so saved student answers never include their identities.
    const auto visible=[&]{
        std::vector<std::pair<LONG,std::size_t>> placements;
        for(std::size_t id=0;id<5;++id){
            const auto row=GetDlgItem(view.window(),100+static_cast<int>(id));
            check(row&&IsWindowVisible(row),"An optional card was hidden or removed");
            RECT bounds{};GetWindowRect(row,&bounds);
            placements.emplace_back(bounds.top,id);
        }
        check(!GetDlgItem(view.window(),105),"An Insert or drag duplicated a card");
        std::sort(placements.begin(),placements.end());
        for(std::size_t i=1;i<placements.size();++i)
            check(placements[i].first>placements[i-1].first,"Optional cards overlap or lose their independent positions");
        std::vector<std::size_t> sequence;
        for(const auto& [top,id]:placements){(void)top;sequence.push_back(id);}
        return sequence;
    };
    const auto drag_to=[&](HWND source,POINT target){
        RECT from{};GetWindowRect(source,&from);
        const auto x=target.x-from.left,y=target.y-from.top;
        SendMessageW(source,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(10),px(10)));
        SendMessageW(source,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(source,WM_LBUTTONUP,0,MAKELPARAM(x,y));pump();
        check(GetCapture()!=source,"Optional-card gesture retained mouse capture");
    };
    const auto outside=[&](HWND row,int dx,int dy){
        RECT bounds{};GetWindowRect(row,&bounds);
        return POINT{dx<0?bounds.left+dx:bounds.right+dx,
            dy<0?bounds.top+dy:bounds.bottom+dy};
    };
    const auto first=GetDlgItem(view.window(),100);
    const auto first_blank=GetDlgItem(view.window(),103);
    const auto second_blank=GetDlgItem(view.window(),104);
    check(visible()==std::vector<std::size_t>({0,1,2,3,4})&&
        view.order()==original&&view.reading_regions().size()==3,
        "Optional cards changed the authored source or narration");

    // Keyboard, double-click and Delete/Backspace/Insert must never hide a
    // separator, and purely visual moves never publish a new scored answer.
    SendMessageW(first_blank,WM_SYSKEYDOWN,VK_UP,0);pump();
    check(visible()==std::vector<std::size_t>({0,1,3,2,4})&&
        view.order()==original&&changes==0,"Blank Alt+Up changed a scored answer");
    SendMessageW(second_blank,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(10,8));
    SendMessageW(first_blank,WM_KEYDOWN,VK_DELETE,0);
    SendMessageW(second_blank,WM_KEYDOWN,VK_BACK,0);
    SendMessageW(first,WM_KEYDOWN,VK_INSERT,0);pump();
    check(visible()==std::vector<std::size_t>({0,1,3,2,4})&&
        changes==0,"A legacy hide/restore shortcut modified optional cards");

    SendMessageW(first,WM_SYSKEYDOWN,VK_DOWN,0);pump();
    check(view.order()==std::vector<std::size_t>({1,0,2})&&changes==1,
        "Moving a real line did not publish its changed scored order");
    const auto saved=view.order();
    SendMessageW(first_blank,WM_SYSKEYDOWN,VK_UP,0);pump();
    SendMessageW(first_blank,WM_SYSKEYDOWN,VK_UP,0);pump();
    check(visible().front()==3&&view.order()==saved&&changes==1,
        "An optional blank cannot reach the first visible position");
    for(int i=0;i<4;++i)SendMessageW(first_blank,WM_SYSKEYDOWN,VK_DOWN,0);
    pump();
    check(visible().back()==3&&view.order()==saved&&changes==1,
        "An optional blank cannot reach the last visible position");
    for(int i=0;i<3;++i)SendMessageW(second_blank,WM_SYSKEYDOWN,VK_UP,0);
    pump();
    check(visible().front()==4&&changes==1,
        "The second optional blank cannot cross real lines");
    for(int i=0;i<4;++i)SendMessageW(second_blank,WM_SYSKEYDOWN,VK_DOWN,0);
    pump();
    check(visible()==std::vector<std::size_t>({1,0,2,3,4})&&changes==1,
        "Moving several optional blanks did not preserve every source line");

    // Dragging outside a card's width or past either vertical endpoint still
    // targets the nearest row. Narrow blank cards never block a real line.
    drag_to(first_blank,outside(second_blank,px(120),px(40)));
    check(visible().back()==3&&view.order()==saved&&changes==1,
        "A blank drag past the bottom endpoint failed or changed student credit");
    drag_to(second_blank,outside(GetDlgItem(view.window(),101),-px(120),-px(40)));
    check(visible().front()==4&&view.order()==saved&&changes==1,
        "A blank drag past the top endpoint failed or changed student credit");
    drag_to(first,outside(second_blank,-px(120),-px(40)));
    check(visible().front()==0&&view.order()==original&&changes==2,
        "A real line cannot cross a blank separator to the top");
    drag_to(first,outside(first_blank,px(120),px(40)));
    check(visible().back()==0&&view.order()==std::vector<std::size_t>({1,2,0})&&changes==3,
        "A real line cannot cross optional separators to the bottom");

    // Consecutive separators at the leading edge must not trap code cards.
    for(int i=0;i<2;++i)SendMessageW(first_blank,WM_SYSKEYDOWN,VK_UP,0);
    pump();
    check(visible()[0]==4&&visible()[1]==3&&changes==3,
        "Optional separators could not sit next to one another");
    drag_to(first,outside(second_blank,-px(120),-px(40)));
    check(visible().front()==0&&view.order()==original&&changes==4,
        "A real line was blocked by consecutive separators");
    check(view.reading_regions().size()==3,"Blank cards became narration or answer rows");

    view.set_completed(true);view.set_enabled(false);
    const auto locked=visible();
    SendMessageW(first_blank,WM_KEYDOWN,VK_DELETE,0);
    SendMessageW(second_blank,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(10,8));
    SendMessageW(first,WM_SYSKEYDOWN,VK_DOWN,0);pump();
    check(visible()==locked&&view.order()==original,
        "Correctly completed cards were removed or reordered while locked");
    view.set_enabled(true);view.reset();pump();
    check(visible()==std::vector<std::size_t>({0,1,2,3,4})&&view.order()==original,
        "Reset lost a visible card or an original source index");

    // Reopening a previously correct or incorrect answer recreates only the
    // disposable visual cards; it must not modify the scored saved permutation.
    for(const auto& reopened_order:std::vector<std::vector<std::size_t>>{{0,1,2},{1,2,0}}){
        OrderCodeView reopened(host,fragments,reopened_order,{},2);
        MoveWindow(reopened.window(),0,0,px(650),reopened.height_for_width(px(650)),TRUE);
        check(reopened.order()==reopened_order&&reopened.reading_regions().size()==3,
            "Reopening an earned or incorrect order rewrote saved source identities");
        for(int id=100;id<105;++id)
            check(IsWindowVisible(GetDlgItem(reopened.window(),id)),
                "Reopening an order hid an optional separator");
    }
}

void preserve_code_spacing_in_rows_and_editor(HWND host){
    using namespace lgaed::presentation;
    const std::vector<std::string> fragments{
        "    int piezas{4};\n\n        return piezas;", "", "  \t  ",
        "set(A 1)\n\nset(B 2)", "std::cout << \"\\n\";"};
    int changes=0;
    OrderCodeView view(host,fragments,{0,1,2,3,4},[&]{++changes;});
    const auto dpi=static_cast<int>(GetDpiForWindow(host));
    const auto px=[&](int value){return MulDiv(value,dpi,96);};
    MoveWindow(view.window(),0,0,px(700),view.height_for_width(px(700)),TRUE);pump();
    for(std::size_t i=0;i<fragments.size();++i){
        const auto row=GetDlgItem(view.window(),100+static_cast<int>(i));
        check(row&&edit_text(row)==fragments[i],
            "An authored C++/CMake fragment lost indentation or blank-line bytes");
    }
    const auto first=GetDlgItem(view.window(),100),empty=GetDlgItem(view.window(),101);
    check(SendMessageW(first,EM_GETLINECOUNT,0,0)>=3&&
        SendMessageW(GetDlgItem(view.window(),103),EM_GETLINECOUNT,0,0)>=3,
        "The code-card renderer collapsed significant vertical blank lines");
    RECT tall{},shorter{};
    GetWindowRect(first,&tall);GetWindowRect(GetDlgItem(view.window(),104),&shorter);
    check(tall.bottom-tall.top>shorter.bottom-shorter.top,
        "Three lines of original code were flattened to one row");
    POINTL x0{},x4{},x5{};
    SendMessageW(first,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&x0),0);
    SendMessageW(first,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&x4),4);
    SendMessageW(first,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&x5),5);
    const auto glyph=x5.x-x4.x;
    check(glyph>0&&(x4.x-x0.x)*10>=glyph*36,
        "Four indent spaces were rendered narrower than their full monospace advances");
    SendMessageW(empty,WM_SYSKEYDOWN,VK_DOWN,0);pump();
    check(view.order()==std::vector<std::size_t>({0,2,1,3,4})&&changes==1,
        "An empty decorative card became immovable or changed another instruction");
    const auto reading=view.reading_regions();
    check(reading.size()==5&&reading[2].second.empty()&&reading[1].second=="  \t  ",
        "Moving blank cards altered their original identity or narrated text");
    view.set_text_scale(150);view.set_wrap(true);
    (void)view.height_for_width(px(350));pump();
    for(std::size_t i=0;i<fragments.size();++i)
        check(edit_text(GetDlgItem(view.window(),100+static_cast<int>(i)))==fragments[i],
            "Zoom or wrap changed source spaces, escaped newlines or blank lines");
    view.reset();
    check(view.order()==std::vector<std::size_t>({0,1,2,3,4}),
        "Reset of optional blank cards did not retain the original positions");

    // The same RichEdit engine backs editable C++ and CMake source fields.
    // Formatting is a view operation; editing must keep literal LF, tabs and
    // the escaped two-character sequence \n distinct.
    const auto edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",
        WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_WANTRETURN,
        0,0,px(680),px(240),host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(edit!=nullptr,"Cannot create the original native source edit control");
    const std::string cpp="#include <iostream>\n\nint main() {\n    std::cout << \"\\n\";\n}\n";
    const std::string cmake="cmake_minimum_required(VERSION 3.20)\n\nif(WITH_TOOLS)\n    add_executable(tool main.cpp)\nendif()\n";
    try{
        set_edit_text(edit,cpp);
        style_cpp(edit);
        check(edit_text(edit)==cpp,"The source editor collapsed C++ indentation, LF or the literal newline escape");
        set_edit_text(edit,cmake);
        check(edit_text(edit)==cmake,"The source editor collapsed a meaningful CMake statement break");
        set_rich_text(edit,cpp,{},true);
        check(edit_text(edit)==cpp,"The read-only code-example presentation changed source whitespace");
        DestroyWindow(edit);
    }catch(...){DestroyWindow(edit);throw;}
}

int main(int argc,char** argv){using namespace lgaed::presentation;
    const bool drag_commit_only=argc==2&&std::string_view(argv[1])=="--drag-commit";
    const bool whitespace_only=argc==2&&std::string_view(argv[1])=="--whitespace";
    if(argc>1&&!drag_commit_only&&!whitespace_only)return 2;
    struct DpiScope{DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}} dpi_scope;
    check(SUCCEEDED(OleInitialize(nullptr)),"Cannot initialize the application's native OLE apartment");struct OleScope{~OleScope(){OleUninitialize();}} apartment;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto host=CreateWindowExW(0,L"STATIC",L"Code order QA",WS_OVERLAPPEDWINDOW|(drag_commit_only?0:WS_VISIBLE),40,40,800,640,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
        if(whitespace_only){
            optional_root_metadata_preserves_published_history();
            source_bound_order_groups_preserve_history();
            generic_order_groups_accept_new_owner();
            optional_visual_chips_do_not_publish_answers(host);
            preserve_code_spacing_in_rows_and_editor(host);
            DestroyWindow(host);
            std::cout<<"PASS native C++/CMake code/drag display retains indentation and vertical line spacing\n";
            return 0;
        }
        drag_commits_after_releasing_capture(host);
        if(drag_commit_only){DestroyWindow(host);return 0;}
        fast_drag_reaches_its_target(host);
        const std::vector<std::string> source{"std::cout << \"Acceso autorizado\";","#include <iostream>","return 0;\n}","int main() {"};
        const auto dpi=static_cast<int>(GetDpiForWindow(host));const auto px=[&](int value){return MulDiv(value,dpi,96);};
        MoveWindow(host,40,40,px(800),px(640),TRUE);
        int changes=0;std::unique_ptr<OrderCodeView> instance;
        {ReentrantPaint probe;instance=std::make_unique<OrderCodeView>(host,source,std::vector<std::size_t>{0,1,2,3},[&]{++changes;});
            const auto initial_placements=probe.placements;
            MoveWindow(instance->window(),px(10),px(10),px(700),instance->height_for_width(px(700)),TRUE);pump();
            check(probe.host_creations==1&&probe.row_creations==4&&probe.layout_requested&&!probe.dc_failed&&probe.placements>initial_placements,
                "The regression did not paint the real host/children during construction and final layout");
            check(changes==0&&instance->order()==std::vector<std::size_t>({0,1,2,3}),"Reentrant creation/layout altered the authored arrangement");
            for(std::size_t i=0;i<source.size();++i)check(edit_text(GetDlgItem(instance->window(),100+static_cast<int>(i)))==source[i],
                "Reentrant creation/layout lost an original source fragment");}
        auto& view=*instance;
        check(GetDlgItem(view.window(),100)&&GetDlgItem(view.window(),103),"Missing original fragment rows");
        const auto syntax=[&](std::size_t index,std::string_view token,COLORREF expected){const auto row=GetDlgItem(view.window(),100+static_cast<int>(index));const auto at=source[index].find(token);
            check(at!=std::string::npos,"The real syntax fixture token is missing");const auto format=character(row,static_cast<LONG>(wide(std::string_view(source[index]).substr(0,at)).size()));
            check(format.yHeight==rich_text_height()&&std::wstring_view(format.szFaceName)==reader_theme::code_font_face(),"The first-open fragment lost its original18sp monospace font");
            if(!reader_theme::palette(row).high_contrast)check(format.crTextColor==expected&&!(format.dwEffects&CFE_AUTOCOLOR),"A first-open fragment is black, automatic or lost the original Android syntax palette");};
        syntax(0,"std",RGB(171,141,227));syntax(0,"<<",RGB(231,155,186));syntax(0,"\"Acceso",RGB(122,195,141));syntax(0,";",RGB(225,230,235));
        syntax(1,"#include",RGB(92,156,231));syntax(1,"<iostream>",RGB(122,195,141));syntax(2,"return",RGB(231,155,186));syntax(2,"0",RGB(224,151,109));
        const auto bounds=[&](HWND row){RECT rect{};GetWindowRect(row,&rect);MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&rect),2);return rect;};
        const auto first_rect=bounds(GetDlgItem(view.window(),100)),short_rect=bounds(GetDlgItem(view.window(),103));
        check(first_rect.left>=px(18)&&first_rect.top==px(2)&&short_rect.right-short_rect.left<first_rect.right-first_rect.left,
            "Code fragments are full-width text rows instead of independent Android content-width chips");
        auto previous_rect=first_rect;
        for(int index=0;index<4;++index){const auto row=GetDlgItem(view.window(),100+index);const auto rect=bounds(row);RECT format{};SendMessageW(row,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
            POINTL first{};SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
            check(std::abs(first.x-px(16))<=1&&std::abs(first.y-px(8))<=1,"A chip does not preserve Android16dp horizontal and8dp vertical text padding");
            if(index)check(rect.top-previous_rect.bottom==px(4),"Adjacent code chips lost the original2dp top/bottom margins");
            const auto region=CreateRectRgn(0,0,0,0);check(GetWindowRgn(row,region)!=ERROR&&!PtInRegion(region,0,0)&&PtInRegion(region,(rect.right-rect.left)/2,(rect.bottom-rect.top)/2),"Code-card text is not clipped inside its original rounded outline");DeleteObject(region);previous_rect=rect;
        }
        const auto source_row=GetDlgItem(view.window(),100);const auto space=static_cast<LONG>(source[0].find(' '));POINTL before_space{},after_space{},letter0{},letter1{};
        SendMessageW(source_row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&before_space),space);SendMessageW(source_row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&after_space),space+1);
        SendMessageW(source_row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&letter0),0);SendMessageW(source_row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&letter1),1);
        check((after_space.x-before_space.x)*100<(letter1.x-letter0.x)*112&&
            (after_space.x-before_space.x)*100>(letter1.x-letter0.x)*88,
            "Code-card spaces no longer occupy the full monospace character width");
        // ClearType may leave only two fully opaque pixels in a thin numeral.
        // Measure its actual antialiased coverage against the known dark gutter,
        // retaining the same center tolerance and the zoom-growth assertion.
        const auto number_ink=[](COLORREF value){return GetRValue(value)>28||GetGValue(value)>28||GetBValue(value)>28;};
        int normal_number_ink_height=0;
        if(!reader_theme::palette(view.window()).high_contrast){Drawing raster(view.window());
            check(GetPixel(raster.dc,px(1),first_rect.top+px(5))==RGB(20,20,20),"The numbered gutter lost Android's inset surface");
            check(GetPixel(raster.dc,first_rect.left+px(16),first_rect.top+px(4))==RGB(25,25,25),"The code chip lost Android's low surface");
            int edge=0;for(int y=first_rect.top;y<first_rect.top+px(2);++y)for(int x=first_rect.left+px(20);x<first_rect.right-px(20);++x)if(GetPixel(raster.dc,x,y)==RGB(74,74,74))++edge;
            check(edge>0,"The first-open Android chip outline was not actually painted");
            for(const auto& item:view.reading_regions()){const auto rect=bounds(item.first);const auto saved=SaveDC(raster.dc);SetViewportOrgEx(raster.dc,rect.left,rect.top,nullptr);
                // Print the complete real child. An artificial inset here used
                // to preserve the parent's outline even if the child erased it.
                SendMessageW(item.first,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(raster.dc),PRF_CLIENT|PRF_ERASEBKGND);RestoreDC(raster.dc,saved);}
            bool purple=false,green=false,blue=false,pink=false;RECT area{};GetClientRect(view.window(),&area);GdiFlush();
            for(int y=0;y<area.bottom;++y)for(int x=first_rect.left;x<area.right;++x){const auto ink=raster.pixel(x,y);purple|=ink==RGB(171,141,227);green|=ink==RGB(122,195,141);blue|=ink==RGB(92,156,231);pink|=ink==RGB(231,155,186);}
            check(purple&&green&&blue&&pink,"Real first-open RichEdit rendering omitted syntax ink despite metadata");
            int composed_edge=0;for(int y=first_rect.top;y<first_rect.top+px(2);++y)for(int x=first_rect.left+px(20);x<first_rect.right-px(20);++x)
                if(raster.pixel(x,y)==RGB(74,74,74))++composed_edge;
            check(composed_edge>0,"Printing the complete real code child erased its original chip outline");
            int top=raster.height,bottom=-1;for(int y=first_rect.top;y<first_rect.bottom;++y)for(int x=0;x<first_rect.left;++x)if(number_ink(raster.pixel(x,y))){top=std::min(top,y);bottom=std::max(bottom,y);}
            normal_number_ink_height=bottom-top+1;
            check(normal_number_ink_height>0&&std::abs((top+bottom)-(first_rect.top+first_rect.bottom))<=px(8),
                (std::string("The original18sp line number is absent or no longer centered beside its fragment: ")+std::to_string(normal_number_ink_height)+" ink top/bottom="+std::to_string(top)+","+std::to_string(bottom)+" card top/bottom="+std::to_string(first_rect.top)+","+std::to_string(first_rect.bottom)).c_str());
        }
        const auto shaded_start=static_cast<LONG>(source[0].find("Acceso"));CHARRANGE shaded{shaded_start,shaded_start+6},old_selection{};
        SendMessageW(source_row,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&old_selection));SendMessageW(source_row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&shaded));
        CHARFORMAT2W pending{};pending.cbSize=sizeof(pending);pending.dwMask=CFM_COLOR;pending.crTextColor=RGB(110,115,120);
        SendMessageW(source_row,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&pending));SendMessageW(source_row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old_selection));
        click(GetDlgItem(view.window(),101));click(GetDlgItem(view.window(),103));
        check(view.order()==std::vector<std::size_t>({1,3,0,2}),"Click ordering differs from Android nextTapPosition");
        check(character(source_row,shaded_start).crTextColor==pending.crTextColor,"Reordering repainted an existing pending word shade with syntax or system ink");
        view.set_enabled(false);check(character(source_row,shaded_start).crTextColor==pending.crTextColor&&IsWindowEnabled(source_row),"Locking a correct arrangement replaced its existing word shade or dimmed syntax");view.set_enabled(true);
        SendMessageW(source_row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&shaded));pending.crTextColor=RGB(122,195,141);SendMessageW(source_row,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&pending));SendMessageW(source_row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old_selection));
        const auto regions=view.reading_regions();check(regions[0].second==source[1]&&regions[1].second==source[3],"Narration did not follow visible row order");
        CHARRANGE unselected{};const auto first=GetDlgItem(view.window(),101);
        SendMessageW(first,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&unselected));
        SendMessageW(first,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(12,8));
        CHARRANGE after_double{};SendMessageW(first,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_double));
        check(after_double.cpMin==after_double.cpMax,"Double click selected fragment text instead of retaining row interaction");
        SendMessageW(GetDlgItem(view.window(),102),WM_SYSKEYDOWN,VK_UP,0);
        check(view.order()==std::vector<std::size_t>({1,3,2,0}),"Accessible reorder lost stable source identity");
        view.reset();const auto origin=GetDlgItem(view.window(),100),target=GetDlgItem(view.window(),103);
        RECT a{},b{};GetWindowRect(origin,&a);GetWindowRect(target,&b);
        SendMessageW(origin,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));
        // Also retain the sustained-drag path after the fast gesture above.
        // Sleep(25) can span only one coarse GetTickCount64 tick on Windows.
        const auto ready=GetTickCount64()+20;while(GetTickCount64()<ready)Sleep(1);
        SendMessageW(origin,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(12,b.top-a.top+8));
        BYTE opacity=0;DWORD flags=0;check(GetLayeredWindowAttributes(origin,nullptr,&opacity,&flags)&&opacity==140&&(flags&LWA_ALPHA),"A real drag did not retain Android's55-percent source opacity");
        SendMessageW(origin,WM_LBUTTONUP,0,MAKELPARAM(12,b.top-a.top+8));
        check(!(GetWindowLongPtrW(origin,GWL_EXSTYLE)&WS_EX_LAYERED),"Finishing a drag retained source transparency");
        check(view.order()==std::vector<std::size_t>({1,2,3,0}),"Actual drag did not move the stable fragment to its target");
        const auto before=view.order();SendMessageW(origin,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);
        check(view.order()==before,"Mouse wheel started a reorder");
        const auto wheel_row=GetDlgItem(view.window(),103);
        SendMessageW(wheel_row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));
        SendMessageW(wheel_row,WM_MOUSEWHEEL,MAKEWPARAM(MK_LBUTTON,-WHEEL_DELTA),0);
        SendMessageW(wheel_row,WM_LBUTTONUP,0,MAKELPARAM(12,8));
        check(view.order()==before,"A wheel inside a pending mouse press became a click-to-order");
        view.set_enabled(false);click(wheel_row);check(view.order()==before&&IsWindowEnabled(wheel_row),"Solved ordering changed or dimmed its original syntax text");
        view.set_enabled(true);view.set_wrap(true);const auto narrow=view.height_for_width(px(180));view.set_wrap(false);
        check(narrow>view.height_for_width(px(180)),"Line wrapping did not use the actual resized text geometry");
        view.set_text_scale(150);check(view.order()==before&&changes>=4,"Presentation changed authored fragment order");
        check(view.reading_regions()[0].second==source[before[0]],"Source text changed during wrapping/zoom");
        check(character(source_row,shaded_start).crTextColor==RGB(122,195,141)&&character(source_row,0).yHeight==rich_text_height(),
            "Restoring a shaded word or scaling/reflowing replaced its original syntax or base font");
        MoveWindow(view.window(),px(10),px(10),px(700),view.height_for_width(px(700)),TRUE);pump();
        if(normal_number_ink_height>0){Drawing scaled(view.window());const auto first_scaled=bounds(GetDlgItem(view.window(),100+static_cast<int>(view.order().front())));GdiFlush();
            int top=scaled.height,bottom=-1;for(int y=first_scaled.top;y<first_scaled.bottom;++y)for(int x=0;x<first_scaled.left;++x)if(number_ink(scaled.pixel(x,y))){top=std::min(top,y);bottom=std::max(bottom,y);}
            check(bottom-top+1>normal_number_ink_height,"The painted line number stayed tiny while its code increased to150percent");}
        {std::vector<std::string> many_fragments(112,";");many_fragments.front()="111";OrderCodeView many(host,std::move(many_fragments),{},{});many.set_text_scale(150);(void)many.height_for_width(px(700));
            const auto row=GetDlgItem(many.window(),100);POINTL start{},end{};SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&start),0);SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),3);
            RECT row_bounds{};GetWindowRect(row,&row_bounds);MapWindowPoints(nullptr,many.window(),reinterpret_cast<POINT*>(&row_bounds),2);
            check(row_bounds.left>=end.x-start.x+px(3)+1,"Three-digit original line numbers are clipped by a fixed-width gutter");}
        view.reset();for(const auto i:{3,2,1,0})click(GetDlgItem(view.window(),100+i));
        const auto incorrect=view.order();view.restart_tap_sequence();
        check(view.order()==incorrect,"Retry reset destroyed the student's existing arrangement");
        click(GetDlgItem(view.window(),101));check(view.order().front()==1,"An incorrect complete click sequence could not be retried without erasing the draft");
        std::cout<<"PASS code ordering: original rows, click order, real drag, wheel exclusion, keyboard, reading order, wrap, scale, solved state\n";
        DestroyWindow(host);return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL code ordering: "<<error.what()<<'\n';DestroyWindow(host);return 1;}
}
