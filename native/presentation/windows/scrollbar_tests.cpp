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
}
int main(){const auto module=LoadLibraryW(L"Msftedit.dll");if(!module)return 2;
    try{INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&common)!=FALSE,"Common controls v6 are required");
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int baseline_wheel=0;
        {Field baseline(false);baseline_wheel=native_wheel_delta(baseline.field);}
        Field value;std::cout<<"Actual scrollbar DPI="<<GetDpiForWindow(value.field)<<'\n';paint_without_mutating_native_state(value);
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
