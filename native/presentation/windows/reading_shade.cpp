#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/programming_technical_ranges.hpp"
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <algorithm>

namespace lgaed::presentation {
namespace {
std::wstring native_text(HWND edit){GETTEXTLENGTHEX length{GTL_NUMCHARS|GTL_PRECISE,1200};const auto size=SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    std::wstring text(static_cast<std::size_t>(std::max<LRESULT>(0,size))+1,L'\0');GETTEXTEX request{static_cast<DWORD>(text.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    text.resize(static_cast<std::size_t>(SendMessageW(edit,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&request),reinterpret_cast<LPARAM>(text.data()))));return text;}
struct Transaction {
    HWND window;CHARRANGE selection{};POINT scroll{};LRESULT events=0,modified=0;
    ITextDocument* document=nullptr;bool suspend=false,frozen=false,redraw=false,readonly=false;
    explicit Transaction(HWND edit,bool formatting=false):window(edit){
        SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        events=SendMessageW(window,EM_GETEVENTMASK,0,0);modified=SendMessageW(window,EM_GETMODIFY,0,0);
        SendMessageW(window,EM_SETEVENTMASK,0,0);
        IRichEditOle* rich=nullptr;SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&rich));
        if(rich){rich->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void**>(&document));rich->Release();}
        if(document){long count=0;frozen=SUCCEEDED(document->Freeze(&count));suspend=SUCCEEDED(document->Undo(tomSuspend,&count));}
        // Keep WS_VISIBLE stable during word ticks. Native TOM batches both
        // formatting and selection painting without hiding the child window.
        // The legacy path is retained only for controls without usable TOM.
        if(!frozen){redraw=(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0;if(redraw)SendMessageW(window,WM_SETREDRAW,FALSE,0);}
        // TOM rejects color writes on readonly lesson text with E_ACCESSDENIED.
        // Unlock only within this synchronous, frozen formatting transaction;
        // otherwise every word falls back to moving the visible UI selection.
        readonly=formatting&&document&&(GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY)!=0;
        if(readonly)SendMessageW(window,EM_SETREADONLY,FALSE,0);
    }
    ~Transaction(){
        CHARRANGE current{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&current));
        if(current.cpMin!=selection.cpMin||current.cpMax!=selection.cpMax)
            SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        SendMessageW(window,EM_SETMODIFY,modified,0);
        if(readonly)SendMessageW(window,EM_SETREADONLY,TRUE,0);
        if(document){long count=0;if(suspend)document->Undo(tomResume,&count);if(frozen)document->Unfreeze(&count);document->Release();}
        if(redraw){SendMessageW(window,WM_SETREDRAW,TRUE,0);InvalidateRect(window,nullptr,FALSE);}
        // Unfreeze can reflow the native document and round its scroll offset
        // to a line boundary. Restore the viewport after that deferred layout.
        POINT offset{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&offset));
        if(offset.x!=scroll.x||offset.y!=scroll.y)SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        // No whole-control invalidation on TOM: only changed ranges are dirty.
        // Suppress selection/scroll notifications until the batch is complete.
        SendMessageW(window,EM_SETEVENTMASK,0,events);
    }
};
struct OriginalInk {COLORREF color=0,background=0;bool automatic=false,automatic_background=true,underlined=false;};
bool original_ink(ITextDocument* document,long at,OriginalInk& ink){
    if(!document)return false;
    ITextRange* range=nullptr;ITextFont* font=nullptr;
    if(FAILED(document->Range(at,at+1,&range))||!range)return false;
    const auto queried=range->GetFont(&font);range->Release();
    if(FAILED(queried)||!font)return false;
    long foreground=tomUndefined,background=tomUndefined,underline=tomUndefined;
    const auto front=font->GetForeColor(&foreground),back=font->GetBackColor(&background);
    const auto under=font->GetUnderline(&underline);font->Release();
    if(FAILED(front)||FAILED(back)||foreground==tomUndefined||background==tomUndefined)return false;
    ink.automatic=foreground==tomAutoColor;ink.color=ink.automatic?0:static_cast<COLORREF>(foreground);
    ink.automatic_background=background==tomAutoColor;ink.background=ink.automatic_background?0:static_cast<COLORREF>(background);
    ink.underlined=SUCCEEDED(under)&&underline!=tomNone&&underline!=tomUndefined;return true;
}
void color(HWND edit,ITextDocument* document,long start,long end,COLORREF color,bool automatic=false){
    if(end<=start)return;
    // A UI selection expands to complete OfficeMath objects. A TOM range can
    // instead format the exact numerator/denominator/glyph supplied by the map.
    if(document){ITextRange* range=nullptr;ITextFont* font=nullptr;
        if(SUCCEEDED(document->Range(start,end,&range))&&range){const auto queried=range->GetFont(&font);range->Release();
            if(SUCCEEDED(queried)&&font){const auto result=font->SetForeColor(automatic?tomAutoColor:static_cast<long>(color));font->Release();if(SUCCEEDED(result))return;}}}
    CHARRANGE selection{start,end};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.dwEffects=automatic?CFE_AUTOCOLOR:0;format.crTextColor=color;SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));}
void background(HWND edit,ITextDocument* document,long start,long end,COLORREF fill,bool automatic=false){
    if(end<=start)return;
    if(document){ITextRange* range=nullptr;ITextFont* font=nullptr;
        if(SUCCEEDED(document->Range(start,end,&range))&&range){const auto queried=range->GetFont(&font);range->Release();
            if(SUCCEEDED(queried)&&font){const auto result=font->SetBackColor(automatic?tomAutoColor:static_cast<long>(fill));font->Release();if(SUCCEEDED(result))return;}}}
    CHARRANGE selection{start,end};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_BACKCOLOR;format.dwEffects=automatic?CFE_AUTOBACKCOLOR:0;format.crBackColor=fill;
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
}
}
ReadingShade::ReadingShade(HWND window,std::string source):window_(window),source_(wide(source)){
    if(!IsWindow(window_))return;displayed_=native_text(window_);map_.resize(source_.size()+1);
    if(const auto mathematical=native_math_text_mapping(window_,source)){
        map_=mathematical->source_to_native;
        if(map_.size()!=source_.size()+1||!std::ranges::is_sorted(map_)||map_.back()!=displayed_.size())return;
    }else{
        const auto project=[&](std::span<const technical_prose::Range> wrappers){
            std::wstring expected;std::size_t hidden=0;
            for(std::size_t at=0;at<source_.size();++at){map_[at]=expected.size();
                while(hidden<wrappers.size()&&wrappers[hidden].second<=at)++hidden;
                if(hidden<wrappers.size()&&wrappers[hidden].first<=at)continue;
                if(source_[at]!=L'\r'||at+1>=source_.size()||source_[at+1]!=L'\n')expected+=source_[at]==L'\n'?L'\r':source_[at];
            }
            map_.back()=expected.size();return expected;
        };
        // A real code field keeps its literal backticks. Prose hides only the
        // same trusted1/3-character wrappers used by the original renderer;
        // fence tags and all interior UTF16/newline units remain source-owned.
        if(project({})!=displayed_){const auto spans=technical_prose::inline_code_spans(source_);
            if(project(spans.wrappers)!=displayed_)return;}
    }
    Transaction transaction(window_);
    for(long at=0;at<static_cast<long>(displayed_.size());++at){OriginalInk ink;
        // Reading a TOM range is allowed on readonly text and leaves the UI
        // selection untouched, including while changing narration segments.
        if(!original_ink(transaction.document,at,ink)){
            CHARRANGE selection{at,at+1};SendMessageW(window_,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(window_,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
            ink.automatic=(format.dwEffects&CFE_AUTOCOLOR)!=0;ink.color=ink.automatic?0:format.crTextColor;
            ink.automatic_background=(format.dwEffects&CFE_AUTOBACKCOLOR)!=0;ink.background=ink.automatic_background?0:format.crBackColor;
            ink.underlined=(format.dwEffects&CFE_UNDERLINE)!=0;
        }
        if(!ink_.empty()&&ink_.back().color==ink.color&&ink_.back().automatic==ink.automatic&&ink_.back().background==ink.background&&
            ink_.back().automatic_background==ink.automatic_background&&ink_.back().underlined==ink.underlined)ink_.back().end=at+1;
        else ink_.push_back({at,at+1,ink.color,ink.background,ink.automatic,ink.automatic_background,ink.underlined});}
    valid_=true;
}
ReadingShade::~ReadingShade(){restore();}
bool ReadingShade::matches(HWND window,std::string_view source)const{return valid_&&window==window_&&IsWindow(window_)&&wide(source)==source_&&native_text(window_)==displayed_;}
void ReadingShade::read_through(std::size_t source){
    if(!valid_||!IsWindow(window_)||native_text(window_)!=displayed_)return;const auto boundary=static_cast<long>(map_[std::min(source,map_.size()-1)]);if(boundary==boundary_)return;
    Transaction transaction(window_,true);
    const auto dim=[&](long start,long end){
        for(const auto& ink:ink_)if(ink.end>start&&ink.start<end){
            const auto begin=std::max(ink.start,start),last=std::min(ink.end,end);
            const auto surface=ink.automatic_background?reader_theme::background(window_):ink.background;
            if(ink.underlined){
                color(window_,transaction.document,begin,last,ink.color,ink.automatic);
                background(window_,transaction.document,begin,last,reader_theme::unread_reference_background(window_,surface,
                    ink.automatic?reader_theme::foreground(window_):ink.color));
            }else color(window_,transaction.document,begin,last,reader_theme::unread_color(surface));
        }
    };
    if(boundary_<0){
        // On first use the prefix already has its original syntax colors.
        dim(boundary,static_cast<long>(displayed_.size()));
    }else if(boundary>boundary_){
        // Restore only the newly read characters, never the unchanged prefix
        // or unread lines below the current word.
        for(const auto& ink:ink_)if(ink.end>boundary_&&ink.start<boundary){
            color(window_,transaction.document,std::max(ink.start,boundary_),std::min(ink.end,boundary),ink.color,ink.automatic);
            if(ink.underlined)background(window_,transaction.document,std::max(ink.start,boundary_),std::min(ink.end,boundary),ink.background,ink.automatic_background);
        }
    }else{
        // Seeking backward dims only the interval that became unread again.
        dim(boundary,boundary_);
    }
    boundary_=boundary;
}
void ReadingShade::restore(){
    if(!valid_||boundary_<0||!IsWindow(window_)||native_text(window_)!=displayed_)return;
    Transaction transaction(window_,true);
    for(const auto& ink:ink_)if(ink.end>boundary_){
        color(window_,transaction.document,std::max(ink.start,boundary_),ink.end,ink.color,ink.automatic);
        if(ink.underlined)background(window_,transaction.document,std::max(ink.start,boundary_),ink.end,ink.background,ink.automatic_background);
    }
    boundary_=-1;
}
}
