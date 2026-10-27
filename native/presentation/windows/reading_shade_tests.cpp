#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/native_math_text.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed::presentation;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct Field {
    HWND parent = nullptr, edit = nullptr;
    explicit Field(std::string_view source,bool code = false,std::span<const lgaed::curriculum::Emphasis> emphasis = {},bool readonly = false) {
        parent = CreateWindowExW(0,L"STATIC",L"Reading range QA",WS_OVERLAPPEDWINDOW,
            0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent != nullptr,"Cannot create the native QA parent");
        edit = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | (readonly ? ES_READONLY : 0),
            8,8,420,160,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(edit != nullptr,"Cannot create a real RichEdit target");
        reader_theme::apply(edit,reader_theme::Surface::canvas); set_rich_text(edit,source,emphasis,code);
    }
    ~Field() { if (edit) DestroyWindow(edit); if (parent) DestroyWindow(parent); }
};
CHARFORMAT2W character(HWND edit,LONG index) {
    CHARRANGE before{}; SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{index,index + 1}; SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W result{}; result.cbSize = sizeof(result);
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&result));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before)); return result;
}
std::vector<CHARFORMAT2W> ink(HWND edit) {
    const auto length = wide(edit_text(edit)).size(); std::vector<CHARFORMAT2W> result;
    for (std::size_t index = 0; index < length; ++index) result.push_back(character(edit,static_cast<LONG>(index)));
    return result;
}
COLORREF pending(HWND target) {
    return reader_theme::unread_color(reader_theme::background(target));
}
double contrast(COLORREF first,COLORREF second){const auto luminance=[](COLORREF value){const auto linear=[](BYTE channel){const auto fraction=channel/255.;return fraction<=.04045?fraction/12.92:std::pow((fraction+.055)/1.055,2.4);};return .2126*linear(GetRValue(value))+.7152*linear(GetGValue(value))+.0722*linear(GetBValue(value));};const auto a=luminance(first),b=luminance(second);return(std::max(a,b)+.05)/(std::min(a,b)+.05);}
void styles_equal(const CHARFORMAT2W& before,const CHARFORMAT2W& after) {
    constexpr DWORD styles = CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT | CFE_PROTECTED;
    require((before.dwEffects & styles) == (after.dwEffects & styles),"Reading ranges changed emphasis/underline effects");
    require(before.yHeight == after.yHeight && std::wstring_view(before.szFaceName) == after.szFaceName,
            "Reading ranges changed font dimensions or typeface");
}
void prefix(HWND edit,const std::vector<CHARFORMAT2W>& before,std::size_t boundary,bool native_math = false) {
    const auto after = ink(edit); require(after.size() == before.size(),"Shading changed the original displayed text length");
    const auto native = native_math ? wide(edit_text(edit)) : std::wstring{};
    for (std::size_t index = 0; index < after.size(); ++index) {
        // OfficeMath's noncharacter delimiters have object-level formatting;
        // they are not displayed glyphs. Inspect each actual letter, number
        // and operator, including both UTF16 units of mathematical letters.
        if (native_math && native[index] >= 0xFDD0 && native[index] <= 0xFDEF) continue;
        styles_equal(before[index],after[index]);
        if (index < boundary) {
            require(after[index].crTextColor == before[index].crTextColor,"A read token did not recover its original syntax color");
            require((after[index].dwEffects & CFE_AUTOCOLOR) == (before[index].dwEffects & CFE_AUTOCOLOR),
                    "A read token lost its automatic color identity");
        } else require(after[index].crTextColor == reader_theme::unread_color((before[index].dwEffects&CFE_AUTOBACKCOLOR)
                           ?reader_theme::background(edit):before[index].crBackColor) && !(after[index].dwEffects & CFE_AUTOCOLOR),
                       "An unread word, operator or punctuation mark escaped the pending gray");
    }
}
std::size_t source_units(std::string_view source,std::size_t bytes) { return wide(source.substr(0,bytes)).size(); }

struct WordTickTrace {
    unsigned selections=0,formats=0,redraw=0;
    HWND target;
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto& trace=*reinterpret_cast<WordTickTrace*>(raw);
        if(message==EM_EXSETSEL||message==EM_SETSEL)++trace.selections;
        if(message==EM_SETCHARFORMAT)++trace.formats;
        if(message==WM_SETREDRAW)++trace.redraw;
        return DefSubclassProc(item,message,wp,lp);
    }
    explicit WordTickTrace(HWND edit):target(edit){require(SetWindowSubclass(edit,proc,0x574f5244,reinterpret_cast<DWORD_PTR>(this)),"Cannot observe native reading word ticks");}
    ~WordTickTrace(){if(IsWindow(target))RemoveWindowSubclass(target,proc,0x574f5244);}
    void clear(){selections=formats=redraw=0;}
};
void readonly_word_ticks_use_ranges_without_selecting_or_hiding_text() {
    const auto ole=OleInitialize(nullptr);require(SUCCEEDED(ole),"Cannot initialize the readonly TOM range test");
    struct OleScope{~OleScope(){OleUninitialize();}} apartment;
    const std::string source="int valor = 12; // conserva colores\n";
    // Measure the actual native readonly restriction before exercising our
    // transaction. The diagnostic separates a rejected range write from an
    // unavailable TOM implementation; both fields below use real RichEdit.
    {
        Field probe(source,true,{},true);IRichEditOle* rich=nullptr;ITextDocument* document=nullptr;
        SendMessageW(probe.edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&rich));
        require(rich!=nullptr,"Readonly RichEdit did not expose its TOM document");
        const auto queried=rich->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void**>(&document));rich->Release();
        require(SUCCEEDED(queried)&&document,"Readonly RichEdit did not support native TOM ranges");
        ITextRange* range=nullptr;ITextFont* font=nullptr;
        const auto ranged=document->Range(0,3,&range);document->Release();
        require(SUCCEEDED(ranged)&&range,"Cannot select a native readonly TOM probe range");
        const auto formatted=range->GetFont(&font);range->Release();
        require(SUCCEEDED(formatted)&&font,"Cannot query the readonly TOM probe font");
        const auto readonly_result=font->SetForeColor(RGB(1,2,3));
        SendMessageW(probe.edit,EM_SETREADONLY,FALSE,0);
        const auto writable_result=font->SetForeColor(RGB(4,5,6));font->Release();
        require(SUCCEEDED(writable_result),"Native TOM range color was unavailable even after a synchronous unlock");
        std::cout<<"INFO native TOM SetForeColor HRESULT readonly=0x"<<std::hex<<static_cast<unsigned long>(readonly_result)
            <<" writable=0x"<<static_cast<unsigned long>(writable_result)<<std::dec<<'\n';
    }
    bool all_ticks_used_ranges=true;
    for(const bool readonly:{false,true}){
        Field field(source,true,{},readonly);
        // An existing editable-source history must survive the same rendering
        // transaction used by readonly lesson prose and examples.
        if(readonly)SendMessageW(field.edit,EM_SETREADONLY,FALSE,0);
        SendMessageW(field.edit,EM_EMPTYUNDOBUFFER,0,0);SendMessageW(field.edit,EM_SETSEL,static_cast<WPARAM>(-1),-1);
        SendMessageW(field.edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"X"));
        if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
        const auto current=source+'X';const auto before=ink(field.edit);CHARRANGE selected{4,9};
        SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        const auto events=SendMessageW(field.edit,EM_GETEVENTMASK,0,0),modified=SendMessageW(field.edit,EM_GETMODIFY,0,0);
        const auto focus=GetFocus();
        POINT before_scroll{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before_scroll));
        WordTickTrace trace(field.edit);ReadingShade shade(field.edit,current);
        all_ticks_used_ranges=all_ticks_used_ranges&&!trace.selections&&!trace.formats&&!trace.redraw;
        std::cout<<"INFO "<<(readonly?"readonly":"editable")<<" construction selections="<<trace.selections
            <<" format_messages="<<trace.formats<<" redraw_toggles="<<trace.redraw<<'\n';
        for(const auto boundary:{0u,4u,10u,4u,static_cast<unsigned>(wide(current).size())}){
            trace.clear();shade.read_through(boundary);
            all_ticks_used_ranges=all_ticks_used_ranges&&!trace.selections&&!trace.formats&&!trace.redraw;
            std::cout<<"INFO "<<(readonly?"readonly":"editable")<<" tick="<<boundary<<" selections="<<trace.selections
                <<" format_messages="<<trace.formats<<" redraw_toggles="<<trace.redraw<<'\n';
            prefix(field.edit,before,boundary);
            CHARRANGE after{};POINT scroll{};SendMessageW(field.edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
            SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
            require(after.cpMin==selected.cpMin&&after.cpMax==selected.cpMax&&scroll.x==before_scroll.x&&scroll.y==before_scroll.y,
                "A readonly reading tick changed selection or viewport");
            require(GetFocus()==focus&&((GetWindowLongPtrW(field.edit,GWL_STYLE)&ES_READONLY)!=0)==readonly&&
                (GetWindowLongPtrW(field.edit,GWL_STYLE)&WS_VISIBLE)&&events==SendMessageW(field.edit,EM_GETEVENTMASK,0,0)&&modified==SendMessageW(field.edit,EM_GETMODIFY,0,0),
                "A reading tick changed readonly, visibility, notifications or modified state");
        }
        shade.restore();prefix(field.edit,before,before.size());
        if(readonly)SendMessageW(field.edit,EM_SETREADONLY,FALSE,0);
        require(SendMessageW(field.edit,EM_UNDO,0,0)&&edit_text(field.edit)==source&&SendMessageW(field.edit,EM_REDO,0,0)&&edit_text(field.edit)==current,
            "Readonly reading ticks removed or polluted the original text Undo/Redo history");
    }
    require(all_ticks_used_ranges,"Readonly word highlighting fell back to UI selection/format messages instead of its available TOM ranges");
}
void fenced_code_keeps_original_reading_offsets(){
    const std::string source="Inicio á 😀.\r\n```cpp\r\nstd::cout << \"área\";\r\nreturn 0;\r\n```\r\nFin con `std::cin`.";
    const std::string displayed="Inicio á 😀.\ncpp\nstd::cout << \"área\";\nreturn 0;\n\nFin con std::cin.";
    const std::vector<lgaed::curriculum::Emphasis> emphasis{{"área",0,"bold"},{"Fin",0,"italic"}};
    Field field(source,false,emphasis,true);require(edit_text(field.edit)==displayed,"Fenced rendering removed its language tag/newlines or left visible wrappers");
    const auto before=ink(field.edit);const auto native=wide(displayed);
    require((before[native.find(L"área")].dwEffects&CFE_BOLD)&&(before[native.find(L"Fin")].dwEffects&CFE_ITALIC),
        "Hiding a fenced wrapper displaced the authored UTF8 emphasis");
    CHARRANGE selected{1,5};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    POINT wanted{0,24},position{};SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&wanted));SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
    const auto events=SendMessageW(field.edit,EM_GETEVENTMASK,0,0),modified=SendMessageW(field.edit,EM_GETMODIFY,0,0);
    ReadingShade shade(field.edit,source);require(shade.matches(field.edit,source)&&shade.length()==wide(source).size(),"A fenced source lost its original UTF16 narration mapping");
    const auto step=[&](std::size_t original,std::size_t visible){shade.read_through(original);prefix(field.edit,before,visible);
        CHARRANGE selection{};POINT scroll{};SendMessageW(field.edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        require(selection.cpMin==selected.cpMin&&selection.cpMax==selected.cpMax&&scroll.x==position.x&&scroll.y==position.y&&
            (GetWindowLongPtrW(field.edit,GWL_STYLE)&ES_READONLY)&&events==SendMessageW(field.edit,EM_GETEVENTMASK,0,0)&&modified==SendMessageW(field.edit,EM_GETMODIFY,0,0),
            "Reading a fenced fragment changed readonly selection, scrolling, notifications or modified state");};
    step(0,0);step(source_units(source,source.find("😀"))+2,native.find(L"😀")+2);
    step(source_units(source,source.find("```"))+1,native.find(L"cpp"));
    step(source_units(source,source.find("std::cout")),native.find(L"std::cout"));
    step(source_units(source,source.find("Fin")),native.find(L"Fin"));
    step(source_units(source,source.find("cpp")),native.find(L"cpp"));
    step(wide(source).size(),native.size());shade.read_through(0);shade.restore();prefix(field.edit,before,before.size());
    for(const auto& [original,expected]:std::vector<std::pair<std::string,std::string>>{
        {"Uno `a\nb` fin.","Uno a\nb fin."},
        {"Vacío ``` \t\r\n``` y ``.","Vacío ``` \t\n``` y ``."},
        {"Sin cierre ```cpp\nint x;","Sin cierre ```cpp\nint x;"},
        {"Conserva \\(x`y`\\) y texto.","Conserva \\(x`y`\\) y texto."}}){
        Field sample(original,false,{},true);require(edit_text(sample.edit)==expected,"Delimiter projection changed blank, multiline, unclosed or protected mathematical source");
        const auto colors=ink(sample.edit);ReadingShade ranges(sample.edit,original);require(ranges.matches(sample.edit,original),"A delimiter edge case lost its actual displayed source mapping");
        ranges.read_through(0);prefix(sample.edit,colors,0);ranges.read_through(ranges.length());prefix(sample.edit,colors,colors.size());
    }
    {
        const std::string original="const char* text = \"`literal`\";";Field code(original,true,{},true);const auto colors=ink(code.edit);
        require(edit_text(code.edit)==original,"A true C++ source lost its literal backticks");ReadingShade ranges(code.edit,original);
        require(ranges.matches(code.edit,original),"A true C++ source was projected as prose delimiters");
        ranges.read_through(0);prefix(code.edit,colors,0);ranges.read_through(ranges.length());prefix(code.edit,colors,colors.size());
    }
    {
        const std::string original="Antes ```cpp\nstd::cout;\n``` y \\(x^2\\) después.";Field mixed(original,false,{},true);
        require(set_native_math_inline_text(mixed.edit,original),"The fenced/math fixture did not build a real native formula");
        const auto mapping=native_math_text_mapping(mixed.edit,original);const auto colors=ink(mixed.edit);
        require(mapping&&edit_text(mixed.edit).find('`')==std::string::npos,"A mixed native formula retained a visible fenced delimiter");
        ReadingShade ranges(mixed.edit,original);require(ranges.matches(mixed.edit,original),"Fenced projection replaced the authoritative OfficeMath source map");
        for(const auto boundary:{std::size_t{0},source_units(original,original.find("std::cout")),source_units(original,original.find("después")),std::size_t{0},wide(original).size()}){
            ranges.read_through(boundary);prefix(mixed.edit,colors,mapping->source_to_native.at(boundary),true);
        }
        ranges.restore();prefix(mixed.edit,colors,colors.size(),true);
    }
}
}

int main(int argc,char* argv[]) {
    const bool readonly_only=argc==2&&std::string_view(argv[1])=="--readonly";
    const bool fences_only=argc==2&&std::string_view(argv[1])=="--fences";
    if(argc>1&&!readonly_only&&!fences_only)return 2;
    std::cout << std::unitbuf; const auto module = LoadLibraryW(L"Msftedit.dll");
    if (!module) return 2;
    int passed = 0, failed = 0;
    const auto test = [&](const char* name,const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    if(fences_only){test("Fenced and inline code keep original UTF16 reading offsets and native math mapping",fenced_code_keeps_original_reading_offsets);FreeLibrary(module);return failed?1:0;}
    test("Readonly and editable word ticks preserve real colors and UI state without selection or visibility changes",readonly_word_ticks_use_ranges_without_selecting_or_hiding_text);
    if(readonly_only){FreeLibrary(module);return failed?1:0;}
    test("Fenced and inline code keep original UTF16 reading offsets and native math mapping",fenced_code_keeps_original_reading_offsets);
    test("Android technical backgrounds survive readonly forward backward and restored narration",[]{
        const std::string source="Lee std::cout y continúa con C++.";Field field(source,false,{},true);
        const auto before=ink(field.edit);const auto displayed=wide(edit_text(field.edit));
        const auto technical=displayed.find(L"std::cout"),language=displayed.find(L"C++");
        require(technical!=displayed.npos&&language!=displayed.npos,"The technical-background reading fixture changed its original terms");
        const bool high_contrast=reader_theme::palette(field.edit).high_contrast;
        require(((before[technical].dwEffects&CFE_AUTOBACKCOLOR)!=0)==high_contrast&&
            (before[0].dwEffects&CFE_AUTOBACKCOLOR)&&(before[language].dwEffects&CFE_AUTOBACKCOLOR),
            "The real prose renderer did not keep Android's technical background or the C++ exception");
        CHARRANGE selected{1,7};SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        POINT position{};SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
        const auto events=SendMessageW(field.edit,EM_GETEVENTMASK,0,0),modified=SendMessageW(field.edit,EM_GETMODIFY,0,0);
        const auto stable=[&]{const auto after=ink(field.edit);CHARRANGE selection{};POINT scroll{};
            SendMessageW(field.edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
            require(selection.cpMin==selected.cpMin&&selection.cpMax==selected.cpMax&&scroll.x==position.x&&scroll.y==position.y&&
                (GetWindowLongPtrW(field.edit,GWL_STYLE)&ES_READONLY)&&events==SendMessageW(field.edit,EM_GETEVENTMASK,0,0)&&modified==SendMessageW(field.edit,EM_GETMODIFY,0,0),
                "Technical-background narration moved the readonly selection or changed its viewport/events");
            for(std::size_t at=0;at<before.size();++at){
                require((before[at].dwEffects&CFE_AUTOBACKCOLOR)==(after[at].dwEffects&CFE_AUTOBACKCOLOR)&&
                    ((before[at].dwEffects&CFE_AUTOBACKCOLOR)||before[at].crBackColor==after[at].crBackColor),
                    "Word shading removed or spread a technical background");
                styles_equal(before[at],after[at]);
            }
            require(edit_text(field.edit)==source,"Technical-background narration changed original prose or C++");
        };
        ReadingShade shade(field.edit,source);
        for(const auto boundary:{std::size_t{0},technical,technical+4,technical+9,language,std::size_t{0},displayed.size()}){
            shade.read_through(boundary);stable();prefix(field.edit,before,boundary);
            const auto after=ink(field.edit);
            for(std::size_t at=boundary;at<after.size();++at){const auto background=(before[at].dwEffects&CFE_AUTOBACKCOLOR)
                    ?reader_theme::background(field.edit):before[at].crBackColor;
                require(contrast(after[at].crTextColor,background)>=4.5,"Pending technical text lost readable contrast against its own background");}
        }
        shade.read_through(technical);shade.restore();stable();prefix(field.edit,before,before.size());
    });
    test("Android neutral pending gray follows real surfaces and preserves green feedback",[]{
        require(reader_theme::unread_color(RGB(0,0,0))==RGB(122,122,122)&&reader_theme::unread_color(RGB(255,255,255))==RGB(80,80,80),"Pending gray no longer uses Android's original dark/light preferences");
        for(int channel=0;channel<=255;++channel){const auto background=RGB(channel,channel,channel);const auto gray=reader_theme::unread_color(background);require(GetRValue(gray)==GetGValue(gray)&&GetGValue(gray)==GetBValue(gray)&&contrast(gray,background)>=4.5,"An intermediate surface lacks opaque neutral pending ink with4.5contrast");}
        for(const auto background:{RGB(16,17,20),RGB(28,31,36),RGB(38,35,48),RGB(49,56,66),RGB(250,250,250),RGB(238,238,238),GetSysColor(COLOR_WINDOW)})
            require(contrast(reader_theme::unread_color(background),background)>=4.5,"An actual lesson or system-contrast surface lost its readable pending ink");
        for(const auto surface:{reader_theme::Surface::canvas,reader_theme::Surface::card,reader_theme::Surface::field}){
            Field field("Correcto. Conserva la explicación.");reader_theme::apply(field.edit,surface);reader_theme::set_text_color(field.edit,RGB(180,220,190));set_rich_text(field.edit,"Correcto. Conserva la explicación.");
            const auto before=ink(field.edit);ReadingShade shade(field.edit,"Correcto. Conserva la explicación.");shade.read_through(0);prefix(field.edit,before,0);shade.read_through(10);prefix(field.edit,before,10);
            require(contrast(pending(field.edit),reader_theme::background(field.edit))>=4.5,"A real themed control chose pending ink for another surface");shade.restore();prefix(field.edit,before,before.size());
            require(reader_theme::palette().high_contrast||character(field.edit,0).crTextColor==RGB(180,220,190),"Completing feedback did not restore its original Android green");
        }
    });
    test("Forward and backward range application coexists inside one paragraph",[] {
        const std::string source = "Uno dos tres, cuatro. ¿Cinco?"; Field field(source); const auto before = ink(field.edit);
        ReadingShade shade(field.edit,source); require(shade.matches(field.edit,source),"The original displayed source was not matched");
        shade.read_through(0); prefix(field.edit,before,0);
        shade.read_through(4); prefix(field.edit,before,4);
        shade.read_through(8); prefix(field.edit,before,8);
        shade.read_through(4); prefix(field.edit,before,4);
        const auto paused = ink(field.edit); shade.read_through(4); const auto resumed = ink(field.edit);
        for (std::size_t index = 0; index < paused.size(); ++index)
            require(paused[index].crTextColor == resumed[index].crTextColor,"An unchanged/paused boundary advanced the shading");
        shade.read_through(shade.length()); prefix(field.edit,before,before.size());
        shade.read_through(0); prefix(field.edit,before,0);
        require(edit_text(field.edit) == source,"Word ranges changed lesson punctuation or accents");
    });
    test("UTF16 backticks CRLF accents and supplementary characters map to original source offsets",[] {
        const std::string source = "á 😀 `std::cout`;\r\nfórmula 6 × 100 = 600.";
        const std::string displayed = "á 😀 std::cout;\nfórmula 6 × 100 = 600.";
        Field field(source); require(edit_text(field.edit) == displayed,"The actual renderer did not produce the expected original visible text");
        const auto before = ink(field.edit); ReadingShade shade(field.edit,source);
        require(shade.length() == wide(source).size(),"Source ranges use byte or code-point offsets instead of UTF16");
        const auto cout_at = source.find("cout");
        shade.read_through(source_units(source,cout_at)); prefix(field.edit,before,wide(displayed).find(L"cout"));
        const auto formula_at = source.find("fórmula");
        shade.read_through(source_units(source,formula_at)); prefix(field.edit,before,wide(displayed).find(L"fórmula"));
        shade.read_through(source_units(source,source.find("😀")));
        prefix(field.edit,before,wide(displayed).find(L"😀"));
        shade.restore(); prefix(field.edit,before,before.size());
        require(edit_text(field.edit) == displayed,"Mapping normalized or duplicated the original visible content");
    });
    test("C++ tokens symbols punctuation and mathematical operators remain pending until their supplied range",[] {
        const std::string source = "C++: `#include <iostream>`; `std::cout << \"área\";` y 6 × 100 = 600. ¿Listo?";
        Field field(source); const auto displayed = edit_text(field.edit); const auto before = ink(field.edit);
        ReadingShade shade(field.edit,source); shade.read_through(0); prefix(field.edit,before,0);
        const auto include_end = source.find("#include") + std::string_view("#include").size();
        shade.read_through(source_units(source,include_end));
        prefix(field.edit,before,wide(displayed).find(L"#include") + 8);
        const auto operator_at = source.find("<<"); shade.read_through(source_units(source,operator_at));
        prefix(field.edit,before,wide(displayed).find(L"<<"));
        const auto equals_at = source.find('='); shade.read_through(source_units(source,equals_at));
        prefix(field.edit,before,wide(displayed).find(L'='));
        shade.read_through(shade.length()); prefix(field.edit,before,before.size());
        require(edit_text(field.edit) == displayed,"Technical tokens changed while applying reading ranges");
    });
    test("Native mathematical alphabet scalars retain distinct source ranges and backward shading",[] {
        const std::string source = R"(\frac{x}{x}+x^{2}+y)"; Field field(source);
        require(set_native_math_text(field.edit,source),"The real native mathematical fixture was not built up");
        const auto mapping = native_math_text_mapping(field.edit,source); require(mapping.has_value(),"A built native formula lost its source mapping");
        const auto first = source.find('x'),second = source.find('x',first + 1),third = source.find('x',second + 1),last = source.find('y');
        const auto a = mapping->source_to_native[first],b = mapping->source_to_native[second],c = mapping->source_to_native[third],d = mapping->source_to_native[last];
        require(a < b && b < c && c < d,"Repeated mathematical identifiers collapsed into one rendered occurrence");
        for (const auto start : {a,b,c,d}) {
            require(start + 1 < mapping->native_text.size() && mapping->native_text[start] >= 0xD800 && mapping->native_text[start] <= 0xDBFF
                && mapping->native_text[start + 1] >= 0xDC00 && mapping->native_text[start + 1] <= 0xDFFF,
                "A mathematical ASCII identifier mapped to a delimiter or half of a native supplementary scalar");
        }
        const auto before = ink(field.edit); ReadingShade shade(field.edit,source);
        require(shade.matches(field.edit,source),"The native formula cannot use original narration offsets");
        shade.read_through(second); prefix(field.edit,before,b,true);
        shade.read_through(last); prefix(field.edit,before,d,true);
        shade.read_through(first); prefix(field.edit,before,a,true);
        shade.read_through(source.size()); prefix(field.edit,before,before.size(),true);
        require(native_math_text_mapping(field.edit,source)->native_text == mapping->native_text,"Shading changed a mathematical glyph or operator");
    });
    test("Original bold italic underline fonts and syntax colors survive pending and read ranges",[] {
        const std::string source = "Negrita cursiva y `std::cout << \"área\";` final.";
        const std::vector<lgaed::curriculum::Emphasis> emphasis{{"Negrita",0,"bold"},{"cursiva",0,"italic"}};
        Field field(source,false,emphasis); const auto displayed = edit_text(field.edit);
        const auto at = static_cast<LONG>(wide(displayed).find(L"std::cout"));
        CHARRANGE range{at,at + 9}; SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W underline{}; underline.cbSize = sizeof(underline); underline.dwMask = CFM_UNDERLINE; underline.dwEffects = CFE_UNDERLINE;
        SendMessageW(field.edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&underline));
        const auto before = ink(field.edit);
        require(before[0].dwEffects & CFE_BOLD,"The real renderer did not apply bold to the fixture");
        require(before[wide(displayed).find(L"cursiva")].dwEffects & CFE_ITALIC,"The real renderer did not apply italic to the fixture");
        { ReadingShade shade(field.edit,source); shade.read_through(0); prefix(field.edit,before,0);
          shade.read_through(source_units(source,source.find(" final"))); prefix(field.edit,before,wide(displayed).find(L" final")); }
        prefix(field.edit,before,before.size());
    });
    test("Selection scroll geometry event mask modified flag and native Undo Redo are unchanged",[] {
        std::string source; for (int line = 0; line < 60; ++line) source += "línea de lectura con acento\n";
        Field field(source); SendMessageW(field.edit,EM_EMPTYUNDOBUFFER,0,0);
        SendMessageW(field.edit,EM_SETSEL,static_cast<WPARAM>(-1),-1);
        SendMessageW(field.edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"X"));
        const auto current = source + 'X';
        CHARRANGE selected{3,10}; SendMessageW(field.edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        POINT wanted{0,90}, before_scroll{}; SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&wanted));
        SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before_scroll));
        SendMessageW(field.edit,EM_SETEVENTMASK,0,ENM_CHANGE | ENM_SELCHANGE | ENM_REQUESTRESIZE);
        const auto events = SendMessageW(field.edit,EM_GETEVENTMASK,0,0), modified = SendMessageW(field.edit,EM_GETMODIFY,0,0);
        RECT before_rect{}; GetWindowRect(field.edit,&before_rect);
        std::string geometry_trace; bool stable_viewport = true;
        const auto record_viewport = [&](const char* stage) {
            POINT scroll{}; RECT bounds{};
            SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll)); GetWindowRect(field.edit,&bounds);
            stable_viewport = stable_viewport && scroll.x == before_scroll.x && scroll.y == before_scroll.y && EqualRect(&bounds,&before_rect);
            geometry_trace += std::string("\n  ") + stage + ": scroll=" + std::to_string(scroll.x) + "," + std::to_string(scroll.y)
                + " rect=" + std::to_string(bounds.left) + "," + std::to_string(bounds.top) + ","
                + std::to_string(bounds.right) + "," + std::to_string(bounds.bottom);
        };
        record_viewport("before construction");
        { ReadingShade shade(field.edit,current); record_viewport("after construction");
          shade.read_through(0); record_viewport("after read 0");
          shade.read_through(25); record_viewport("after read 25");
          shade.read_through(5); record_viewport("after read 5"); }
        record_viewport("after destruction");
        CHARRANGE after_selection{}; POINT after_scroll{}; RECT after_rect{};
        SendMessageW(field.edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_selection));
        SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after_scroll)); GetWindowRect(field.edit,&after_rect);
        require(selected.cpMin == after_selection.cpMin && selected.cpMax == after_selection.cpMax,"Shading moved the existing text selection");
        require(stable_viewport && before_scroll.x == after_scroll.x && before_scroll.y == after_scroll.y && EqualRect(&before_rect,&after_rect),
                ("Shading changed scroll or component dimensions" + geometry_trace).c_str());
        require(events == SendMessageW(field.edit,EM_GETEVENTMASK,0,0) && modified == SendMessageW(field.edit,EM_GETMODIFY,0,0),
                "Shading changed edit events or the document modified state");
        require(edit_text(field.edit) == current && SendMessageW(field.edit,EM_UNDO,0,0) && edit_text(field.edit) == source,
                "Shading added an Undo item or removed the original text edit");
        require(SendMessageW(field.edit,EM_REDO,0,0) && edit_text(field.edit) == current,"Shading broke native Redo");
    });
    test("Hidden target visibility is retained through construction updates restore and destruction",[] {
        const std::string source = "Texto oculto por pantalla completa."; Field field(source); ShowWindow(field.edit,SW_HIDE);
        const auto visible = [&] { return (GetWindowLongPtrW(field.edit,GWL_STYLE) & WS_VISIBLE) != 0; };
        require(!visible(),"The visibility fixture was not hidden");
        { ReadingShade shade(field.edit,source); require(!visible(),"Constructing shading revealed a hidden paragraph");
          shade.read_through(5); require(!visible(),"Updating shading revealed a hidden paragraph");
          shade.restore(); require(!visible(),"Restoring shading revealed a hidden paragraph"); }
        require(!visible(),"Destroying shading revealed a hidden paragraph");
    });
    test("Changed source invalidates the mapping and destroyed HWNDs are never accessed as a live target",[] {
        const std::string source = "Original con `std::cout`."; Field field(source);
        auto shade = std::make_unique<ReadingShade>(field.edit,source); shade->read_through(0);
        set_rich_text(field.edit,"Contenido nuevo y válido."); const auto before = ink(field.edit);
        require(!shade->matches(field.edit,source),"Changed visible source retained a stale reading mapping");
        shade->read_through(shade->length()); shade->restore(); prefix(field.edit,before,before.size());
        const auto destroyed = field.edit; DestroyWindow(field.edit); field.edit = nullptr;
        require(!shade->matches(destroyed,source),"A destroyed HWND remained a matching live target");
        shade->read_through(0); shade->restore(); shade.reset();
        Field another(source); require(edit_text(another.edit) == "Original con std::cout.","Destroyed-target cleanup changed another control");
    });
    test("Pending ink follows the actual Windows contrast mode without changing system settings",[] {
        HIGHCONTRASTW system_contrast{}; system_contrast.cbSize = sizeof(system_contrast);
        const bool system = SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(system_contrast),&system_contrast,0) && (system_contrast.dwFlags & HCF_HIGHCONTRASTON);
        require(reader_theme::palette().high_contrast == system,"The palette disagrees with the actual Windows contrast mode");
        Field field("Texto pendiente."); ReadingShade shade(field.edit,"Texto pendiente."); shade.read_through(0);
        require(character(field.edit,0).crTextColor == pending(field.edit)&&contrast(pending(field.edit),reader_theme::background(field.edit))>=4.5,"Pending ink ignored the actual system mode or contrast floor");
        std::cout << "INFO Actual high contrast: " << (system ? "enabled" : "disabled; enabled-mode visual validation not claimed") << '\n';
    });
    FreeLibrary(module);
    std::cout << passed << " deterministic range-application groups passed; " << failed
              << " failed. These tests do not measure speech/audio synchronization.\n";
    return failed ? 1 : 0;
}
