#include "presentation/windows/code_gutter.hpp"
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
    HWND window=nullptr;HFONT font=nullptr;int width=0,line_height=1,original_margin=0;
    bool updating=false,revealing_caret=false,client_pointer=false,native_selection=false;
    std::optional<COLORREF> background_color,foreground_color;
    int right_padding=3,horizontal_padding=8;
    std::vector<LONG> starts{0};
    explicit Impl(HWND target):window(target){RECT format{};SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));original_margin=format.left;}
    ~Impl(){if(window&&IsWindow(window)){RemoveWindowSubclass(window,subclass,subclass_id);RemovePropW(window,property);
        SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN,MAKELPARAM(original_margin,0));InvalidateRect(window,nullptr,FALSE);}
        if(font)DeleteObject(font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    void invalidate()const{if(!window)return;RECT bounds{};GetClientRect(window,&bounds);bounds.right=width;InvalidateRect(window,&bounds,FALSE);}
    RECT text_viewport()const{
        RECT client{},format{};GetClientRect(window,&client);SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        int numerator=0,denominator=0;SendMessageW(window,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
        // RichEdit zooms the formatting insets with the document. Its client
        // remains larger than the area in which glyphs are actually drawn.
        RECT result{};result.left=std::clamp<LONG>(MulDiv(format.left,numerator,denominator),0,client.right);
        result.top=std::clamp<LONG>(MulDiv(format.top,numerator,denominator),0,client.bottom);
        result.right=std::clamp<LONG>(client.right-MulDiv(client.right-format.right,numerator,denominator),result.left,client.right);
        result.bottom=std::clamp<LONG>(client.bottom-MulDiv(client.bottom-format.bottom,numerator,denominator),result.top,client.bottom);
        return result;
    }
    struct VisibleRows{LONG top=0,bottom=0;};
    VisibleRows complete_rows()const{
        const auto viewport=text_viewport();VisibleRows result{viewport.top,viewport.bottom};
        const auto count=SendMessageW(window,EM_GETLINECOUNT,0,0);
        auto line=std::max<LRESULT>(0,SendMessageW(window,EM_GETFIRSTVISIBLELINE,0,0)-1);
        bool complete=false;
        for(;line<count;++line){const auto character=SendMessageW(window,EM_LINEINDEX,line,0);if(character<0)break;
            POINTL position{};SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&position),character);
            if(position.y>=viewport.bottom)break;
            auto height=line_height;
            if(line+1<count){const auto next=SendMessageW(window,EM_LINEINDEX,line+1,0);POINTL below{};
                if(next>=0){SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&below),next);
                    if(below.y>position.y)height=below.y-position.y;}}
            else if(line>0){const auto previous=SendMessageW(window,EM_LINEINDEX,line-1,0);POINTL above{};
                if(previous>=0){SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&above),previous);
                    if(position.y>above.y)height=position.y-above.y;}}
            if(position.y+height<=viewport.top)continue;
            if(position.y>=viewport.top&&position.y+height<=viewport.bottom)complete=true;
            else if(position.y<viewport.top)result.top=std::max(result.top,position.y+height);
            else result.bottom=std::min(result.bottom,position.y);
        }
        // A field shorter than one glyph cannot show a complete row. Keep
        // native rendering usable in that degenerate temporary resize.
        return complete?result:VisibleRows{viewport.top,viewport.bottom};
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
        const auto viewport=text_viewport();if(height>viewport.bottom-viewport.top)return;
        const auto shift=point.y<viewport.top?point.y-viewport.top:point.y+height>viewport.bottom?point.y+height-viewport.bottom:0;
        if(!shift)return;
        POINT scroll{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));scroll.y=std::max(0L,scroll.y+shift);
        revealing_caret=true;SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));revealing_caret=false;
    }
    void refresh(bool content,bool metrics){
        if(updating||!window)return;updating=true;
        try{
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
        }catch(...){updating=false;throw;}
    }
    CodeGutterSnapshot snapshot()const{
        CodeGutterSnapshot result;if(!window||!IsWindow(window))return result;
        result.active=true;result.width=width;result.logical_lines=starts.size();
        RECT area{};GetClientRect(window,&area);
        const auto first_line=SendMessageW(window,EM_GETFIRSTVISIBLELINE,0,0);
        const auto first_character=static_cast<LONG>(std::max<LRESULT>(0,SendMessageW(window,EM_LINEINDEX,first_line,0)));
        auto begin=std::upper_bound(starts.begin(),starts.end(),first_character);if(begin!=starts.begin())--begin;
        for(auto item=begin;item!=starts.end();++item){POINTL location{};
            SendMessageW(window,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&location),*item);
            if(location.y>=area.bottom)break;
            if(location.y+line_height>0)result.visible.push_back({static_cast<std::size_t>(item-starts.begin())+1,*item,location});}
        return result;
    }
    void paint(HDC dc)const{
        if(!dc||!window)return;const auto saved=SaveDC(dc);RECT client{};GetClientRect(window,&client);
        const auto viewport=text_viewport();
        RECT reserved=client;reserved.right=std::min<LONG>(client.right,std::max<LONG>(width,viewport.left));
        IntersectClipRect(dc,client.left,client.top,client.right,client.bottom);
        // The native horizontal viewport clips at the client edge, not at the
        // formatting margin. Keep the text gap fixed beside the number band.
        const auto editor_fill=CreateSolidBrush(presentation::reader_theme::background(window));
        FillRect(dc,&reserved,editor_fill);
        const auto visible=complete_rows();
        // Keep whole display rows at both vertical edges. Partial rows are
        // blank until native scrolling brings the complete row into view;
        // source positions, selection and native offsets remain unchanged.
        const RECT top{width,0,client.right,visible.top},bottom{width,visible.bottom,client.right,client.bottom};
        if(!IsRectEmpty(&top))FillRect(dc,&top,editor_fill);
        if(!IsRectEmpty(&bottom))FillRect(dc,&bottom,editor_fill);
        DeleteObject(editor_fill);
        RECT area=client;area.right=width;
        IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
        const auto colors=presentation::reader_theme::palette();const auto background=colors.high_contrast?presentation::reader_theme::background(window):background_color.value_or(presentation::reader_theme::background(window));
        const auto fill=CreateSolidBrush(background);FillRect(dc,&area,fill);DeleteObject(fill);
        SetTextColor(dc,colors.high_contrast?GetSysColor(COLOR_WINDOWTEXT):foreground_color.value_or(
            presentation::reader_theme::applied(window)?colors.muted:GetSysColor(COLOR_GRAYTEXT)));SetBkMode(dc,TRANSPARENT);
        if(font)SelectObject(dc,font);
        IntersectClipRect(dc,0,visible.top,width,visible.bottom);
        // The band already contains only complete native display rows. GDI's
        // font cell can differ by a pixel; it must not reject a native row.
        for(const auto& line:snapshot().visible){if(line.text_position.y<visible.top||line.text_position.y>=visible.bottom)continue;
            const auto label=std::to_wstring(line.number);RECT bounds{0,line.text_position.y,width-px(right_padding),line.text_position.y+line_height};
            DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&bounds,DT_RIGHT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP);}
        RestoreDC(dc,saved);
    }
    static LRESULT CALLBACK subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(window,subclass,subclass_id);RemovePropW(window,property);self.window=nullptr;return DefSubclassProc(window,message,wp,lp);}
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
        const bool may_scroll=message==WM_SIZE||message==WM_VSCROLL||message==WM_HSCROLL||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||message==WM_IME_REQUEST||
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
            if(message==WM_PAINT){const auto dc=GetDC(window);if(dc){self.paint(dc);ReleaseDC(window,dc);}}
            else if(message==WM_PRINTCLIENT)self.paint(reinterpret_cast<HDC>(wp));
            else if(message==WM_SETFONT||message==EM_SETZOOM||message==WM_DPICHANGED_AFTERPARENT||
                (message==EM_SETCHARFORMAT&&lp&&(wp==SCF_DEFAULT||wp==SCF_ALL)&&
                    (reinterpret_cast<const CHARFORMAT2W*>(lp)->dwMask&(CFM_SIZE|CFM_FACE|CFM_CHARSET|CFM_BOLD|CFM_ITALIC))))self.refresh(false,true);
            else if(message==WM_SETTEXT||message==EM_SETTEXTEX||message==EM_STREAMIN||message==EM_REPLACESEL||message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==EM_UNDO||message==EM_REDO)self.refresh(true,false);
            else if(may_scroll)self.invalidate();
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
                if(before.x!=after.x||before.y!=after.y)InvalidateRect(window,nullptr,FALSE);
            }
            if(message==WM_SIZE||message==EM_SETRECT||message==EM_SETRECTNP||message==EM_SETTARGETDEVICE)
                InvalidateRect(window,nullptr,FALSE);
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
    impl_->refresh(true,true);
}
CodeGutter::~CodeGutter()=default;
CodeGutterSnapshot CodeGutter::snapshot()const{return impl_->snapshot();}
void CodeGutter::set_appearance(COLORREF background,COLORREF foreground,int right_padding,int horizontal_padding){
    impl_->background_color=background;impl_->foreground_color=foreground;
    impl_->right_padding=std::clamp(right_padding,0,32);impl_->horizontal_padding=std::clamp(horizontal_padding,0,64);
    impl_->refresh(false,false);
}
CodeGutterSnapshot code_gutter_snapshot(HWND window){const auto gutter=reinterpret_cast<const CodeGutter*>(GetPropW(window,property));return gutter?gutter->snapshot():CodeGutterSnapshot{};}
void set_code_gutter_appearance(HWND window,COLORREF background,COLORREF foreground,int right_padding,int horizontal_padding){if(const auto gutter=reinterpret_cast<CodeGutter*>(GetPropW(window,property)))gutter->set_appearance(background,foreground,right_padding,horizontal_padding);}
}
