#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
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
        for (std::wsregex_iterator match(source.begin(), source.end(), technical), end; match != end; ++match) {
            const auto start = static_cast<std::size_t>(match->position()); const auto count = static_cast<std::size_t>(match->length());
            format(edit, start, start + count, reader_theme::foreground(edit), reader_theme::code_font_face());
            apply_cpp(edit, std::wstring_view(source).substr(start,count), start, true);
        }
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
