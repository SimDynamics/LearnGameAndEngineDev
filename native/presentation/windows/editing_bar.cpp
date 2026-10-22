#include "presentation/windows/editing_bar.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
constexpr wchar_t bar_class[] = L"LearnGameAndEngineDev.EditingBar";
constexpr UINT_PTR focus_timer = 1;
constexpr int first_command = 9000;
enum class Command { tab, parentheses, braces, brackets, quotes, apostrophes,
    semicolon, colon, less, greater, undo, redo };
struct Item { const wchar_t* label; wchar_t character; int width; };
constexpr std::array<Item,12> items{{
    {L"TAB",0,48}, {L"( )",L'(',40}, {L"{ }",L'{',40}, {L"[ ]",L'[',40},
    {L"\" \"",L'"',42}, {L"' '",L'\'',42}, {L";",L';',32}, {L":",L':',32},
    {L"<",L'<',32}, {L">",L'>',32}, {L"Deshacer",0,76}, {L"Rehacer",0,76}
}};
}

struct EditingBar::Impl {
    HWND parent = nullptr, bar = nullptr, target = nullptr;
    std::array<HWND,items.size()> buttons{};
    HFONT font = nullptr;
    bool shown = false, closing = false;
    std::function<windows::CodeEditor*(HWND)> resolve;
    std::function<void()> visibility_changed;

    Impl(HWND owner, std::function<windows::CodeEditor*(HWND)> resolver,
        std::function<void()> changed)
        : parent(owner), resolve(std::move(resolver)), visibility_changed(std::move(changed)) {}

    ~Impl() {
        closing = true;
        if (bar && IsWindow(bar)) { KillTimer(bar,focus_timer); DestroyWindow(bar); }
        if (font) DeleteObject(font);
    }
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(parent)),96); }
    bool owns_focus(HWND focus) const { return focus && bar && (focus == bar || IsChild(bar,focus)); }
    windows::CodeEditor* editor(HWND handle) const {
        if (!handle || !IsWindow(handle) || !IsChild(parent,handle) || !IsWindowEnabled(handle) ||
            (GetWindowLongPtrW(handle,GWL_STYLE) & ES_READONLY)) return nullptr;
        // Never retain an adapter between messages: a page rebuild can destroy
        // the HWND and its CodeEditor independently of the floating controls.
        return resolve(handle);
    }
    void theme() {
        reader_theme::apply(bar,reader_theme::Surface::header);
        for (const auto button : buttons) if (button) reader_theme::apply(button,reader_theme::Surface::card);
    }
    void update_font() {
        const auto next = CreateFontW(-MulDiv(10,static_cast<int>(GetDpiForWindow(parent)),72),0,0,0,
            FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if (!next) return;
        for (const auto button : buttons) SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if (font) DeleteObject(font);
        font = next;
    }
    void layout() {
        if (!bar) return;
        RECT area{}; GetClientRect(bar,&area);
        const int padding = px(5), gap = px(3);
        int natural = 0; for (const auto& item : items) natural += px(item.width);
        const int available = std::max(1,static_cast<int>(area.right) - padding * 2 - gap * (static_cast<int>(items.size()) - 1));
        int x = padding;
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto width = std::max(1,MulDiv(px(items[index].width),available,natural));
            MoveWindow(buttons[index],x,padding,width,std::max(1,static_cast<int>(area.bottom) - padding * 2),TRUE);
            x += width + gap;
        }
    }
    void initialize() {
        if (!IsWindow(parent) || !resolve) throw std::invalid_argument("EditingBar requires a parent and a live-editor resolver");
        static const bool registered = [] {
            WNDCLASSW type{};
            type.lpfnWndProc = procedure; type.hInstance = GetModuleHandleW(nullptr);
            type.hCursor = LoadCursorW(nullptr,IDC_ARROW); type.lpszClassName = bar_class;
            return RegisterClassW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();
        if (!registered) throw std::runtime_error("Cannot register contextual editing controls");
        bar = CreateWindowExW(WS_EX_CONTROLPARENT,bar_class,L"Símbolos y edición de código",
            WS_CHILD | WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if (!bar) throw std::runtime_error("Cannot create contextual editing controls");
        for (std::size_t index = 0; index < items.size(); ++index) {
            buttons[index] = CreateWindowExW(0,L"BUTTON",items[index].label,WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0,0,1,1,bar,reinterpret_cast<HMENU>(static_cast<INT_PTR>(first_command + index)),GetModuleHandleW(nullptr),nullptr);
            if (!buttons[index]) throw std::runtime_error("Cannot create a code shortcut");
        }
        theme(); update_font();
        if (!SetTimer(bar,focus_timer,100,nullptr)) throw std::runtime_error("Cannot observe the active code editor");
    }
    void refresh() {
        if (closing || !bar || !IsWindow(bar)) return;
        const auto focus = GetFocus();
        if (!owns_focus(focus)) target = editor(focus) ? focus : nullptr;
        const auto active = editor(target);
        if (!active) target = nullptr;
        const bool visible = target != nullptr;
        if (visible != shown) {
            shown = visible;
            ShowWindow(bar,shown ? SW_SHOWNOACTIVATE : SW_HIDE);
            if (visibility_changed) visibility_changed();
        }
        // The controls remain available even at history boundaries; the same
        // CodeEditor decides whether Undo/Redo has an operation to perform.
    }
    void execute(int id) {
        if (id < first_command || id >= first_command + static_cast<int>(items.size())) return;
        refresh();
        const auto destination = target;
        if (!editor(destination)) return;
        SetFocus(destination);
        if (GetFocus() != destination) { refresh(); return; }
        const auto adapter = editor(destination);
        if (!adapter) { refresh(); return; }
        const auto command = static_cast<Command>(id - first_command);
        if (command == Command::tab) adapter->insert_tab();
        else if (command == Command::undo) adapter->undo();
        else if (command == Command::redo) adapter->redo();
        else SendMessageW(destination,WM_CHAR,items[static_cast<std::size_t>(command)].character,1);
        refresh();
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window,message,wparam,lparam);
        try {
            LRESULT color = 0;
            if (reader_theme::control_color(message,wparam,lparam,color)) return color;
            switch (message) {
            case WM_COMMAND:
                if (HIWORD(wparam) == BN_CLICKED) self->execute(LOWORD(wparam));
                return 0;
            case WM_DRAWITEM:
                if (reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
                break;
            case WM_ERASEBKGND: {
                RECT area{}; GetClientRect(window,&area);
                FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(reader_theme::Surface::header)); return 1;
            }
            case WM_SIZE: self->layout(); return 0;
            case WM_TIMER: if (wparam == focus_timer) { self->refresh(); return 0; } break;
            case WM_DPICHANGED_AFTERPARENT: self->update_font(); self->layout(); return 0;
            case WM_THEMECHANGED: case WM_SETTINGCHANGE: self->theme(); return 0;
            case WM_DESTROY: KillTimer(window,focus_timer); return 0;
            case WM_NCDESTROY:
                self->bar = nullptr; self->target = nullptr; self->shown = false;
                SetWindowLongPtrW(window,GWLP_USERDATA,0); break;
            }
        } catch (...) {
            // As in CodeEditor, a declined edit cannot escape a window callback.
            MessageBeep(MB_ICONWARNING);
        }
        return DefWindowProcW(window,message,wparam,lparam);
    }
};

EditingBar::EditingBar(HWND parent, std::function<windows::CodeEditor*(HWND)> resolve,
    std::function<void()> visibility_changed)
    : impl_(std::make_unique<Impl>(parent,std::move(resolve),std::move(visibility_changed))) { impl_->initialize(); }
EditingBar::~EditingBar() = default;
void EditingBar::refresh() { impl_->refresh(); }
void EditingBar::place(int x, int y, int width, int height) {
    if (impl_->bar) MoveWindow(impl_->bar,x,y,std::max(1,width),std::max(1,height),TRUE);
}
bool EditingBar::visible() const { return impl_->shown; }
HWND EditingBar::window() const { return impl_->bar; }

} // namespace lgaed::presentation
