#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/native_math_text.hpp"

#include <richedit.h>
#include <algorithm>
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
    explicit Field(std::string_view source,bool code = false,std::span<const lgaed::curriculum::Emphasis> emphasis = {}) {
        parent = CreateWindowExW(0,L"STATIC",L"Reading range QA",WS_OVERLAPPEDWINDOW,
            0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent != nullptr,"Cannot create the native QA parent");
        edit = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
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
COLORREF pending() {
    return reader_theme::palette().high_contrast ? GetSysColor(COLOR_GRAYTEXT) : RGB(135,139,148);
}
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
        } else require(after[index].crTextColor == pending() && !(after[index].dwEffects & CFE_AUTOCOLOR),
                       "An unread word, operator or punctuation mark escaped the pending gray");
    }
}
std::size_t source_units(std::string_view source,std::size_t bytes) { return wide(source.substr(0,bytes)).size(); }
}

int main() {
    std::cout << std::unitbuf; const auto module = LoadLibraryW(L"Msftedit.dll");
    if (!module) return 2;
    int passed = 0, failed = 0;
    const auto test = [&](const char* name,const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
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
        { ReadingShade shade(field.edit,current); shade.read_through(0); shade.read_through(25); shade.read_through(5); }
        CHARRANGE after_selection{}; POINT after_scroll{}; RECT after_rect{};
        SendMessageW(field.edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_selection));
        SendMessageW(field.edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after_scroll)); GetWindowRect(field.edit,&after_rect);
        require(selected.cpMin == after_selection.cpMin && selected.cpMax == after_selection.cpMax,"Shading moved the existing text selection");
        require(before_scroll.x == after_scroll.x && before_scroll.y == after_scroll.y && EqualRect(&before_rect,&after_rect),
                "Shading changed scroll or component dimensions");
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
        HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
        const bool system = SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
        require(reader_theme::palette().high_contrast == system,"The palette disagrees with the actual Windows contrast mode");
        Field field("Texto pendiente."); ReadingShade shade(field.edit,"Texto pendiente."); shade.read_through(0);
        require(character(field.edit,0).crTextColor == pending(),"Pending ink ignored the actual system mode");
        std::cout << "INFO Actual high contrast: " << (system ? "enabled" : "disabled; enabled-mode visual validation not claimed") << '\n';
    });
    FreeLibrary(module);
    std::cout << passed << " deterministic range-application groups passed; " << failed
              << " failed. These tests do not measure speech/audio synchronization.\n";
    return failed ? 1 : 0;
}
