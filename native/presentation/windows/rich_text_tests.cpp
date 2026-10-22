#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <richedit.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Window { HWND value; ~Window() { if (value) DestroyWindow(value); } };
CHARFORMAT2W character(HWND edit, LONG position) {
    CHARRANGE before{}; SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{position,position + 1}; SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W result{}; result.cbSize = sizeof(result);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&result));
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
    return result;
}
}
int main() {
    const auto module = LoadLibraryW(L"Msftedit.dll");
    if (!module) return 2;
    try {
        using namespace lgaed::presentation;
        Window parent{CreateWindowExW(0, L"STATIC", L"RichEdit QA", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(parent.value != nullptr, "Cannot create QA parent");
        Window editor{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 500, 300, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(editor.value != nullptr, "Cannot create real RichEdit");
        const std::string original = "#include <iostream>\n\nint main() {\n\tstd::cout << \"área  A\";  \n}\n";
        set_edit_text(editor.value, original);
        check(edit_text(editor.value) == original, "RichEdit did not preserve exact indentation, spaces or final LF");
        set_edit_text(editor.value, "{\\rtf1 This is C++ text, not RTF.}\n");
        check(edit_text(editor.value) == "{\\rtf1 This is C++ text, not RTF.}\n", "Code was interpreted as RTF");
        set_edit_text(editor.value, original);
        PARAFORMAT2 tabs{}; tabs.cbSize = sizeof(tabs); tabs.dwMask = PFM_TABSTOPS;
        tabs.cTabCount = 1; tabs.rgxTabs[0] = 720;
        SendMessageW(editor.value, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&tabs));
        SendMessageW(editor.value, EM_EMPTYUNDOBUFFER, 0, 0);
        CHARRANGE end{-1,-1}; SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
        SendMessageW(editor.value, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
        CHARRANGE before{3,8}; SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
        const auto modified = SendMessageW(editor.value, EM_GETMODIFY, 0, 0);
        style_cpp(editor.value);
        CHARRANGE after{}; SendMessageW(editor.value, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
        check(before.cpMin == after.cpMin && before.cpMax == after.cpMax, "Syntax formatting moved selection");
        check(SendMessageW(editor.value, EM_GETMODIFY, 0, 0) == modified, "Syntax formatting changed modified state");
        PARAFORMAT2 after_tabs{}; after_tabs.cbSize = sizeof(after_tabs);
        SendMessageW(editor.value, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&after_tabs));
        check(after_tabs.cTabCount == 1 && after_tabs.rgxTabs[0] == 720, "Syntax formatting replaced the editor's measured tab stops");
        check(edit_text(editor.value) == original + 'X', "Syntax formatting changed source text");
        check(SendMessageW(editor.value, EM_UNDO, 0, 0) != 0 && edit_text(editor.value) == original, "Syntax formatting entered the text undo history");
        check(SendMessageW(editor.value, EM_REDO, 0, 0) != 0 && edit_text(editor.value) == original + 'X', "Syntax formatting broke redo history");
        set_rich_text(editor.value, "El `std::cout` conserva su texto.", {}, false);
        check(edit_text(editor.value) == "El std::cout conserva su texto.", "Inline code delimiters were displayed or text was lost");
        check(utf8(wide("Acentos áéíóú y carácter 𐐀")) == "Acentos áéíóú y carácter 𐐀", "UTF-8/UTF-16 conversion lost Unicode");
        {
            const auto colors = reader_theme::palette();
            HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
            const bool system_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
                && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
            check(colors.high_contrast == system_contrast, "Theme ignored Windows high contrast");
            if (system_contrast) {
                check(colors.canvas == GetSysColor(COLOR_WINDOW) && colors.text == GetSysColor(COLOR_WINDOWTEXT),
                    "High-contrast theme replaced the user's system colors");
            } else {
                check(colors.canvas == RGB(30,31,33) && colors.header == RGB(37,40,45)
                    && colors.card == RGB(40,41,45) && colors.field == RGB(38,41,46)
                    && colors.text == RGB(222,223,225) && colors.accent == RGB(77,211,224),
                    "Reader theme diverged from the Android reference presentation");
            }
            set_edit_text(editor.value, original);
            SendMessageW(editor.value, EM_EMPTYUNDOBUFFER, 0, 0);
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
            SendMessageW(editor.value, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
            const auto mask = SendMessageW(editor.value, EM_GETEVENTMASK, 0, 0);
            const auto previous_modify = SendMessageW(editor.value, EM_GETMODIFY, 0, 0);
            reader_theme::apply(editor.value, reader_theme::Surface::field);
            style_cpp(editor.value);
            SendMessageW(editor.value, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
            check(before.cpMin == after.cpMin && before.cpMax == after.cpMax,
                "Local theme/syntax moved the code selection");
            check(mask == SendMessageW(editor.value, EM_GETEVENTMASK, 0, 0)
                && previous_modify == SendMessageW(editor.value, EM_GETMODIFY, 0, 0),
                "Local theme/syntax changed the editor event or modified state");
            check(reader_theme::background(editor.value) == colors.field && edit_text(editor.value) == original + 'X',
                "Local field theme changed the original code");
            check(character(editor.value, 0).crTextColor == (system_contrast ? colors.text : RGB(92,156,231)),
                "Themed syntax lost the Android preprocessor color or system contrast");
            const auto quote = static_cast<LONG>(wide(original).find(L'"'));
            check(character(editor.value, quote).crTextColor == (system_contrast ? colors.text : RGB(122,195,141)),
                "Themed syntax lost the original string color");
            check(character(editor.value, static_cast<LONG>(wide(original).find(L'}'))).crTextColor == colors.text,
                "Themed syntax left uncolored punctuation unreadable");
            check(SendMessageW(editor.value, EM_UNDO, 0, 0) != 0 && edit_text(editor.value) == original,
                "Theming entered the native Undo history");
            check(SendMessageW(editor.value, EM_REDO, 0, 0) != 0 && edit_text(editor.value) == original + 'X',
                "Theming broke native Redo");
            const auto technical = static_cast<LONG>(wide(original).find(L"std"));
            CHARRANGE underlined{technical,technical + 3};
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&underlined));
            CHARFORMAT2W underline{}; underline.cbSize = sizeof(underline);
            underline.dwMask = CFM_UNDERLINE; underline.dwEffects = CFE_UNDERLINE;
            SendMessageW(editor.value, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&underline));
            style_cpp(editor.value);
            check((character(editor.value,technical).dwEffects & CFE_UNDERLINE) != 0,
                "Themed syntax removed an existing underline span");

            Window text{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | ES_READONLY,
                0, 0, 500, 120, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(text.value != nullptr, "Cannot create themed prose control");
            reader_theme::apply(text.value, reader_theme::Surface::canvas);
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"valor",0,"bold_italic"}}};
            set_rich_text(text.value, "Álgebra con `std::cout` y valor.", emphasis);
            check(edit_text(text.value) == "Álgebra con std::cout y valor.", "Theming altered Unicode or displayed inline delimiters");
            check(character(text.value, 0).crTextColor == colors.text, "Themed prose kept an unreadable system foreground");
            const auto prose = wide(edit_text(text.value));
            check(std::wstring(character(text.value, static_cast<LONG>(prose.find(L"std"))).szFaceName) == L"Consolas",
                "Theming removed the technical fragment's monospace span");
            const auto value_style = character(text.value, static_cast<LONG>(prose.find(L"valor")));
            check((value_style.dwEffects & (CFE_BOLD | CFE_ITALIC)) == (CFE_BOLD | CFE_ITALIC),
                "Theming erased authored bold/italic emphasis");

            Window untouched{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE,
                0, 0, 200, 80, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(untouched.value != nullptr, "Cannot create original palette control");
            set_rich_text(untouched.value, "Original system presentation");
            check(!reader_theme::applied(untouched.value)
                && character(untouched.value, 0).crTextColor == GetSysColor(COLOR_WINDOWTEXT),
                "A themed HWND changed unrelated controls");
            const auto dc = GetDC(parent.value); LRESULT result = 0;
            check(!reader_theme::control_color(WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(untouched.value), result), "Theme intercepted an unrelated control");
            check(reader_theme::control_color(WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(editor.value), result)
                && result == reinterpret_cast<LRESULT>(reader_theme::brush(reader_theme::Surface::field)),
                "Themed field did not use its local brush");
            ReleaseDC(parent.value, dc);

            Window button{CreateWindowExW(0, L"BUTTON", L"&Comprobar", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 130, 36, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            Window radio{CreateWindowExW(0, L"BUTTON", L"Una opción", WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON,
                0, 40, 130, 36, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(button.value && radio.value, "Cannot create real themed buttons");
            const auto focus = GetFocus();
            const auto radio_dialog_code = SendMessageW(radio.value,WM_GETDLGCODE,0,0);
            reader_theme::apply(button.value, reader_theme::Surface::card);
            reader_theme::apply(radio.value, reader_theme::Surface::card);
            check((GetWindowLongPtrW(button.value,GWL_STYLE) & (BS_TYPEMASK | WS_TABSTOP)) == (BS_OWNERDRAW | WS_TABSTOP),
                "Theming lost button Tab navigation or failed to draw the push button");
            check((GetWindowLongPtrW(radio.value,GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON,
                "Theming converted a radio button into a push button");
            check(SendMessageW(radio.value,WM_GETDLGCODE,0,0) == radio_dialog_code
                && (radio_dialog_code & DLGC_RADIOBUTTON) != 0,
                "Radio theming changed native dialog-key handling or its role");
            wchar_t caption[64]{}; GetWindowTextW(button.value,caption,64);
            check(std::wstring(caption) == L"&Comprobar" && GetFocus() == focus, "Theming changed button text or moved keyboard focus");
            const auto screen = GetDC(parent.value); const auto memory = CreateCompatibleDC(screen);
            const auto bitmap = CreateCompatibleBitmap(screen,130,36); const auto previous_bitmap = SelectObject(memory,bitmap);
            DRAWITEMSTRUCT item{}; item.CtlType = ODT_BUTTON; item.hwndItem = button.value;
            item.hDC = memory; item.rcItem = {0,0,130,36};
            for (const bool primary : {false,true}) {
                reader_theme::primary_button(button.value,primary);
                for (const UINT state : std::array<UINT,5>{0,ODS_SELECTED,ODS_DISABLED,ODS_FOCUS,ODS_FOCUS | ODS_NOFOCUSRECT}) {
                    item.itemState = state;
                    check(reader_theme::draw_button(item), "Themed button could not draw a normal, pressed, disabled or focused state");
                }
            }
            item.hwndItem = radio.value;
            check(!reader_theme::draw_button(item), "Theme intercepted native radio-button drawing");
            const auto radio_dpi = GetDpiForWindow(radio.value);
            const auto glyph_diameter = MulDiv(16,static_cast<int>(radio_dpi ? radio_dpi : 96),96);
            const auto glyph_left = MulDiv(3,static_cast<int>(radio_dpi ? radio_dpi : 96),96);
            const auto glyph_center_x = glyph_left + glyph_diameter / 2;
            const auto glyph_center_y = 36 / 2;
            const auto raster = [&] {
                SendMessageW(radio.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            };
            const auto glyph_pixels = [&](COLORREF ink) {
                unsigned count = 0;
                for (int y = 0; y < 36; ++y)
                    for (int x = 0; x < std::min(130,glyph_left + glyph_diameter + 2); ++x)
                        if (GetPixel(memory,x,y) == ink) ++count;
                return count;
            };
            SendMessageW(radio.value,BM_SETCHECK,BST_UNCHECKED,0); raster();
            check(glyph_pixels(colors.muted) >= 24 && GetPixel(memory,glyph_center_x,glyph_center_y) == colors.card,
                "Unchecked radio did not draw a visible 16-DIP outline and empty center");
            unsigned caption_pixels = 0;
            for (int y = 0; y < 36; ++y) for (int x = glyph_left + glyph_diameter + 4; x < 130; ++x)
                if (GetPixel(memory,x,y) != colors.card) ++caption_pixels;
            check(caption_pixels >= 12,"Radio theming hid the original native caption");
            SendMessageW(radio.value,BM_SETCHECK,BST_CHECKED,0); raster();
            check(SendMessageW(radio.value,BM_GETCHECK,0,0) == BST_CHECKED
                && GetPixel(memory,glyph_center_x,glyph_center_y) == colors.accent
                && glyph_pixels(colors.accent) >= 32,"Selected radio did not preserve native state and draw its accent center");
            EnableWindow(radio.value,FALSE); raster();
            const auto disabled_ink = colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.muted;
            check(GetPixel(memory,glyph_center_x,glyph_center_y) == disabled_ink,
                "Disabled radio did not respect accessible/system disabled colors");
            EnableWindow(radio.value,TRUE);
            SendMessageW(radio.value,BM_SETCHECK,BST_CHECKED,0);
            check(SendMessageW(radio.value,BM_GETCHECK,0,0) == BST_CHECKED,
                "Radio theming broke native checked-state persistence");
            GetWindowTextW(radio.value,caption,64);
            check(std::wstring(caption) == L"Una opción"
                && (GetWindowLongPtrW(radio.value,GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON,
                "Radio drawing changed the original caption or native control type");
            SelectObject(memory,previous_bitmap); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(parent.value,screen);
        }
        std::cout << "Real RichEdit: exact source/LF, RTF safety, selection/Undo/Redo, inline spans, local Android theme and native contrast/buttons: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; FreeLibrary(module); return 1;
    }
    FreeLibrary(module); return 0;
}
