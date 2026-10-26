#include "presentation/windows/reader_theme.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <oleacc.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace lgaed::presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::wstring text(HWND field){std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(field))+1,L'\0');
    result.resize(static_cast<std::size_t>(GetWindowTextW(field,result.data(),static_cast<int>(result.size()))));return result;}
SCROLLINFO scroll(HWND field,int axis){SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;
    check(GetScrollInfo(field,axis,&info)!=FALSE,"Cannot read the real native scroll state");return info;}
bool equal(const SCROLLINFO& first,const SCROLLINFO& second){return first.nMin==second.nMin&&first.nMax==second.nMax&&
    first.nPage==second.nPage&&first.nPos==second.nPos&&first.nTrackPos==second.nTrackPos;}
RECT bar(HWND field,LONG axis){SCROLLBARINFO info{};info.cbSize=sizeof(info);
    check(GetScrollBarInfo(field,axis,&info)!=FALSE,"Cannot read the native scrollbar geometry");
    check(!(info.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)),"The test did not expose a real scrollbar");
    RECT window{};GetWindowRect(field,&window);auto result=info.rcScrollBar;OffsetRect(&result,-window.left,-window.top);return result;}
void pump(){MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessageW(&event);}}
int native_wheel_delta(HWND field){
    SetFocus(field);pump();SendMessageW(field,WM_VSCROLL,SB_TOP,0);pump();POINT at{100,100};ClientToScreen(field,&at);
    const auto before=scroll(field,SB_VERT).nPos;
    SendMessageW(field,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),MAKELPARAM(at.x,at.y));pump();
    return scroll(field,SB_VERT).nPos-before;
}
struct Field {
    HWND host=nullptr,field=nullptr;HFONT font=nullptr;
    explicit Field(bool themed=true){host=CreateWindowExW(0,L"STATIC",L"Isolated native scrollbar QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        30,30,700,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the scrollbar QA host");
        field=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_DISABLENOSCROLL,20,20,480,300,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(field!=nullptr,"Cannot create the actual RichEdit under test");
        if(themed)reader_theme::apply(field,reader_theme::Surface::field);
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
        check(font!=nullptr,"Create the native source font");SendMessageW(field,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(field,EM_SETTARGETDEVICE,0,1);
        std::wstring source;
        for(unsigned line=0;line<80;++line)source+=L"\t    std::cout << \"literal  spaces\"; "+std::wstring(220,L'x')+L"\r\n";
        SetWindowTextW(field,source.c_str());SendMessageW(field,EM_LINESCROLL,0,20);
        CHARRANGE selection{7,19};SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        RedrawWindow(field,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);pump();
    }
    ~Field(){if(host)DestroyWindow(host);if(font)DeleteObject(font);}
};
struct HiddenFrameProbe {
    unsigned exposed_frames=0;
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value){
        auto& self=*reinterpret_cast<HiddenFrameProbe*>(value);
        if((message==WM_NCPAINT||message==WM_SIZE)&&(GetWindowLongPtrW(window,GWL_STYLE)&WS_VSCROLL))++self.exposed_frames;
        return DefSubclassProc(window,message,wp,lp);
    }
};
void hidden_viewport_frame(HWND host){
    const auto viewport=CreateWindowExW(0,L"STATIC",L"Page viewport",WS_CHILD|WS_VISIBLE,
        10,10,440,260,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(viewport!=nullptr,"Create the actual custom page viewport");
    reader_theme::apply(viewport,reader_theme::Surface::canvas);
    HiddenFrameProbe probe;
    check(SetWindowSubclass(viewport,HiddenFrameProbe::proc,9919,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,
        "Observe native frame layout and painting before default processing");
    SCROLLINFO initial{sizeof(initial),SIF_RANGE|SIF_PAGE|SIF_POS,0,1200,100,20};
    SetScrollInfo(viewport,SB_VERT,&initial,FALSE);ShowScrollBar(viewport,SB_VERT,FALSE);
    check(probe.exposed_frames>0,"The baseline did not reproduce SetScrollInfo's transient scrollbar layout");
    probe.exposed_frames=0;
    reader_theme::set_scrollbar_frame_hidden(viewport,true);
    RECT before{};GetClientRect(viewport,&before);
    for(int cycle=0;cycle<12;++cycle){
        SCROLLINFO requested{sizeof(requested),SIF_RANGE|SIF_PAGE|SIF_POS,0,1300+cycle*20,
            static_cast<UINT>(100+cycle%3),cycle*30};
        SetScrollInfo(viewport,SB_VERT,&requested,FALSE);
        const auto actual=scroll(viewport,SB_VERT);
        check(actual.nMin==requested.nMin&&actual.nMax==requested.nMax&&actual.nPage==requested.nPage&&actual.nPos==requested.nPos,
            "Suppressing a viewport frame changed its native range, page or position");
        ShowScrollBar(viewport,SB_VERT,FALSE);
        RECT after{};GetClientRect(viewport,&after);
        check(EqualRect(&before,&after)!=FALSE,"A hidden page scrollbar briefly reserved client width");
    }
    check(probe.exposed_frames==0,"A page scrollbar reached native painting before being hidden");
    DestroyWindow(viewport);
    std::cout<<"PASS page scrollbar: reproduced synchronous native frame, suppressed before paint, full client width and native scroll metadata preserved\n";
}
void paint_without_mutating_native_state(Field& value){
    const auto content=text(value.field);const auto style=GetWindowLongPtrW(value.field,GWL_STYLE);
    const auto vertical=scroll(value.field,SB_VERT),horizontal=scroll(value.field,SB_HORZ);
    CHARRANGE selection{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
    RECT bounds{};GetWindowRect(value.field,&bounds);const auto screen=GetDC(value.field),memory=CreateCompatibleDC(screen);
    const auto bitmap=CreateCompatibleBitmap(screen,bounds.right-bounds.left,bounds.bottom-bounds.top);ReleaseDC(value.field,screen);
    const auto old=SelectObject(memory,bitmap);RECT area{0,0,bounds.right-bounds.left,bounds.bottom-bounds.top};
    const auto seed=CreateSolidBrush(RGB(255,0,255));FillRect(memory,&area,seed);DeleteObject(seed);
    reader_theme::paint_scrollbars(value.field,memory);
    if(!reader_theme::palette(value.field).high_contrast){
        const auto v=bar(value.field,OBJID_VSCROLL),h=bar(value.field,OBJID_HSCROLL);
        check(GetPixel(memory,v.left+1,(v.top+v.bottom)/2)==reader_theme::background(value.field),"The vertical track retained a white native strip");
        check(GetPixel(memory,(h.left+h.right)/2,h.top+1)==reader_theme::background(value.field),"The horizontal track retained a white native strip");
        check(GetPixel(memory,20,20)==RGB(255,0,255),"Scrollbar painting touched the source/client area");
    }
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
    CHARRANGE after{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(content==text(value.field)&&style==GetWindowLongPtrW(value.field,GWL_STYLE)&&selection.cpMin==after.cpMin&&selection.cpMax==after.cpMax,
        "Scrollbar styling altered source whitespace, selection or native styles");
    check(equal(vertical,scroll(value.field,SB_VERT))&&equal(horizontal,scroll(value.field,SB_HORZ)),"Scrollbar styling introduced a parallel or changed scroll position");
}
struct TrackingPaintProbe {
    struct Sample {UINT message,request;COLORREF ink;SCROLLINFO position;};
    int axis=SB_HORZ;
    bool raster=true;
    std::vector<Sample> samples;
    static Sample measure(HWND window,int axis,UINT message,UINT request){
        Sample sample{message,request,CLR_INVALID,{sizeof(SCROLLINFO),SIF_ALL}};
        SCROLLBARINFO info{sizeof(info)};RECT outer{};
        if(GetScrollBarInfo(window,axis==SB_HORZ?OBJID_HSCROLL:OBJID_VSCROLL,&info)&&GetWindowRect(window,&outer)){
            GetScrollInfo(window,axis,&sample.position);
            const auto dc=GetWindowDC(window);
            const auto x=axis==SB_HORZ?(info.rcScrollBar.left+info.rcScrollBar.right)/2:info.rcScrollBar.left+1;
            const auto y=axis==SB_HORZ?info.rcScrollBar.top+1:(info.rcScrollBar.top+info.rcScrollBar.bottom)/2;
            sample.ink=GetPixel(dc,x-outer.left,y-outer.top);ReleaseDC(window,dc);
        }
        return sample;
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value){
        auto& self=*reinterpret_cast<TrackingPaintProbe*>(value);
        const auto result=DefSubclassProc(window,message,wp,lp);
        if(IsWindow(window)&&(message==WM_HSCROLL||message==WM_VSCROLL||message==WM_NCLBUTTONDOWN||message==WM_NCMOUSEMOVE||message==WM_NCMOUSELEAVE||
            message==WM_MOUSEMOVE||message==WM_MOUSELEAVE||message==WM_LBUTTONUP||message==WM_CAPTURECHANGED||message==WM_SETCURSOR||
            message==EM_SHOWSCROLLBAR||message==WM_PAINT||message==WM_NCPAINT)){
            if(self.raster)self.samples.push_back(measure(window,self.axis,message,LOWORD(wp)));
            else{
                Sample sample{message,LOWORD(wp),CLR_INVALID,{sizeof(SCROLLINFO),SIF_ALL}};
                GetScrollInfo(window,self.axis,&sample.position);self.samples.push_back(sample);
            }
        }
        return result;
    }
};
void native_thumb_tracking(Field& value,int axis,bool visible_screen=false){
    if(reader_theme::palette(value.field).high_contrast)return;
    SetForegroundWindow(value.host);SetFocus(value.field);pump();
    const auto original_text=text(value.field);const auto style=GetWindowLongPtrW(value.field,GWL_STYLE);
    CHARRANGE original_selection{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&original_selection));
    const bool sideways=axis==SB_HORZ;const UINT scroll_message=sideways?WM_HSCROLL:WM_VSCROLL;
    SendMessageW(value.field,scroll_message,sideways?SB_LEFT:SB_TOP,0);pump();
    SCROLLBARINFO info{sizeof(info)};check(GetScrollBarInfo(value.field,sideways?OBJID_HSCROLL:OBJID_VSCROLL,&info)!=FALSE,"Read native thumb before tracking");
    const auto initial=scroll(value.field,axis);
    RECT client_bounds{};GetClientRect(value.field,&client_bounds);
    const auto extent=sideways?client_bounds.right:client_bounds.bottom;
    if(initial.nMax<=extent||!initial.nPage||(info.rgstate[3]&STATE_SYSTEM_UNAVAILABLE))
        std::cerr<<"Tracking fixture: axis="<<axis<<", range="<<initial.nMax<<", page="<<initial.nPage<<", extent="<<extent<<", thumb state="<<info.rgstate[3]<<'\n';
    check(initial.nMax>extent&&initial.nPage>0&&!(info.rgstate[3]&STATE_SYSTEM_UNAVAILABLE),"The native tracking fixture must expose a document larger than the viewport");
    POINT original_pointer{};GetCursorPos(&original_pointer);
    const POINT first=sideways?POINT{info.rcScrollBar.left+(info.xyThumbTop+info.xyThumbBottom)/2,(info.rcScrollBar.top+info.rcScrollBar.bottom)/2}:
        POINT{(info.rcScrollBar.left+info.rcScrollBar.right)/2,info.rcScrollBar.top+(info.xyThumbTop+info.xyThumbBottom)/2};
    const POINT last=sideways?POINT{info.rcScrollBar.right-info.dxyLineButton-30,first.y}:POINT{first.x,info.rcScrollBar.bottom-info.dxyLineButton-30};
    check(WindowFromPoint(first)==value.field,"The isolated scrollbar fixture must own the mouse target");
    check(SendMessageW(value.field,WM_NCHITTEST,0,MAKELPARAM(first.x,first.y))==(sideways?HTHSCROLL:HTVSCROLL),"The visible scrollbar must retain native hit testing");
    TrackingPaintProbe probe;probe.axis=axis;probe.raster=!visible_screen;
    check(SetWindowSubclass(value.field,TrackingPaintProbe::proc,9920,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,"Observe actual native thumb tracking");
    SetCursorPos(first.x,first.y);
    // Real button state matters to USER's modal scrollbar loop: sent mouse
    // messages alone never exercise thumb movement. Keep native hit testing,
    // capture and WM_HSCROLL generation in charge of this isolated fixture.
    std::atomic<bool> finished=false,input_ok=true;
    TrackingPaintProbe::Sample during{};bool button_down=false;
    struct VisibleFrame{bool released;double milliseconds;COLORREF ink;};std::vector<VisibleFrame> frames;frames.reserve(12);
    const POINT screen_pixel=sideways?POINT{(info.rcScrollBar.left+info.rcScrollBar.right)/2,info.rcScrollBar.top+1}:
        POINT{info.rcScrollBar.left+1,(info.rcScrollBar.top+info.rcScrollBar.bottom)/2};
    const auto read_screen=[screen_pixel]{const auto dc=GetDC(nullptr);const auto ink=GetPixel(dc,screen_pixel.x,screen_pixel.y);ReleaseDC(nullptr,dc);return ink;};
    COLORREF screen_after_window_read=CLR_INVALID;
    const auto dpi_context=GetThreadDpiAwarenessContext();
    std::thread input([field=value.field,first,last,axis,dpi_context,visible_screen,read_screen,&finished,&input_ok,&during,&button_down,&frames,&screen_after_window_read]{
        const auto previous_dpi=SetThreadDpiAwarenessContext(dpi_context);
        struct RestoreDpi{DPI_AWARENESS_CONTEXT previous;~RestoreDpi(){if(previous)SetThreadDpiAwarenessContext(previous);}} restore_dpi{previous_dpi};
        if(!previous_dpi){input_ok=false;finished=true;return;}
        Sleep(25);
        if(WindowFromPoint(first)!=field){input_ok=false;finished=true;return;}
        INPUT down{};down.type=INPUT_MOUSE;down.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        if(SendInput(1,&down,sizeof(down))!=1)input_ok=false;
        Sleep(75);SetCursorPos(last.x,last.y);
        const auto sample_screen=[&](bool released){
            const auto start=std::chrono::steady_clock::now();
            for(const int milliseconds:{1,5,10,25,50,100}){
                const auto deadline=start+std::chrono::milliseconds(milliseconds);
                while(std::chrono::steady_clock::now()<deadline){
                    if(deadline-std::chrono::steady_clock::now()>std::chrono::milliseconds(2))Sleep(1);
                    else YieldProcessor();
                }
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                frames.push_back({released,elapsed,read_screen()});
            }
        };
        if(visible_screen){
            // Never read a window DC during this phase: GetPixel on the UI
            // thread can flush its GDI batch and conceal a presentation bug.
            sample_screen(false);
            button_down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
            during=TrackingPaintProbe::measure(field,axis,0,0);
            Sleep(20);screen_after_window_read=read_screen();
        }else{
            Sleep(25);button_down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
            during=TrackingPaintProbe::measure(field,axis,0,0);Sleep(50);
        }
        INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;
        if(SendInput(1,&up,sizeof(up))!=1){input_ok=false;PostMessageW(field,WM_LBUTTONUP,0,0);}
        if(visible_screen)sample_screen(true);
        finished=true;
    });
    while(!finished){MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);pump();}
    input.join();pump();
    const auto immediate=TrackingPaintProbe::measure(value.field,axis,0,0).ink;
    const auto tracked=scroll(value.field,axis);
    SetCursorPos(original_pointer.x,original_pointer.y);pump();RemoveWindowSubclass(value.field,TrackingPaintProbe::proc,9920);
    const auto expected=reader_theme::background(value.field);
    const bool moved=std::any_of(probe.samples.begin(),probe.samples.end(),[&](const auto& sample){
        return sample.message==scroll_message&&sample.request==SB_THUMBTRACK&&sample.position.nTrackPos>initial.nPos;});
    const bool dark=!probe.raster||std::all_of(probe.samples.begin(),probe.samples.end(),[&](const auto& sample){return sample.ink==expected;});
    const bool visible_dark=!visible_screen||std::all_of(frames.begin(),frames.end(),[&](const auto& frame){return frame.ink==expected;});
    if(!input_ok||!moved||!button_down||during.ink!=expected||immediate!=expected||!dark||!visible_dark){
        std::cerr<<"Tracking diagnostics: axis="<<axis<<", range="<<initial.nMax<<", page="<<initial.nPage<<", final position="<<tracked.nPos<<", immediate track="<<immediate<<", expected="<<expected
            <<", pressed="<<button_down<<", during track="<<during.ink<<", during position="<<during.position.nPos<<", during tracking="<<during.position.nTrackPos<<'\n';
        for(const auto& sample:probe.samples)std::cerr<<"  message="<<sample.message<<", request="<<sample.request<<", position="<<sample.position.nPos<<", tracking="<<sample.position.nTrackPos<<", track color="<<sample.ink<<'\n';
        for(const auto& frame:frames)std::cerr<<"  visible screen "<<(frame.released?"after release":"during hold")<<" at "<<frame.milliseconds<<" ms: "<<frame.ink<<'\n';
        if(visible_screen)std::cerr<<"  visible screen 20 ms after window DC read: "<<screen_after_window_read<<'\n';
    }
    check(input_ok,"Native mouse input was not accepted by the isolated scrollbar fixture");
    check(moved,"The test did not move the actual native thumb");
    check(button_down,"The during-tracking sample must run before releasing the native mouse button");
    check(during.ink==expected,"USER repainted a light nonclient track after the scroll callback while the native thumb was held");
    check(visible_dark,"The visible desktop showed a light native track before its window raster was observed");
    check(immediate==expected,"Native thumb release left a light nonclient track before any later paint");
    check(dark,"A native tracking/capture/leave message repainted a light nonclient track");
    CHARRANGE selection{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
    check(text(value.field)==original_text&&GetWindowLongPtrW(value.field,GWL_STYLE)==style&&selection.cpMin==original_selection.cpMin&&selection.cpMax==original_selection.cpMax,
        "Native tracking changed text, selection or native scrollbar styles");
    const auto final_range=scroll(value.field,axis);SCROLLBARINFO final_geometry{sizeof(final_geometry)};
    check(GetScrollBarInfo(value.field,sideways?OBJID_HSCROLL:OBJID_VSCROLL,&final_geometry)!=FALSE,"Read the native scrollbar geometry after tracking");
    check(final_range.nMin==initial.nMin&&final_range.nMax==initial.nMax&&final_range.nPage==initial.nPage,
        "Native tracking changed the source range or page size");
    check(EqualRect(&info.rcScrollBar,&final_geometry.rcScrollBar)&&info.dxyLineButton==final_geometry.dxyLineButton,
        "Native tracking moved or resized the scrollbar frame and hit targets");
    check(SendMessageW(value.field,WM_NCHITTEST,0,MAKELPARAM(last.x,last.y))==(sideways?HTHSCROLL:HTVSCROLL),
        "Native scrollbar hit testing changed after tracking");
    std::cout<<"PASS native "<<(sideways?"horizontal":"vertical")<<(visible_screen?" visible presentation":" window raster")<<" thumb tracking: dark during hold and after release, native input, source, selection, range and geometry preserved\n";
}
void slider_colors_and_native_state(HWND host){
    const auto slider=CreateWindowExW(0,TRACKBAR_CLASSW,L"Content size",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_NOTICKS,
        10,10,360,48,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(slider!=nullptr,"Cannot create the native slider fixture");reader_theme::apply(slider,reader_theme::Surface::card);
    SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(slider,TBM_SETPOS,TRUE,40);
    const auto screen=GetDC(slider),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,360,48);ReleaseDC(slider,screen);
    const auto old=SelectObject(dc,bitmap);SendMessageW(slider,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    RECT thumb{};SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    if(!reader_theme::palette(slider).high_contrast)check(GetPixel(dc,(thumb.left+thumb.right)/2,(thumb.top+thumb.bottom)/2)==reader_theme::palette(slider).accent,
        "Slider thumb retained Windows blue instead of the route accent");
    check(SendMessageW(slider,TBM_GETPOS,0,0)==40,"Slider styling changed the native value");
    SendMessageW(slider,WM_KEYDOWN,VK_RIGHT,0);check(SendMessageW(slider,TBM_GETPOS,0,0)==41,"Slider styling consumed keyboard navigation");
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(slider);
}
void switch_colors_and_native_state(HWND host){
    const auto control=CreateWindowExW(0,L"BUTTON",L"Continuar",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_RIGHTBUTTON,
        10,70,360,48,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(control!=nullptr,"Cannot create the native options switch fixture");reader_theme::apply(control,reader_theme::Surface::card);reader_theme::switch_button(control);
    const auto original_style=GetWindowLongPtrW(control,GWL_STYLE);const auto original_text=text(control);const auto dpi=static_cast<int>(GetDpiForWindow(control));
    const auto px=[dpi](int value){return std::max(1,MulDiv(value,dpi?dpi:96,96));};
    const auto screen=GetDC(control),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,360,48);ReleaseDC(control,screen);const auto previous=SelectObject(dc,bitmap);
    RECT client{};GetClientRect(control,&client);const int padding=px(3),width=std::min(px(36),static_cast<int>(client.right)-padding*2),height=std::min(px(20),static_cast<int>(client.bottom));
    const int left=client.right-padding-width,cy=client.bottom/2;
    const auto blend=[](COLORREF ink,COLORREF bg,unsigned alpha){const auto c=[alpha](unsigned a,unsigned b){return (a*alpha+b*(255-alpha)+127)/255;};return RGB(c(GetRValue(ink),GetRValue(bg)),c(GetGValue(ink),GetGValue(bg)),c(GetBValue(ink),GetBValue(bg)));};
    SendMessageW(control,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(control,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    if(!reader_theme::palette(control).high_contrast){
        check(GetPixel(dc,left+height/2,cy)==RGB(145,148,158),"Unchecked switch thumb does not use Android's exact neutral tint");
        check(GetPixel(dc,client.right-padding-px(4),cy)==blend(RGB(120,124,134),reader_theme::background(control),90),"Unchecked switch track does not composite Android's alpha-90 tint");
    }
    SendMessageW(control,BM_SETCHECK,BST_CHECKED,0);SendMessageW(control,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    if(!reader_theme::palette(control).high_contrast){
        check(GetPixel(dc,client.right-padding-height/2,cy)==reader_theme::palette(control).accent,"Checked switch thumb did not move right with the configured accent");
        check(GetPixel(dc,left+px(4),cy)==blend(reader_theme::palette(control).accent,reader_theme::background(control),135),"Checked switch track does not composite Android's alpha-135 tint");
    }
    check(SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED&&GetWindowLongPtrW(control,GWL_STYLE)==original_style&&text(control)==original_text,
        "Drawing a switch changed its native checkbox state, style or accessible caption");
    SetFocus(control);SendMessageW(control,WM_KEYDOWN,VK_SPACE,1);SendMessageW(control,WM_KEYUP,VK_SPACE,1);
    check(GetFocus()==control&&SendMessageW(control,BM_GETCHECK,0,0)==BST_UNCHECKED,"Switch styling consumed native Space toggling or moved keyboard focus");
    const auto baseline=CreateWindowExW(0,L"BUTTON",L"Continuar",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_RIGHTBUTTON,
        10,130,360,48,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(baseline!=nullptr,"Cannot create the unstyled checkbox baseline");
    const auto disabled=SendMessageW(control,BM_GETCHECK,0,0);SendMessageW(baseline,BM_SETCHECK,disabled,0);EnableWindow(control,FALSE);EnableWindow(baseline,FALSE);
    // BM_CLICK injects native button messages even into a disabled HWND; it is
    // not OS hit-testing a user's click. Keep that programmatic native contract
    // identical rather than adding a switch-only input policy to the painter.
    SendMessageW(control,BM_CLICK,0,0);SendMessageW(baseline,BM_CLICK,0,0);const auto native_after=SendMessageW(baseline,BM_GETCHECK,0,0);
    check(!IsWindowEnabled(control)&&!IsWindowEnabled(baseline)&&SendMessageW(control,BM_GETCHECK,0,0)==native_after,"Switch painting changed disabled checkbox behaviour relative to its actual native baseline");EnableWindow(control,TRUE);
    reader_theme::switch_button(control,false);SendMessageW(control,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    check(GetWindowLongPtrW(control,GWL_STYLE)==original_style&&text(control)==original_text&&SendMessageW(control,BM_GETCHECK,0,0)==native_after,"Returning to ordinary checkbox presentation changed its native contract");
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(baseline);DestroyWindow(control);
    std::cout<<"PASS Android switches: exact checked/unchecked track and thumb tints, native checkbox state, Space, focus, disabled native baseline and reversible presentation\n";
}
}
int main(){const auto module=LoadLibraryW(L"Msftedit.dll");if(!module)return 2;
    try{INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&common)!=FALSE,"Common controls v6 are required");
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int baseline_wheel=0;
        {Field baseline(false);baseline_wheel=native_wheel_delta(baseline.field);}
        Field value;hidden_viewport_frame(value.host);slider_colors_and_native_state(value.host);switch_colors_and_native_state(value.host);std::cout<<"Actual scrollbar DPI="<<GetDpiForWindow(value.field)<<'\n';paint_without_mutating_native_state(value);
        native_thumb_tracking(value,SB_HORZ,true);native_thumb_tracking(value,SB_VERT,true);
        native_thumb_tracking(value,SB_HORZ);native_thumb_tracking(value,SB_VERT);
        // A directly sent WM_MOUSEWHEEL can be ignored by RichEdit when this
        // isolated host is inactive. Compare the same actual unstyled control;
        // physical wheel routing is also exercised in the application UI QA.
        check(native_wheel_delta(value.field)==baseline_wheel,"Styling changed the native response to the same wheel message");
        const auto before=scroll(value.field,SB_VERT);
        SendMessageW(value.field,WM_VSCROLL,SB_LINEDOWN,0);pump();
        check(scroll(value.field,SB_VERT).nPos>before.nPos,"Native vertical scrollbar movement no longer works");
        paint_without_mutating_native_state(value);
        SendMessageW(value.field,EM_LINESCROLL,15,0);paint_without_mutating_native_state(value);
        MoveWindow(value.field,20,20,420,260,TRUE);paint_without_mutating_native_state(value);
        if(!reader_theme::palette(value.field).high_contrast){RedrawWindow(value.field,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);pump();
            const auto v=bar(value.field,OBJID_VSCROLL);const auto actual=GetWindowDC(value.field);
            const auto ink=GetPixel(actual,v.left+1,(v.top+v.bottom)/2);ReleaseDC(value.field,actual);
            check(ink==reader_theme::background(value.field),"The automatic native nonclient repaint was not styled");}
        ShowScrollBar(value.field,SB_HORZ,FALSE);const auto hidden_style=GetWindowLongPtrW(value.field,GWL_STYLE);
        const auto dc=GetWindowDC(value.field);reader_theme::paint_scrollbars(value.field,dc);ReleaseDC(value.field,dc);
        check(GetWindowLongPtrW(value.field,GWL_STYLE)==hidden_style,"Painting re-enabled a hidden native scrollbar");
        std::cout<<"PASS native scrollbars: dark tracks, client clipping, native positions, unstyled wheel equivalence, vertical/horizontal movement, resize, selection and exact text\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL native scrollbars: "<<error.what()<<'\n';return 1;}}
