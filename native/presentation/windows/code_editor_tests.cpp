#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/rich_text.hpp"

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

struct ChangeProbe {
    bool streaming = false;
    unsigned notifications = 0;
    unsigned notifications_during_stream = 0;
    bool callback_failed = false;
    EditorDocument observed;
    std::string visible;
};

LRESULT CALLBACK stream_probe(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR data) {
    auto& probe = *reinterpret_cast<ChangeProbe*>(data);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, stream_probe, 2);
    if (message != EM_STREAMIN) return DefSubclassProc(window, message, wparam, lparam);
    const auto previous = probe.streaming;
    probe.streaming = true;
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    probe.streaming = previous;
    return result;
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND && HIWORD(wparam) == EN_CHANGE && lparam) {
        const auto control = reinterpret_cast<HWND>(lparam);
        const auto probe = reinterpret_cast<ChangeProbe*>(GetPropW(control, L"lgaed.test.change-probe"));
        // Fail the unsafe notification without hanging this regression inside
        // RichEdit. On a complete snapshot, run the parent's real read/format path.
        if (probe && probe->streaming) { ++probe->notifications_during_stream; return 0; }
        if (const auto editor = reinterpret_cast<CodeEditor*>(GetPropW(control, L"lgaed.test.editor"))) {
            try {
                editor->capture_change();
                if (probe) {
                    ++probe->notifications;
                    probe->observed = editor->document();
                    probe->visible = lgaed::presentation::edit_text(control);
                    lgaed::presentation::style_cpp(control);
                }
            } catch (...) { if (probe) probe->callback_failed = true; }
        } else if (probe) ++probe->notifications;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

struct Fixture {
    HWND host = nullptr;
    HWND edit = nullptr;
    HWND input = nullptr;
    std::unique_ptr<CodeEditor> adapter;

    Fixture(EditorDocument document = {}, bool multiline = true,
        std::function<void()> submit = {}, bool code_assistance = true, ChangeProbe* probe = nullptr) {
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
        if (probe) {
            require(SetPropW(edit, L"lgaed.test.change-probe", reinterpret_cast<HANDLE>(probe)) != FALSE,
                "Attach parent-change observer");
            require(SetWindowSubclass(edit, stream_probe, 2, reinterpret_cast<DWORD_PTR>(probe)) != FALSE,
                "Observe real native stream boundaries");
        }
        adapter = std::make_unique<CodeEditor>(edit, std::move(document), multiline,
            std::move(submit), code_assistance);
        SetPropW(edit, L"lgaed.test.editor", reinterpret_cast<HANDLE>(adapter.get()));
    }
    ~Fixture() {
        RemovePropW(edit, L"lgaed.test.change-probe");
        if (edit) RemoveWindowSubclass(edit, stream_probe, 2);
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

    test("Snapshot notifications let the parent read and recolor only after the stream completes", [] {
        const std::string source = "\r\n    std::cout << \"Zona inicial\";\r\n    return 0;\r\n";
        auto original = predefined_code_document(source);
        original.selection_start = original.selection_end = 6;
        ChangeProbe probe;
        Fixture field(original, true, {}, true, &probe);
        require(probe.notifications == 0 && probe.notifications_during_stream == 0,
            "Initial loading must not notify a parent before its adapter is assigned");
        const auto events = SendMessageW(field.edit, EM_GETEVENTMASK, 0, 0) |
            static_cast<LRESULT>(ENM_KEYEVENTS | ENM_SELCHANGE);
        SendMessageW(field.edit, EM_SETEVENTMASK, 0, events);
        field.cursor(5);
        field.key(VK_BACK, L'\b');
        const auto edited = field.adapter->document();
        require(probe.notifications == 1 && probe.notifications_during_stream == 0 && !probe.callback_failed,
            "A completed unit deletion sends exactly one safe parent notification");
        require(probe.observed.text == edited.text && probe.observed.selection_start == 1 &&
            probe.observed.indentation_starts == edited.indentation_starts,
            "The parent sees completed text, selection and indentation identity");
        require(probe.visible == edited.text, "The native text read during notification is complete");
        for (unsigned round = 0; round != 4; ++round) {
            SendMessageW(field.edit, WM_UNDO, 0, 0);
            require(probe.notifications == 2 + round * 2 && probe.notifications_during_stream == 0 &&
                !probe.callback_failed, "Undo notifies once after all streaming and formatting finish");
            require(probe.observed.text == source && probe.observed.indentation_starts == original.indentation_starts &&
                probe.observed.selection_start == 6, "Undo observers see exact CRLF, origin and original caret");
            SendMessageW(field.edit, EM_REDO, 0, 0);
            require(probe.notifications == 3 + round * 2 && probe.observed.text == edited.text &&
                probe.observed.indentation_starts == edited.indentation_starts &&
                probe.observed.selection_start == edited.selection_start, "Redo remains identity aware after syntax styling");
        }
        require(SendMessageW(field.edit, EM_GETEVENTMASK, 0, 0) == events,
            "Snapshot loading and parent syntax styling preserve the complete event mask");
        field.adapter->reset(original);
        require(probe.notifications == 9 && probe.notifications_during_stream == 0,
            "Owner-driven reset is silent even after editing and undo");
    });

    test("A snapshot load exception restores notification masking and edit capture", [] {
        ChangeProbe probe;
        Fixture field({"x", {}, 1, 1}, true, {}, true, &probe);
        const auto events = SendMessageW(field.edit, EM_GETEVENTMASK, 0, 0) |
            static_cast<LRESULT>(ENM_KEYEVENTS | ENM_SELCHANGE);
        SendMessageW(field.edit, EM_SETEVENTMASK, 0, events);
        bool rejected = false;
        try { field.adapter->reset({std::string("a\0b", 3), {}, 0, 0}); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && SendMessageW(field.edit, EM_GETEVENTMASK, 0, 0) == events,
            "Even a rejected plain-text load restores the exact event mask");
        require(probe.notifications == 0 && probe.notifications_during_stream == 0,
            "An incomplete or rejected snapshot never notifies the parent");
        field.adapter->reset({"x", {}, 1, 1});
        field.character(L'y');
        require(field.adapter->document().text == "xy" && !probe.callback_failed,
            "The applying guard is restored so subsequent native edits are captured");
        field.adapter->undo();
        require(field.adapter->document().text == "x" && probe.observed.text == "x",
            "Undo remains observable and functional after a rejected load");
    });

    test("Select All and native replacement never persist the implicit final paragraph mark", [] {
        for (const std::string source : {"#include <iostream>\nint main() {\n    std::cout << \"Zona segura\";\n    return 0;\n}\n",
                                        "    return 0;", "\r\n    std::cout << \"🧪\";\r    return 0;\r\n"}) {
            const auto original = predefined_code_document(source);
            ChangeProbe probe;
            Fixture field(original, true, {}, true, &probe);
            const auto length = lgaed::presentation::wide(source).size();
            const auto visible_length = lgaed::presentation::wide(lgaed::presentation::edit_text(field.edit)).size();
            SendMessageW(field.edit, EM_SETSEL, 0, -1);
            CHARRANGE native{};
            SendMessageW(field.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&native));
            require(native.cpMin == 0 && native.cpMax == static_cast<LONG>(visible_length + 1),
                "Native Select All includes RichEdit's synthetic final paragraph mark");
            const auto selected = field.adapter->document();
            require(selected.text == source && selected.selection_start == 0 &&
                selected.selection_end == static_cast<std::int64_t>(length),
                "The document maps only the synthetic boundary while preserving original CRLF and Unicode");
            const auto replacement = lgaed::presentation::wide(source + "// edición después del acierto\n");
            SendMessageW(field.edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
            const auto edited = field.adapter->document();
            require(edited.text != source && edited.selection_start == edited.selection_end &&
                edited.selection_end == static_cast<std::int64_t>(lgaed::presentation::wide(edited.text).size()),
                "A native replacement captures its real final insertion position");
            SendMessageW(field.edit, WM_UNDO, 0, 0);
            const auto undone = field.adapter->document();
            require(undone.text == source && undone.indentation_starts == original.indentation_starts &&
                undone.selection_start == 0 && undone.selection_end == static_cast<std::int64_t>(length),
                "Undo restores source identity and a full selection of source characters, not the hidden CR");
            require(!probe.callback_failed && probe.notifications_during_stream == 0 &&
                probe.observed.selection_end == static_cast<std::int64_t>(length),
                "The actual parent callback observes valid source offsets during Undo");
            SendMessageW(field.edit, EM_REDO, 0, 0);
            require(field.adapter->document().text == edited.text &&
                field.adapter->document().selection_end == edited.selection_end,
                "Redo retains the exact replacement and valid cursor");
            field.adapter->undo();
            field.key(VK_BACK, L'\b');
            require(field.adapter->document().text.empty(), "Deleting the restored full selection removes exactly the source");
            field.adapter->undo();
            require(field.adapter->document().text == source &&
                field.adapter->document().indentation_starts == original.indentation_starts,
                "Undo after full deletion still restores the complete original indentation identity");
        }
    });

    UnregisterClassW(type.lpszClassName, type.hInstance);
    FreeLibrary(rich_edit);
    std::cout << "Native code editor: " << passed << " passed, " << failed << " failed.\n";
    return failed ? 1 : 0;
}
