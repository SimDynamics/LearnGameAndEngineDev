#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <imm.h>
#include <richole.h>
#include <tom.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::string source(std::size_t lines){std::string result;for(std::size_t i=1;i<=lines;++i){if(i>1)result+='\n';result+="int line_"+std::to_string(i)+" = "+std::to_string(i)+";";}return result;}
struct Fixture{
    HWND host=nullptr,edit=nullptr;HFONT font=nullptr;std::unique_ptr<CodeEditor> editor;
    Fixture(std::string text,bool multiline=true,bool assistance=true,int width=500,bool wrap=false){
        host=CreateWindowExW(0,L"STATIC",L"Native code gutter QA",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create gutter QA host");
        const BOOL disable_transitions=TRUE;DwmSetWindowAttribute(host,DWMWA_TRANSITIONS_FORCEDISABLED,&disable_transitions,sizeof(disable_transitions));
        const DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|(multiline?ES_MULTILINE|WS_VSCROLL|ES_AUTOVSCROLL|ES_WANTRETURN:0)|(wrap?0:ES_AUTOHSCROLL);
        edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",style,0,0,width,220,host,nullptr,GetModuleHandleW(nullptr),nullptr);check(edit!=nullptr,"Cannot create native RichEdit");
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        presentation::reader_theme::apply(edit,presentation::reader_theme::Surface::field);
        editor=std::make_unique<CodeEditor>(edit,predefined_code_document(text),multiline,std::function<void()>{},assistance);
        if(wrap)SendMessageW(edit,EM_SETTARGETDEVICE,0,0);
    }
    ~Fixture(){editor.reset();if(host)DestroyWindow(host);if(font)DeleteObject(font);}
};
struct Raster{
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;int width=0,height=0;
    explicit Raster(HWND target,bool native_only=false){RECT rect{};GetClientRect(target,&rect);width=rect.right;height=rect.bottom;const auto screen=GetDC(target);
        dc=CreateCompatibleDC(screen);bitmap=CreateCompatibleBitmap(screen,width,height);ReleaseDC(target,screen);check(dc&&bitmap,"Cannot create gutter raster");
        previous=SelectObject(dc,bitmap);
        if(native_only){const auto native=reinterpret_cast<WNDPROC>(GetClassLongPtrW(target,GCLP_WNDPROC));check(native!=nullptr,"Cannot locate original RichEdit renderer");
            CallWindowProcW(native,target,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);}
        else SendMessageW(target,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);}
    ~Raster(){if(dc&&previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
};
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
struct ClientPixels{
    int width=0,height=0;std::vector<DWORD> values;
    void save(const char* path)const{std::ofstream output(path,std::ios::binary);output<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(const auto pixel:values){const char rgb[]{static_cast<char>((pixel>>16)&255),static_cast<char>((pixel>>8)&255),static_cast<char>(pixel&255)};output.write(rgb,3);}}
    explicit ClientPixels(HWND target){RECT area{};GetClientRect(target,&area);width=area.right;height=area.bottom;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        // Read the two already-painted native surfaces in their own DPI
        // coordinates. No WM_PAINT/WM_PRINTCLIENT is triggered by observing
        // them. The continuous tracking tests separately sample the desktop.
        const auto screen=GetDC(target),memory=CreateCompatibleDC(screen);void* bits=nullptr;
        const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);check(screen&&memory&&bitmap&&bits,"Cannot capture native code pixels");
        const auto old=SelectObject(memory,bitmap);check(BitBlt(memory,0,0,width,height,screen,0,0,SRCCOPY)!=FALSE,"Cannot read native code repaint");
        const auto band=FindWindowExW(GetParent(target),nullptr,L"LearnGameAndEngineDev.FixedCodeNumbers",nullptr);
        if(band){RECT band_area{};GetClientRect(band,&band_area);const auto band_dc=GetDC(band);
            check(band_dc&&BitBlt(memory,0,0,std::min<LONG>(width,band_area.right),std::min<LONG>(height,band_area.bottom),band_dc,0,0,SRCCOPY),"Cannot read the painted fixed number surface");
            ReleaseDC(band,band_dc);}
        GdiFlush();values.assign(static_cast<DWORD*>(bits),static_cast<DWORD*>(bits)+static_cast<std::size_t>(width)*height);
        SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(target,screen);
    }
};
void source_origin_is_not_masked(bool readonly){
    const std::string text="#include <iostream> // "+std::string(110,'x')+"\n\nint main() {\n    std::cout << \"Informe\\n\";\n    std::cout << \"Zona Este\\n\";\n    std::cout << \"Fin\\n\";\n    return 0;\n}\n"+source(1000);
    Fixture field(text,true,true,450,false);presentation::set_rich_text(field.edit,text,{},true);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    set_code_gutter_appearance(field.edit,RGB(20,20,20),RGB(111,119,130),3,8);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,380,SWP_NOACTIVATE);
    const auto document=field.editor->document();
    for(const int zoom:{75,100,150,200}){
        SendMessageW(field.edit,EM_SETZOOM,zoom,100);
        RECT format{},client{};SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));GetClientRect(field.edit,&client);
        format.top=MulDiv(16,100,zoom);format.right=client.right-MulDiv(16,100,zoom);format.bottom=client.bottom-MulDiv(16,100,zoom);
        SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        for(const bool bottom:{false,true})for(const LONG horizontal:{0,40,105,0}){
            SendMessageW(field.edit,WM_VSCROLL,bottom?SB_BOTTOM:SB_TOP,0);
            POINT requested{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&requested));requested.x=horizontal;
            SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&requested));
            RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
            POINT actual{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&actual));
            auto line=std::max<LRESULT>(0,SendMessageW(field.edit,EM_GETFIRSTVISIBLELINE,0,0));POINTL origin{},next{};
            const auto native_top=static_cast<LONG>(static_cast<long long>(format.top)*zoom/100);
            for(;;++line){const auto first=SendMessageW(field.edit,EM_LINEINDEX,line,0);check(first>=0,"Missing complete source row for native clipping comparison");
                SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&origin),first);if(origin.y>=native_top)break;}
            const auto second=SendMessageW(field.edit,EM_LINEINDEX,line+1,0);check(second>=0,"Missing next source row for native clipping comparison");
            SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next),second);
            const auto left=origin.x+actual.x;const auto right=std::min(client.right-1,static_cast<LONG>(MulDiv(format.right,zoom,100)));
            Raster visible(field.edit),native(field.edit,true);std::size_t ink=0,covered=0;
            const auto background=presentation::reader_theme::background(field.edit);
            for(int y=std::max(0L,origin.y);y<std::min<LONG>(visible.height,next.y);++y)for(int x=std::max(0L,left);x<right;++x){
                const auto expected=GetPixel(native.dc,x,y);if(expected!=background){++ink;if(GetPixel(visible.dc,x,y)!=expected)++covered;}}
            if(covered||!ink)std::cerr<<"Source-origin diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", dpi="<<GetDpiForWindow(field.edit)
                <<", requested="<<horizontal<<", actual="<<actual.x<<", vertical="<<actual.y<<", native_origin="<<origin.x<<", fixed_origin="<<left<<", format="<<format.left
                <<", gutter="<<code_gutter_snapshot(field.edit).width<<", ink="<<ink<<", covered="<<covered<<'\n';
            check(ink>0&&covered==0,"The fixed gutter covered source glyphs that native RichEdit exposes after its real text origin");
            const auto after=field.editor->document();check(after.text==document.text&&after.selection_start==document.selection_start&&after.selection_end==document.selection_end,
                "Checking the fixed gutter source boundary changed text or selection");
        }
    }
}
void native_tom_edit_refreshes_numbers(){
    Fixture field(source(36));
    Microsoft::WRL::ComPtr<IRichEditOle> rich;
    check(SendMessageW(field.edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()))!=0,"Missing native TOM editor");
    Microsoft::WRL::ComPtr<ITextDocument> document;check(SUCCEEDED(rich.As(&document)),"Missing native TOM document");
    const std::wstring replacement=L"#include <iostream>\n\nint main() {\n    // Construye el informe con tres instrucciones de salida.\n    std::cout << \"Informe\\n\";\n    std::cout << \"Zona Este\\n\";\n    std::cout << \"Fin\\n\";\n    return 0;\n}\n";
    const auto text=SysAllocStringLen(replacement.data(),static_cast<UINT>(replacement.size()));check(text!=nullptr,"Cannot allocate TOM source fixture");
    Microsoft::WRL::ComPtr<ITextRange> range;check(SUCCEEDED(document->Range(0,tomForward,&range)),"Cannot address TOM source story");
    const auto replaced=range->SetText(text);SysFreeString(text);check(SUCCEEDED(replaced),"The native TOM source replacement failed");
    // UI Automation's Value pattern can write through TOM without sending
    // WM_SETTEXT/EM_REPLACESEL to the editor. The owner's EN_CHANGE capture is
    // the authoritative completed edit; observing it must refresh its gutter.
    field.editor->capture_change();
    const auto gutter=code_gutter_snapshot(field.edit);
    if(gutter.logical_lines!=10)std::cerr<<"TOM gutter diagnostics: logical="<<gutter.logical_lines<<", native="<<SendMessageW(field.edit,EM_GETLINECOUNT,0,0)<<'\n';
    check(gutter.logical_lines==10,"A TOM/UIA edit retained the previous source's logical line numbers");
    const auto length=GetWindowTextLengthW(field.edit);LONG previous=-1;
    for(const auto& line:gutter.visible){
        check(line.number<=10&&line.character<=length&&line.character>previous,"Stale paragraph offsets clamped several line numbers onto the final row");
        check(line.character==SendMessageW(field.edit,EM_LINEINDEX,line.number-1,0),"The captured TOM source has mismatched native paragraph identities");
        previous=line.character;
    }
    const auto captured=field.editor->document();check(captured.text==presentation::utf8(replacement),"Refreshing native line numbers changed the externally supplied source");
    field.editor->undo();check(field.editor->document().text==source(36)&&code_gutter_snapshot(field.edit).logical_lines==36,
        "Refreshing externally edited line numbers damaged Undo or its historical paragraph count");
    field.editor->redo();check(field.editor->document().text==presentation::utf8(replacement)&&code_gutter_snapshot(field.edit).logical_lines==10,
        "Redo did not restore the external source and its actual line numbers");
}
void erase_does_not_expose_uncomposed_gutter(){
    Fixture field(source(20));presentation::set_rich_text(field.edit,source(20),{},true);
    set_code_gutter_appearance(field.edit,RGB(20,20,20),RGB(111,119,130),3,8);
    Raster pixels(field.edit);const auto gutter=code_gutter_snapshot(field.edit);std::vector<COLORREF> before;
    for(int y=0;y<pixels.height;++y)for(int x=0;x<gutter.width;++x)before.push_back(GetPixel(pixels.dc,x,y));
    check(SendMessageW(field.edit,WM_ERASEBKGND,reinterpret_cast<WPARAM>(pixels.dc),0)!=0,"The composed code client did not own its background erase");
    std::size_t at=0;for(int y=0;y<pixels.height;++y)for(int x=0;x<gutter.width;++x)
        check(GetPixel(pixels.dc,x,y)==before[at++],"A separate native background erase exposed an empty margin before the composed numbers");
}
void code_scroll_repaint(bool readonly){
    std::string text;for(int line=1;line<=24;++line){if(line>1)text+='\n';text+="int line_"+std::to_string(line)+" = "+std::to_string(line)+"; // "+std::string(140,'x');}
    Fixture field(text,true,true,500,false);
    presentation::set_rich_text(field.edit,text,{},true);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    set_code_gutter_appearance(field.edit,RGB(20,20,20),RGB(111,119,130),3,3);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,380,SWP_NOACTIVATE);
    const auto before=field.editor->document();
    for(const int zoom:{75,100,150,200}){
    SendMessageW(field.edit,EM_SETZOOM,zoom,100);SendMessageW(field.edit,EM_SHOWSCROLLBAR,SB_HORZ,TRUE);
    RECT native_format{};SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&native_format));
    RECT content{};GetClientRect(field.edit,&content);content.left=native_format.left;
    content.top=MulDiv(16,100,zoom);content.right-=MulDiv(16,100,zoom);content.bottom-=MulDiv(16,100,zoom);
    SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&content));
    RedrawWindow(field.host,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);pump();
    for(const LONG x:{180,25,120,0}){
        POINT desired{x,37};SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&desired));pump();UpdateWindow(field.edit);
        POINT actual{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&actual));
        check(actual.x==x,"Horizontal repaint fixture did not actually scroll");
        const ClientPixels incremental(field.edit);
        RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
        const ClientPixels complete(field.edit);
        std::size_t different=0;for(std::size_t at=0;at<incremental.values.size();++at)
            if((incremental.values[at]&0xffffff)!=(complete.values[at]&0xffffff))++different;
        if(different)std::cerr<<"Repaint diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", x="<<x<<", y="<<actual.y<<", different="<<different<<'\n';
        if(different&&!readonly){incremental.save("gutter-scroll-before.ppm");complete.save("gutter-scroll-after.ppm");}
        check(different==0,"Horizontal code scroll retained stale gutter or text pixels before a full repaint");
        RECT fixed{};SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&fixed));
        const auto band=code_gutter_snapshot(field.edit).width;
        POINTL origin{};SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&origin),0);
        const auto gap_right=origin.x+actual.x;
        const auto background=presentation::reader_theme::background(field.edit);
        std::size_t gap_ink=0;
        for(int y=0;y<incremental.height;++y)for(int column=band;column<gap_right;++column){
            const auto pixel=incremental.values[static_cast<std::size_t>(y)*incremental.width+column];
            const auto ink=RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255);if(ink!=background)++gap_ink;
        }
        if(gap_ink)std::cerr<<"Gap diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", x="<<x<<", band="<<band<<", format="<<fixed.left<<", ink="<<gap_ink<<'\n';
        check(gap_right>=band+10&&gap_ink==0,"Scrolled text invaded the fixed gap beside the line numbers");
        if(!x){
            if(origin.x<band+10)std::cerr<<"Origin diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", origin="<<origin.x<<", band="<<band<<", format="<<fixed.left<<'\n';
            check(origin.x>=band+10,"The fixed gutter gap covered the start of the first source character at native x=0");}
        const auto after=field.editor->document();check(before.text==after.text&&before.selection_start==after.selection_start&&before.selection_end==after.selection_end,
            "Repainting horizontal code changed the document or selection");
    }
    }
}
void code_vertical_edges(bool readonly){
    Fixture field(source(9),true,true,500,false);presentation::set_rich_text(field.edit,source(9),{},true);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,380,SWP_NOACTIVATE);
    Microsoft::WRL::ComPtr<IRichEditOle> rich;
    check(SendMessageW(field.edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()))!=0,"Missing native text document");
    Microsoft::WRL::ComPtr<ITextDocument> document;check(SUCCEEDED(rich.As(&document)),"Missing native text metrics");
    for(const int zoom:{75,100,150,200}){
        SendMessageW(field.edit,EM_SETZOOM,zoom,100);SendMessageW(field.edit,EM_SHOWSCROLLBAR,SB_HORZ,TRUE);
        RECT native_format{};SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&native_format));
        RECT area{};GetClientRect(field.edit,&area);RECT format=area;format.left=native_format.left;
        format.top=MulDiv(16,100,zoom);format.right-=MulDiv(16,100,zoom);format.bottom-=MulDiv(16,100,zoom);
        SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        CHARFORMAT2W style{};style.cbSize=sizeof(style);SendMessageW(field.edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&style));
        const auto dc=GetDC(field.edit);const auto font=CreateFontW(-MulDiv(MulDiv(style.yHeight,GetDeviceCaps(dc,LOGPIXELSY),1440),zoom,100),
            0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,style.bCharSet,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,style.bPitchAndFamily,style.szFaceName);
        const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);DeleteObject(font);ReleaseDC(field.edit,dc);
        for(const bool end:{false,true}){
            // Exercise the actual scrollbar endpoint, not an arbitrary
            // EM_SETSCROLLPOS coordinate that RichEdit permits beyond its
            // thumb range even when the whole document already fits.
            SendMessageW(field.edit,WM_VSCROLL,end?SB_BOTTOM:SB_TOP,0);
            RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
            const auto character=end?GetWindowTextLengthW(field.edit)-1:0;
            Microsoft::WRL::ComPtr<ITextRange> range;check(SUCCEEDED(document->Range(character,character,&range)),"Cannot measure edge line");
            LONG x=0,baseline=0;check(SUCCEEDED(range->GetPoint(tomStart|tomClientCoord|tomAllowOffClient|TA_BASELINE|TA_LEFT,&x,&baseline)),"Cannot locate edge glyph baseline");
            const auto top=baseline-metrics.tmAscent,bottom=baseline+metrics.tmDescent;
            if(top<0||bottom>area.bottom){POINT actual{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&actual));
                SCROLLINFO native{sizeof(native),SIF_ALL};GetScrollInfo(field.edit,SB_VERT,&native);
                std::cerr<<"Edge diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", end="<<end<<", glyph="<<top<<','<<bottom<<", client="<<area.bottom
                    <<", scroll="<<actual.y<<", range="<<native.nMin<<','<<native.nMax<<", page="<<native.nPage<<", pos="<<native.nPos<<'\n';}
            check(top>=0&&bottom<=area.bottom,"The first or final code line was clipped at a vertical viewport edge");
        }
    }
}
COLORREF pixel_color(DWORD pixel){return RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255);}
void partial_number_edges(bool readonly){
    const auto text=source(36);Fixture field(text,true,true,500,false);presentation::set_rich_text(field.edit,text,{},true);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    set_code_gutter_appearance(field.edit,RGB(20,20,20),RGB(111,119,130),3,3);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,580,SWP_NOACTIVATE);
    const auto before=field.editor->document();
    for(const int zoom:{75,100,150,200}){
        SendMessageW(field.edit,EM_SETZOOM,zoom,100);
        const auto native_inset=MulDiv(16,100,zoom);
        const LONG display_inset=0;
        const auto configure=[&]{RECT area{},format{};GetClientRect(field.edit,&area);SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
            format.top=native_inset;format.right=area.right-native_inset;format.bottom=area.bottom-native_inset;
            SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,WM_VSCROLL,SB_TOP,0);};
        const auto position=[&](LONG line){const auto character=SendMessageW(field.edit,EM_LINEINDEX,line,0);check(character>=0,"Missing native paragraph for a partial-number edge");
            POINTL point{};SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),character);return point;};
        MoveWindow(field.edit,0,0,500,420,TRUE);configure();RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
        const auto second=position(1),third=position(2),seventh=position(6),eighth=position(7);
        const auto top_height=third.y-second.y,bottom_height=eighth.y-seventh.y;
        check(top_height>4&&bottom_height>4,"Partial-number fixture has no measurable native rows");
        Raster full(field.edit);const auto gutter=code_gutter_snapshot(field.edit);
        const auto background=presentation::reader_theme::palette().high_contrast?presentation::reader_theme::background(field.edit):RGB(20,20,20);
        const auto compare=[&](LONG line,POINTL original,LONG height,bool top){
            const auto current=position(line);RECT area{};GetClientRect(field.edit,&area);
            const auto viewport_top=display_inset,viewport_bottom=area.bottom-display_inset;
            check(top?(current.y<viewport_top&&current.y+height>viewport_top):(current.y<viewport_bottom&&current.y+height>viewport_bottom),
                "The partial-number fixture did not intersect its requested native viewport edge");
            Raster partial(field.edit);std::size_t ink=0,different=0;
            for(int y=std::max(current.y,viewport_top);y<std::min(current.y+height,viewport_bottom);++y)for(int x=0;x<gutter.width;++x){
                // Use the same label rendered fully before scrolling/resizing
                // as the pixel reference, rather than reproducing its font.
                const auto expected=GetPixel(full.dc,x,y-current.y+original.y);if(expected!=background)++ink;
                if(GetPixel(partial.dc,x,y)!=expected)++different;
            }
            if(!ink||different)std::cerr<<"Partial-number diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", top="<<top<<", row="<<line+1
                <<", y="<<current.y<<", viewport="<<viewport_top<<','<<viewport_bottom<<", ink="<<ink<<", different="<<different<<'\n';
            check(ink>0&&different==0,"A visible portion of a native line number disappeared at the viewport edge");
        };
        POINT requested{0,second.y-display_inset+top_height/2};SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&requested));
        RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();compare(1,second,top_height,true);
        MoveWindow(field.edit,0,0,500,seventh.y+bottom_height/2+display_inset,TRUE);configure();
        RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();compare(6,seventh,bottom_height,false);
        const auto after=field.editor->document();check(after.text==before.text&&after.selection_start==before.selection_start&&after.selection_end==before.selection_end,
            "Showing partially visible line numbers changed source or selection");
    }
}
void code_whole_rows(bool readonly,bool wrap=false){
    // Regression now follows Android: clipped source rows remain visible,
    // rather than erasing them to enforce whole-line-only presentation.
    const auto text=source(24);Fixture field(text,true,true,wrap?180:500,wrap);
    presentation::set_rich_text(field.edit,text,{},true);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,380,SWP_NOACTIVATE);
    SetFocus(field.host);
    for(int zoom:{75,100,150,200}){
        SendMessageW(field.edit,EM_SETZOOM,zoom,100);
        RECT client{},format{};GetClientRect(field.edit,&client);SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        format.top=11;format.bottom=client.bottom-27;SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        check(format.top==0&&format.bottom==client.bottom,"Code retained an invisible vertical formatting inset");
        for(LONG y:{7,23,57,91}){
            POINT requested{0,y};SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&requested));
            RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
            const auto gutter=code_gutter_snapshot(field.edit);Raster composed(field.edit),native(field.edit,true);
            const auto left=std::max<LONG>(gutter.width+10,format.left*zoom/100+1);
            std::size_t ink=0,different=0;const auto background=presentation::reader_theme::background(field.edit);
            for(int row=0;row<client.bottom;++row)for(int x=left;x<client.right;++x){
                const auto original=GetPixel(native.dc,x,row);if(original!=background)++ink;
                if(GetPixel(composed.dc,x,row)!=original)++different;
            }
            check(ink>0&&!different,"Partially visible source glyphs differ from the native renderer");
        }
    }
    check(field.editor->document().text==text&&code_gutter_snapshot(field.edit).logical_lines==24,
        "Partial-line clipping changed source characters or logical numbering");
}
void scrollbar_release_keeps_viewport(bool readonly){
    const auto text=source(30);Fixture field(text,true,true,500,false);presentation::set_rich_text(field.edit,text,{},true);
    struct ImeScope{
        HWND edit;HMODULE library;using Associate=HIMC(WINAPI*)(HWND,HIMC);Associate associate;HIMC previous;
        explicit ImeScope(HWND value):edit(value),library(LoadLibraryW(L"Imm32.dll")),associate(library?reinterpret_cast<Associate>(GetProcAddress(library,"ImmAssociateContext")):nullptr),previous(nullptr){
            check(associate!=nullptr,"Cannot isolate the scrollbar fixture's IME context");previous=associate(edit,nullptr);}
        ~ImeScope(){associate(edit,previous);FreeLibrary(library);}
    } ime(field.edit);
    // IMR_QUERYCHARPOSITION independently scrolls stock RichEdit to its caret.
    // Isolate this one synthetic gesture test from asynchronous focus/IME
    // requests, restoring its own HWND's context before the fixture dies.
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,380,SWP_NOACTIVATE);
    SetFocus(field.edit);CHARRANGE caret{0,0};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&caret));
    // Finish the native focus/selection reveal before moving its scrollbar;
    // otherwise a first deferred caret reveal can race the thumb request.
    RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
    SCROLLINFO range{sizeof(range),SIF_ALL};GetScrollInfo(field.edit,SB_VERT,&range);
    const auto maximum=std::max(range.nMin,range.nMax-static_cast<int>(range.nPage)+1);
    check(maximum>20,"The scrollbar release fixture has no vertical overflow");
    SendMessageW(field.edit,WM_VSCROLL,MAKEWPARAM(SB_THUMBPOSITION,maximum/2),0);pump();
    POINT before{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
    check(before.y>0,"The thumb did not leave the insertion point outside the viewport");
    // A native nonclient scrollbar's tracking loop can finish with client
    // MOVE/UP messages. They are not a text-selection gesture.
    SendMessageW(field.edit,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(100,80));
    SendMessageW(field.edit,WM_LBUTTONUP,0,MAKELPARAM(100,80));pump();
    POINT after{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
    CHARRANGE selected{};SendMessageW(field.edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
    if(before.x!=after.x||before.y!=after.y||selected.cpMin||selected.cpMax)
        std::cerr<<"Scrollbar release diagnostics: readonly="<<readonly<<", before="<<before.x<<','<<before.y<<", after="<<after.x<<','<<after.y
            <<", selection="<<selected.cpMin<<','<<selected.cpMax<<'\n';
    check(before.x==after.x&&before.y==after.y&&selected.cpMin==0&&selected.cpMax==0&&field.editor->document().text==text,
        "Releasing the scrollbar or panning unexpectedly jumped back to the offscreen caret");
    SetFocus(field.host);
    MoveWindow(field.edit,0,0,500,280,TRUE);RECT format{};GetClientRect(field.edit,&format);
    format.left=code_gutter_snapshot(field.edit).width+10;format.top=16;format.bottom-=16;format.right-=16;
    SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));pump();UpdateWindow(field.edit);
    const ClientPixels resized(field.edit);RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
    const ClientPixels repainted(field.edit);check(std::equal(resized.values.begin(),resized.values.end(),repainted.values.begin(),
        [](DWORD left,DWORD right){return (left&0xffffff)==(right&0xffffff);}),
        "Resizing kept an old masked edge over newly complete source rows");
}
void lesson_number_at_exact_edge(bool readonly){
    const auto text=source(36);Fixture field(text,true,true,880,false);
    // Match LessonView's order: format an empty code field, then stream the
    // saved source through CodeEditor, without reformatting its paragraphs.
    field.editor.reset();presentation::set_rich_text(field.edit,"",{},true);
    field.editor=std::make_unique<CodeEditor>(field.edit,predefined_code_document(text),true);
    presentation::style_cpp(field.edit);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    set_code_gutter_appearance(field.edit,RGB(20,20,20),RGB(111,119,130),3,3);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,1000,600,SWP_NOACTIVATE);
    for(const int zoom:{75,100,150,200}){
        SendMessageW(field.edit,EM_SETZOOM,zoom,100);SendMessageW(field.edit,EM_SHOWSCROLLBAR,SB_HORZ,FALSE);
        const auto inset=MulDiv(16,100,zoom);const LONG display_inset=0;
        const auto configure=[&]{RECT client{},format{};GetClientRect(field.edit,&client);SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
            format.top=inset;format.right=client.right-inset;format.bottom=client.bottom-inset;
            SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));SendMessageW(field.edit,WM_VSCROLL,SB_TOP,0);};
        configure();RedrawWindow(field.host,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);pump();
        for(const int rows:{3,4,5,6,7}){
            const auto next=SendMessageW(field.edit,EM_LINEINDEX,rows,0);POINTL boundary{};SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&boundary),next);
            MoveWindow(field.edit,0,0,880,boundary.y+display_inset,TRUE);configure();
            RedrawWindow(field.host,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);pump();
            RECT client{},format{};GetClientRect(field.edit,&client);SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
            const LONG top=MulDiv(format.top,zoom,100),bottom=client.bottom-MulDiv(client.bottom-format.bottom,zoom,100);
            const ClientPixels pixels(field.edit);const auto gutter=code_gutter_snapshot(field.edit);
            const auto background=presentation::reader_theme::palette().high_contrast?presentation::reader_theme::background(field.edit):RGB(20,20,20);
            int complete=0;
            for(const auto& line:gutter.visible){const auto visual=SendMessageW(field.edit,EM_LINEFROMCHAR,line.character,0),next_start=SendMessageW(field.edit,EM_LINEINDEX,visual+1,0);
                if(next_start<0)continue;POINTL below{};SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&below),next_start);
                if(line.text_position.y<top||below.y>bottom)continue;++complete;
                const auto height=below.y-line.text_position.y;std::size_t ink=0;
                for(int y=line.text_position.y+height/4;y<line.text_position.y+height*3/4;++y)for(int column=0;column<gutter.width;++column)
                    if(pixel_color(pixels.values[static_cast<std::size_t>(y)*pixels.width+column])!=background)++ink;
                if(!ink)std::cerr<<"Exact edge diagnostics: readonly="<<readonly<<", zoom="<<zoom<<", rows="<<rows<<", number="<<line.number
                    <<", native_row="<<line.text_position.y<<','<<below.y<<", viewport="<<top<<','<<bottom<<'\n';
                check(ink>0,"A complete lesson code row exactly at the formatting edge omitted its number");
            }
            if(complete!=rows)std::cerr<<"Exact edge setup: readonly="<<readonly<<", zoom="<<zoom<<", requested="<<rows<<", actual="<<complete
                <<", boundary="<<boundary.y<<", viewport="<<top<<','<<bottom<<'\n';
            check(complete==rows,"The exact-edge lesson fixture did not expose its expected complete native rows");
        }
    }
    check(field.editor->document().text==text,"Edge numbering changed the saved lesson source");
}
void aligned(HWND target,const CodeGutterSnapshot& gutter){
    check(gutter.active&&!gutter.visible.empty(),"Native gutter has no visible paragraph numbers");
    std::size_t previous=0;
    for(const auto& line:gutter.visible){POINTL point{};SendMessageW(target,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),line.character);
        check(point.y==line.text_position.y&&point.x==line.text_position.x,"Number coordinates diverged from their actual text start");
        check(line.number>previous,"Wrapped display lines created duplicate paragraph numbers");previous=line.number;}
}
}
int main(int argc,char** argv){const bool scroll_only=argc==2&&std::string_view(argv[1])=="--scroll-paint";if(argc>1&&!scroll_only)return 2;
    std::cout<<std::unitbuf;const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    presentation::reader_theme::prepare_fonts();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);int passed=0,failed=0;
    const auto test=[&](const char* name,const std::function<void()>& action){try{action();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("TOM/UIA source replacement refreshes logical numbers without touching Undo",native_tom_edit_refreshes_numbers);
    test("The fixed gutter is never separately erased before its composed frame",erase_does_not_expose_uncomposed_gutter);
    test("Editable gutter never covers native source after the real text origin",[]{source_origin_is_not_masked(false);});
    test("Readonly gutter never covers native source after the real text origin",[]{source_origin_is_not_masked(true);});
    test("Editable code scrolling matches a complete native repaint",[]{code_scroll_repaint(false);});
    test("Readonly code scrolling matches a complete native repaint",[]{code_scroll_repaint(true);});
    test("Editable code preserves complete first and final lines at 75-200 percent",[]{code_vertical_edges(false);});
    test("Readonly code preserves complete first and final lines at 75-200 percent",[]{code_vertical_edges(true);});
    test("Editable line numbers retain their visible pixels at both partial viewport edges",[]{partial_number_edges(false);});
    test("Readonly line numbers retain their visible pixels at both partial viewport edges",[]{partial_number_edges(true);});
    test("Editable code preserves partially visible source rows",[]{code_whole_rows(false);});
    test("Readonly code preserves partially visible source rows",[]{code_whole_rows(true);});
    test("Wrapped editable code retains partial visual rows and complete paragraphs",[]{code_whole_rows(false,true);});
    test("Wrapped readonly code retains partial visual rows and complete paragraphs",[]{code_whole_rows(true,true);});
    test("Editable code scrollbar release preserves the offscreen caret and new viewport geometry",[]{scrollbar_release_keeps_viewport(false);});
    test("Readonly code scrollbar release preserves selection and new viewport geometry",[]{scrollbar_release_keeps_viewport(true);});
    test("Editable lesson code numbers every complete native row at its exact client edge",[]{lesson_number_at_exact_edge(false);});
    test("Readonly lesson code numbers every complete native row at its exact client edge",[]{lesson_number_at_exact_edge(true);});
    if(scroll_only){FreeLibrary(rich);std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;}
    test("Gutter applies only to assisted multiline code",[]{Fixture code("int main() {}"),single("std::cout",false),input("A\n B",true,false);
        check(code_gutter_snapshot(code.edit).active,"C++ multiline editor omitted its gutter");
        check(!code_gutter_snapshot(single.edit).active&&!code_gutter_snapshot(input.edit).active,"Single-line response or stdin acquired line numbers");});
    test("Width grows for 100+ lines without wrapping or changing source",[]{Fixture field(source(9));const auto initial=code_gutter_snapshot(field.edit);
        const auto text=source(130);field.editor->reset(predefined_code_document(text));const auto gutter=code_gutter_snapshot(field.edit);
        check(gutter.logical_lines==130&&gutter.width>initial.width,"Three-digit numbers did not receive a wider native margin");
        check(field.editor->document().text==text,"Margin update changed source characters");aligned(field.edit,gutter);
        RECT format{};SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        const auto text_gap=MulDiv(10,static_cast<int>(GetDpiForWindow(field.edit)),96);
        check(format.left>=gutter.width+text_gap,"Native text lost the Android gap after the number column");});
    test("Actual paragraph starts remain aligned through vertical scrolling",[]{Fixture field(source(130));SendMessageW(field.edit,EM_LINESCROLL,0,95);
        aligned(field.edit,code_gutter_snapshot(field.edit));
        check(SendMessageW(field.edit,EM_GETFIRSTVISIBLELINE,0,0)>0,"Native relative scrolling did not move the editor");
        // RichEdit's relative line-scroll step can differ from the actual formatted
        // paragraph height. Locate paragraph 100 through its real native position,
        // rather than assuming that requesting 95 steps makes it visible.
        const auto document=field.editor->document();POINT scroll{},position{};RECT area{};
        SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));GetClientRect(field.edit,&area);
        const auto character=SendMessageW(field.edit,EM_LINEINDEX,99,0);check(character>=0,"Native paragraph 100 is missing");
        SendMessageW(field.edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&position),character);
        POINT destination{scroll.x,std::max(0L,scroll.y+position.y-area.bottom/4)};
        SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&destination));
        const auto gutter=code_gutter_snapshot(field.edit);aligned(field.edit,gutter);
        const auto hundred=std::ranges::find(gutter.visible,std::size_t{100},&CodeGutterLine::number);
        check(hundred!=gutter.visible.end(),"Real scroll did not expose paragraph 100");
        const auto after=field.editor->document();check(after.text==document.text&&after.selection_start==document.selection_start&&after.selection_end==document.selection_end,
            "Scrolling the real gutter changed source or cursor selection");
        Raster pixels(field.edit);const auto background=presentation::reader_theme::background(field.edit);unsigned ink=0;
        for(int y=std::max(0L,hundred->text_position.y);y<std::min(pixels.height,static_cast<int>(hundred->text_position.y)+17);++y)
            for(int x=0;x<gutter.width;++x)if(GetPixel(pixels.dc,x,y)!=background)++ink;
        check(ink>10,"Three-digit line number was not drawn in its real text row");
        const auto old=SelectObject(pixels.dc,field.font);RECT measured{0,0,0,0};DrawTextW(pixels.dc,L"130",3,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);SelectObject(pixels.dc,old);
        // Android reserves 8dp inside the editor number column, plus one
        // pixel against fractional glyph clipping. The text gap is separate.
        const auto number_padding=MulDiv(8,static_cast<int>(GetDpiForWindow(field.edit)),96)+1;
        check(measured.right+number_padding<=gutter.width,"Three-digit number can be clipped or split in the reserved margin");});
    test("Soft wrapping numbers each source paragraph exactly once",[]{Fixture field("// "+std::string(72,'x')+"\nint value = 2;",true,true,180,true);
        check(SendMessageW(field.edit,EM_GETLINECOUNT,0,0)>2,"Native fixture did not actually soft-wrap");
        auto gutter=code_gutter_snapshot(field.edit);check(gutter.logical_lines==2,"Soft wrap was mistaken for source lines");aligned(field.edit,gutter);
        SendMessageW(field.edit,EM_LINESCROLL,0,2);gutter=code_gutter_snapshot(field.edit);aligned(field.edit,gutter);
        check(gutter.logical_lines==2,"Scrolling changed the number of source paragraphs");});
    test("Painting preserves exact CRLF tabs selection indentation and undo",[]{const std::string original="int main() {\r\n\t    return 0;\r\n}\r\n";Fixture field(original);
        CHARRANGE selection{2,6};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));const auto before=field.editor->document();
        set_code_gutter_appearance(field.edit,RGB(24,26,31),RGB(111,119,130),3);
        {Raster pixels(field.edit);const auto gutter=code_gutter_snapshot(field.edit);const auto colors=presentation::reader_theme::palette();
            const auto expected_background=colors.high_contrast?presentation::reader_theme::background(field.edit):RGB(24,26,31);
            check(GetPixel(pixels.dc,0,1)==expected_background,"The explicit laboratory gutter color was not painted, or replaced system high contrast");
            const auto expected_foreground=colors.high_contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(111,119,130);unsigned ink=0;
            for(int y=0;y<pixels.height;++y)for(int x=0;x<gutter.width;++x)if(GetPixel(pixels.dc,x,y)==expected_foreground)++ink;
            check(ink>0,"The explicit laboratory line-number ink was not rendered");}
        const auto after=field.editor->document();check(after.text==before.text&&after.indentation_starts==before.indentation_starts&&after.selection_start==before.selection_start&&after.selection_end==before.selection_end,
            "Gutter painting changed source, indentation identity or selection");
        const LONG length=GetWindowTextLengthW(field.edit);CHARRANGE end{length,length};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));SendMessageW(field.edit,WM_CHAR,L'x',1);
        const auto edited=field.editor->document();check(edited.text!=before.text,"Real editor insertion did not execute");
        {Raster pixels(field.edit);}field.editor->undo();check(field.editor->document().text==original,"Gutter formatting polluted undo or normalized the original source");
        field.editor->redo();check(field.editor->document().text==edited.text,"Gutter painting damaged redo");});
    test("Native text zoom scales gutter metrics without changing paragraph identities",[]{Fixture field(source(130));
        const auto before=field.editor->document();SendMessageW(field.edit,EM_SETZOOM,75,100);const auto reduced=code_gutter_snapshot(field.edit);
        SendMessageW(field.edit,EM_SETZOOM,150,100);const auto large=code_gutter_snapshot(field.edit);
        check(large.width>reduced.width&&large.logical_lines==reduced.logical_lines&&large.logical_lines==130,"Gutter did not measure actual RichEdit zoom");
        aligned(field.edit,large);const auto after=field.editor->document();
        check(before.text==after.text&&before.indentation_starts==after.indentation_starts&&before.selection_start==after.selection_start&&before.selection_end==after.selection_end,
            "Text zoom changed source, tab origin or selection");});
    FreeLibrary(rich);std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
