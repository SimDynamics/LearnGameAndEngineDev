#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <ole2.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void click(HWND row){SendMessageW(row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));SendMessageW(row,WM_LBUTTONUP,0,MAKELPARAM(10,10));}
CHARFORMAT2W character(HWND row,LONG position){CHARRANGE old{},selected{position,position+1};SendMessageW(row,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&old));
    SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));CHARFORMAT2W result{};result.cbSize=sizeof(result);
    SendMessageW(row,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&result));SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old));return result;}
void pump(){MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){if(event.message==WM_QUIT)continue;TranslateMessage(&event);DispatchMessageW(&event);}}
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
int main(){using namespace lgaed::presentation;
    struct DpiScope{DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}} dpi_scope;
    check(SUCCEEDED(OleInitialize(nullptr)),"Cannot initialize the application's native OLE apartment");struct OleScope{~OleScope(){OleUninitialize();}} apartment;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto host=CreateWindowExW(0,L"STATIC",L"Code order QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,800,640,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
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
        check((after_space.x-before_space.x)*100<(letter1.x-letter0.x)*95&&
            (after_space.x-before_space.x)*100>(letter1.x-letter0.x)*65,"The original80-percent space advance was lost or changed actual source characters");
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
            int top=raster.height,bottom=-1;for(int y=first_rect.top;y<first_rect.bottom;++y)for(int x=0;x<first_rect.left;++x)if(raster.pixel(x,y)==RGB(111,119,130)){top=std::min(top,y);bottom=std::max(bottom,y);}
            normal_number_ink_height=bottom-top+1;
            check(normal_number_ink_height>0&&std::abs((top+bottom)-(first_rect.top+first_rect.bottom))<=px(8),
                "The original18sp line number is absent or no longer centered beside its fragment");
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
        // Exercise the real20ms boundary using the same clock as the gesture.
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
            int top=scaled.height,bottom=-1;for(int y=first_scaled.top;y<first_scaled.bottom;++y)for(int x=0;x<first_scaled.left;++x)if(scaled.pixel(x,y)==RGB(111,119,130)){top=std::min(top,y);bottom=std::max(bottom,y);}
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
