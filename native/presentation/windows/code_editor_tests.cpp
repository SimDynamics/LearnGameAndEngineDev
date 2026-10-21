#include "presentation/windows/code_editor.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <functional>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed::windows;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND && HIWORD(wparam) == EN_CHANGE && lparam) {
        if (const auto editor = reinterpret_cast<CodeEditor*>(GetPropW(
                reinterpret_cast<HWND>(lparam), L"lgaed.test.editor"))) editor->capture_change();
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

struct Fixture {
    HWND host = nullptr;
    HWND edit = nullptr;
    HWND input = nullptr;
    std::unique_ptr<CodeEditor> adapter;

    Fixture(EditorDocument document = {}, bool multiline = true,
        std::function<void()> submit = {}, bool code_assistance = true) {
        host = CreateWindowExW(0, L"LgaedCodeEditorTestHost", L"Hidden editor QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 640, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!host) throw std::runtime_error("Cannot create editor test host");
        const auto style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
            (multiline ? ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_AUTOVSCROLL : 0);
        edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", style, 0, 0, 600, 200,
            host, reinterpret_cast<HMENU>(100), GetModuleHandleW(nullptr), nullptr);
        input = CreateWindowExW(0, L"EDIT", L"Entrada intacta", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 210, 600, 25, host, reinterpret_cast<HMENU>(101), GetModuleHandleW(nullptr), nullptr);
        if (!edit || !input) throw std::runtime_error("Cannot create native edit controls");
        adapter = std::make_unique<CodeEditor>(edit, std::move(document), multiline,
            std::move(submit), code_assistance);
        SetPropW(edit, L"lgaed.test.editor", reinterpret_cast<HANDLE>(adapter.get()));
    }
    ~Fixture() {
        RemovePropW(edit, L"lgaed.test.editor");
        adapter.reset();
        if (host) DestroyWindow(host);
    }
    void cursor(LONG start, LONG end = -1) {
        CHARRANGE range{start, end < 0 ? start : end};
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    }
    void character(wchar_t value) { SendMessageW(edit, WM_CHAR, value, 1); }
    void key(UINT value, wchar_t character_value = 0) {
        SendMessageW(edit, WM_KEYDOWN, value, 1);
        if (character_value) character(character_value);
        SendMessageW(edit, WM_KEYUP, value, 1);
    }
};

} // namespace

int main() {
    const auto rich_edit = LoadLibraryW(L"Msftedit.dll");
    if (!rich_edit) { std::cerr << "Missing Windows RichEdit\n"; return 1; }
    WNDCLASSW type{};
    type.lpfnWndProc = host_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"LgaedCodeEditorTestHost";
    if (!RegisterClassW(&type)) { FreeLibrary(rich_edit); return 1; }
    unsigned passed = 0;
    unsigned failed = 0;
    const auto test = [&](std::string_view name, const std::function<void()>& action) {
        try {
            action();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    test("Predefined indentation has origin and preserves source LF tabs and literals", [] {
        const std::string source = "#include <iostream>\n\nint main() {\n    std::cout << \"A  B\";\n    return 0;\n}\n";
        const auto predefined = predefined_code_document(source);
        require(predefined.indentation_starts.size() == 2, "Both predefined indentations");
        Fixture field(predefined);
        require(field.adapter->document().text == source, "Do not rewrite code or final LF");
        require(field.adapter->document().indentation_starts == predefined.indentation_starts,
            "Preserve original identities");
    });

    test("Physical Tab stays in editor even with a following stdin control", [] {
        Fixture field;
        SetFocus(field.edit);
        require(GetFocus() == field.edit, "Focus native editor");
        MSG tab{field.edit, WM_KEYDOWN, VK_TAB, 1};
        // IsDialogMessage may dispatch a wanted key to the control and return
        // TRUE. Its result is not evidence that focus navigation occurred.
        if (!IsDialogMessageW(field.host, &tab)) {
            TranslateMessage(&tab);
            DispatchMessageW(&tab);
        }
        MSG character{};
        while (PeekMessageW(&character, field.edit, WM_CHAR, WM_CHAR, PM_REMOVE)) DispatchMessageW(&character);
        SendMessageW(field.edit, WM_KEYUP, VK_TAB, 1);
        require(field.adapter->document().text == "    ", "One physical key creates exactly one unit");
        require(field.adapter->document().indentation_starts == std::vector<std::int64_t>{0}, "TAB origin");
        require(GetFocus() == field.edit, "Never move focus to stdin");
        wchar_t input[40]{};
        GetWindowTextW(field.input, input, 40);
        require(std::wstring(input) == L"Entrada intacta", "stdin unchanged");
    });

    test("Physical and toolbar Tab share completion and complete-unit deletion", [] {
        for (int spaces = 0; spaces < 4; ++spaces) {
            for (bool physical : {false, true}) {
                Fixture field({std::string(static_cast<std::size_t>(spaces), ' '), {}, spaces, spaces});
                if (physical) field.key(VK_TAB, L'\t'); else field.adapter->insert_tab();
                auto document = field.adapter->document();
                require(document.text == "    " && document.indentation_starts == std::vector<std::int64_t>{0},
                    "Complete only the current four-column unit");
                require(document.selection_start == 4 && document.selection_end == 4, "Cursor at tab stop");
                field.key(VK_BACK, L'\b');
                require(field.adapter->document().text.empty(), "Backspace removes all completed spaces");
            }
        }
    });

    test("Four manually typed spaces never acquire TAB identity", [] {
        Fixture field;
        for (int index = 0; index != 4; ++index) field.character(L' ');
        require(field.adapter->document().indentation_starts.empty(), "Manual origin stays manual");
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "   ", "Delete only one manual space");
        field.adapter->undo();
        require(field.adapter->document().text == "    " && field.adapter->document().indentation_starts.empty(),
            "Undo must not infer a new TAB");
    });

    test("Predefined Backspace identity survives undo redo and another Backspace", [] {
        Fixture field(predefined_code_document("    std::cout;\n    return 0;\n"));
        field.cursor(4);
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text.starts_with("std::cout"), "Delete predefined unit");
        SendMessageW(field.edit, WM_UNDO, 0, 0);
        require(field.adapter->document().indentation_starts.front() == 0, "Undo restores identity");
        require(field.adapter->document().selection_start == 4, "Undo restores caret");
        SendMessageW(field.edit, EM_REDO, 0, 0);
        require(field.adapter->document().text.starts_with("std::cout"), "Redo original deletion");
        SendMessageW(field.edit, EM_UNDO, 0, 0);
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text.starts_with("std::cout"), "Restored unit deletes identically");
        const auto second = static_cast<LONG>(field.adapter->document().text.find("return"));
        field.cursor(second);
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "std::cout;\nreturn 0;\n", "Second indentation remains a unit");
    });

    test("TAB completion undo restores previous manual origin", [] {
        Fixture field({"   ", {}, 3, 3});
        field.adapter->insert_tab();
        field.adapter->undo();
        require(field.adapter->document().text == "   " && field.adapter->document().indentation_starts.empty(),
            "Undo removes TAB origin but keeps earlier manual spaces");
        field.adapter->redo();
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text.empty(), "Redo restores completed identity");
    });

    test("TAB and manual trailing spaces retain independent deletion units", [] {
        Fixture field;
        field.adapter->insert_tab();
        for (int index = 0; index != 3; ++index) field.character(L' ');
        for (int index = 0; index != 3; ++index) {
            field.key(VK_BACK, L'\b');
            require(field.adapter->document().text.size() == static_cast<std::size_t>(6 - index),
                "Remove manual spaces individually before TAB: iteration=" + std::to_string(index) +
                " actual=" + std::to_string(field.adapter->document().text.size()) +
                " cursor=" + std::to_string(field.adapter->document().selection_start));
        }
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text.empty(), "Then remove original TAB");
    });

    test("Single-line Enter submits once and multiline Enter creates LF", [] {
        int submitted = 0;
        Fixture single({"<<", {}, 2, 2}, false, [&] { ++submitted; });
        single.key(VK_RETURN, L'\r');
        require(submitted == 1 && single.adapter->document().text == "<<", "Submit without newline");
        Fixture multiple({"abc", {}, 3, 3}, true, [&] { ++submitted; });
        multiple.key(VK_RETURN, L'\r');
        require(multiple.adapter->document().text == "abc\n", "Native multiline Enter preserved");
        require(submitted == 1, "Multiline editor does not submit");
    });

    test("Single-line and multiline code inputs share pairs and exact cursor placement", [] {
        for (const auto multiline : {false, true}) {
            Fixture field({}, multiline);
            field.character(L'(');
            require(field.adapter->document().text == "()" && field.adapter->document().selection_start == 1,
                "Create parentheses around cursor");
            field.character(L'"');
            require(field.adapter->document().text == "(\"\")" && field.adapter->document().selection_start == 2,
                "Create quote pair in either input kind");
            field.character(L'x');
            field.character(L'"');
            field.character(L')');
            require(field.adapter->document().text == "(\"x\")" && field.adapter->document().selection_start == 5,
                "Skip existing closing delimiters without duplicates");
        }
    });

    test("Selections wrapped in pairs and undo restore selection", [] {
        Fixture field({"abc", {}, 0, 3});
        field.character(L'[');
        auto document = field.adapter->document();
        require(document.text == "[abc]" && document.selection_start == 1 && document.selection_end == 4,
            "Wrap selected text without losing selection");
        field.adapter->undo();
        document = field.adapter->document();
        require(document.text == "abc" && document.selection_start == 0 && document.selection_end == 3,
            "Undo restores full selection");
    });

    test("Comments and literals do not receive spurious auto pairs", [] {
        Fixture comment({"// comentario ", {}, 14, 14});
        comment.character(L'(');
        require(comment.adapter->document().text == "// comentario (", "No extra delimiter in comment");
        Fixture literal({"\"a\\\"", {}, 3, 3});
        literal.character(L'"');
        require(literal.adapter->document().text == "\"a\\\"\"", "An escaped quote is inserted instead of skipped");
    });

    test("A restored manual draft is never recognized as predefined indentation", [] {
        Fixture field({"    x\n", {}, 4, 4});
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "   x\n", "Draft without metadata stays manual");
        field.adapter->reset(predefined_code_document("    x\n"));
        field.cursor(4);
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "x\n", "Explicit Reset recovers known original identity");
    });

    test("Edits among identical spaces preserve their actual origin positions", [] {
        Fixture field(predefined_code_document("    x"));
        field.cursor(0);
        field.character(L' ');
        require(field.adapter->document().text == "     x" &&
            field.adapter->document().indentation_starts == std::vector<std::int64_t>{1},
            "A new manual leading space shifts rather than impersonates original unit");
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().indentation_starts == std::vector<std::int64_t>{0},
            "Deleting the manual space restores the original unit location: length=" +
            std::to_string(field.adapter->document().text.size()) + " cursor=" +
            std::to_string(field.adapter->document().selection_start));
        field.cursor(4);
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "x", "Original unit still deletes as a unit");
    });

    test("UTF16 indentation offsets preserve non-BMP text and existing tabs", [] {
        const std::string source = "😀\n    x\n\ty\n";
        const auto document = predefined_code_document(source);
        require(document.indentation_starts == std::vector<std::int64_t>{3}, "Offsets count UTF16 code units");
        Fixture field(document);
        require(field.adapter->document().text == source, "Unicode and literal tabs retained exactly");
        field.cursor(7);
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "😀\nx\n\ty\n", "Delete known spaces without corrupting Unicode");
    });

    test("Existing literal tabs use four-column stops and completed spaces stay separate", [] {
        Fixture field({"\tx\n    x\n", {}, 0, 0});
        const auto font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            FIXED_PITCH, L"Consolas");
        require(font != nullptr, "Create fixed width font");
        SendMessageW(field.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        POINT first{}, second{};
        SendMessageW(field.edit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&first), 1);
        SendMessageW(field.edit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&second), 7);
        require(std::abs(first.x - second.x) <= 1, "A literal tab reaches the same column as four spaces: tab=" +
            std::to_string(first.x) + " spaces=" + std::to_string(second.x));
        field.adapter->reset({"\t   ", {}, 4, 4});
        field.adapter->insert_tab();
        require(field.adapter->document().text == "\t    ", "Tab then three spaces completes next stop");
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text == "\t", "Delete completed unit while keeping prior literal tab");
        SendMessageW(field.edit, WM_SETFONT, 0, FALSE);
        DeleteObject(font);
    });

    test("Undo restores vertical scroll as well as text selection and origin", [] {
        std::string source;
        for (int index = 0; index != 80; ++index) source += "linea\n";
        Fixture field({source, {}, 2, 2});
        POINT requested{0, 120};
        SendMessageW(field.edit, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&requested));
        POINT before{};
        SendMessageW(field.edit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&before));
        require(before.y > 0, "Real native vertical scroll available");
        field.character(L'x');
        field.adapter->undo();
        POINT after{};
        SendMessageW(field.edit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
        require(field.adapter->document().text == source && field.adapter->document().selection_start == 2,
            "Restore original content and caret");
        require(after.y == before.y, "Restore exact prior vertical scroll");
    });

    test("Ctrl Z and Ctrl Y use the same identity-aware history", [] {
        Fixture field(predefined_code_document("    x"));
        field.cursor(4);
        field.key(VK_BACK, L'\b');
        struct KeyboardState {
            BYTE original[256]{};
            KeyboardState() { GetKeyboardState(original); }
            ~KeyboardState() { SetKeyboardState(original); }
        } saved;
        BYTE keys[256]{};
        GetKeyboardState(keys);
        keys[VK_CONTROL] |= 0x80;
        require(SetKeyboardState(keys) != FALSE, "Set test thread control modifier");
        field.key(L'Z', static_cast<wchar_t>(0x1A));
        require(field.adapter->document().text == "    x" &&
            field.adapter->document().indentation_starts == std::vector<std::int64_t>{0},
            "Ctrl Z restores predefined identity");
        field.key(L'Y', static_cast<wchar_t>(0x19));
        require(field.adapter->document().text == "x", "Ctrl Y repeats complete deletion");
    });

    test("Prediction data keeps literal characters and ordinary Tab navigation", [] {
        int submitted = 0;
        Fixture field({}, false, [&] { ++submitted; }, false);
        for (const auto character : L"\"([]) ") {
            if (character) field.character(character);
        }
        require(field.adapter->document().text == "\"([]) ", "Never close or alter predicted output data");
        field.adapter->undo();
        require(field.adapter->document().text == "\"([])", "Data still has undo");
        field.adapter->redo();
        field.key(VK_RETURN, L'\r');
        require(submitted == 1 && field.adapter->document().text == "\"([]) ", "Enter submits exact data");
        SetFocus(field.edit);
        MSG tab{field.edit, WM_KEYDOWN, VK_TAB, 1};
        require(IsDialogMessageW(field.host, &tab) != FALSE, "Prediction Tab is ordinary navigation");
        require(GetFocus() == field.input, "Tab can move from prediction to next field");
        require(field.adapter->document().text == "\"([]) ", "Tab does not insert completion into data");
    });

    test("Imported CRLF and CR source survives display navigation and undo byte for byte", [] {
        const std::string source = "\r\n    std::cout;\r    return 0;\n\t x\r\n";
        auto document = predefined_code_document(source);
        require(document.text == source && document.indentation_starts.front() == 2,
            "Original predefined offsets count both CRLF units");
        document.selection_start = document.selection_end = 6;
        const auto original_units = document.indentation_starts;
        Fixture field(document);
        auto shown = field.adapter->document();
        require(shown.text == source && shown.indentation_starts == original_units && shown.selection_start == 6,
            "Displaying and saving an untouched source preserves bytes and identity");
        CHARRANGE caret{};
        SendMessageW(field.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&caret));
        require(caret.cpMin == 5, "RichEdit selection uses the normalized visible LF position");
        field.cursor(1);
        shown = field.adapter->document();
        require(shown.text == source && shown.selection_start == 2 && shown.indentation_starts == original_units,
            "Navigating maps the visible position back to original CRLF offsets");
        field.cursor(5);
        field.key(VK_BACK, L'\b');
        shown = field.adapter->document();
        require(shown.text == "\nstd::cout;\n    return 0;\n\t x\n",
            "An actual native edit produces the current LF representation");
        field.adapter->undo();
        shown = field.adapter->document();
        require(shown.text == source && shown.indentation_starts == original_units && shown.selection_start == 6,
            "Undo restores exact CRLF CR LF tabs origin and raw caret offset");
        field.key(VK_BACK, L'\b');
        require(field.adapter->document().text.starts_with("\nstd::cout"),
            "The restored indentation still deletes as one unit");
    });

    test("RTF-looking drafts remain exact plain text during load editing and undo", [] {
        for (const auto assistance : {true, false}) {
            for (const std::string prefix : {"{\\rtf1\\ansi {\\b fuente} literal}",
                                            "{urtf1\\ansi {\\b fuente} literal}"}) {
                const auto source = prefix + "\r\n\t  fin\r";
                Fixture field({source, {}, 4, 11}, true, {}, assistance);
                auto document = field.adapter->document();
                require(document.text == source && document.selection_start == 4 && document.selection_end == 11,
                    "A draft header must not invoke the RichEdit RTF parser or alter its selection");
                field.cursor(0);
                field.character(L'x');
                require(field.adapter->document().text.starts_with("x" + prefix),
                    "Editing must keep every original backslash and brace literal");
                field.adapter->undo();
                document = field.adapter->document();
                require(document.text == source && document.selection_start == 0 && document.selection_end == 0,
                    "Undo restores exact RTF-looking text, original CRLF/CR/TAB and prior caret");
                field.adapter->redo();
                require(field.adapter->document().text.starts_with("x" + prefix),
                    "Redo cannot reinterpret a draft as formatted RTF");
            }
        }
    });

    UnregisterClassW(type.lpszClassName, type.hInstance);
    FreeLibrary(rich_edit);
    std::cout << "Native code editor: " << passed << " passed, " << failed << " failed.\n";
    return failed ? 1 : 0;
}
