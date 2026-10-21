#include "presentation/windows/rich_text.hpp"

#include <richedit.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Window { HWND value; ~Window() { if (value) DestroyWindow(value); } };
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
        std::cout << "Real RichEdit: exact source/LF, RTF safety, formatting selection and undo/redo, inline code, Unicode: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; FreeLibrary(module); return 1;
    }
    FreeLibrary(module); return 0;
}
