#include "presentation/windows/editing_bar.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::presentation::EditingBar;
using lgaed::windows::CodeEditor;
using lgaed::windows::EditorDocument;
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND && HIWORD(wparam) == EN_CHANGE && lparam) {
        const auto control = reinterpret_cast<HWND>(lparam);
        if (const auto editor = reinterpret_cast<CodeEditor*>(GetPropW(control,L"lgaed.test.editor")))
            editor->capture_change();
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
struct Fixture {
    HWND host = nullptr, source = nullptr, answer = nullptr, input = nullptr, prose = nullptr;
    std::unique_ptr<CodeEditor> source_editor, answer_editor, input_editor;
    std::unique_ptr<EditingBar> bar;
    unsigned changes = 0, submitted = 0;
    Fixture() {
        host = CreateWindowExW(0,L"LgaedEditingBarTestHost",L"Contextual editing QA",WS_OVERLAPPEDWINDOW,
            0,0,720,420,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(host != nullptr,"Create native host");
        const auto edit = [&](int y, bool multiline) {
            return CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                (multiline ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL : 0),
                12,y,680,multiline ? 110 : 28,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        };
        source = edit(12,true); answer = edit(130,false); input = edit(170,true);
        prose = CreateWindowExW(0,L"EDIT",L"Texto normal",WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            12,285,680,25,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(source && answer && input && prose,"Create source, answer, stdin and ordinary text");
        source_editor = std::make_unique<CodeEditor>(source,EditorDocument{},true);
        answer_editor = std::make_unique<CodeEditor>(answer,EditorDocument{},false,[&] { ++submitted; });
        input_editor = std::make_unique<CodeEditor>(input,EditorDocument{},true,std::function<void()>{},false);
        for (const auto& [control,adapter] : {
            std::pair{source,source_editor.get()},std::pair{answer,answer_editor.get()},std::pair{input,input_editor.get()}})
            SetPropW(control,L"lgaed.test.editor",reinterpret_cast<HANDLE>(adapter));
        bar = std::make_unique<EditingBar>(host,[&](HWND control) -> CodeEditor* {
            if (control == source) return source_editor.get();
            if (control == answer) return answer_editor.get();
            return nullptr;
        },[&] { ++changes; });
        bar->place(12,325,680,48);
        ShowWindow(host,SW_SHOWNOACTIVATE);
    }
    ~Fixture() {
        bar.reset();
        for (const auto control : {source,answer,input}) if (control) RemovePropW(control,L"lgaed.test.editor");
        source_editor.reset(); answer_editor.reset(); input_editor.reset();
        if (host) DestroyWindow(host);
    }
    void focus(HWND control) {
        SetFocus(control); require(GetFocus() == control,"Focus the requested real native control"); bar->refresh();
    }
    void cursor(HWND control, LONG begin, LONG end) {
        CHARRANGE range{begin,end}; SendMessageW(control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    }
    HWND shortcut(std::wstring_view label) const {
        for (auto button = GetWindow(bar->window(),GW_CHILD); button; button = GetWindow(button,GW_HWNDNEXT)) {
            wchar_t text[64]{}; GetWindowTextW(button,text,64);
            if (label == text) return button;
        }
        throw std::runtime_error("Missing contextual shortcut");
    }
    void click(std::wstring_view label) {
        const auto button = shortcut(label); SetFocus(button);
        require(GetFocus() == button,"A real shortcut may temporarily receive keyboard focus");
        SendMessageW(button,BM_CLICK,0,0);
    }
    void backspace(HWND control) { SendMessageW(control,WM_CHAR,L'\b',1); }
};
bool pump_until(const std::function<bool()>& complete) {
    const auto deadline = GetTickCount64() + 500;
    while (!complete() && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
    }
    return complete();
}
}

int main() {
    const auto rich_edit = LoadLibraryW(L"Msftedit.dll");
    if (!rich_edit) { std::cerr << "Missing Windows RichEdit\n"; return 1; }
    WNDCLASSW type{};
    type.lpfnWndProc = host_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LgaedEditingBarTestHost";
    if (!RegisterClassW(&type)) { FreeLibrary(rich_edit); return 1; }
    unsigned passed = 0, failed = 0;
    const auto test = [&](std::string_view name, const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("Only live assisted source and answer fields expose the strip",[] {
        Fixture field;
        field.focus(field.source); require(field.bar->visible() && field.changes == 1,"Source enables bar");
        field.bar->refresh(); require(field.changes == 1,"Repeated refresh does not trigger layout recursion");
        field.focus(field.input); require(!field.bar->visible() && field.changes == 2,"stdin excludes code shortcuts");
        field.focus(field.answer); require(field.bar->visible() && field.changes == 3,"Single-line code answer enables bar");
        field.focus(field.prose); require(!field.bar->visible() && field.changes == 4,"Ordinary text has no code shortcuts");
    });
    test("Toolbar TAB shares indentation completion and its native undo identity",[] {
        Fixture field; field.focus(field.source);
        for (int index = 0; index < 3; ++index) SendMessageW(field.source,WM_CHAR,L' ',1);
        field.click(L"TAB");
        require(GetFocus() == field.source,"TAB restores source focus, never stdin");
        auto document = field.source_editor->document();
        require(document.text == "    " && document.indentation_starts == std::vector<std::int64_t>{0},"Complete one TAB unit from three manual spaces");
        field.backspace(field.source); require(field.source_editor->document().text.empty(),"One Backspace removes the completed unit");
        field.click(L"Deshacer");
        require(field.source_editor->document().indentation_starts == std::vector<std::int64_t>{0},"Undo restores the same unit identity");
        field.click(L"Rehacer"); require(field.source_editor->document().text.empty(),"Redo restores deletion");
    });
    test("Manual four spaces never become a TAB through toolbar undo",[] {
        Fixture field; field.focus(field.source);
        for (int index = 0; index < 4; ++index) SendMessageW(field.source,WM_CHAR,L' ',1);
        field.backspace(field.source); require(field.source_editor->document().text == "   ","Manual space deletes individually");
        field.click(L"Deshacer");
        require(field.source_editor->document().text == "    " && field.source_editor->document().indentation_starts.empty(),"Undo retains manual origin");
        field.backspace(field.source); require(field.source_editor->document().text == "   ","Manual origin remains after undo");
    });
    test("Pairs wrap selections through CodeEditor in single and multiple lines",[] {
        Fixture field;
        for (const auto control : {field.source,field.answer}) {
            auto& adapter = control == field.source ? field.source_editor : field.answer_editor;
            adapter->reset({"abc",{},0,3}); field.focus(control);
            field.click(L"( )");
            auto document = adapter->document();
            require(document.text == "(abc)" && document.selection_start == 1 && document.selection_end == 4,"Wrap the exact selected source once");
            require(GetFocus() == control,"Restore the original assisted field");
            field.click(L"Deshacer"); document = adapter->document();
            require(document.text == "abc" && document.selection_start == 0 && document.selection_end == 3,"Undo restores selection and source");
            field.click(L"Rehacer"); require(adapter->document().text == "(abc)","Redo uses the original editor history");
            for (const auto& [label,expected] : {
                std::pair{L"{ }","{}"},std::pair{L"[ ]","[]"},std::pair{L"\" \"","\"\""},std::pair{L"' '","''"}}) {
                adapter->reset({}); field.focus(control); field.click(label); document = adapter->document();
                require(document.text == expected && document.selection_start == 1 && document.selection_end == 1,"Use automatic pairs and place caret inside");
            }
        }
    });
    test("Punctuation is literal and the bar never submits a single-line answer",[] {
        Fixture field; field.focus(field.answer);
        for (const auto label : {L";",L":",L"<",L">"}) field.click(label);
        require(field.answer_editor->document().text == ";:<>","Insert precisely the selected symbols");
        require(field.submitted == 0,"Symbols do not trigger Comprobar");
        SendMessageW(field.answer,WM_KEYDOWN,VK_RETURN,1);
        SendMessageW(field.answer,WM_CHAR,L'\r',1);
        SendMessageW(field.answer,WM_KEYUP,VK_RETURN,1);
        require(field.submitted == 1 && field.answer_editor->document().text == ";:<>","Existing Enter submission remains single and has no LF");
        field.focus(field.input);
        // Native RichEdit handles multiline Return during WM_KEYDOWN. A lone
        // WM_CHAR is not the physical key sequence used by the existing editor.
        SendMessageW(field.input,WM_KEYDOWN,VK_RETURN,1);
        SendMessageW(field.input,WM_CHAR,L'\r',1);
        SendMessageW(field.input,WM_KEYUP,VK_RETURN,1);
        const auto input = field.input_editor->document();
        std::string input_bytes;
        for (const auto byte : input.text) input_bytes += std::to_string(static_cast<unsigned char>(byte)) + ',';
        require(input.text == "\n" && !field.bar->visible(),"Plain multiline stdin retains its own Enter behavior: bytes=" + input_bytes
            + " cursor=" + std::to_string(input.selection_start) + " bar=" + std::to_string(field.bar->visible()));
    });
    test("Destroyed editors and recycled controls cannot receive stale operations",[] {
        Fixture field; field.focus(field.source);
        const auto button = field.shortcut(L"TAB"); SetFocus(button);
        RemovePropW(field.source,L"lgaed.test.editor"); field.source_editor.reset(); DestroyWindow(field.source); field.source = nullptr;
        field.bar->refresh(); require(!field.bar->visible(),"Hide after owner removes the source");
        SendMessageW(button,BM_CLICK,0,0);
        require(field.input_editor->document().text.empty() && field.answer_editor->document().text.empty(),"Stale button cannot edit a different field");
        field.focus(field.answer); field.click(L"TAB");
        require(field.answer_editor->document().text == "    ","The next live answer resolves independently");
    });
    test("Timer follows native focus without stealing focus or changing documents",[] {
        Fixture field;
        SetFocus(field.source);
        require(pump_until([&] { return field.bar->visible(); }),"Timer notices a newly focused source");
        require(GetFocus() == field.source && field.source_editor->document().text.empty(),"Observation never inserts or moves focus");
        SetFocus(field.input);
        require(pump_until([&] { return !field.bar->visible(); }),"Timer hides when focus enters stdin");
        require(GetFocus() == field.input && field.input_editor->document().text.empty(),"stdin contents and focus remain intact");
    });
    UnregisterClassW(type.lpszClassName,type.hInstance); FreeLibrary(rich_edit);
    std::cout << passed << " contextual editing groups passed; " << failed << " failed\n";
    return failed ? 1 : 0;
}
