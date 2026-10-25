#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr UINT_PTR hint_subclass_id=0x4c474854;
struct EditHint {
    std::wstring text;
    COLORREF color=RGB(140,145,155);
    void paint(HWND edit,HDC dc) const {
        if(text.empty()||GetWindowTextLengthW(edit)!=0||(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY)||!dc)return;
        RECT bounds{};SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&bounds));
        if(bounds.right<=bounds.left||bounds.bottom<=bounds.top)return;
        CHARFORMAT2W style{};style.cbSize=sizeof(style);
        SendMessageW(edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&style));
        int numerator=0,denominator=0;SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        if(!numerator||!denominator){numerator=1;denominator=1;}
        const auto height=MulDiv(MulDiv(style.yHeight,static_cast<int>(GetDpiForWindow(edit)),1440),numerator,denominator);
        const auto font=CreateFontW(-std::max(1,height),0,0,0,(style.dwEffects&CFE_BOLD)?FW_BOLD:FW_NORMAL,
            (style.dwEffects&CFE_ITALIC)!=0,FALSE,FALSE,style.bCharSet,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,style.bPitchAndFamily,style.szFaceName[0]?style.szFaceName:L"Roboto");
        if(!font)return;
        const auto saved=SaveDC(dc);if(!saved){DeleteObject(font);return;}
        SelectObject(dc,font);
        // EM_GETRECT keeps the unzoomed formatting origin. Ask TextServices
        // for the displayed insertion point so hint, text and caret share
        // the same position after text zoom, DPI changes and scrolling.
        POINTL origin{MulDiv(bounds.left,numerator,denominator),MulDiv(bounds.top,numerator,denominator)};
        SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&origin),0);
        bounds.left=origin.x;bounds.top=origin.y;
        Microsoft::WRL::ComPtr<IRichEditOle> rich;
        SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;
        Microsoft::WRL::ComPtr<ITextRange> range;
        TEXTMETRICW metrics{};LONG baseline_x=0,baseline_y=0;
        if(rich&&SUCCEEDED(rich.As(&document))&&document->Range(0,0,&range)==S_OK&&range&&
            range->GetPoint(tomStart|tomClientCoord|tomAllowOffClient|TA_BASELINE|TA_LEFT,&baseline_x,&baseline_y)==S_OK&&
            GetTextMetricsW(dc,&metrics)){
            bounds.left=baseline_x;bounds.top=baseline_y-metrics.tmAscent;
        }
        IntersectClipRect(dc,std::max(0L,bounds.left),std::max(0L,bounds.top),bounds.right,bounds.bottom);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,reader_theme::palette(edit).high_contrast?reader_theme::foreground(edit):color);
        const auto wrapping=(GetWindowLongPtrW(edit,GWL_STYLE)&ES_AUTOHSCROLL)?DT_SINGLELINE:DT_WORDBREAK;
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,DT_LEFT|DT_TOP|wrapping|DT_NOPREFIX);
        RestoreDC(dc,saved);DeleteObject(font);
    }
    static LRESULT CALLBACK proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<EditHint*>(raw);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(edit,proc,hint_subclass_id);delete self;return DefSubclassProc(edit,message,wp,lp);}
        const auto result=DefSubclassProc(edit,message,wp,lp);
        if(message==WM_PAINT&&GetWindowTextLengthW(edit)==0){
            // Native WM_PAINT has finished (including its caret handling).
            // Balance this extra overlay draw without replacing that caret.
            const bool restore_caret=GetFocus()==edit&&HideCaret(edit)!=FALSE;
            const auto dc=GetDC(edit);self->paint(edit,dc);if(dc)ReleaseDC(edit,dc);
            if(restore_caret)ShowCaret(edit);
        }
        else if(message==WM_PRINTCLIENT)self->paint(edit,reinterpret_cast<HDC>(wp));
        else if(message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_SETTEXT||
            message==EM_STREAMIN||message==EM_SETTEXTEX||message==EM_REPLACESEL||message==EM_UNDO||message==WM_UNDO||
            message==EM_REDO||message==EM_SETREADONLY||message==EM_SETCHARFORMAT||message==EM_SETZOOM||
            message==WM_SETFONT||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_SIZE)
            InvalidateRect(edit,nullptr,FALSE);
        return result;
    }
};
struct FormatTransaction {
    HWND edit;
    CHARRANGE selection{};
    POINT scroll{};
    LRESULT events = 0, modified = 0;
    ITextDocument* document = nullptr;
    bool suspended = false, visible = false;
    explicit FormatTransaction(HWND target) : edit(target) {
        SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        SendMessageW(edit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        events = SendMessageW(edit, EM_GETEVENTMASK, 0, 0);
        modified = SendMessageW(edit, EM_GETMODIFY, 0, 0);
        SendMessageW(edit, EM_SETEVENTMASK, 0, 0);
        visible = (GetWindowLongPtrW(edit,GWL_STYLE) & WS_VISIBLE) != 0;
        if (visible) SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        IRichEditOle* rich = nullptr;
        SendMessageW(edit, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&rich));
        if (rich) {
            rich->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void**>(&document)); rich->Release();
            if (document) { long count = 0; suspended = SUCCEEDED(document->Undo(tomSuspend, &count)); }
        }
    }
    ~FormatTransaction() {
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        SendMessageW(edit, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(edit, EM_SETMODIFY, static_cast<WPARAM>(modified), 0);
        if (document) { if (suspended) { long count = 0; document->Undo(tomResume, &count); } document->Release(); }
        SendMessageW(edit, EM_SETEVENTMASK, 0, events);
        if (visible) SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(edit, nullptr, FALSE);
    }
};
void select(HWND edit, std::size_t start, std::size_t end) {
    if (end > static_cast<std::size_t>(std::numeric_limits<LONG>::max())) throw std::length_error("Rich text is too long");
    CHARRANGE range{static_cast<LONG>(start), static_cast<LONG>(end)};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
}
double luminance(COLORREF color) {
    const auto channel = [](unsigned value) { const auto scaled = value / 255.0; return scaled <= .04045 ? scaled / 12.92 : std::pow((scaled + .055) / 1.055, 2.4); };
    return .2126 * channel(GetRValue(color)) + .7152 * channel(GetGValue(color)) + .0722 * channel(GetBValue(color));
}
COLORREF accessible(HWND edit, COLORREF original) {
    if (reader_theme::applied(edit) && reader_theme::palette().high_contrast)
        return reader_theme::foreground(edit);
    const auto background = luminance(reader_theme::background(edit));
    // The Android console palette is retained on a dark system surface.
    if (background < .25) return original;
    auto color = original;
    for (int i = 0; i < 16; ++i) {
        const auto light = luminance(color);
        if ((background + .05) / (light + .05) >= 4.5) return color;
        color = RGB(GetRValue(color) * 4 / 5, GetGValue(color) * 4 / 5, GetBValue(color) * 4 / 5);
    }
    return reader_theme::foreground(edit);
}
void format(HWND edit, std::size_t start, std::size_t end, COLORREF color, const wchar_t* font = nullptr) {
    if (start >= end) return;
    select(edit, start, end);
    CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_COLOR; style.crTextColor = color;
    if (font) { style.dwMask |= CFM_FACE; wcscpy_s(style.szFaceName, font); }
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&style));
}
bool identifier_start(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_'; }
bool identifier_part(wchar_t c) { return identifier_start(c) || (c >= L'0' && c <= L'9'); }
struct Ink { std::size_t start, end; COLORREF color; };
std::vector<Ink> cpp_ink(HWND edit, std::wstring_view text) {
    const auto identifier_color = accessible(edit, RGB(171,141,227));
    const auto keyword_color = accessible(edit, RGB(231,155,186));
    const auto string_color = accessible(edit, RGB(122,195,141));
    const auto literal_color = accessible(edit, RGB(224,151,109));
    const auto preprocessor_color = accessible(edit, RGB(92,156,231));
    const auto comment_color = accessible(edit, RGB(172,172,172));
    static const std::set<std::wstring_view> keywords{L"alignas",L"alignof",L"asm",L"break",L"case",L"catch",L"class",L"concept",L"const",L"consteval",L"constexpr",L"constinit",L"continue",L"co_await",L"co_return",L"co_yield",L"default",L"delete",L"do",L"else",L"enum",L"explicit",L"export",L"extern",L"for",L"friend",L"goto",L"if",L"inline",L"mutable",L"namespace",L"new",L"noexcept",L"operator",L"private",L"protected",L"public",L"register",L"requires",L"return",L"sizeof",L"static",L"static_assert",L"struct",L"switch",L"template",L"throw",L"try",L"typedef",L"typename",L"union",L"using",L"virtual",L"volatile",L"while",L"import",L"module"};
    std::vector<Ink> result;
    auto index = std::size_t{0};
    bool include_header = false;
    while (index < text.size()) {
        const auto start = index;
        if (text.substr(index).starts_with(L"//")) {
            const auto end = text.find_first_of(L"\r\n", index + 2); index = end == text.npos ? text.size() : end;
            result.push_back({start,index,comment_color});
        } else if (text.substr(index).starts_with(L"/*")) {
            const auto end = text.find(L"*/", index + 2); index = end == text.npos ? text.size() : end + 2;
            result.push_back({start,index,comment_color});
        } else if (include_header && text[index] == L'<') {
            const auto end = text.find_first_of(L">\r\n", index + 1); index = end == text.npos ? text.size() : end + (text[end] == L'>');
            result.push_back({start,index,string_color}); include_header = false;
        } else if (text[index] == L'"' || text[index] == L'\'') {
            const auto quote = text[index++];
            while (index < text.size()) { const auto c = text[index++]; if (c == L'\\' && index < text.size()) ++index; else if (c == quote) break; }
            result.push_back({start,index,string_color}); include_header = false;
        } else if (text[index] == L'#') {
            ++index; while (index < text.size() && (text[index] == L' ' || text[index] == L'\t')) ++index;
            const auto directive = index; while (index < text.size() && identifier_part(text[index])) ++index;
            include_header = text.substr(directive, index - directive) == L"include";
            result.push_back({start,index,preprocessor_color});
        } else if (identifier_start(text[index])) {
            ++index; while (index < text.size() && identifier_part(text[index])) ++index;
            const auto word = text.substr(start,index-start);
            result.push_back({start,index,keywords.contains(word) ? keyword_color :
                (word == L"true" || word == L"false" || word == L"nullptr" ? literal_color : identifier_color)});
            include_header = false;
        } else if (text[index] >= L'0' && text[index] <= L'9') {
            ++index;
            while (index < text.size()) {
                const auto c = text[index];
                if (identifier_part(c) || c == L'.' || c == L'\'') ++index;
                else if ((c == L'+' || c == L'-') && std::wstring_view(L"eEpP").find(text[index-1]) != text.npos) ++index;
                else break;
            }
            result.push_back({start,index,literal_color}); include_header = false;
        } else {
            ++index;
            if (std::wstring_view(L"+-*/%<>!&|^~:").find(text[start]) != text.npos) result.push_back({start,index,keyword_color});
            if (text[start] == L'\n' || text[start] == L'\r') include_header = false;
        }
    }
    return result;
}
void apply_cpp(HWND edit, std::wstring_view source, std::size_t offset = 0, bool monospaced = false) {
    for (const auto& ink : cpp_ink(edit, source)) format(edit, offset + ink.start, offset + ink.end, ink.color, monospaced ? reader_theme::code_font_face() : nullptr);
}
std::size_t inline_cpp_statement_end(std::wstring_view text,std::size_t initial) {
    auto start=initial;
    while(start<text.size()&&(text[start]==L' '||text[start]==L'\t'))++start;
    const auto tail=text.substr(start);
    if(!tail.starts_with(L"<<")&&!tail.starts_with(L">>")&&!tail.starts_with(L"+=")&&!tail.starts_with(L"-=")&&
        !tail.starts_with(L"*=")&&!tail.starts_with(L"/=")&&!tail.starts_with(L"=")&&!tail.starts_with(L"++")&&!tail.starts_with(L"--"))return initial;
    // Android's ProgrammingTechnicalRanges extends an already recognized
    // C++ name to its statement terminator. Quotes and escapes belong to that
    // fragment; ordinary quoted prose is never a technical seed by itself.
    static const std::wregex prose_joiner(LR"(\s+(?:y|o|pero|porque|and|or|but|because|which|et|ou|mais|car|puis|qui|que)\b)",std::regex_constants::icase);
    const auto limit=std::min(text.size(),start+512);
    std::vector<wchar_t> closing;
    for(auto index=start;index<limit;++index){
        const auto current=text[index];
        if(current==L'"'||current==L'\''){
            const auto quote=current;
            for(++index;index<limit&&text[index]!=quote;++index)if(text[index]==L'\\')++index;
            if(index>=limit)return initial;
            continue;
        }
        if(current==L';'&&closing.empty())return index+1;
        const bool member_dot=current==L'.'&&index+1<limit&&
            ((text[index+1]>=L'0'&&text[index+1]<=L'9')||
                (index>0&&(identifier_part(text[index-1])||text[index-1]==L')'||text[index-1]==L']')&&identifier_part(text[index+1])));
        if(closing.empty()&&(current==L'\r'||current==L'\n'||current==L'!'||current==L'?'||(current==L'.'&&!member_dot)||
            ((current==L' '||current==L'\t')&&std::regex_search(text.begin()+index,text.begin()+limit,prose_joiner,std::regex_constants::match_continuous))))return initial;
        if(current==L'(')closing.push_back(L')');
        else if(current==L'[')closing.push_back(L']');
        else if(current==L'{')closing.push_back(L'}');
        else if(current==L')'||current==L']'||current==L'}'){
            if(closing.empty()||closing.back()!=current)return initial;
            closing.pop_back();
        }
    }
    return initial;
}
struct Stream { const BYTE* bytes; std::size_t size, offset = 0; };
DWORD CALLBACK stream_text(DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG* copied) {
    auto& source = *reinterpret_cast<Stream*>(cookie);
    const auto count = std::min(source.size - source.offset, static_cast<std::size_t>(requested));
    std::memcpy(buffer, source.bytes + source.offset, count); source.offset += count; *copied = static_cast<LONG>(count); return 0;
}
void default_format(HWND edit, bool code, RichTextRole role) {
    select(edit, 0, static_cast<std::size_t>(GetWindowTextLengthW(edit)));
    CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    style.yHeight = rich_text_height(role); style.crTextColor = reader_theme::foreground(edit);
    if(role!=RichTextRole::body&&!reader_theme::palette().high_contrast)style.crTextColor=RGB(255,255,255);
    style.dwEffects = role == RichTextRole::body ? 0 : CFE_BOLD;
    wcscpy_s(style.szFaceName, code ? reader_theme::code_font_face() : L"Roboto");
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&style));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&style));
    PARAFORMAT2 paragraph{}; paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_TABSTOPS | PFM_LINESPACING;
    paragraph.dySpaceAfter = 0; paragraph.cTabCount = MAX_TAB_STOPS;
    paragraph.bLineSpacingRule = 5;
    paragraph.dyLineSpacing = !code && role == RichTextRole::body ? 25 : 20;
    // Preserve the default tab positions. Editable code replaces these with
    // measured tab stops, preserving real tabs and indentation units.
    for (int tab = 0; tab < MAX_TAB_STOPS; ++tab) paragraph.rgxTabs[tab] = (tab + 1) * 792;
    SendMessageW(edit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraph));
}
}

std::wstring wide(std::string_view source) {
    if (source.empty()) return {};
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Text too large");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), nullptr, 0);
    if (!length) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), result.data(), length); return result;
}
std::string utf8(std::wstring_view source) {
    if (source.empty()) return {};
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Text too large");
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Invalid UTF-16 text");
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), result.data(), length, nullptr, nullptr); return result;
}
void set_edit_hint(HWND edit,std::string_view text,COLORREF color) {
    if(!IsWindow(edit))throw std::invalid_argument("An edit hint requires a live RichEdit");
    DWORD_PTR existing=0;
    if(GetWindowSubclass(edit,EditHint::proc,hint_subclass_id,&existing)){
        auto* hint=reinterpret_cast<EditHint*>(existing);hint->text=wide(text);hint->color=color;
    }
    else {
        auto hint=std::make_unique<EditHint>();hint->text=wide(text);hint->color=color;
        if(!SetWindowSubclass(edit,EditHint::proc,hint_subclass_id,reinterpret_cast<DWORD_PTR>(hint.get())))
            throw std::runtime_error("Cannot attach an empty edit hint");
        hint.release();
    }
    InvalidateRect(edit,nullptr,FALSE);
}
void compact_code_spaces(HWND edit) {
    GETTEXTLENGTHEX length{GTL_PRECISE|GTL_NUMCHARS,1200};
    const auto count=SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    if(count<=0)return;
    if(count>16*1024*1024)throw std::length_error("Code display text is too long");
    std::wstring source(static_cast<std::size_t>(count)+1,L'\0');
    GETTEXTEX get{static_cast<DWORD>(source.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    const auto copied=SendMessageW(edit,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&get),reinterpret_cast<LPARAM>(source.data()));
    if(copied<0)throw std::runtime_error("Cannot read code display text");source.resize(static_cast<std::size_t>(copied));
    FormatTransaction transaction(edit);
    // Unlike EM_SETCHARFORMAT, TOM refuses format changes on a readonly
    // display. Unlock only within this synchronous UI-thread transaction,
    // with redraw/events/Undo suspended, then restore before any repaint.
    struct ReadonlyFormat {
        HWND edit;bool readonly;
        explicit ReadonlyFormat(HWND target):edit(target),readonly((GetWindowLongPtrW(target,GWL_STYLE)&ES_READONLY)!=0){
            if(readonly)SendMessageW(edit,EM_SETREADONLY,FALSE,0);
        }
        ~ReadonlyFormat(){if(readonly)SendMessageW(edit,EM_SETREADONLY,TRUE,0);}
    } readonly(edit);
    if(!transaction.document)throw std::runtime_error("Cannot format compact code spaces");
    const auto scale=[&](LONG first,LONG last,LONG percent){
        Microsoft::WRL::ComPtr<ITextRange> range;
        Microsoft::WRL::ComPtr<ITextFont> font;
        Microsoft::WRL::ComPtr<ITextFont2> extended;
        if(FAILED(transaction.document->Range(first,last,&range))||FAILED(range->GetFont(&font))||
            FAILED(font.As(&extended))||FAILED(extended->SetScaling(percent)))
            throw std::runtime_error("Cannot scale native code display spaces");
    };
    // CHARFORMAT2::sSpacing is compatibility metadata: RichEdit does not draw
    // it. TOM2 horizontal scaling changes actual advances without changing
    // characters, height, syntax spans or the text undo history.
    scale(0,static_cast<LONG>(source.size()),100);
    for(std::size_t first=0;first<source.size();){
        if(source[first]!=L' '){++first;continue;}
        auto last=first+1;while(last<source.size()&&source[last]==L' ')++last;
        scale(static_cast<LONG>(first),static_cast<LONG>(last),80);first=last;
    }
}
bool rich_text_hit_test(HWND edit,POINT point) {
    const auto px=[&](int dip){return MulDiv(dip,static_cast<int>(GetDpiForWindow(edit)),96);};
    POINTL location{point.x,point.y};
    const auto character=static_cast<LONG>(SendMessageW(edit,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&location)));
    if(character<0)return false;
    wchar_t value[2]{};TEXTRANGEW range{{character,character+1},value};
    SendMessageW(edit,EM_GETTEXTRANGE,0,reinterpret_cast<LPARAM>(&range));
    if(!value[0]||value[0]==L'\r'||value[0]==L'\n'||value[0]==L' '||value[0]==L'\t')return false;
    POINTL start{},end{};
    SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&start),character);
    SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),character+1);
    // Read this character's actual font without selecting it. Selecting a
    // range would change the caret/history while deciding gesture ownership.
    int height=px(24),advance=px(14);
    Microsoft::WRL::ComPtr<IRichEditOle> ole;
    SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument> document;
    Microsoft::WRL::ComPtr<ITextRange> text_range;
    Microsoft::WRL::ComPtr<ITextFont> text_font;
    if(ole&&SUCCEEDED(ole.As(&document))&&SUCCEEDED(document->Range(character,character+1,&text_range))
        &&SUCCEEDED(text_range->GetFont(&text_font))){
        float points=18;BSTR face=nullptr;
        text_font->GetSize(&points);text_font->GetName(&face);
        int numerator=0,denominator=0;
        SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
        const int zoom=denominator>0?MulDiv(numerator,100,denominator):100;
        const auto font=CreateFontW(-MulDiv(static_cast<int>(points*20+.5f)*zoom,
            static_cast<int>(GetDpiForWindow(edit)),1440*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,face?face:L"Segoe UI");
        if(font){
            const auto dc=GetDC(edit);const auto previous=SelectObject(dc,font);TEXTMETRICW metrics{};SIZE glyph{};
            if(GetTextMetricsW(dc,&metrics))height=metrics.tmHeight+metrics.tmExternalLeading;
            if(GetTextExtentPoint32W(dc,value,1,&glyph))advance=glyph.cx;
            SelectObject(dc,previous);ReleaseDC(edit,dc);DeleteObject(font);
        }
        SysFreeString(face);
    }
    const int right=end.y==start.y&&end.x>start.x?end.x:start.x+advance;
    return point.x>=start.x&&point.x<right&&point.y>=start.y&&point.y<start.y+height;
}
std::string edit_text(HWND edit) {
    GETTEXTLENGTHEX length{GTL_PRECISE | GTL_NUMCHARS, 1200};
    const auto count = SendMessageW(edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length), 0);
    if (count < 0 || count > 16 * 1024 * 1024) throw std::length_error("Editor text length is invalid");
    std::wstring raw(static_cast<std::size_t>(count) + 1, L'\0');
    GETTEXTEX get{static_cast<DWORD>(raw.size() * sizeof(wchar_t)), GT_RAWTEXT, 1200, nullptr, nullptr};
    const auto copied = SendMessageW(edit, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&get), reinterpret_cast<LPARAM>(raw.data()));
    if (copied < 0) throw std::runtime_error("Cannot read editor text");
    raw.resize(static_cast<std::size_t>(copied));
    std::wstring normalized; normalized.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\r') { normalized += L'\n'; if (i + 1 < raw.size() && raw[i + 1] == L'\n') ++i; }
        else normalized += raw[i];
    }
    return utf8(normalized);
}
void set_edit_text(HWND edit, std::string_view text) {
    auto source = wide(text); if (source.find(L'\0') != source.npos) throw std::runtime_error("Editor text contains a null character");
    std::wstring native;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == L'\r') { native += L'\r'; if (i + 1 < source.size() && source[i + 1] == L'\n') ++i; }
        else native += source[i] == L'\n' ? L'\r' : source[i];
    }
    Stream source_stream{reinterpret_cast<const BYTE*>(native.data()), native.size() * sizeof(wchar_t)};
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&source_stream), 0, stream_text};
    // SF_TEXT explicitly prevents code beginning with {\\rtf from becoming RTF.
    SendMessageW(edit, EM_STREAMIN, SF_TEXT | SF_UNICODE, reinterpret_cast<LPARAM>(&stream));
    if (stream.dwError) throw std::runtime_error("Cannot set editor text");
}
void style_cpp(HWND edit) {
    const auto source = wide(edit_text(edit));
    FormatTransaction transaction(edit);
    // Editing controls own their font, measured tab stops and paragraph layout.
    // Recoloring must not replace those settings after every keystroke.
    CHARFORMAT2W base{}; base.cbSize = sizeof(base); base.dwMask = CFM_COLOR;
    base.crTextColor = reader_theme::foreground(edit);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&base));
    apply_cpp(edit, source);
}
void set_rich_text(HWND edit, std::string_view text, std::span<const curriculum::Emphasis> emphasis, bool code, RichTextRole role) {
    std::string displayed;
    std::vector<std::size_t> mapping(text.size() + 1);
    std::vector<std::pair<std::size_t,std::size_t>> fragments;
    for (std::size_t i = 0; i < text.size();) {
        mapping[i] = displayed.size();
        if (!code && text[i] == '`') {
            const auto end = text.find('`', i + 1);
            if (end != text.npos && end > i + 1 && text.substr(i + 1, end - i - 1).find_first_of("\r\n") == text.npos) {
                const auto start = displayed.size();
                for (auto j = i + 1; j < end; ++j) { mapping[j] = displayed.size(); displayed += text[j]; }
                mapping[end] = displayed.size(); fragments.emplace_back(start, displayed.size()); i = end + 1; continue;
            }
        }
        displayed += text[i++];
    }
    mapping[text.size()] = displayed.size();
    set_edit_text(edit, displayed);
    FormatTransaction transaction(edit);
    default_format(edit, code, role);
    const auto source = wide(displayed);
    if (code) apply_cpp(edit, source);
    else {
        static const std::wregex technical(LR"((#\s*include(?:[ \t]*(?:<[^>\r\n]+>|"[^"\r\n]*"))?)|(\bstd[ \t]*::[A-Za-z_0-9:]*)|(\bmain(?:\(\))?)|(\breturn[ \t]+[0-9]+[ \t]*;?)|([Cc]\+\+[0-9]*)|(<<|>>))");
        std::vector<std::pair<std::size_t,std::size_t>> statements;
        for (std::wsregex_iterator match(source.begin(), source.end(), technical), end; match != end; ++match) {
            const auto start = static_cast<std::size_t>(match->position()); const auto count = static_cast<std::size_t>(match->length());
            format(edit, start, start + count, reader_theme::foreground(edit), reader_theme::code_font_face());
            apply_cpp(edit, std::wstring_view(source).substr(start,count), start, true);
            if((*match)[2].matched&&(statements.empty()||start>=statements.back().second)){
                const auto statement_end=inline_cpp_statement_end(source,start+count);
                if(statement_end>start+count)statements.emplace_back(start,statement_end);
            }
        }
        // Apply complete fragments after individual technical mentions so a
        // word such as std::cout or << inside a literal keeps the string ink.
        // Match Android's color-only treatment: preserve existing fonts,
        // source positions and paragraph measurements.
        for(const auto& [start,end]:statements)apply_cpp(edit,std::wstring_view(source).substr(start,end-start),start);
        for (const auto& [start,end] : fragments) {
            const auto begin_wide = wide(std::string_view(displayed).substr(0,start)).size();
            const auto fragment = wide(std::string_view(displayed).substr(start,end-start));
            format(edit, begin_wide, begin_wide + fragment.size(), reader_theme::foreground(edit), reader_theme::code_font_face());
            apply_cpp(edit, fragment, begin_wide, true);
        }
    }
    for (const auto& range : curriculum::emphasis_ranges(text, emphasis)) {
        const auto start = wide(std::string_view(displayed).substr(0, mapping[range.start])).size();
        const auto end = wide(std::string_view(displayed).substr(0, mapping[range.end])).size();
        select(edit,start,end);
        CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_BOLD | CFM_ITALIC;
        style.dwEffects = (role != RichTextRole::body || range.style == "bold" || range.style == "bold_italic" ? CFE_BOLD : 0) |
                          (range.style == "italic" || range.style == "bold_italic" ? CFE_ITALIC : 0);
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&style));
    }
}
} // namespace lgaed::presentation
