#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/native_math_text.hpp"
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
    HWND window;CHARRANGE selection{};POINT scroll{};LRESULT events=0,modified=0;ITextDocument* document=nullptr;bool suspend=false,redraw=false;
    explicit Transaction(HWND edit):window(edit){SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));events=SendMessageW(window,EM_GETEVENTMASK,0,0);modified=SendMessageW(window,EM_GETMODIFY,0,0);
        SendMessageW(window,EM_SETEVENTMASK,0,0);redraw=(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0;if(redraw)SendMessageW(window,WM_SETREDRAW,FALSE,0);IRichEditOle* rich=nullptr;SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&rich));
        if(rich){rich->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void**>(&document));rich->Release();if(document){long count=0;suspend=SUCCEEDED(document->Undo(tomSuspend,&count));}}}
    ~Transaction(){SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));SendMessageW(window,EM_SETMODIFY,modified,0);
        if(document){if(suspend){long count=0;document->Undo(tomResume,&count);}document->Release();}SendMessageW(window,EM_SETEVENTMASK,0,events);if(redraw)SendMessageW(window,WM_SETREDRAW,TRUE,0);InvalidateRect(window,nullptr,FALSE);}
};
void color(HWND edit,ITextDocument* document,long start,long end,COLORREF color,bool automatic=false){
    if(end<=start)return;
    // A UI selection expands to complete OfficeMath objects. A TOM range can
    // instead format the exact numerator/denominator/glyph supplied by the map.
    if(document){ITextRange* range=nullptr;ITextFont* font=nullptr;
        if(SUCCEEDED(document->Range(start,end,&range))&&range){const auto queried=range->GetFont(&font);range->Release();
            if(SUCCEEDED(queried)&&font){const auto result=font->SetForeColor(automatic?tomAutoColor:static_cast<long>(color));font->Release();if(SUCCEEDED(result))return;}}}
    CHARRANGE selection{start,end};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.dwEffects=automatic?CFE_AUTOCOLOR:0;format.crTextColor=color;SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));}
}
ReadingShade::ReadingShade(HWND window,std::string source):window_(window),source_(wide(source)){
    if(!IsWindow(window_))return;displayed_=native_text(window_);map_.resize(source_.size()+1);
    if(const auto mathematical=native_math_text_mapping(window_,source)){
        map_=mathematical->source_to_native;
        if(map_.size()!=source_.size()+1||!std::ranges::is_sorted(map_)||map_.back()!=displayed_.size())return;
    }else{
        std::wstring expected;std::size_t i=0;
        while(i<source_.size()){map_[i]=expected.size();if(source_[i]==L'`'){const auto end=source_.find(L'`',i+1);if(end!=source_.npos){for(auto j=i+1;j<end;++j){map_[j]=expected.size();if(source_[j]!=L'\r'||j+1>=end||source_[j+1]!=L'\n')expected+=source_[j]==L'\n'?L'\r':source_[j];}map_[end]=expected.size();i=end+1;continue;}}
            if(source_[i]!=L'\r'||i+1>=source_.size()||source_[i+1]!=L'\n')expected+=source_[i]==L'\n'?L'\r':source_[i];++i;
        }
        map_.back()=expected.size();if(expected!=displayed_)return;
    }
    Transaction transaction(window_);
    for(long at=0;at<static_cast<long>(displayed_.size());++at){CHARRANGE selection{at,at+1};SendMessageW(window_,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(window_,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
        const bool automatic=(format.dwEffects&CFE_AUTOCOLOR)!=0;if(!ink_.empty()&&ink_.back().color==format.crTextColor&&ink_.back().automatic==automatic)ink_.back().end=at+1;else ink_.push_back({at,at+1,format.crTextColor,automatic});}
    valid_=true;
}
ReadingShade::~ReadingShade(){restore();}
bool ReadingShade::matches(HWND window,std::string_view source)const{return valid_&&window==window_&&IsWindow(window_)&&wide(source)==source_&&native_text(window_)==displayed_;}
void ReadingShade::read_through(std::size_t source){
    if(!valid_||!IsWindow(window_)||native_text(window_)!=displayed_)return;const auto boundary=static_cast<long>(map_[std::min(source,map_.size()-1)]);if(boundary==boundary_)return;
    Transaction transaction(window_);boundary_=boundary;
    for(const auto& ink:ink_)color(window_,transaction.document,ink.start,std::min(ink.end,boundary),ink.color,ink.automatic);
    const auto gray=reader_theme::palette().high_contrast?GetSysColor(COLOR_GRAYTEXT):RGB(135,139,148);
    color(window_,transaction.document,boundary,static_cast<long>(displayed_.size()),gray);
}
void ReadingShade::restore(){if(!valid_||!IsWindow(window_)||native_text(window_)!=displayed_)return;Transaction transaction(window_);for(const auto& ink:ink_)color(window_,transaction.document,ink.start,ink.end,ink.color,ink.automatic);boundary_=-1;}
}
