#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <richedit.h>
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
    bool updating=false;
    std::optional<COLORREF> background_color,foreground_color;
    int right_padding=8;
    std::vector<LONG> starts{0};
    explicit Impl(HWND target):window(target){RECT format{};SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));original_margin=format.left;}
    ~Impl(){if(window&&IsWindow(window)){RemoveWindowSubclass(window,subclass,subclass_id);RemovePropW(window,property);
        SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN,MAKELPARAM(original_margin,0));InvalidateRect(window,nullptr,FALSE);}
        if(font)DeleteObject(font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    void invalidate()const{if(!window)return;RECT bounds{};GetClientRect(window,&bounds);bounds.right=width;InvalidateRect(window,&bounds,FALSE);}
    void refresh(bool content,bool metrics){
        if(updating||!window)return;updating=true;
        try{
            if(content){const auto text=raw_text(window);starts.assign(1,0);
                for(std::size_t i=0;i<text.size();++i)if(text[i]==L'\r'||text[i]==L'\n'){
                    if(text[i]==L'\r'&&i+1<text.size()&&text[i+1]==L'\n')++i;
                    starts.push_back(static_cast<LONG>(i+1));}}
            const auto dc=GetDC(window);if(!dc)throw std::runtime_error("Cannot measure native editor gutter");
            if(metrics||!font){CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(window,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
                int numerator=0,denominator=0;SendMessageW(window,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
                if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
                const auto replacement=CreateFontW(-MulDiv(MulDiv(format.yHeight,GetDeviceCaps(dc,LOGPIXELSY),1440),numerator,denominator),0,0,0,
                    (format.dwEffects&CFE_BOLD)?FW_BOLD:FW_NORMAL,(format.dwEffects&CFE_ITALIC)!=0,FALSE,FALSE,format.bCharSet,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,format.bPitchAndFamily,format.szFaceName);
                if(replacement){if(font)DeleteObject(font);font=replacement;}}
            const auto previous=font?SelectObject(dc,font):nullptr;
            TEXTMETRICW text_metrics{};GetTextMetricsW(dc,&text_metrics);line_height=std::max(1L,text_metrics.tmHeight);
            int digit_width=1;for(wchar_t digit=L'0';digit<=L'9';++digit){SIZE size{};GetTextExtentPoint32W(dc,&digit,1,&size);digit_width=std::max(digit_width,static_cast<int>(size.cx));}
            const auto measured=static_cast<int>(std::to_wstring(starts.size()).size())*digit_width;
            if(previous)SelectObject(dc,previous);ReleaseDC(window,dc);
            const int next=std::min(65535,std::max(px(20),measured+px(16)));
            if(next!=width){width=next;SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN,MAKELPARAM(width,0));}
            updating=false;invalidate();
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
        if(!dc||!window)return;const auto saved=SaveDC(dc);RECT area{};GetClientRect(window,&area);area.right=width;
        IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
        const auto colors=presentation::reader_theme::palette();const auto background=colors.high_contrast?presentation::reader_theme::background(window):background_color.value_or(presentation::reader_theme::background(window));
        const auto fill=CreateSolidBrush(background);FillRect(dc,&area,fill);DeleteObject(fill);
        SetTextColor(dc,colors.high_contrast?GetSysColor(COLOR_WINDOWTEXT):foreground_color.value_or(
            presentation::reader_theme::applied(window)?colors.muted:GetSysColor(COLOR_GRAYTEXT)));SetBkMode(dc,TRANSPARENT);
        if(font)SelectObject(dc,font);
        for(const auto& line:snapshot().visible){const auto label=std::to_wstring(line.number);RECT bounds{px(3),line.text_position.y,width-px(right_padding),line.text_position.y+line_height};
            DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&bounds,DT_RIGHT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP);}
        RestoreDC(dc,saved);
    }
    static LRESULT CALLBACK subclass(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(window,subclass,subclass_id);RemovePropW(window,property);self.window=nullptr;return DefSubclassProc(window,message,wp,lp);}
        const auto result=DefSubclassProc(window,message,wp,lp);
        try{
            if(message==WM_PAINT){const auto dc=GetDC(window);if(dc){self.paint(dc);ReleaseDC(window,dc);}}
            else if(message==WM_PRINTCLIENT)self.paint(reinterpret_cast<HDC>(wp));
            else if(message==WM_SETFONT||message==EM_SETZOOM||message==WM_DPICHANGED_AFTERPARENT||
                (message==EM_SETCHARFORMAT&&lp&&(wp==SCF_DEFAULT||wp==SCF_ALL)&&
                    (reinterpret_cast<const CHARFORMAT2W*>(lp)->dwMask&(CFM_SIZE|CFM_FACE|CFM_CHARSET|CFM_BOLD|CFM_ITALIC))))self.refresh(false,true);
            else if(message==WM_SETTEXT||message==EM_SETTEXTEX||message==EM_STREAMIN||message==EM_REPLACESEL||message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==EM_UNDO||message==EM_REDO)self.refresh(true,false);
            else if(message==WM_SIZE||message==WM_VSCROLL||message==WM_HSCROLL||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||message==EM_SETSCROLLPOS||message==EM_LINESCROLL||message==EM_SCROLLCARET||message==WM_KEYDOWN||message==EM_EXSETSEL)self.invalidate();
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
void CodeGutter::set_appearance(COLORREF background,COLORREF foreground,int right_padding){impl_->background_color=background;impl_->foreground_color=foreground;impl_->right_padding=std::clamp(right_padding,0,32);impl_->invalidate();}
CodeGutterSnapshot code_gutter_snapshot(HWND window){const auto gutter=reinterpret_cast<const CodeGutter*>(GetPropW(window,property));return gutter?gutter->snapshot():CodeGutterSnapshot{};}
void set_code_gutter_appearance(HWND window,COLORREF background,COLORREF foreground,int right_padding){if(const auto gutter=reinterpret_cast<CodeGutter*>(GetPropW(window,property)))gutter->set_appearance(background,foreground,right_padding);}
}
