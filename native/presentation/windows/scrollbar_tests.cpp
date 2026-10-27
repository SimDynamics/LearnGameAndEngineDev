#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <imm.h>
#include <oleacc.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
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
std::vector<DWORD> captured_region(HDC device,HBITMAP bitmap,LONG width,LONG height,RECT region){
    if(width<=0||height<=0||region.left<0||region.top<0||region.right>width||region.bottom>height||IsRectEmpty(&region))return {};
    BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);format.bmiHeader.biWidth=width;
    format.bmiHeader.biHeight=-height;format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;
    std::vector<DWORD> pixels(static_cast<std::size_t>(width)*height),result;
    if(GetDIBits(device,bitmap,0,height,pixels.data(),&format,DIB_RGB_COLORS)!=height)return {};
    result.reserve(static_cast<std::size_t>(region.right-region.left)*(region.bottom-region.top));
    for(int y=region.top;y<region.bottom;++y)for(int x=region.left;x<region.right;++x)
        result.push_back(pixels[static_cast<std::size_t>(y)*width+x]&0x00FFFFFF);
    return result;
}
std::size_t changed_pixels(const std::vector<DWORD>& reference,const std::vector<DWORD>& actual){
    if(reference.empty()||reference.size()!=actual.size())return std::max(reference.size(),actual.size())+1;
    std::size_t changed=0;for(std::size_t at=0;at<reference.size();++at)if(reference[at]!=actual[at])++changed;return changed;
}
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
const wchar_t* compiler_host_class(){
    static const bool registered=[] {
        WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=DefWindowProcW;
        type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        type.hbrBackground=reader_theme::brush(reader_theme::Surface::canvas);
        type.lpszClassName=L"LearnGameAndEngineDev.ScrollbarEditorQA";
        check(RegisterClassExW(&type)!=0,"Register the actual lesson-pane-compatible editor host");return true;
    }();
    (void)registered;return L"LearnGameAndEngineDev.ScrollbarEditorQA";
}
struct Field {
    HWND host=nullptr,field=nullptr;HFONT font=nullptr;
    explicit Field(bool themed=true,bool compiler=false){host=CreateWindowExW(compiler?WS_EX_CONTROLPARENT:0,
        compiler?compiler_host_class():L"STATIC",L"Isolated native scrollbar QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE|
        (compiler?WS_CLIPCHILDREN|WS_CLIPSIBLINGS:0),
        30,30,700,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the scrollbar QA host");
        if(compiler)check(!(GetClassLongPtrW(host,GCL_STYLE)&(CS_OWNDC|CS_CLASSDC|CS_PARENTDC)),
            "The compiler host must match the lesson pane's compatible DC policy");
        field=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|(compiler?0:WS_BORDER|ES_DISABLENOSCROLL),20,20,480,300,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(field!=nullptr,"Cannot create the actual RichEdit under test");
        if(themed)reader_theme::apply(field,reader_theme::Surface::field);
        if(compiler){
            SendMessageW(field,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE|ENM_CHANGE|ENM_SELCHANGE);
            SendMessageW(field,EM_EXLIMITTEXT,0,2*1024*1024);
        }
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
void native_theme_owner_contract(){
    Field value;const auto original_text=text(value.field);const auto original_style=GetWindowLongPtrW(value.field,GWL_STYLE);bool has_theme=false;
    const auto original_extended=GetWindowLongPtrW(value.field,GWL_EXSTYLE);
    CHARRANGE original_selection{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&original_selection));
    check(!reader_theme::native_scrollbar_theme(value.field),"The plain field unexpectedly starts with a native-only policy");
    {
        lgaed::windows::CodeGutter gutter(value.field);
        check(reader_theme::native_scrollbar_theme(value.field),"The real gutter did not select a single native scrollbar renderer");
        const auto before=scroll(value.field,SB_HORZ);const auto track=bar(value.field,OBJID_HSCROLL);
        RECT window{};GetWindowRect(value.field,&window);const auto screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
        const auto bitmap=CreateCompatibleBitmap(screen,window.right-window.left,window.bottom-window.top);ReleaseDC(nullptr,screen);
        check(memory&&bitmap,"Create the native-owner print surface");const auto old=SelectObject(memory,bitmap);
        const RECT all{0,0,window.right-window.left,window.bottom-window.top};const auto seed=CreateSolidBrush(RGB(255,0,255));
        FillRect(memory,&all,seed);DeleteObject(seed);reader_theme::paint_scrollbars(value.field,memory);
        const bool untouched=GetPixel(memory,(track.left+track.right)/2,track.top+1)==RGB(255,0,255);
        SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
        check(untouched,"An opt-in native scrollbar still had a competing custom nonclient overlay");
        check(equal(before,scroll(value.field,SB_HORZ)),"Selecting the native scrollbar renderer changed its scroll state");
        // The fallback is also exclusively native when visual styles are not
        // available; the renderer never depends on a process/global setting.
        const auto themed=GetWindowTheme(value.field);const auto scrollbar=OpenThemeData(value.field,L"SCROLLBAR");
        has_theme=themed||scrollbar;
        if(scrollbar)CloseThemeData(scrollbar);
    }
    check(!reader_theme::native_scrollbar_theme(value.field),"Removing the gutter did not restore its prior scrollbar policy");
    check(GetWindowLongPtrW(value.field,GWL_EXSTYLE)==original_extended,"Removing the gutter changed the original extended window styles");
    reader_theme::use_native_scrollbar_theme(value.field);
    SendMessageW(value.field,EM_SETREADONLY,TRUE,0);
    {lgaed::windows::CodeGutter gutter(value.field);check(reader_theme::native_scrollbar_theme(value.field),"Read-only code did not retain its single native scrollbar owner");}
    check(reader_theme::native_scrollbar_theme(value.field),"Removing a gutter overwrote a pre-existing native scrollbar policy");
    SendMessageW(value.field,EM_SETREADONLY,FALSE,0);
    reader_theme::use_native_scrollbar_theme(value.field,false);
    CHARRANGE after{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    check(text(value.field)==original_text&&GetWindowLongPtrW(value.field,GWL_STYLE)==original_style&&
        original_selection.cpMin==after.cpMin&&original_selection.cpMax==after.cpMax,
        "Native scrollbar policy transitions changed source, selection or input styles");
    const auto source=edit_text(value.field);
    {
        lgaed::windows::CodeEditor no_numbers(value.field,lgaed::windows::predefined_code_document(source),true,{},true,true,false);
        check(reader_theme::native_scrollbar_theme(value.field)&&!lgaed::windows::code_gutter_snapshot(value.field).active,
            "A C++ editor without line numbers lost the native scrollbar policy");
        check(no_numbers.document().text==source,"The no-number policy changed its actual code document");
    }
    check(!reader_theme::native_scrollbar_theme(value.field)&&edit_text(value.field)==source,
        "The no-number editor failed to restore its prior policy or code");
    const auto single=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
        20,340,420,28,value.host,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(single!=nullptr,"Create an actual single-line C++ answer editor");reader_theme::apply(single,reader_theme::Surface::field);
    {
        lgaed::windows::CodeEditor answer(single,lgaed::windows::predefined_code_document("std::cout"),false,{},true,true,false);
        check(reader_theme::native_scrollbar_theme(single)&&!lgaed::windows::code_gutter_snapshot(single).active&&
            !(GetWindowLongPtrW(single,GWL_STYLE)&ES_MULTILINE)&&answer.document().text=="std::cout",
            "A single-line C++ answer lost its native policy, source or input style");
    }
    check(!reader_theme::native_scrollbar_theme(single),"The single-line answer failed to restore its prior policy");DestroyWindow(single);
    std::cout<<"PASS native scrollbar owner: gutter opt-in, no custom overlay, "<<(has_theme?"Windows theme":"Windows system fallback")<<" and prior policy restored\n";
}
struct EditorFormatHost {
    HWND field=nullptr;
    lgaed::windows::CodeEditor* editor=nullptr;
    unsigned changes=0;
    static LRESULT CALLBACK proc(HWND host,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
        auto& self=*reinterpret_cast<EditorFormatHost*>(context);
        if(message==WM_COMMAND&&reinterpret_cast<HWND>(lp)==self.field&&HIWORD(wp)==EN_CHANGE&&self.editor){
            ++self.changes;self.editor->capture_change();style_cpp(self.field);
        }
        return DefSubclassProc(host,message,wp,lp);
    }
};
// This is the same real editable adapter, margin and nested syntax callback as
// a lesson compiler. Sampling on the worker reads ScreenDC first; a WindowDC
// observer on the UI thread can flush GDI and conceal an intermediate frame.
void editor_format_cycles(){
    Field value(true,true);std::string source;
    POINT original_pointer{};check(GetCursorPos(&original_pointer)!=FALSE,"Read the pointer before the formatting fixture");
    struct RestorePointer{POINT position;~RestorePointer(){SetCursorPos(position.x,position.y);}} restore_pointer{original_pointer};
    check(!(GetAsyncKeyState(VK_LBUTTON)&0x8000),"The formatting fixture requires the native mouse button to be released");
    RECT host_client{};GetClientRect(value.host,&host_client);
    const auto pointer_padding=std::max(8,MulDiv(8,static_cast<int>(GetDpiForWindow(value.host)),96));
    POINT outside_pointer{host_client.right-pointer_padding,host_client.bottom-pointer_padding};ClientToScreen(value.host,&outside_pointer);
    SetForegroundWindow(value.host);check(SetCursorPos(outside_pointer.x,outside_pointer.y)!=FALSE,"Move the pointer outside the observed RichEdit");pump();
    check(WindowFromPoint(outside_pointer)==value.host,"The parked pointer must stay in the fixture's empty host area");
    for(unsigned line=0;line<40;++line)source+="    std::cout << \"literal\\n\"; // "+std::string(150,'x')+'\n';
    auto editor=std::make_unique<lgaed::windows::CodeEditor>(value.field,lgaed::windows::predefined_code_document(source),true);
    lgaed::windows::set_code_gutter_appearance(value.field,reader_theme::background(value.field),reader_theme::palette(value.field).muted);
    EditorFormatHost context{value.field,editor.get()};
    check(SetWindowSubclass(value.host,EditorFormatHost::proc,9921,reinterpret_cast<DWORD_PTR>(&context))!=FALSE,
        "Forward actual editable syntax callbacks");
    struct Detach{HWND host;~Detach(){RemoveWindowSubclass(host,EditorFormatHost::proc,9921);}} detach{value.host};
    style_cpp(value.field);SetForegroundWindow(value.host);SetFocus(value.field);SendMessageW(value.field,EM_SETSEL,0,0);
    SendMessageW(value.field,WM_VSCROLL,SB_TOP,0);SendMessageW(value.field,WM_HSCROLL,SB_LEFT,0);pump();
    const auto gutter=lgaed::windows::code_gutter_snapshot(value.field);
    check(gutter.active&&!gutter.visible.empty(),"The real compiler margin and its number labels must be present");
    check(reader_theme::native_scrollbar_theme(value.field),"The actual compiler gutter must use one native scrollbar owner");
    check(!(GetWindowLongPtrW(value.field,GWL_STYLE)&ES_READONLY),"The formatting fixture must stay editable");
    if(reader_theme::palette(value.field).high_contrast)return;
    const auto theme=OpenThemeData(value.field,L"SCROLLBAR");
    const bool themed=GetWindowTheme(value.field)||theme;
    if(theme)CloseThemeData(theme);
    const auto expected=reader_theme::background(value.field);
    SCROLLBARINFO horizontal{sizeof(horizontal)},vertical{sizeof(vertical)};
    check(GetScrollBarInfo(value.field,OBJID_HSCROLL,&horizontal)&&GetScrollBarInfo(value.field,OBJID_VSCROLL,&vertical),
        "The real editor needs both native bars for the formatting regression");
    check(!(horizontal.rgstate[0]&STATE_SYSTEM_INVISIBLE)&&!(vertical.rgstate[0]&STATE_SYSTEM_INVISIBLE),
        "The real editor formatting fixture did not overflow in both axes");
    const POINT h{(horizontal.rcScrollBar.left+horizontal.rcScrollBar.right)/2,horizontal.rcScrollBar.top+1};
    const POINT v{vertical.rcScrollBar.left+1,(vertical.rcScrollBar.top+vertical.rcScrollBar.bottom)/2};
    check(WindowFromPoint(h)==value.field&&WindowFromPoint(v)==value.field,
        "The isolated real editor must own both observed screen points");
    const auto dpi=static_cast<int>(GetDpiForWindow(value.field));
    const auto radius=std::max(1,MulDiv(2,dpi,96));
    const POINT h_thumb{horizontal.rcScrollBar.left+(horizontal.xyThumbTop+horizontal.xyThumbBottom)/2,
        (horizontal.rcScrollBar.top+horizontal.rcScrollBar.bottom)/2};
    const POINT v_thumb{(vertical.rcScrollBar.left+vertical.rcScrollBar.right)/2,
        vertical.rcScrollBar.top+(vertical.xyThumbTop+vertical.xyThumbBottom)/2};
    const POINT h_arrow{horizontal.rcScrollBar.right-horizontal.dxyLineButton/2+radius,
        (horizontal.rcScrollBar.top+horizontal.rcScrollBar.bottom)/2};
    const POINT v_arrow{(vertical.rcScrollBar.left+vertical.rcScrollBar.right)/2,
        vertical.rcScrollBar.bottom-vertical.dxyLineButton/2+radius};
    std::array<POINT,6> points{h,v,h_thumb,v_thumb,h_arrow,v_arrow};
    const auto arrow_box=[](const SCROLLBARINFO& bar,bool sideways){
        auto box=bar.rcScrollBar;
        if(sideways)box.left=box.right-bar.dxyLineButton;
        else box.top=box.bottom-bar.dxyLineButton;
        return box;
    };
    const std::array<RECT,2> arrow_boxes{arrow_box(horizontal,true),arrow_box(vertical,false)};
    const auto colors=reader_theme::palette(value.field);
    RECT outer{},client{};GetWindowRect(value.field,&outer);GetClientRect(value.field,&client);POINT origin{};ClientToScreen(value.field,&origin);
    const RECT gutter_region{origin.x-outer.left,origin.y-outer.top,origin.x-outer.left+gutter.width,origin.y-outer.top+client.bottom};
    check(gutter.width>0&&gutter.width<=client.right,"The format cycles need the complete visible number band");
    std::array<COLORREF,6> expected_inks{expected,expected,colors.muted,colors.muted,colors.muted,colors.muted};
    struct Sample{unsigned phase,phase_end;std::size_t gutter_changes=0,first_changed=0;std::array<std::size_t,2> arrow_changes{};DWORD reference_pixel=0,actual_pixel=0;HWND foreground=nullptr,foreground_end=nullptr;RECT bounds{},bounds_end{};bool controlled=true;std::array<COLORREF,6> inks;};std::vector<Sample> samples;samples.reserve(2048);
    std::atomic<unsigned> phase=0;std::atomic<bool> ready=false,complete_frame=false,stop=false,sampling_ok=true,environment_ok=true;
    std::array<COLORREF,6> warmup_inks{};bool warmup_numbers=false,warmup_arrows=false;
    const auto dpi_context=GetThreadDpiAwarenessContext();
    std::thread observer([&]{
        const auto prior=SetThreadDpiAwarenessContext(dpi_context);
        if(!prior){sampling_ok=false;ready=true;return;}
        const auto screen=GetDC(nullptr);const auto memory=CreateCompatibleDC(screen);
        RECT observed{};GetWindowRect(value.field,&observed);
        const auto bitmap=CreateCompatibleBitmap(screen,observed.right-observed.left,observed.bottom-observed.top);
        if(!screen||!memory||!bitmap){
            if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);
            sampling_ok=false;ready=true;SetThreadDpiAwarenessContext(prior);return;
        }
        const auto previous=SelectObject(memory,bitmap);
        std::array<COLORREF,6> candidate{};ULONGLONG candidate_since=0;
        bool arrow_points_ready=false,alias_checked=false;
        std::vector<DWORD> previous_band,gutter_reference;
        std::array<std::vector<DWORD>,2> previous_arrows,arrow_reference;
        ready=true;
        while(!stop){
            Sample sample{};sample.phase=phase.load();sample.foreground=GetForegroundWindow();
            sample.controlled=sample.foreground==value.host&&GetWindowRect(value.field,&sample.bounds)&&EqualRect(&sample.bounds,&observed)&&
                IsWindowVisible(value.field)&&!IsIconic(value.host);
            // One screen snapshot reads all components from the same presented
            // frame. Never ask the UI thread for a WindowDC before sampling.
            if(!BitBlt(memory,0,0,observed.right-observed.left,observed.bottom-observed.top,screen,observed.left,observed.top,SRCCOPY))sampling_ok=false;
            sample.phase_end=phase.load();
            sample.foreground_end=GetForegroundWindow();
            sample.controlled=sample.controlled&&sample.foreground_end==value.host&&GetWindowRect(value.field,&sample.bounds_end)&&EqualRect(&sample.bounds_end,&observed);
            if(!sample.controlled)environment_ok=false;
            if(!arrow_points_ready){
                for(std::size_t arrow=0;arrow<arrow_boxes.size();++arrow){
                    const auto box=arrow_boxes[arrow];const auto background=GetPixel(memory,box.left-observed.left+1,box.top-observed.top+1);
                    const auto cx=(box.left+box.right)/2,cy=(box.top+box.bottom)/2;
                    const auto rx=std::max<LONG>(1,(box.right-box.left)/3),ry=std::max<LONG>(1,(box.bottom-box.top)/3);
                    int contrast=0;POINT glyph=points[4+arrow];
                    for(LONG y=cy-ry;y<=cy+ry;++y)for(LONG x=cx-rx;x<=cx+rx;++x){
                        const auto ink=GetPixel(memory,x-observed.left,y-observed.top);
                        const auto distance=std::abs(static_cast<int>(GetRValue(ink))-GetRValue(background))+
                            std::abs(static_cast<int>(GetGValue(ink))-GetGValue(background))+std::abs(static_cast<int>(GetBValue(ink))-GetBValue(background));
                        if(ink!=CLR_INVALID&&distance>contrast){contrast=distance;glyph={x,y};}
                    }
                    if(contrast>=30)points[4+arrow]=glyph;
                }
                // Windows can omit an idle arrow glyph. Observe both complete
                // button images as well; a glyph need not exist to detect a
                // native/custom skin transition or an incomplete button frame.
                arrow_points_ready=true;
            }
            for(std::size_t index=0;index<points.size();++index)sample.inks[index]=GetPixel(memory,points[index].x-observed.left,points[index].y-observed.top);
            SelectObject(memory,previous);
            auto band=captured_region(screen,bitmap,observed.right-observed.left,observed.bottom-observed.top,gutter_region);
            std::array<std::vector<DWORD>,2> arrows;
            for(std::size_t index=0;index<arrows.size();++index){auto box=arrow_boxes[index];OffsetRect(&box,-observed.left,-observed.top);
                arrows[index]=captured_region(screen,bitmap,observed.right-observed.left,observed.bottom-observed.top,box);}
            if(arrow_points_ready&&!alias_checked){
                for(std::size_t arrow=4;arrow<points.size();++arrow){
                    const auto x=points[arrow].x-observed.left,y=points[arrow].y-observed.top;
                    const auto raw=captured_region(screen,bitmap,observed.right-observed.left,observed.bottom-observed.top,RECT{x,y,x+1,y+1});
                    if(raw.size()!=1||RGB((raw[0]>>16)&255,(raw[0]>>8)&255,raw[0]&255)!=sample.inks[arrow])sampling_ok=false;
                }
                alias_checked=true;
            }
            SelectObject(memory,bitmap);
            if(band.empty())sampling_ok=false;
            if(!complete_frame){
                const bool numbers=!band.empty()&&std::any_of(band.begin(),band.end(),[&](DWORD ink){return ink!=band.front();});
                warmup_inks=sample.inks;warmup_numbers=numbers;warmup_arrows=arrow_points_ready;
                const bool stable_band=numbers&&band==previous_band;previous_band=band;
                const bool stable_arrows=!arrows[0].empty()&&!arrows[1].empty()&&arrows==previous_arrows;previous_arrows=arrows;
                const auto dark=[](COLORREF ink){return GetRValue(ink)<160&&GetGValue(ink)<160&&GetBValue(ink)<160;};
                const bool visible=arrow_points_ready&&alias_checked&&(!themed||(dark(sample.inks[0])&&dark(sample.inks[1])))&&
                    sample.inks[2]!=sample.inks[0]&&sample.inks[3]!=sample.inks[1];
                if(!visible)candidate_since=0;
                else if(!candidate_since||candidate!=sample.inks){candidate=sample.inks;candidate_since=GetTickCount64();}
                else if(GetTickCount64()-candidate_since>=100&&stable_band&&stable_arrows){expected_inks=candidate;gutter_reference=band;arrow_reference=arrows;complete_frame=true;}
            }else {
                sample.gutter_changes=changed_pixels(gutter_reference,band);
                for(std::size_t index=0;index<arrows.size();++index)sample.arrow_changes[index]=changed_pixels(arrow_reference[index],arrows[index]);
                if(sample.gutter_changes&&gutter_reference.size()==band.size())for(std::size_t at=0;at<band.size();++at)if(gutter_reference[at]!=band[at]){
                    sample.first_changed=at;sample.reference_pixel=gutter_reference[at];sample.actual_pixel=band[at];break;
                }
            }
            if(sample.phase||!sample.controlled)samples.push_back(sample);
            Sleep(0);
        }
        SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
        if(prior)SetThreadDpiAwarenessContext(prior);
    });
    struct JoinObserver{std::atomic<bool>& stop;std::thread& observer;~JoinObserver(){stop=true;if(observer.joinable())observer.join();}} join{stop,observer};
    const auto validate_environment=[&]{
        RECT current{};GetWindowRect(value.field,&current);const auto foreground=GetForegroundWindow();
        const auto owner_h=WindowFromPoint(h),owner_v=WindowFromPoint(v);
        const bool intact=environment_ok&&foreground==value.host&&EqualRect(&current,&outer)&&owner_h==value.field&&owner_v==value.field;
        if(intact)return;
        std::cerr<<"INVALID screen environment: expected host="<<value.host<<" field="<<value.field<<" foreground="<<foreground
            <<" owner H="<<owner_h<<" owner V="<<owner_v<<" initial bounds="<<outer.left<<','<<outer.top<<".."<<outer.right<<','<<outer.bottom
            <<" final bounds="<<current.left<<','<<current.top<<".."<<current.right<<','<<current.bottom<<'\n';
        const auto invalid=std::find_if(samples.begin(),samples.end(),[](const auto& sample){return !sample.controlled;});
        if(invalid!=samples.end())std::cerr<<"First invalid capture: phase="<<invalid->phase<<"->"<<invalid->phase_end
            <<" foreground="<<invalid->foreground<<"->"<<invalid->foreground_end<<" bounds="<<invalid->bounds.left<<','<<invalid->bounds.top
            <<".."<<invalid->bounds.right<<','<<invalid->bounds.bottom<<" after="<<invalid->bounds_end.left<<','<<invalid->bounds_end.top
            <<".."<<invalid->bounds_end.right<<','<<invalid->bounds_end.bottom<<'\n';
        throw std::runtime_error("Screen regression invalid: its window lost foreground, moved or was occluded; pixel findings are not accepted");
    };
    while(!ready)YieldProcessor();
    // The STATIC host and RichEdit can reach the compositor in separate startup
    // frames. Arm only after the actual editor has presented every component;
    // the regression observes later edits/formatting, not a not-yet-drawn host.
    const auto warmup=GetTickCount64()+3000;
    while(!complete_frame&&sampling_ok&&environment_ok&&GetTickCount64()<warmup){pump();Sleep(1);}
    const bool warmed=complete_frame.load();
    if(!environment_ok||!warmed){stop=true;observer.join();validate_environment();
        std::cerr<<"Warmup: numbers="<<warmup_numbers<<" arrows="<<warmup_arrows<<" inks=";
        for(const auto ink:warmup_inks)std::cerr<<ink<<',';std::cerr<<" environment="<<environment_ok<<" sampling="<<sampling_ok<<'\n';}
    check(warmed,"The real editor never presented a complete dark scrollbar with visible thumbs before the formatting cycles");
    // Formatting only must keep native selection, offsets, source and undo.
    CHARRANGE original_selection{};POINT original_scroll{};
    SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&original_selection));
    SendMessageW(value.field,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&original_scroll));
    phase=1;for(unsigned tick=0;tick<8;++tick){style_cpp(value.field);pump();}
    phase=2;{
        ReadingShade shade(value.field,source);
        for(unsigned tick=0;tick<12;++tick){shade.read_through(tick*19);pump();}
        shade.restore();pump();
    }
    CHARRANGE after_selection{};POINT after_scroll{};
    SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_selection));
    SendMessageW(value.field,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after_scroll));
    const bool formatting_preserved=original_selection.cpMin==after_selection.cpMin&&original_selection.cpMax==after_selection.cpMax&&
        original_scroll.x==after_scroll.x&&original_scroll.y==after_scroll.y;
    for(unsigned edit=0;edit<8;++edit){
        phase=3;check(editor->insert_shortcut("x"),"Insert through the real compiler adapter");
        phase=4;editor->undo();
        phase=5;SendMessageW(value.field,WM_CHAR,L'y',0);
        SendMessageW(value.field,WM_CHAR,L'\b',0);
        check(editor->document().text==source,"Backspace did not restore the pre-insertion compiler source");
        phase=6;editor->undo();editor->undo();pump();
    }
    phase=7;for(unsigned cycle=0;cycle<8;++cycle){
        SendMessageW(value.field,EM_SHOWSCROLLBAR,SB_HORZ,TRUE);
        SendMessageW(value.field,EM_SCROLLCARET,0,0);pump();
    }
    const auto until=GetTickCount64()+110;while(GetTickCount64()<until){pump();Sleep(1);}
    stop=true;observer.join();
    // Ownership queries are deliberately after every ScreenDC capture: querying
    // another UI thread during rendering could delay the observer itself.
    validate_environment();
    check(sampling_ok,"The real editor screen sampler could not capture its presented frame");
    bool dark=!samples.empty();std::array<std::array<bool,6>,8> printed{};std::array<unsigned,6> failures{},light{};
    for(const auto& sample:samples)for(std::size_t index=0;index<points.size();++index)if(sample.inks[index]!=expected_inks[index]){
        ++failures[index];if(GetRValue(sample.inks[index])>=220&&GetGValue(sample.inks[index])>=220&&GetBValue(sample.inks[index])>=220)++light[index];
        if(sample.phase<printed.size()&&!printed[sample.phase][index]){
            printed[sample.phase][index]=true;std::cerr<<"Editor frame: phase="<<sample.phase<<"->"<<sample.phase_end<<" component="<<index<<" ink="<<sample.inks[index]<<" expected="<<expected_inks[index]
                <<'\n';
        }
        dark=false;
    }
    if(!dark)for(std::size_t index=0;index<failures.size();++index)
        std::cerr<<"Editor component "<<index<<": incomplete="<<failures[index]<<" light="<<light[index]<<" frames="<<samples.size()<<'\n';
    if(!dark){
        POINT cursor{};GetCursorPos(&cursor);
        std::cerr<<"Formatting pointer: original="<<original_pointer.x<<','<<original_pointer.y<<" parked="<<outside_pointer.x<<','<<outside_pointer.y
            <<" current="<<cursor.x<<','<<cursor.y<<" left down="<<((GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0)<<'\n';
        for(std::size_t arrow=0;arrow<arrow_boxes.size();++arrow){
            const auto box=arrow_boxes[arrow];const auto point=points[4+arrow];
            std::cerr<<"Native arrow probe "<<arrow<<": box="<<box.left<<','<<box.top<<".."<<box.right<<','<<box.bottom<<" point="<<point.x<<','<<point.y
                <<" owns="<<(WindowFromPoint(point)==value.field)<<" hit="<<SendMessageW(value.field,WM_NCHITTEST,0,MAKELPARAM(point.x,point.y))<<" baseline="<<expected_inks[4+arrow]<<'\n';
        }
    }
    const bool stable_gutter=std::all_of(samples.begin(),samples.end(),[](const auto& sample){return sample.gutter_changes==0;});
    std::array<bool,8> band_reported{};
    if(!stable_gutter)for(const auto& sample:samples)if(sample.gutter_changes&&sample.phase<band_reported.size()&&!band_reported[sample.phase]){
        band_reported[sample.phase]=true;const auto width=gutter_region.right-gutter_region.left;
        std::cerr<<"Editor number band: phase="<<sample.phase<<"->"<<sample.phase_end<<" changed pixels="<<sample.gutter_changes
            <<" first x/y="<<gutter_region.left+sample.first_changed%width<<','<<gutter_region.top+sample.first_changed/width
            <<" RGB="<<((sample.reference_pixel>>16)&255)<<','<<((sample.reference_pixel>>8)&255)<<','<<(sample.reference_pixel&255)
            <<" -> "<<((sample.actual_pixel>>16)&255)<<','<<((sample.actual_pixel>>8)&255)<<','<<(sample.actual_pixel&255)<<'\n';
    }
    check(formatting_preserved,"Syntax/narration formatting moved the actual selection or viewport");
    check(editor->document().text==source&&context.changes>0,"Editable syntax cycles lost source bytes or bypassed actual parent callbacks");
    check(reader_theme::native_scrollbar_theme(value.field),"An edit/read cycle restored a competing custom scrollbar skin");
    for(const auto& sample:samples)for(std::size_t index=0;index<sample.arrow_changes.size();++index)if(sample.arrow_changes[index]){
        std::cerr<<"Editor arrow button: phase="<<sample.phase<<" component="<<index<<" changed pixels="<<sample.arrow_changes[index]<<'\n';
        check(false,"An entire native arrow button changed during the stationary formatting/edit cycles");}
    check(dark,"A real editable compiler exposed an incomplete native scrollbar frame during syntax, narration, edit or caret layout");
    check(stable_gutter,"The complete number band changed during syntax, narration, edits, undo or caret cycles");
    std::cout<<"PASS actual CodeEditor scrollbar presentation (single native "<<(themed?"dark theme":"Windows system fallback")<<"): complete number band identical in "<<samples.size()<<" frames, nested syntax, narration ticks, undo, caret and bar visibility cycles\n";
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
struct PresentedBar {
    COLORREF track=CLR_INVALID,thumb=CLR_INVALID;
    std::vector<DWORD> gutter_pixels;
    LONG thumb_top=0,thumb_bottom=0,native_top=0,native_bottom=0;
    int position=0,tracking_position=0;
    bool valid=false,stable_thumb=false,stable_arrow=false,arrow_visible=false,thumb_visible=false,controlled=false;
};
PresentedBar presented_bar(HWND field,int axis,bool tracking,const RECT* gutter=nullptr){
    PresentedBar result;const bool sideways=axis==SB_HORZ;SCROLLBARINFO before{sizeof(before)},after{sizeof(after)};
    const auto host=GetAncestor(field,GA_ROOT),foreground=GetForegroundWindow();
    SCROLLINFO range{sizeof(range),SIF_ALL},range_after{sizeof(range_after),SIF_ALL};
    const auto object=sideways?OBJID_HSCROLL:OBJID_VSCROLL;
    if(!GetScrollBarInfo(field,object,&before)||!GetScrollInfo(field,axis,&range)||
        (before.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)))return result;
    const auto width=before.rcScrollBar.right-before.rcScrollBar.left,height=before.rcScrollBar.bottom-before.rcScrollBar.top;
    if(width<=0||height<=0)return result;
    RECT observed=before.rcScrollBar;
    if(gutter&&!GetWindowRect(field,&observed))return result;
    const auto capture_width=observed.right-observed.left,capture_height=observed.bottom-observed.top;
    if(capture_width<=0||capture_height<=0)return result;
    const auto bar_x=before.rcScrollBar.left-observed.left,bar_y=before.rcScrollBar.top-observed.top;
    const auto thumb_start=[sideways,tracking](const SCROLLBARINFO& info,const SCROLLINFO& limits){
        const auto extent=sideways?info.rcScrollBar.right-info.rcScrollBar.left:info.rcScrollBar.bottom-info.rcScrollBar.top;
        const auto button=std::min<LONG>(info.dxyLineButton,extent/2);
        const auto travel=std::max<LONG>(0,extent-2*button-(info.xyThumbBottom-info.xyThumbTop));
        const auto maximum=std::max<long long>(limits.nMin,static_cast<long long>(limits.nMax)-std::max<long long>(0,static_cast<long long>(limits.nPage)-1));
        const auto position=tracking||(info.rgstate[3]&STATE_SYSTEM_PRESSED)?limits.nTrackPos:limits.nPos;
        const auto delta=std::clamp<long long>(position,limits.nMin,maximum)-limits.nMin;
        const auto span=maximum-limits.nMin;
        return static_cast<LONG>(button+(span?(delta*travel+span/2)/span:0));
    };
    const auto screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    const auto bitmap=memory?CreateCompatibleBitmap(screen,capture_width,capture_height):nullptr;
    if(!screen||!memory||!bitmap){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);return result;}
    const auto old=SelectObject(memory,bitmap);
    // When a fixed number band is present, the bar and every band pixel come
    // from this same desktop frame, before any observer obtains a WindowDC.
    const bool copied=BitBlt(memory,0,0,capture_width,capture_height,screen,observed.left,observed.top,SRCCOPY)!=FALSE;
    const auto pixel=[&](int x,int y){return GetPixel(memory,bar_x+x,bar_y+y);};
    result.track=pixel(sideways?width/2:1,sideways?1:height/2);
    // USER reports the committed xyThumb* geometry while drawing its live drag
    // at SIF_TRACKPOS. Observe that live position instead of the vacated thumb.
    result.native_top=before.xyThumbTop;result.native_bottom=before.xyThumbBottom;
    result.position=range.nPos;result.tracking_position=range.nTrackPos;
    result.thumb_top=thumb_start(before,range);result.thumb_bottom=result.thumb_top+before.xyThumbBottom-before.xyThumbTop;
    result.thumb=pixel(sideways?(result.thumb_top+result.thumb_bottom)/2:width/2,
        sideways?height/2:(result.thumb_top+result.thumb_bottom)/2);
    const auto arrow=std::min<LONG>(before.dxyLineButton,(sideways?width:height)/2);
    unsigned thumb_ink=0;
    for(LONG along=arrow+2;along<(sideways?width:height)-arrow-2;++along){
        const auto ink=pixel(sideways?along:width/2,sideways?height/2:along);
        if(ink!=CLR_INVALID&&ink!=result.track)++thumb_ink;
    }
    result.thumb_visible=thumb_ink>=static_cast<unsigned>(std::max<LONG>(2,(before.xyThumbBottom-before.xyThumbTop)/4));
    const RECT button=sideways?RECT{width-arrow,0,width,height}:RECT{0,height-arrow,width,height};
    const auto button_background=pixel(button.left+1,button.top+1);unsigned visible=0;
    for(int y=button.top+2;y<button.bottom-2;++y)for(int x=button.left+2;x<button.right-2;++x){
        const auto ink=pixel(x,y);if(ink!=CLR_INVALID&&ink!=button_background)++visible;
    }
    result.arrow_visible=visible>=2;
    SelectObject(memory,old);
    if(copied&&gutter)result.gutter_pixels=captured_region(screen,bitmap,capture_width,capture_height,*gutter);
    DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
    if(!GetScrollBarInfo(field,object,&after)||!GetScrollInfo(field,axis,&range_after))return result;
    result.valid=copied&&EqualRect(&before.rcScrollBar,&after.rcScrollBar)&&
        !(after.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN));
    result.stable_thumb=thumb_start(before,range)==thumb_start(after,range_after)&&
        before.xyThumbBottom-before.xyThumbTop==after.xyThumbBottom-after.xyThumbTop&&
        before.rgstate[3]==after.rgstate[3]&&before.xyThumbBottom>before.xyThumbTop&&
        range.nMin==range_after.nMin&&range.nMax==range_after.nMax&&range.nPage==range_after.nPage&&
        range.nPos==range_after.nPos&&range.nTrackPos==range_after.nTrackPos;
    result.stable_arrow=before.rgstate[5]==after.rgstate[5];
    const POINT observed_point{(before.rcScrollBar.left+before.rcScrollBar.right)/2,before.rcScrollBar.top+1};
    result.controlled=foreground==host&&GetForegroundWindow()==host&&WindowFromPoint(observed_point)==field;
    return result;
}
void native_thumb_tracking(Field& value,int axis,bool visible_screen=false){
    if(reader_theme::palette(value.field).high_contrast)return;
    struct ImeScope{
        HWND field;HMODULE library=LoadLibraryW(L"Imm32.dll");using Associate=HIMC(WINAPI*)(HWND,HIMC);
        Associate associate=library?reinterpret_cast<Associate>(GetProcAddress(library,"ImmAssociateContext")):nullptr;HIMC previous=nullptr;
        explicit ImeScope(HWND window):field(window){check(associate!=nullptr,"Cannot isolate the native tracking fixture's IME context");previous=associate(field,nullptr);}
        ~ImeScope(){if(associate)associate(field,previous);if(library)FreeLibrary(library);}
    } ime(value.field);
    // IMR_QUERYCHARPOSITION can reveal an offscreen insertion point after a
    // synthetic focus change. It is unrelated to this scrollbar gesture; keep
    // this fixture's IME isolated and restore its original context afterwards.
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
    const auto gutter=lgaed::windows::code_gutter_snapshot(value.field);
    const bool observe_gutter=visible_screen&&sideways&&gutter.active;
    RECT gutter_region{};
    if(observe_gutter){
        RECT outer{},client{};GetWindowRect(value.field,&outer);GetClientRect(value.field,&client);POINT origin{};ClientToScreen(value.field,&origin);
        check(gutter.width>0&&gutter.width<=client.right&&!gutter.visible.empty(),"The horizontal drag needs a visible complete number band");
        const auto fixed=FindWindowExW(GetParent(value.field),nullptr,L"LearnGameAndEngineDev.FixedCodeNumbers",nullptr);RECT fixed_client{};
        check(fixed&&GetClientRect(fixed,&fixed_client)&&fixed_client.right>gutter.width,"The live fixed surface must also protect the source gap");
        gutter_region={origin.x-outer.left,origin.y-outer.top,origin.x-outer.left+fixed_client.right,origin.y-outer.top+client.bottom};
        const RECT screen_band{origin.x,origin.y,origin.x+fixed_client.right,origin.y+client.bottom};
        check(!PtInRect(&screen_band,first)&&!PtInRect(&screen_band,last),"The native pointer must stay outside the observed number band");
        CHARRANGE selection{};SendMessageW(value.field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));POINTL caret{};
        SendMessageW(value.field,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&caret),selection.cpMax);
        // USER's caret rectangle can stay at 0,0 for an offscreen insertion
        // point. Native document geometry identifies that point; the complete
        // desktop-band comparison below still detects any visible caret pixel.
        check(caret.x>=gutter.width||caret.x<0,"The insertion point must stay outside the observed number band");
    }
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
    struct VisibleFrame{bool released;double milliseconds;PresentedBar bar;};std::vector<VisibleFrame> frames;frames.reserve(observe_gutter?21:14);
    const auto read_screen=[field=value.field,axis,observe_gutter,gutter_region](bool tracking){return presented_bar(field,axis,tracking,observe_gutter?&gutter_region:nullptr);};
    std::vector<DWORD> gutter_reference;
    auto expected=reader_theme::background(value.field);
    if(visible_screen){
        PresentedBar initial_screen;std::vector<DWORD> previous_band;const auto deadline=GetTickCount64()+1000;
        COLORREF previous_track=CLR_INVALID,previous_thumb=CLR_INVALID;ULONGLONG stable_since=0;
        do{
            pump();initial_screen=read_screen(false);
            const bool numbers=!observe_gutter||(!initial_screen.gutter_pixels.empty()&&std::any_of(initial_screen.gutter_pixels.begin(),initial_screen.gutter_pixels.end(),[&](DWORD ink){return ink!=initial_screen.gutter_pixels.front();}));
            const bool stable_band=!observe_gutter||(numbers&&initial_screen.gutter_pixels==previous_band);
            if(initial_screen.track!=previous_track||initial_screen.thumb!=previous_thumb||!stable_band)stable_since=GetTickCount64();
            previous_track=initial_screen.track;previous_thumb=initial_screen.thumb;
            if(initial_screen.controlled&&initial_screen.valid&&initial_screen.thumb!=initial_screen.track&&initial_screen.arrow_visible&&stable_band&&GetTickCount64()-stable_since>=100){gutter_reference=initial_screen.gutter_pixels;break;}
            previous_band=initial_screen.gutter_pixels;Sleep(1);
        }while(GetTickCount64()<deadline);
        check(initial_screen.controlled&&initial_screen.valid&&initial_screen.thumb!=initial_screen.track&&initial_screen.arrow_visible,
            "The native tracking fixture never presented a complete scrollbar before input");
        check(!observe_gutter||!gutter_reference.empty(),"The number band never presented two identical frames containing its visible labels");
        if(reader_theme::native_scrollbar_theme(value.field))expected=initial_screen.track;
    }
    const auto dpi_context=GetThreadDpiAwarenessContext();
    std::thread input([field=value.field,first,last,axis,dpi_context,visible_screen,observe_gutter,read_screen,&finished,&input_ok,&during,&button_down,&frames]{
        const auto previous_dpi=SetThreadDpiAwarenessContext(dpi_context);
        struct RestoreDpi{DPI_AWARENESS_CONTEXT previous;~RestoreDpi(){if(previous)SetThreadDpiAwarenessContext(previous);}} restore_dpi{previous_dpi};
        if(!previous_dpi){input_ok=false;finished=true;return;}
        Sleep(25);
        if(WindowFromPoint(first)!=field||GetForegroundWindow()!=GetAncestor(field,GA_ROOT)||
            (GetAsyncKeyState(VK_LBUTTON)&0x8000)){input_ok=false;finished=true;return;}
        // Warmup pumps real desktop input. Position and press in one input
        // batch so a stale cursor position cannot click outside this fixture.
        INPUT down[2]{};for(auto& event:down)event.type=INPUT_MOUSE;
        const auto left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
        down[0].mi.dx=static_cast<LONG>((static_cast<long long>(first.x-left)*65535)/std::max(1,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1));
        down[0].mi.dy=static_cast<LONG>((static_cast<long long>(first.y-top)*65535)/std::max(1,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1));
        down[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
        down[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        if(SendInput(2,down,sizeof(INPUT))!=2){input_ok=false;finished=true;return;}
        Sleep(75);
        const auto sample_screen=[&](bool released){
            const auto start=std::chrono::steady_clock::now();
            for(const int milliseconds:{0,1,5,10,25,50,100}){
                const auto deadline=start+std::chrono::milliseconds(milliseconds);
                while(std::chrono::steady_clock::now()<deadline){
                    if(deadline-std::chrono::steady_clock::now()>std::chrono::milliseconds(2))Sleep(1);
                    else YieldProcessor();
                }
                auto frame=read_screen(!released);const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                frames.push_back({released,elapsed,std::move(frame)});
            }
        };
        if(visible_screen){
            // Never read a window DC during this phase: GetPixel on the UI
            // thread can flush its GDI batch and conceal a presentation bug.
            if(observe_gutter&&last.x>first.x+1){
                // A short move exercises RichEdit's client pixel copy; a jump
                // wider than the viewport can merely invalidate everything.
                SetCursorPos(first.x+std::min<LONG>(8,(last.x-first.x)/2),first.y);sample_screen(false);
            }
            SetCursorPos(last.x,last.y);sample_screen(false);
            button_down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
            during={0,0,frames.back().bar.track,{sizeof(SCROLLINFO),SIF_ALL}};GetScrollInfo(field,axis,&during.position);Sleep(20);
        }else{
            SetCursorPos(last.x,last.y);
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
    if(visible_screen)check(std::all_of(frames.begin(),frames.end(),[](const auto& frame){return frame.bar.controlled;}),
        "Desktop tracking QA was interrupted or occluded by another window; pixel findings are not accepted");
    const auto immediate=TrackingPaintProbe::measure(value.field,axis,0,0).ink;
    const auto tracked=scroll(value.field,axis);
    SetCursorPos(original_pointer.x,original_pointer.y);pump();RemoveWindowSubclass(value.field,TrackingPaintProbe::proc,9920);
    const bool moved=std::any_of(probe.samples.begin(),probe.samples.end(),[&](const auto& sample){
        return sample.message==scroll_message&&sample.request==SB_THUMBTRACK&&sample.position.nTrackPos>initial.nPos;});
    const bool dark=!probe.raster||std::all_of(probe.samples.begin(),probe.samples.end(),[&](const auto& sample){return sample.ink==expected;});
    const bool visible_dark=!visible_screen||std::all_of(frames.begin(),frames.end(),[&](const auto& frame){
        // Stock DarkMode_Explorer briefly fades its thumb through the track
        // color after a drag (also reproduced by --stock-tracking, without any
        // app subclass). Preserve that native animation. Require a continuous
        // dark frame/arrow and repeated live-position thumb observations below.
        // Our custom renderer has no animation and must retain every thumb.
        return frame.bar.valid&&frame.bar.track==expected&&(reader_theme::native_scrollbar_theme(value.field)||frame.bar.thumb_visible)&&
            (!frame.bar.stable_arrow||frame.bar.arrow_visible);
    });
    const auto observed=[&](bool released){return std::count_if(frames.begin(),frames.end(),[&](const auto& frame){
        return frame.released==released&&frame.milliseconds>=34&&frame.bar.stable_thumb&&frame.bar.thumb_visible&&frame.bar.thumb!=frame.bar.track;
    })>=2;};
    const bool observed_thumb=!visible_screen||(observed(false)&&observed(true));
    const bool stable_gutter=!observe_gutter||std::all_of(frames.begin(),frames.end(),[&](const auto& frame){return changed_pixels(gutter_reference,frame.bar.gutter_pixels)==0;});
    if(!stable_gutter){
        const auto failed=std::find_if(frames.begin(),frames.end(),[&](const auto& frame){return changed_pixels(gutter_reference,frame.bar.gutter_pixels)!=0;});
        const auto save=[&](const char* path,const std::vector<DWORD>& pixels){std::ofstream output(path,std::ios::binary);
            output<<"P6\n"<<(gutter_region.right-gutter_region.left)<<' '<<(gutter_region.bottom-gutter_region.top)<<"\n255\n";
            for(const auto pixel:pixels){const char rgb[]{static_cast<char>((pixel>>16)&255),static_cast<char>((pixel>>8)&255),static_cast<char>(pixel&255)};output.write(rgb,3);}};
        save("gutter-tracking-reference.ppm",gutter_reference);save("gutter-tracking-failure.ppm",failed->bar.gutter_pixels);
    }
    if(!input_ok||!moved||!button_down||during.ink!=expected||immediate!=expected||!dark||!visible_dark||!observed_thumb||!stable_gutter){
        std::cerr<<"Tracking diagnostics: axis="<<axis<<", range="<<initial.nMax<<", page="<<initial.nPage<<", final position="<<tracked.nPos<<", immediate track="<<immediate<<", expected="<<expected
            <<", pressed="<<button_down<<", during track="<<during.ink<<", during position="<<during.position.nPos<<", during tracking="<<during.position.nTrackPos<<'\n';
        for(const auto& sample:probe.samples)std::cerr<<"  message="<<sample.message<<", request="<<sample.request<<", position="<<sample.position.nPos<<", tracking="<<sample.position.nTrackPos<<", track color="<<sample.ink<<'\n';
        for(const auto& frame:frames)std::cerr<<"  visible screen "<<(frame.released?"after release":"during hold")<<" at "<<frame.milliseconds<<" ms: track="<<frame.bar.track
            <<" thumb="<<frame.bar.thumb<<" geometry="<<frame.bar.thumb_top<<".."<<frame.bar.thumb_bottom<<" stable="<<frame.bar.stable_thumb
            <<" committed geometry="<<frame.bar.native_top<<".."<<frame.bar.native_bottom<<" position="<<frame.bar.position<<" tracking="<<frame.bar.tracking_position
            <<" arrow="<<frame.bar.arrow_visible<<" arrow stable="<<frame.bar.stable_arrow<<" thumb present="<<frame.bar.thumb_visible
            <<" number band changes="<<(observe_gutter?changed_pixels(gutter_reference,frame.bar.gutter_pixels):0)<<'\n';
    }
    check(input_ok,"Native mouse input was not accepted by the isolated scrollbar fixture");
    check(moved,"The test did not move the actual native thumb");
    check(button_down,"The during-tracking sample must run before releasing the native mouse button");
    check(during.ink==expected,"USER repainted a light nonclient track after the scroll callback while the native thumb was held");
    check(visible_dark,"The visible desktop showed an incomplete native track/thumb/arrow before its window raster was observed");
    check(observed_thumb,"The native thumb did not provide stable live-position observations during both hold and release");
    check(stable_gutter,"The visible number band changed during horizontal native tracking before any WindowDC observer");
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
    if(visible_screen){
        const auto held=std::find_if(frames.rbegin(),frames.rend(),[](const auto& frame){return !frame.released&&frame.bar.stable_thumb;});
        check(held!=frames.rend()&&held->bar.thumb_top>=final_geometry.xyThumbTop-1&&held->bar.thumb_top<=final_geometry.xyThumbTop+1&&
            held->bar.thumb_bottom>=final_geometry.xyThumbBottom-1&&held->bar.thumb_bottom<=final_geometry.xyThumbBottom+1,
            "The observed live thumb did not agree with USER's independently committed geometry");
    }
    check(SendMessageW(value.field,WM_NCHITTEST,0,MAKELPARAM(last.x,last.y))==(sideways?HTHSCROLL:HTVSCROLL),
        "Native scrollbar hit testing changed after tracking");
    std::cout<<"PASS native "<<(sideways?"horizontal":"vertical")<<(visible_screen?" visible presentation":" window raster")<<" thumb tracking: complete during hold and after release, native input, source, selection, range and geometry preserved";
    if(visible_screen)std::cout<<", first screen sample="<<frames.front().milliseconds<<" ms";
    if(observe_gutter)std::cout<<", complete number band identical in all "<<frames.size()<<" frames";
    std::cout<<'\n';
}
void editor_native_tracking(){
    Field value(true,true);
    auto editor=std::make_unique<lgaed::windows::CodeEditor>(value.field,
        lgaed::windows::predefined_code_document(edit_text(value.field)),true);
    lgaed::windows::set_code_gutter_appearance(value.field,reader_theme::background(value.field),reader_theme::palette(value.field).muted);
    style_cpp(value.field);
    // The first line is as long as every other line. Its end stays to the
    // right of the fixed band throughout both the short and full pan; the
    // system caret cannot become a false number-band pixel difference.
    const auto line_end=SendMessageW(value.field,EM_LINELENGTH,0,0);SendMessageW(value.field,EM_SETSEL,static_cast<WPARAM>(line_end),line_end);pump();
    check(lgaed::windows::code_gutter_snapshot(value.field).active,
        "Native scrollbar tracking must include the compiler's actual fixed number margin");
    native_thumb_tracking(value,SB_HORZ,true);native_thumb_tracking(value,SB_VERT,true);
    std::cout<<"PASS actual CodeEditor horizontal/vertical native tracking with its live fixed gutter\n";
}
void custom_native_tracking(){
    Field value;native_thumb_tracking(value,SB_HORZ,true);native_thumb_tracking(value,SB_VERT,true);
    std::cout<<"PASS custom nonclient renderer uses USER's live tracking position without a parallel scroll state\n";
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
int main(int argc,char* argv[]){const auto module=LoadLibraryW(L"Msftedit.dll");if(!module)return 2;
    try{INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&common)!=FALSE,"Common controls v6 are required");
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        lgaed::windows::ensure_winrt_apartment();
        if(argc==2&&std::string_view(argv[1])=="--editor-cycles"){editor_format_cycles();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--editor-tracking"){editor_native_tracking();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--stock-tracking"){
            Field value(false,true);reader_theme::use_native_scrollbar_theme(value.field);
            check(SUCCEEDED(SetWindowTheme(value.field,L"DarkMode_Explorer",nullptr)),"Cannot enable the stock Windows dark scrollbar");
            native_thumb_tracking(value,SB_HORZ,true);native_thumb_tracking(value,SB_VERT,true);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--native-theme-contract"){native_theme_owner_contract();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--custom-tracking"){custom_native_tracking();return 0;}
        check(argc==1,"Unknown scrollbar QA mode");
        int baseline_wheel=0;
        {Field baseline(false);baseline_wheel=native_wheel_delta(baseline.field);}
        native_theme_owner_contract();editor_format_cycles();editor_native_tracking();
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
