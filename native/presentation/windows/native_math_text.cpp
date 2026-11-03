#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr wchar_t mapping_property[] = L"LGAED.NativeMathText";
constexpr UINT_PTR mapping_subclass = 0x4D415448;
struct Projection {
    std::wstring source,linear;
    std::vector<std::size_t> mapping;
    std::size_t at = 0;
    explicit Projection(std::string_view value) : source(wide(value)),mapping(source.size() + 1,0) {}
    void emit(std::wstring_view value) { linear += value; }
    void mark(std::size_t from,std::size_t to,std::size_t begin,std::size_t end) {
        for (auto index = from; index < to; ++index) mapping[index] = begin;
        mapping[to] = end;
    }
    void group() {
        if (at < source.size() && source[at] == L'{') {
            const auto brace = at++; mapping[brace] = linear.size(); parse(L'}');
            if (at < source.size()) mapping[at++] = linear.size();
        } else if (at < source.size()) { mapping[at] = linear.size(); emit(source.substr(at,1)); mapping[++at] = linear.size(); }
    }
    void parse(wchar_t stop = 0) {
        static const std::map<std::wstring,std::wstring> symbols{
            {L"times",L"×"},{L"cdot",L"·"},{L"div",L"÷"},{L"pm",L"±"},{L"mp",L"∓"},
            {L"le",L"≤"},{L"leq",L"≤"},{L"ge",L"≥"},{L"geq",L"≥"},{L"ne",L"≠"},{L"neq",L"≠"},
            {L"pi",L"π"},{L"theta",L"θ"},{L"alpha",L"α"},{L"beta",L"β"},{L"gamma",L"γ"},{L"delta",L"δ"},
            {L"infty",L"∞"},{L"sum",L"∑"},{L"prod",L"∏"},{L"int",L"∫"},{L"to",L"→"},
            {L"sin",L"sin"},{L"cos",L"cos"},{L"tan",L"tan"},{L"log",L"log"},{L"ln",L"ln"},{L"lim",L"lim"},
            {L"angle",L"∠"},{L"circ",L"°"},{L"degree",L"°"},{L"in",L"∈"},{L"notin",L"∉"},
            {L"cup",L"∪"},{L"cap",L"∩"},{L"emptyset",L"∅"},{L"ldots",L"…"},{L"dots",L"…"}
        };
        while (at < source.size() && (!stop || source[at] != stop)) {
            const auto start = at; mapping[at] = linear.size(); const auto begin = linear.size();
            if (source[at] == L'\\') {
                ++at;
                if (at < source.size() && std::wstring_view(L"()[]$").find(source[at]) != std::wstring_view::npos) { ++at; mark(start,at,begin,begin); continue; }
                if (at < source.size() && source[at] == L'\\') { ++at; emit(L"@"); mark(start,at,begin,linear.size()); continue; }
                const auto command_start = at;
                while (at < source.size() && ((source[at] >= L'a' && source[at] <= L'z') || (source[at] >= L'A' && source[at] <= L'Z'))) ++at;
                const auto name = source.substr(command_start,at - command_start); mark(start,at,begin,begin);
                if (name == L"frac" || name == L"dfrac" || name == L"tfrac") { emit(L"("); group(); emit(L")/("); group(); emit(L")"); }
                else if (name == L"sqrt") { emit(L"√("); group(); emit(L")"); }
                else if (name == L"text" || name == L"mathrm" || name == L"operatorname" || name == L"mathbf") group();
                else if (name == L"left" || name == L"right") {}
                else if (name == L"begin") {
                    const auto open = at; if (at < source.size() && source[at] == L'{') { const auto close = source.find(L'}',at);
                        if (close != source.npos) { const auto environment = source.substr(at + 1,close - at - 1); at = close + 1;
                            emit(environment == L"cases" ? L"{█(" : L"■("); mark(open,at,begin,linear.size()); }
                    }
                } else if (name == L"end") { const auto open = at; const auto close = source.find(L'}',at); if (close != source.npos) at = close + 1; emit(L")"); mark(open,at,begin,linear.size()); }
                else if (const auto found = symbols.find(name); found != symbols.end()) { emit(found->second); mark(start,at,begin,linear.size()); }
                else if (name.empty() && at < source.size()) { emit(source.substr(at++,1)); mark(start,at,begin,linear.size()); }
                else throw std::invalid_argument("Unsupported native mathematical command: " + utf8(name));
                continue;
            }
            if (source[at] == L'{') { group(); continue; }
            if (source[at] == L'$') { ++at; mark(start,at,begin,begin); continue; }
            if ((source[at] == L'^' || source[at] == L'_') && at + 1 < source.size() && source[at + 1] == L'{') {
                emit(source.substr(at++,1)); emit(L"("); group(); emit(L")"); mapping[at] = linear.size(); continue;
            }
            emit(source.substr(at++,1)); mapping[at] = linear.size();
        }
        mapping[at] = linear.size();
    }
};
std::wstring raw_text(HWND window) {
    GETTEXTLENGTHEX length{GTL_PRECISE | GTL_NUMCHARS,1200};
    const auto count = SendMessageW(window,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    std::wstring result(static_cast<std::size_t>(count) + 1,L'\0'); GETTEXTEX request{static_cast<DWORD>(result.size() * sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    const auto copied = SendMessageW(window,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&request),reinterpret_cast<LPARAM>(result.data())); result.resize(static_cast<std::size_t>(copied)); return result;
}
std::vector<std::size_t> align(std::wstring_view linear,std::wstring_view native) {
    if (linear.size() > 4096 || native.size() > 8192) throw std::length_error("Native mathematical display is too large");
    struct Unit { std::size_t begin,end; std::wstring key; };
    const auto units = [](std::wstring_view text) {
        std::vector<Unit> result;
        for (std::size_t at = 0; at < text.size();) {
            const auto begin = at++; unsigned code = text[begin];
            if (code >= 0xD800 && code <= 0xDBFF && at < text.size() && text[at] >= 0xDC00 && text[at] <= 0xDFFF)
                code = 0x10000 + ((code - 0xD800) << 10) + (text[at++] - 0xDC00);
            auto key = std::wstring(text.substr(begin,at - begin));
            // TOM changes a mathematical ASCII x into the supplementary-plane
            // italic x. Compare those math alphabet styles by NFKC, while
            // retaining actual UTF16 positions and every displayed character.
            // Do not fold punctuation, operators, case or arbitrary prose.
            if ((code >= 0x1D400 && code <= 0x1D7FF) || code == 0x210E) {
                wchar_t folded[16]{};
                const auto count = NormalizeString(NormalizationKC,key.data(),static_cast<int>(key.size()),folded,16);
                if (count > 0) key.assign(folded,static_cast<std::size_t>(count));
            }
            result.push_back({begin,at,std::move(key)});
        }
        return result;
    };
    const auto source_units = units(linear),native_units = units(native);
    const auto columns = native_units.size() + 1; std::vector<std::uint16_t> scores((source_units.size() + 1) * columns);
    for (auto i = source_units.size(); i-- > 0;) for (auto j = native_units.size(); j-- > 0;)
        scores[i * columns + j] = source_units[i].key == native_units[j].key ? static_cast<std::uint16_t>(1 + scores[(i + 1) * columns + j + 1])
            : std::max(scores[(i + 1) * columns + j],scores[i * columns + j + 1]);
    std::vector<std::size_t> result(linear.size() + 1,native.size()); std::size_t i = 0,j = 0; result[0] = 0;
    const auto map_unit = [&](const Unit& from,std::size_t begin,std::size_t end) {
        for (auto at = from.begin; at < from.end; ++at) result[at] = begin;
        result[from.end] = end;
    };
    while (i < source_units.size() && j < native_units.size()) {
        if (source_units[i].key == native_units[j].key) { map_unit(source_units[i++],native_units[j].begin,native_units[j].end); ++j; }
        else if (scores[(i + 1) * columns + j] >= scores[i * columns + j + 1]) { map_unit(source_units[i++],native_units[j].begin,native_units[j].begin); }
        else ++j;
    }
    for (; i < source_units.size(); ++i) map_unit(source_units[i],native.size(),native.size());
    result.back() = native.size();
    for (std::size_t index = 1; index < result.size(); ++index) result[index] = std::max(result[index],result[index - 1]);
    return result;
}
LRESULT CALLBACK mapping_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR) {
    if (message == WM_NCDESTROY) { delete reinterpret_cast<NativeMathTextMapping*>(RemovePropW(window,mapping_property)); RemoveWindowSubclass(window,mapping_proc,mapping_subclass); }
    return DefSubclassProc(window,message,wparam,lparam);
}
struct InlineProjection {
    Projection value;
    std::vector<std::pair<std::size_t,std::size_t>> spans;
    std::set<std::size_t> literal_spans;
    InlineProjection(std::string_view source,std::span<const NativeMathFragment> fragments):value(source){
        std::size_t at=0;
        const auto plain=[&](std::size_t end){while(at<end){value.mapping[at]=value.linear.size();value.linear+=value.source[at++];value.mapping[at]=value.linear.size();}};
        for(const auto& fragment:fragments){
            if(fragment.start<at||fragment.end<=fragment.start||fragment.end>value.source.size())throw std::invalid_argument("The original mathematics renderer returned an invalid source range");
            const auto splits_surrogate=[&](std::size_t index){return index>0&&index<value.source.size()&&IS_HIGH_SURROGATE(value.source[index-1])&&IS_LOW_SURROGATE(value.source[index]);};
            if(splits_surrogate(fragment.start)||splits_surrogate(fragment.end))throw std::invalid_argument("A mathematical source range splits a Unicode character");
            plain(fragment.start);auto first=fragment.start,last=fragment.end;const auto original=std::wstring_view(value.source).substr(first,last-first);
            if((original.starts_with(L"\\(")&&original.ends_with(L"\\)"))||(original.starts_with(L"\\[")&&original.ends_with(L"\\]"))||(original.starts_with(L"$$")&&original.ends_with(L"$$"))){first+=2;last-=2;}
            else if(original.size()>=2&&original.front()==L'$'&&original.back()==L'$'){++first;--last;}
            Projection normalized(fragment.formula);if(fragment.literal){normalized.linear=normalized.source;std::iota(normalized.mapping.begin(),normalized.mapping.end(),0);}else normalized.parse();
            const auto offset=value.linear.size();const auto source_mapping=align(std::wstring_view(value.source).substr(first,last-first),normalized.linear);
            for(auto index=fragment.start;index<first;++index)value.mapping[index]=offset;
            for(std::size_t index=0;index<source_mapping.size();++index)value.mapping[first+index]=offset+source_mapping[index];
            value.linear+=normalized.linear;for(auto index=last;index<=fragment.end;++index)value.mapping[index]=value.linear.size();
            spans.push_back({offset,value.linear.size()});if(fragment.literal)literal_spans.insert(offset);at=fragment.end;
        }
        plain(value.source.size());
    }
    InlineProjection(std::string_view source,bool inline_only) : value(source) {
        if (!inline_only) { value.parse(); spans.push_back({0,value.linear.size()}); return; }
        std::size_t at = 0;
        const auto append_plain = [&](std::size_t end) {
            while (at < end) { value.mapping[at] = value.linear.size(); value.linear += value.source[at++]; value.mapping[at] = value.linear.size(); }
        };
        while (at < value.source.size()) {
            const auto round = value.source.find(L"\\(",at),square = value.source.find(L"\\[",at);
            const auto begin = std::min(round,square);
            if (begin == std::wstring::npos) { append_plain(value.source.size()); break; }
            append_plain(begin); const auto closing = value.source[begin + 1] == L'(' ? L"\\)" : L"\\]";
            const auto end = value.source.find(closing,begin + 2);
            if (end == std::wstring::npos) { append_plain(value.source.size()); break; }
            Projection fragment(utf8(value.source.substr(begin + 2,end - begin - 2))); fragment.parse();
            const auto offset = value.linear.size(); value.mapping[begin] = value.mapping[begin + 1] = offset;
            for (std::size_t index = 0; index < fragment.mapping.size(); ++index) value.mapping[begin + 2 + index] = offset + fragment.mapping[index];
            value.linear += fragment.linear; spans.push_back({offset,value.linear.size()});
            value.mapping[end] = value.mapping[end + 1] = value.mapping[end + 2] = value.linear.size(); at = end + 2;
        }
    }
};
struct DisplayTransaction {
    HWND window; bool readonly,visible;
    LRESULT events,modified;
    CHARRANGE selection{}; POINT scroll{};
    explicit DisplayTransaction(HWND target) : window(target),
        readonly((GetWindowLongPtrW(window,GWL_STYLE) & ES_READONLY) != 0),
        visible((GetWindowLongPtrW(window,GWL_STYLE) & WS_VISIBLE) != 0),
        events(SendMessageW(window,EM_GETEVENTMASK,0,0)),modified(SendMessageW(window,EM_GETMODIFY,0,0)) {
        SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection)); SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(window,EM_SETEVENTMASK,0,0); if (visible) SendMessageW(window,WM_SETREDRAW,FALSE,0);
        if (readonly) SendMessageW(window,EM_SETREADONLY,FALSE,0);
    }
    ~DisplayTransaction() {
        if (readonly) SendMessageW(window,EM_SETREADONLY,TRUE,0);
        SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection)); SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(window,EM_SETMODIFY,modified,0); SendMessageW(window,EM_SETEVENTMASK,0,events);
        if (visible) SendMessageW(window,WM_SETREDRAW,TRUE,0); InvalidateRect(window,nullptr,TRUE);
    }
};
bool set_math_text(HWND window,std::string_view original,bool inline_only,std::span<const curriculum::Emphasis> emphasis = {},RichTextRole role = RichTextRole::body,std::span<const NativeMathFragment> fragments = {},std::string_view locale="es") {
    InlineProjection converted=fragments.empty()?InlineProjection(original,inline_only):InlineProjection(original,fragments); auto& projection = converted.value;
    if (converted.spans.empty()) return false;
    if (projection.linear.size() > 4096) throw std::length_error("Native mathematical display is too large");
    std::vector<curriculum::Emphasis> displayed_emphasis(emphasis.begin(),emphasis.end());
    for (auto& entry : displayed_emphasis) { InlineProjection range(entry.text,inline_only); entry.text = utf8(range.value.linear); }
    // System RichEdit disables math display by default even though TOM2 can
    // build and return its structural markers. Enable the documented feature
    // on this mathematical display control, never on code editing controls.
    SendMessageW(window,EM_SETEDITSTYLEEX,0,SES_EX_NOMATH);
    if (SendMessageW(window,EM_GETEDITSTYLEEX,0,0) & SES_EX_NOMATH)
        throw std::runtime_error("This RichEdit cannot enable native mathematical display");
    DisplayTransaction transaction(window);
    set_rich_text(window,utf8(projection.linear),fragments.empty()?std::span<const curriculum::Emphasis>(displayed_emphasis):std::span<const curriculum::Emphasis>{},false,role,locale,converted.spans);
    const auto reference_ranges=rich_text_reference_ranges(window);
    CHARFORMAT2W style{}; style.cbSize = sizeof(style); style.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_MATH; style.dwEffects = CFE_MATH;
    style.yHeight = rich_text_height(role); style.crTextColor = reader_theme::foreground(window); wcscpy_s(style.szFaceName,L"Cambria Math");
    Microsoft::WRL::ComPtr<IRichEditOle> rich; SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument2> document; Microsoft::WRL::ComPtr<ITextRange2> range;
    bool built = false; if (rich) rich.As(&document);
    const auto before_math = raw_text(window); const auto linear_to_visible = align(projection.linear,before_math);
    for (auto span = converted.spans.rbegin(); span != converted.spans.rend(); ++span) {
        const auto begin = static_cast<LONG>(linear_to_visible.at(span->first)),end = static_cast<LONG>(linear_to_visible.at(span->second));
        CHARRANGE selected{begin,end}; SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected)); SendMessageW(window,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));
        if (!converted.literal_spans.contains(span->first)&&document && SUCCEEDED(document->Range2(begin,end,range.ReleaseAndGetAddressOf()))) built = range->BuildUpMath(0) == S_OK || built;
    }
    if(!fragments.empty())for(const auto& emphasis_range:curriculum::emphasis_ranges(original,emphasis)){
        const auto begin=wide(original.substr(0,emphasis_range.start)).size(),end=wide(original.substr(0,emphasis_range.end)).size();
        // TOM may replace a mathematical letter/digit with a supplementary
        // styled character when its weight changes. Resolve every authored
        // emphasis against the current story, retaining original overlap/order.
        const auto current=align(projection.linear,raw_text(window));
        CHARRANGE selected{static_cast<LONG>(current.at(projection.mapping.at(begin))),static_cast<LONG>(current.at(projection.mapping.at(end)))};SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        CHARFORMAT2W emphasis_style{};emphasis_style.cbSize=sizeof(emphasis_style);emphasis_style.dwMask=CFM_BOLD|CFM_ITALIC;
        emphasis_style.dwEffects=(role!=RichTextRole::body||emphasis_range.style=="bold"||emphasis_range.style=="bold_italic"?CFE_BOLD:0)|(emphasis_range.style=="italic"||emphasis_range.style=="bold_italic"?CFE_ITALIC:0);
        SendMessageW(window,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&emphasis_style));
    }
    NativeMathTextMapping mapping; mapping.source = original; mapping.native_text = raw_text(window); mapping.built_up = built;
    // Reference ownership was published after hiding inline delimiters, before
    // TOM expanded mathematical scalars/objects. Remap those native boundaries,
    // rather than treating them as offsets into the still-delimited linear text.
    map_rich_text_reference_ranges(window,reference_ranges,align(before_math,mapping.native_text));
    const auto linear_mapping = align(projection.linear,mapping.native_text); mapping.source_to_native.reserve(projection.mapping.size());
    for (const auto position : projection.mapping) mapping.source_to_native.push_back(linear_mapping.at(std::min(position,linear_mapping.size() - 1)));
    mapping.source_to_native.front() = 0; mapping.source_to_native.back() = mapping.native_text.size();
    delete reinterpret_cast<NativeMathTextMapping*>(RemovePropW(window,mapping_property)); SetPropW(window,mapping_property,new NativeMathTextMapping(std::move(mapping)));
    SetWindowSubclass(window,mapping_proc,mapping_subclass,0);
    return built;
}
}
bool set_native_math_text(HWND window,std::string_view original,std::string_view locale) { return set_math_text(window,original,false,{},RichTextRole::body,{},locale); }
bool set_native_math_inline_text(HWND window,std::string_view original,std::span<const curriculum::Emphasis> emphasis,RichTextRole role,std::string_view locale) { return set_math_text(window,original,true,emphasis,role,{},locale); }
bool set_native_math_fragments(HWND window,std::string_view original,std::span<const NativeMathFragment> fragments,RichTextRole role,std::string_view locale){if(fragments.empty())return false;return set_math_text(window,original,true,{},role,fragments,locale);}
bool set_original_math_text(HWND window,std::string_view original,std::string_view mode,const std::function<std::string(std::string_view)>& request,std::span<const curriculum::Emphasis> emphasis,RichTextRole role,std::string_view locale){
    using namespace winrt::Windows::Data::Json;
    delete reinterpret_cast<NativeMathTextMapping*>(RemovePropW(window,mapping_property));
    set_rich_text(window,original,emphasis,false,role,locale);
    if(mode=="text"||mode=="off"||original.empty())return false;
    if(!request){if(mode=="inline"||mode=="block")return set_math_text(window,original,false,emphasis,role,{},locale);return set_native_math_inline_text(window,original,emphasis,role,locale);}
    JsonObject query;query.SetNamedValue(L"action",JsonValue::CreateStringValue(L"text_layout"));query.SetNamedValue(L"text",JsonValue::CreateStringValue(wide(original)));query.SetNamedValue(L"mode",JsonValue::CreateStringValue(wide(mode)));
    const auto reply=JsonObject::Parse(wide(request(winrt::to_string(query.Stringify()))));if(!reply.HasKey(L"fragments"))throw std::runtime_error(winrt::to_string(reply.GetNamedString(L"error",L"The original mathematical text renderer returned no source ranges")));
    const auto length=wide(original).size();std::vector<NativeMathFragment> fragments;
    for(const auto& value:reply.GetNamedArray(L"fragments")){const auto fragment=value.GetObject();const auto start=fragment.GetNamedNumber(L"start",-1),end=fragment.GetNamedNumber(L"end",-1);
        if(start<0||end<=start||std::floor(start)!=start||std::floor(end)!=end||end>length)throw std::runtime_error("The original mathematical text renderer returned an invalid source range");
        fragments.push_back({static_cast<std::size_t>(start),static_cast<std::size_t>(end),winrt::to_string(fragment.GetNamedString(L"formula")),fragment.GetNamedBoolean(L"literal",false)});
    }
    return fragments.empty()?false:set_math_text(window,original,true,emphasis,role,fragments,locale);
}
std::optional<NativeMathTextMapping> native_math_text_mapping(HWND window,std::string_view source) {
    if (!IsWindow(window)) return {};
    const auto* mapping = reinterpret_cast<const NativeMathTextMapping*>(GetPropW(window,mapping_property));
    if (!mapping || mapping->source != source || mapping->native_text != raw_text(window)) return {};
    return *mapping;
}
} // namespace lgaed::presentation
