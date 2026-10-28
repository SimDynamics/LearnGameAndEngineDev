#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/editor_render_transaction.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace lgaed::windows {
namespace {
constexpr wchar_t property[]=L"lgaed.code-gutter";
constexpr UINT_PTR subclass_id=0x4C474743;
std::wstring raw_text(HWND window){
    GETTEXTLENGTHEX length{GTL_NUMCHARS|GTL_PRECISE,1200};
    const auto count=SendMessageW(window,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    if(count<0||count>4*1024*1024)throw std::runtime_error("Invalid native editor length");
    std::wstring result(static_cast<std::size_t>(count)+1,L'\0');
    GETTEXTEX get{static_cast<DWORD>(result.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    const auto copied=SendMessageW(window,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&get),reinterpret_cast<LPARAM>(result.data()));
    if(copied<0)throw std::runtime_error("Cannot read native editor paragraphs");
    result.resize(static_cast<std::size_t>(copied));return result;
}
}
struct CodeGutter::Impl {
    HWND window=nullptr,band_window=nullptr;HFONT font=nullptr;int width=0,line_height=1,original_margin=0;
    bool updating=false,publishing_scroll=false,revealing_caret=false,client_pointer=false,native_selection=false,native_theme_before=false;
    bool restore_clip_siblings=false,setting_clip_style=false;
    bool pending_content=false,pending_metrics=false,setting_format=false;int wheel_remainder=0;
    std::optional<COLORREF> background_color,foreground_color;
    int right_padding=3,horizontal_padding=8;
    std::vector<LONG> starts{0};
    mutable std::optional<CodeGutterSnapshot> committed_geometry;
    explicit Impl(HWND target):window(target),native_theme_before(presentation::reader_theme::native_scrollbar_theme(target)){
        RECT format{};SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));original_margin=format.left;
        restore_clip_siblings=(GetWindowLongPtrW(window,GWL_STYLE)&WS_CLIPSIBLINGS)!=0;}
    ~Impl(){if(band_window&&IsWindow(band_window))DestroyWindow(band_window);
        if(window&&IsWindow(window)){RemoveWindowSubclass(window,subclass,subclass_id);RemovePropW(window,property);
        SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN,MAKELPARAM(original_margin,0));
        const auto style=GetWindowLongPtrW(window,GWL_STYLE);
        const auto restored=restore_clip_siblings?style|WS_CLIPSIBLINGS:style&~static_cast<LONG_PTR>(WS_CLIPSIBLINGS);
        if(style!=restored)SetWindowLongPtrW(window,GWL_STYLE,restored);
        presentation::reader_theme::use_native_scrollbar_theme(window,native_theme_before);InvalidateRect(window,nullptr,FALSE);}
        if(font)DeleteObject(font);}
    static LRESULT CALLBACK band_proc(HWND band,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(band,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(band,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->band_window=band;return TRUE;}
        if(!self)return DefWindowProcW(band,message,wp,lp);
        if(message==WM_NCDESTROY){SetWindowLongPtrW(band,GWLP_USERDATA,0);if(self->band_window==band)self->band_window=nullptr;return DefWindowProcW(band,message,wp,lp);}
        if(message==WM_NCHITTEST)return HTTRANSPARENT;
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_WINDOWPOSCHANGING&&lp&&self->window&&IsWindow(self->window)){
            auto& position=*reinterpret_cast<WINDOWPOS*>(lp);RECT client{};GetClientRect(self->window,&client);
            POINT origin{};MapWindowPoints(self->window,GetParent(band),&origin,1);
            position.x=origin.x;position.y=origin.y;position.cx=self->fixed_width();position.cy=client.bottom;
            position.flags&=~(SWP_NOMOVE|SWP_NOSIZE);
        }
        if(message==WM_PRINTCLIENT){self->paint_fixed_surface(reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_PAINT&&presentation::editor_render::active(self->window)){ValidateRect(band,nullptr);return 0;}
        if(message==WM_PAINT){
            PAINTSTRUCT paint{};const auto target=BeginPaint(band,&paint);RECT client{};GetClientRect(band,&client);
            const auto memory=target?CreateCompatibleDC(target):nullptr;
            const auto bitmap=memory&&client.right>0&&client.bottom>0?CreateCompatibleBitmap(target,client.right,client.bottom):nullptr;
            if(bitmap){const auto old=SelectObject(memory,bitmap);self->paint_fixed_surface(memory);
                BitBlt(target,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,
                    memory,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);}
            else if(target)self->paint_fixed_surface(target);
            if(memory)DeleteDC(memory);EndPaint(band,&paint);return 0;
        }
        return DefWindowProcW(band,message,wp,lp);
    }
    void position_band(){
        if(!band_window||!window)return;RECT client{};GetClientRect(window,&client);
        const auto parent=GetParent(window);if(GetParent(band_window)!=parent)SetParent(band_window,parent);
        POINT origin{};MapWindowPoints(window,parent,&origin,1);
        const auto visible=(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0;
        SetWindowPos(band_window,HWND_TOP,origin.x,origin.y,fixed_width(),client.bottom,
            SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOREDRAW|SWP_NOCOPYBITS|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        InvalidateRect(band_window,nullptr,FALSE);
    }
    void create_band(){
        static const bool registered=[] {
            WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=band_proc;
            type.lpszClassName=L"LearnGameAndEngineDev.FixedCodeNumbers";
            if(!RegisterClassExW(&type))throw std::runtime_error("Cannot register the fixed code number surface");return true;
        }();
        (void)registered;setting_clip_style=true;SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)|WS_CLIPSIBLINGS);setting_clip_style=false;
        RECT client{};GetClientRect(window,&client);
        // A sibling is outside RichEdit's ScrollWindowEx surface. A child,
        // even with WS_CLIPCHILDREN, is erased by its native pixel copy.
        if(!CreateWindowExW(WS_EX_NOPARENTNOTIFY|WS_EX_NOACTIVATE,L"LearnGameAndEngineDev.FixedCodeNumbers",L"",WS_CHILD|WS_CLIPSIBLINGS|WS_DISABLED,
            0,0,std::min<LONG>(width,client.right),client.bottom,GetParent(window),nullptr,GetModuleHandleW(nullptr),this))
            throw std::runtime_error("Cannot create the fixed code number surface");
        position_band();
    }
    void publish_scroll(){
        if(updating||publishing_scroll||!window||!IsWindowVisible(window)||presentation::editor_render::active(window))return;
        const auto target=window;const auto context=reinterpret_cast<DWORD_PTR>(this);publishing_scroll=true;
        // ScrollWindowEx has already copied native pixels. Complete the new
        // client and fixed margin within this viewport event, rather than
        // exposing their intermediate copy until the next paint-queue turn.
        RedrawWindow(target,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_NOERASE|RDW_ALLCHILDREN);
        DWORD_PTR attached=0;
        if(GetWindowSubclass(target,subclass,subclass_id,&attached)&&attached==context){
            if(const auto band=reinterpret_cast<Impl*>(attached)->band_window)UpdateWindow(band);
            if(GetWindowSubclass(target,subclass,subclass_id,&attached)&&attached==context)
                reinterpret_cast<Impl*>(attached)->publishing_scroll=false;
        }
    }
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    RECT text_viewport(bool source_rows=false)const{
        RECT client{},format{};GetClientRect(window,&client);SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        int numerator=0,denominator=0;SendMessageW(window,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
        // RichEdit zooms the formatting insets with the document. Its client
        // remains larger than the area in which glyphs are actually drawn.
        // Native TextServices truncates fractional insets; MulDiv rounds and
        // can paint the fixed band over the first legitimate source pixel.
        // Derive this from the formatting insets, not paragraph zero: that
        // paragraph may be far outside the viewport or have its own indent.
        const auto scaled=[&](LONG inset){return static_cast<LONG>(static_cast<long long>(inset)*numerator/denominator);};
        RECT result{};result.left=std::clamp<LONG>(scaled(format.left),0,client.right);
        // Vertical layout rounds its first row origin. At 150%, a 7px top
        // inset starts at 11px; truncating it to 10 admits a partial row.
        result.top=std::clamp<LONG>(source_rows?MulDiv(format.top,numerator,denominator):scaled(format.top),0,client.bottom);
        result.right=std::clamp<LONG>(client.right-scaled(client.right-format.right),result.left,client.right);
        result.bottom=std::clamp<LONG>(client.bottom-scaled(client.bottom-format.bottom),result.top,client.bottom);
        return result;
    }
    LONG fixed_width()const{RECT client{};GetClientRect(window,&client);return std::min<LONG>(client.right,std::max<LONG>(width,text_viewport().left));}
    void edge_to_edge_format(){
        if(setting_format||!IsWindow(window))return;
        RECT client{},format{};GetClientRect(window,&client);SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        if(client.bottom<=0||(format.top==0&&format.bottom==client.bottom))return;
        // RichEdit formatting insets are permanent clipping planes, unlike
        // Android's scrollable content padding. Keep only horizontal margins.
        format.top=0;format.bottom=client.bottom;
        setting_format=true;
        SendMessageW(window,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
        setting_format=false;
    }
    void reveal_caret(){
        if(revealing_caret||GetFocus()!=window)return;
        CHARRANGE selected{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));LONG character=selected.cpMax;
        Microsoft::WRL::ComPtr<IRichEditOle> rich;
        Microsoft::WRL::ComPtr<ITextDocument> document;
        Microsoft::WRL::ComPtr<ITextSelection> text_selection;
        if(SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()))&&rich&&
                SUCCEEDED(rich.As(&document))&&SUCCEEDED(document->GetSelection(&text_selection))&&text_selection){
            LONG flags=0;if(SUCCEEDED(text_selection->GetFlags(&flags))&&(flags&tomSelStartActive))character=selected.cpMin;
        }
        const auto line=SendMessageW(window,EM_LINEFROMCHAR,character,0),start=SendMessageW(window,EM_LINEINDEX,line,0);
        if(start<0)return;POINTL point{};SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),start);
        auto height=line_height;const auto next=SendMessageW(window,EM_LINEINDEX,line+1,0);
        if(next>=0){POINTL below{};SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&below),next);
            if(below.y>point.y)height=below.y-point.y;}
        else if(line>0){const auto previous=SendMessageW(window,EM_LINEINDEX,line-1,0);POINTL above{};
            if(previous>=0){SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&above),previous);
                if(point.y>above.y)height=point.y-above.y;}}
        const auto viewport=text_viewport(true);if(height>viewport.bottom-viewport.top)return;
        const auto shift=point.y<viewport.top?point.y-viewport.top:point.y+height>viewport.bottom?point.y+height-viewport.bottom:0;
        if(!shift)return;
        POINT scroll{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));scroll.y=std::max(0L,scroll.y+shift);
        revealing_caret=true;SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));revealing_caret=false;
    }
    void refresh(bool content,bool metrics){
        if(updating||!window)return;
        if(presentation::editor_render::active(window)){pending_content|=content;pending_metrics|=metrics;return;}
        updating=true;
        try{
            edge_to_edge_format();
            if(content){const auto text=raw_text(window);starts.assign(1,0);
                for(std::size_t i=0;i<text.size();++i)if(text[i]==L'\r'||text[i]==L'\n'){
                    if(text[i]==L'\r'&&i+1<text.size()&&text[i+1]==L'\n')++i;
                    starts.push_back(static_cast<LONG>(i+1));}}
            const auto dc=GetDC(window);if(!dc)throw std::runtime_error("Cannot measure native editor gutter");
            int numerator=0,denominator=0;SendMessageW(window,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
            if(metrics||!font){CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(window,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
                const auto replacement=CreateFontW(-MulDiv(MulDiv(format.yHeight,GetDeviceCaps(dc,LOGPIXELSY),1440),numerator,denominator),0,0,0,
                    (format.dwEffects&CFE_BOLD)?FW_BOLD:FW_NORMAL,(format.dwEffects&CFE_ITALIC)!=0,FALSE,FALSE,format.bCharSet,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,format.bPitchAndFamily,format.szFaceName);
                if(replacement){if(font)DeleteObject(font);font=replacement;}}
            const auto previous=font?SelectObject(dc,font):nullptr;
            TEXTMETRICW text_metrics{};GetTextMetricsW(dc,&text_metrics);line_height=std::max(1L,text_metrics.tmHeight);
            int digit_width=1;for(wchar_t digit=L'0';digit<=L'9';++digit){SIZE size{};GetTextExtentPoint32W(dc,&digit,1,&size);digit_width=std::max(digit_width,static_cast<int>(size.cx));}
            const auto measured=static_cast<int>(std::to_wstring(starts.size()).size())*digit_width;
            if(previous)SelectObject(dc,previous);ReleaseDC(window,dc);
            // Match the Android number column rather than folding its text
            // gap into the painted band. The extra pixel protects fractional
            // glyph extents when the font/DPI/zoom changes.
            const int next=std::min(65535-px(10),std::max(px(18),measured+px(std::max(horizontal_padding,right_padding))+1));
            const auto margins=static_cast<DWORD>(SendMessageW(window,EM_GETMARGINS,0,0));
            width=next;
            // The band is measured in displayed pixels; the native margin is
            // unzoomed. Round upward so zoom cannot cover the first glyph.
            const auto native_margin=static_cast<int>(std::clamp<long long>((static_cast<long long>(width+px(10))*denominator+numerator-1)/numerator,0,65535));
            if(LOWORD(margins)!=native_margin)SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN,MAKELPARAM(native_margin,0));
            updating=false;
            // A text/metric change can make a previously masked edge row
            // complete again, or move a fixed margin. Remove the old overlay
            // from the whole client before drawing its new geometry.
            InvalidateRect(window,nullptr,FALSE);
            if(band_window){position_band();InvalidateRect(band_window,nullptr,FALSE);}
        }catch(...){updating=false;throw;}
    }
    CodeGutterSnapshot snapshot()const{
        CodeGutterSnapshot result;if(!window||!IsWindow(window))return result;
        if(presentation::editor_render::active(window)&&committed_geometry)return *committed_geometry;
        result.active=true;result.width=width;result.logical_lines=starts.size();
        RECT area{};GetClientRect(window,&area);
        const auto first_line=SendMessageW(window,EM_GETFIRSTVISIBLELINE,0,0);
        const auto first_character=static_cast<LONG>(std::max<LRESULT>(0,SendMessageW(window,EM_LINEINDEX,first_line,0)));
        auto begin=std::upper_bound(starts.begin(),starts.end(),first_character);if(begin!=starts.begin())--begin;
        std::optional<LONG> previous_y;
        for(auto item=begin;item!=starts.end();++item){POINTL location{};
            SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&location),*item);
            // During a frozen edit TextServices can temporarily report the
            // first row's position for every paragraph. Publishing those
            // coordinates stacks all labels on top of one another. Keep the
            // last committed rows until native layout is strictly ordered.
            if(previous_y&&location.y<=*previous_y){
                if(committed_geometry)return *committed_geometry;
                result.visible.clear();return result;
            }
            previous_y=location.y;
            if(location.y>=area.bottom)break;
            if(location.y+line_height>0)result.visible.push_back({static_cast<std::size_t>(item-starts.begin())+1,*item,location});}
        committed_geometry=result;
        return result;
    }
    void paint(HDC dc,bool numbers=true)const{
        if(!dc||!window)return;const auto saved=SaveDC(dc);RECT client{};GetClientRect(window,&client);
        const auto viewport=text_viewport();
        RECT reserved=client;reserved.right=std::min<LONG>(client.right,std::max<LONG>(width,viewport.left));
        IntersectClipRect(dc,client.left,client.top,client.right,client.bottom);
        // The native horizontal viewport clips at the client edge, not at the
        // formatting margin. Keep the text gap fixed beside the number band.
        const auto editor_fill=CreateSolidBrush(presentation::reader_theme::background(window));
        FillRect(dc,&reserved,editor_fill);
        // Leave partially visible source glyphs to native client clipping.
        DeleteObject(editor_fill);
        if(numbers)paint_band(dc);RestoreDC(dc,saved);
    }
    void paint_fixed_surface(HDC dc)const{
        if(!dc||!window)return;RECT area{};GetClientRect(window,&area);area.right=fixed_width();
        const auto fill=CreateSolidBrush(presentation::reader_theme::background(window));FillRect(dc,&area,fill);DeleteObject(fill);
        paint_band(dc);
    }
    void paint_band(HDC dc)const{
        if(!dc||!window)return;const auto saved=SaveDC(dc);RECT client{};GetClientRect(window,&client);
        RECT area=client;area.right=std::min<LONG>(client.right,width);
        IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
        const auto colors=presentation::reader_theme::palette();const auto background=colors.high_contrast?presentation::reader_theme::background(window):background_color.value_or(presentation::reader_theme::background(window));
        const auto fill=CreateSolidBrush(background);FillRect(dc,&area,fill);DeleteObject(fill);
        SetTextColor(dc,colors.high_contrast?GetSysColor(COLOR_WINDOWTEXT):foreground_color.value_or(
            presentation::reader_theme::applied(window)?colors.muted:GetSysColor(COLOR_GRAYTEXT)));SetBkMode(dc,TRANSPARENT);
        if(font)SelectObject(dc,font);
        // Every paragraph intersecting the native viewport keeps its label,
        // even when only part of the number is visible at a vertical edge.
        // Source-row masking must not make the number column lose a label.
        IntersectClipRect(dc,0,client.top,width,client.bottom);
        for(const auto& line:snapshot().visible){
            const auto label=std::to_wstring(line.number);RECT bounds{0,line.text_position.y,width-px(right_padding),line.text_position.y+line_height};
            DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&bounds,DT_RIGHT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP);}
        RestoreDC(dc,saved);
    }
    static LRESULT paint_client(HWND window,DWORD_PTR data){
        const bool caret_hidden=GetFocus()==window&&HideCaret(window)!=FALSE;
        PAINTSTRUCT paint{};const auto target=BeginPaint(window,&paint);
        if(!target){if(caret_hidden)ShowCaret(window);return 0;}
        RECT client{};GetClientRect(window,&client);
        const auto memory=CreateCompatibleDC(target);
        const auto bitmap=memory&&client.right>0&&client.bottom>0?CreateCompatibleBitmap(target,client.right,client.bottom):nullptr;
        if(memory&&bitmap){
            const auto old=SelectObject(memory,bitmap);
            const auto background=CreateSolidBrush(presentation::reader_theme::background(window));
            FillRect(memory,&client,background);DeleteObject(background);
            // Compose the native document and fixed overlay off screen. The
            // old native WM_PAINT followed by GetDC exposed an erased gutter
            // (and temporarily uncovered source pixels) between both draws.
            DefSubclassProc(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            DWORD_PTR attached=0;
            if(GetWindowSubclass(window,subclass,subclass_id,&attached)&&attached==data)
                reinterpret_cast<Impl*>(attached)->paint(memory,false);
            BitBlt(target,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,
                paint.rcPaint.bottom-paint.rcPaint.top,memory,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY);
            SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
        }else{
            if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);
            // Preserve native rendering if the temporary surface cannot be
            // allocated; no text, selection or history depends on the buffer.
            DefSubclassProc(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(target),PRF_CLIENT);
            DWORD_PTR attached=0;
            if(GetWindowSubclass(window,subclass,subclass_id,&attached)&&attached==data)
                reinterpret_cast<Impl*>(attached)->paint(target,false);
        }
        EndPaint(window,&paint);if(caret_hidden)ShowCaret(window);
        // This path replaces native WM_PAINT in the subclass chain. Restore
        // its nonclient refresh once the composed client frame is complete;
        // offscreen WM_PRINTCLIENT must never repaint the real scrollbar.
        presentation::reader_theme::refresh_scrollbars(window);return 0;
    }
    static LRESULT CALLBACK subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){if(self.band_window&&IsWindow(self.band_window))DestroyWindow(self.band_window);
            RemoveWindowSubclass(window,subclass,subclass_id);RemovePropW(window,property);self.window=nullptr;return DefSubclassProc(window,message,wp,lp);}
        if(message==WM_STYLECHANGING&&wp==static_cast<WPARAM>(GWL_STYLE)&&lp&&self.band_window){
            auto& style=*reinterpret_cast<STYLESTRUCT*>(lp);
            if(!self.setting_clip_style&&((style.styleOld^style.styleNew)&WS_CLIPSIBLINGS))
                self.restore_clip_siblings=(style.styleNew&WS_CLIPSIBLINGS)!=0;
            style.styleNew|=WS_CLIPSIBLINGS;
        }
        if(message==presentation::editor_render::committed_message){
            const bool metrics=self.pending_metrics;self.pending_metrics=false;self.pending_content=false;
            self.refresh(true,metrics);self.publish_scroll();return 0;
        }
        if(message==WM_PAINT&&presentation::editor_render::active(window)){ValidateRect(window,nullptr);return 0;}
        RECT formatting{};
        if((message==EM_SETRECT||message==EM_SETRECTNP)&&lp){
            formatting=*reinterpret_cast<const RECT*>(lp);RECT client{};GetClientRect(window,&client);
            formatting.top=0;formatting.bottom=client.bottom;lp=reinterpret_cast<LPARAM>(&formatting);
        }
        if(message==WM_MOUSEWHEEL&&!(LOWORD(wp)&(MK_CONTROL|MK_SHIFT))){
            SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE};
            if(GetScrollInfo(window,SB_VERT,&range)&&range.nPage&&range.nMax-range.nMin+1>static_cast<int>(range.nPage)){
                UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
                self.wheel_remainder+=static_cast<short>(HIWORD(wp));
                const int turns=self.wheel_remainder/WHEEL_DELTA;self.wheel_remainder%=WHEEL_DELTA;
                if(lines==WHEEL_PAGESCROLL){for(int n=0;n<std::abs(turns);++n)SendMessageW(window,EM_SCROLL,turns>0?SB_PAGEUP:SB_PAGEDOWN,0);}
                else if(turns&&lines)SendMessageW(window,EM_LINESCROLL,0,static_cast<LPARAM>(-turns*static_cast<int>(std::min<UINT>(lines,1000))));
                // Native smooth-wheel animation can bypass our sibling gutter.
                // Use its line/page scroll API, then publish the same viewport.
                if(turns){InvalidateRect(window,nullptr,FALSE);if(self.band_window)InvalidateRect(self.band_window,nullptr,FALSE);self.publish_scroll();}
                return 0;
            }
        }
        if(message==WM_ERASEBKGND)return 1; // The composed client includes its complete background.
        if(message==WM_PAINT)return paint_client(window,data);
        const bool selection_message=message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_LBUTTONUP||
            (message==WM_MOUSEMOVE&&(wp&MK_LBUTTON));
        CHARRANGE previous_selection{};
        if(selection_message)SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&previous_selection));
        if(message==WM_NCLBUTTONDOWN||message==WM_CAPTURECHANGED||message==WM_CANCELMODE){self.client_pointer=false;self.native_selection=false;}
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){
            self.client_pointer=true;
            self.native_selection=(wp&(MK_SHIFT|MK_CONTROL))||(!(GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY)&&
                presentation::rich_text_hit_test(window,{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))}));
        }
        const bool client_was_pressed=self.client_pointer,was_selection=self.native_selection;
        const bool may_scroll=message==WM_TIMER||message==EM_SCROLL||message==WM_SIZE||message==WM_VSCROLL||message==WM_HSCROLL||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||message==WM_IME_REQUEST||
            message==EM_SETSCROLLPOS||message==EM_LINESCROLL||message==EM_SCROLLCARET||message==WM_KEYDOWN||message==WM_CHAR||
            message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_LBUTTONUP||(message==WM_MOUSEMOVE&&(wp&MK_LBUTTON))||message==EM_EXSETSEL||message==EM_SETSEL;
        POINT before{};if(may_scroll)SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
        const auto result=DefSubclassProc(window,message,wp,lp);
        DWORD_PTR attached=0;
        if(!GetWindowSubclass(window,subclass,subclass_id,&attached)||attached!=data)return result;
        try{
            bool reveal_selection=false;
            if(selection_message&&client_was_pressed){CHARRANGE selected{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
                const bool changed=selected.cpMin!=previous_selection.cpMin||selected.cpMax!=previous_selection.cpMax;
                if(changed){self.native_selection=true;if(GetCapture()==window)self.client_pointer=true;}
                reveal_selection=was_selection||self.native_selection;
            }
            if(message==WM_LBUTTONUP){self.client_pointer=false;self.native_selection=false;}
            if(message==WM_PRINTCLIENT)self.paint(reinterpret_cast<HDC>(wp));
            else if(message==WM_SETFONT||message==EM_SETZOOM||message==WM_DPICHANGED_AFTERPARENT||
                (message==EM_SETCHARFORMAT&&lp&&(wp==SCF_DEFAULT||wp==SCF_ALL)&&
                    (reinterpret_cast<const CHARFORMAT2W*>(lp)->dwMask&(CFM_SIZE|CFM_FACE|CFM_CHARSET|CFM_BOLD|CFM_ITALIC))))self.refresh(false,true);
            else if(message==WM_SETTEXT||message==EM_SETTEXTEX||message==EM_STREAMIN||message==EM_REPLACESEL||message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==EM_UNDO||message==EM_REDO)self.refresh(true,false);
            if(may_scroll){POINT after{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
                const bool typing=message==WM_CHAR&&(wp>=L' '||wp==L'\r'||wp==L'\n'||wp==L'\t'||wp==L'\b');
                const bool navigation=message==WM_KEYDOWN&&((wp>=VK_PRIOR&&wp<=VK_DOWN)||wp==VK_BACK||wp==VK_DELETE||wp==VK_RETURN);
                if(message==EM_SCROLLCARET||typing||navigation||reveal_selection)
                    self.reveal_caret();
                SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
                // RichEdit scrolls by copying existing client pixels, including
                // this fixed overlay. Repainting only the number band leaves
                // old band/text pixels copied into the document on the return
                // movement. Redraw the affected viewport after a real offset
                // change; the native document, selection and offsets stay intact.
                if(before.x!=after.x||before.y!=after.y){
                    InvalidateRect(window,nullptr,FALSE);
                    if(self.band_window)InvalidateRect(self.band_window,nullptr,FALSE);
                    const bool native_scroll=message==WM_HSCROLL||message==WM_VSCROLL||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||
                        message==EM_SETSCROLLPOS||message==EM_LINESCROLL||message==EM_SCROLL||message==WM_TIMER||message==WM_MOUSEMOVE;
                    if(native_scroll)self.publish_scroll();
                }
            }
            if(message==WM_SHOWWINDOW&&self.band_window)SetWindowPos(self.band_window,nullptr,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|(wp?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
            if(message==WM_WINDOWPOSCHANGED)self.position_band();
            if(message==WM_SIZE||message==EM_SETRECT||message==EM_SETRECTNP||message==EM_SETTARGETDEVICE){
                self.edge_to_edge_format();
                self.position_band();if(self.band_window)InvalidateRect(self.band_window,nullptr,FALSE);
                InvalidateRect(window,nullptr,FALSE);
                // The owner can be committing many rows with SWP_NOREDRAW.
                // Let its final viewport paint publish this geometry; a
                // synchronous child paint here can flush its parent mid-layout.
            }
        }catch(...){/* Margin failure must never change or reject the source edit. */}
        return result;
    }
};
CodeGutter::CodeGutter(HWND window){
    if(!IsWindow(window))throw std::invalid_argument("Code gutter requires a native RichEdit");
    if(GetPropW(window,property))throw std::invalid_argument("Native editor already has a gutter");
    impl_=std::make_unique<Impl>(window);
    if(!SetWindowSubclass(window,Impl::subclass,subclass_id,reinterpret_cast<DWORD_PTR>(impl_.get())))throw std::runtime_error("Cannot attach native gutter");
    if(!SetPropW(window,property,reinterpret_cast<HANDLE>(this)))throw std::runtime_error("Cannot expose native gutter state");
    presentation::reader_theme::use_native_scrollbar_theme(window);
    impl_->refresh(true,true);
    impl_->create_band();
}
CodeGutter::~CodeGutter()=default;
CodeGutterSnapshot CodeGutter::snapshot()const{return impl_->snapshot();}
void CodeGutter::refresh_content(){impl_->refresh(true,false);}
void CodeGutter::set_appearance(COLORREF background,COLORREF foreground,int right_padding,int horizontal_padding){
    right_padding=std::clamp(right_padding,0,32);horizontal_padding=std::clamp(horizontal_padding,0,64);
    if(impl_->background_color==background&&impl_->foreground_color==foreground&&
        impl_->right_padding==right_padding&&impl_->horizontal_padding==horizontal_padding)return;
    impl_->background_color=background;impl_->foreground_color=foreground;
    impl_->right_padding=right_padding;impl_->horizontal_padding=horizontal_padding;
    impl_->refresh(false,false);
}
CodeGutterSnapshot code_gutter_snapshot(HWND window){const auto gutter=reinterpret_cast<const CodeGutter*>(GetPropW(window,property));return gutter?gutter->snapshot():CodeGutterSnapshot{};}
void set_code_gutter_appearance(HWND window,COLORREF background,COLORREF foreground,int right_padding,int horizontal_padding){if(const auto gutter=reinterpret_cast<CodeGutter*>(GetPropW(window,property)))gutter->set_appearance(background,foreground,right_padding,horizontal_padding);}
}
