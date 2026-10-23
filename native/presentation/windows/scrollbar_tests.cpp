#include "presentation/windows/reader_theme.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <oleacc.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

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
    SetFocus(field);pump();POINT at{100,100};ClientToScreen(field,&at);
    const auto before=scroll(field,SB_VERT).nPos;
    SendMessageW(field,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),MAKELPARAM(at.x,at.y));pump();
    return scroll(field,SB_VERT).nPos-before;
}
struct Field {
    HWND host=nullptr,field=nullptr;
    explicit Field(bool themed=true){host=CreateWindowExW(0,L"STATIC",L"Isolated native scrollbar QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        30,30,700,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the scrollbar QA host");
        field=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_DISABLENOSCROLL,20,20,480,300,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(field!=nullptr,"Cannot create the actual RichEdit under test");if(themed)reader_theme::apply(field,reader_theme::Surface::field);
        std::wstring source;
        for(unsigned line=0;line<80;++line)source+=L"\t    std::cout << \"literal  spaces\"; "+std::wstring(220,L'x')+L"\r\n";
        SetWindowTextW(field,source.c_str());SendMessageW(field,EM_LINESCROLL,0,20);
        CHARRANGE selection{7,19};SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        RedrawWindow(field,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);pump();
    }
    ~Field(){if(host)DestroyWindow(host);}
};
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
        Field value;slider_colors_and_native_state(value.host);switch_colors_and_native_state(value.host);std::cout<<"Actual scrollbar DPI="<<GetDpiForWindow(value.field)<<'\n';paint_without_mutating_native_state(value);
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
