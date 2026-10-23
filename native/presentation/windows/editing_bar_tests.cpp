#include "presentation/windows/editing_bar.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "core/lesson_state.hpp"

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
        bar->place(12,310,680,bar->logical_height());
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
        struct Search{std::wstring_view label;HWND result=nullptr;} search{label};
        EnumChildWindows(bar->window(),[](HWND item,LPARAM context)->BOOL{auto& current=*reinterpret_cast<Search*>(context);
            wchar_t type[32]{};GetClassNameW(item,type,32);if(_wcsicmp(type,L"BUTTON")!=0)return TRUE;
            std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(item,text.data(),static_cast<int>(text.size()))));
            if(current.label==text){current.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
        if(search.result)return search.result;
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
            field.click(L"(");
            auto document = adapter->document();
            require(document.text == "(abc)" && document.selection_start == 1 && document.selection_end == 4,"Wrap the exact selected source once");
            require(GetFocus() == control,"Restore the original assisted field");
            field.click(L"Deshacer"); document = adapter->document();
            require(document.text == "abc" && document.selection_start == 0 && document.selection_end == 3,"Undo restores selection and source");
            field.click(L"Rehacer"); require(adapter->document().text == "(abc)","Redo uses the original editor history");
            for (const auto& [label,expected] : {
                std::pair{L"{","{}"},std::pair{L"[","[]"},std::pair{L"\"","\"\""},std::pair{L"'","''"}}) {
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
    test("Original 44 defaults and personal JSON arrays retain exact order and content",[] {
        lgaed::state::StateData data;const auto defaults=lgaed::presentation::parse_state_shortcuts(data);
        require(defaults.size()==44&&defaults.front()=="("&&defaults.back()==".*"&&defaults[25]=="::"&&defaults[26]=="<<"&&defaults[41]=="...",
            "Native defaults lost the original Android technical shortcut list");
        data.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=std::string(R"json([" std::cout ","<<","<<","", ")"])json");const auto before=data;
        require(lgaed::presentation::parse_state_shortcuts(data)==std::vector<std::string>({" std::cout ","<<","<<","",")"})&&data==before,
            "Personal shortcuts were trimmed, deduplicated, extended or persisted while being read");
        data.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=std::string("[]");
        require(lgaed::presentation::parse_state_shortcuts(data).empty(),"An intentional empty personal list was replaced by defaults");
        data.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=std::string("{bad JSON");
        require(lgaed::presentation::parse_state_shortcuts(data)==defaults,"A malformed stored array did not use the existing default policy");
    });
    test("Shortcut viewport scrolls naturally while TAB Undo and Redo stay fixed",[] {
        Fixture field;field.focus(field.source);field.bar->place(12,310,400,field.bar->logical_height());
        const auto first=field.shortcut(L"("),last=field.shortcut(L".*"),tab=field.shortcut(L"TAB"),viewport=GetParent(first);
        RECT first_before{},tab_before{},last_after{},viewport_area{};GetWindowRect(first,&first_before);GetWindowRect(tab,&tab_before);
        SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS};require(GetScrollInfo(viewport,SB_HORZ,&scroll)&&scroll.nPage>0&&scroll.nMax>=static_cast<int>(scroll.nPage),
            "The configured list has no real horizontal scrolling range");
        require(first_before.right-first_before.left>=MulDiv(36,static_cast<int>(GetDpiForWindow(viewport)),96),"All 44 shortcuts were compressed into tiny buttons");
        SendMessageW(viewport,WM_HSCROLL,SB_RIGHT,0);GetScrollInfo(viewport,SB_HORZ,&scroll);GetWindowRect(last,&last_after);MapWindowPoints(nullptr,viewport,reinterpret_cast<POINT*>(&last_after),2);GetClientRect(viewport,&viewport_area);
        RECT tab_after{};GetWindowRect(tab,&tab_after);
        require(scroll.nPos>0&&last_after.left>=0&&last_after.right<=viewport_area.right&&EqualRect(&tab_before,&tab_after),
            "Horizontal scrolling did not expose the final shortcut independently of fixed controls");
        require(field.source_editor->document().text.empty()&&GetFocus()==field.source,"Scrolling the shortcut list edited code or moved its focus");
        require(!(GetWindowLongPtrW(viewport,GWL_STYLE)&WS_HSCROLL),"The Android-style shortcut strip still exposes a native scrollbar and reserves its height");
        field.bar->set_shortcuts({});require(field.shortcut(L"TAB")&&field.shortcut(L"Deshacer")&&field.shortcut(L"Rehacer"),"An empty personal list removed fixed editing actions");
    });
    test("Shortcut geometry and ink match Android while history icons remain borderless",[]{
        Fixture field;field.focus(field.source);const auto tab=field.shortcut(L"TAB"),symbol=field.shortcut(L"("),undo=field.shortcut(L"Deshacer");
        const int dpi=static_cast<int>(GetDpiForWindow(tab));const auto px=[dpi](int size){return MulDiv(size,dpi,96);};
        require(field.bar->logical_height()==52,"The editing bar does not preserve Android's normal52dp height");
        LOGFONTW font{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(symbol,WM_GETFONT,0,0)),sizeof(font),&font);
        require(-font.lfHeight==px(16)&&std::wstring(font.lfFaceName)==lgaed::presentation::reader_theme::code_font_face(),"Symbol shortcuts lost Android's monospace16sp typography");
        RECT tab_bounds{},symbol_bounds{};GetClientRect(tab,&tab_bounds);GetClientRect(symbol,&symbol_bounds);
        require(tab_bounds.right==px(56)&&tab_bounds.bottom==px(40)&&symbol_bounds.right>=px(40)&&symbol_bounds.bottom==px(40),
            "The TAB/shortcut keys do not match Android's56/40dp widths and40dp height");
        if(!lgaed::presentation::reader_theme::palette(tab).high_contrast){
            const auto screen=GetDC(tab),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,px(60),px(40));ReleaseDC(tab,screen);
            require(dc&&bitmap,"Create shortcut regression drawing surface");const auto old=SelectObject(dc,bitmap);
            DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.hDC=dc;draw.hwndItem=tab;draw.CtlID=9000;draw.rcItem=tab_bounds;
            require(SendMessageW(field.bar->window(),WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw))!=FALSE&&GetPixel(dc,px(5),px(20))==RGB(35,35,35),
                "The shortcut key does not use Android's #232323 background");
            GetClientRect(undo,&draw.rcItem);draw.hwndItem=undo;draw.CtlID=9001;
            require(SendMessageW(field.bar->window(),WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw))!=FALSE
                &&GetPixel(dc,0,px(20))==lgaed::presentation::reader_theme::palette(undo).header,
                "The Undo icon still has a square key background or outline");
            SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        }
        require(field.source_editor->document().text.empty()&&GetFocus()==field.source,"Shortcut visual presentation changed source or editor focus");
    });
    test("Multi-character shortcuts are one transaction and single-line macros never submit",[] {
        Fixture field;field.bar->set_shortcuts({"std::cout","<<","(\n)"});field.answer_editor->reset({"abc",{},0,3});field.focus(field.answer);
        field.click(L"std::cout");require(field.answer_editor->document().text=="std::cout"&&field.submitted==0,"A configured token was split, changed or submitted");
        field.click(L"Deshacer");auto original=field.answer_editor->document();require(original.text=="abc"&&original.selection_start==0&&original.selection_end==3,"One Undo did not restore the entire replaced selection");
        field.click(L"Deshacer");require(field.answer_editor->document().text=="abc","A multi-character shortcut introduced intermediate history items");
        field.click(L"Rehacer");require(field.answer_editor->document().text=="std::cout","Redo did not restore one full configured token");
        const auto before=field.answer_editor->document();field.click(L"(\n)");const auto rejected=field.answer_editor->document();
        require(before.text==rejected.text&&before.selection_start==rejected.selection_start&&before.selection_end==rejected.selection_end&&field.submitted==0,
            "A shortcut inserted a forbidden line break or invoked single-line Enter checking");
        field.source_editor->reset({});field.focus(field.source);field.click(L"(\n)");require(field.source_editor->document().text=="(\n)","Multiline code rejected a deliberate multiline snippet");
    });
    UnregisterClassW(type.lpszClassName,type.hInstance); FreeLibrary(rich_edit);
    std::cout << passed << " contextual editing groups passed; " << failed << " failed\n";
    return failed ? 1 : 0;
}
