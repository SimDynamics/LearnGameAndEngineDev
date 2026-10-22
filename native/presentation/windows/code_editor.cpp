#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/rich_text.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lgaed::windows {
namespace {

std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("Editor source must be valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::invalid_argument("Editor source must be valid UTF-16");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring lf(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == L'\r') {
            result.push_back(L'\n');
            if (index + 1 < text.size() && text[index + 1] == L'\n') ++index;
        } else result.push_back(text[index]);
    }
    return result;
}

std::int64_t view_offset(std::wstring_view original, std::int64_t offset) {
    const auto limit = static_cast<std::size_t>(std::clamp(offset, std::int64_t{0},
        static_cast<std::int64_t>(original.size())));
    std::int64_t result = 0;
    for (std::size_t index = 0; index < limit; ++index) {
        ++result;
        if (original[index] == L'\r' && index + 1 < limit && original[index + 1] == L'\n') ++index;
    }
    return result;
}

std::int64_t original_offset(std::wstring_view original, std::int64_t offset) {
    std::int64_t view = 0;
    std::size_t index = 0;
    while (index < original.size() && view < offset) {
        if (original[index] == L'\r' && index + 1 < original.size() && original[index + 1] == L'\n') ++index;
        ++index;
        ++view;
    }
    return static_cast<std::int64_t>(index);
}

std::wstring control_text(HWND control) {
    GETTEXTLENGTHEX length{GTL_NUMCHARS | GTL_PRECISE, 1200};
    const auto size = SendMessageW(control, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length), 0);
    if (size < 0 || size > 4 * 1024 * 1024) throw std::runtime_error("Editor text exceeds its supported size");
    std::wstring text(static_cast<std::size_t>(size) + 1, L'\0');
    GETTEXTEX get{static_cast<DWORD>(text.size() * sizeof(wchar_t)), GT_RAWTEXT, 1200, nullptr, nullptr};
    const auto copied = SendMessageW(control, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&get),
        reinterpret_cast<LPARAM>(text.data()));
    if (copied < 0) throw std::runtime_error("Cannot read editor text");
    text.resize(static_cast<std::size_t>(copied));
    return lf(text);
}

CHARRANGE selection(HWND control) {
    CHARRANGE range{};
    SendMessageW(control, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    GETTEXTLENGTHEX length{GTL_NUMCHARS | GTL_PRECISE, 1200};
    const auto characters = SendMessageW(control, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length), 0);
    if (characters < 0 || characters > 4 * 1024 * 1024)
        throw std::runtime_error("Editor text length is invalid");
    // RichEdit's Select All may include its undeletable final paragraph CR.
    // GT_RAWTEXT excludes that synthetic mark. Translate only its boundary to
    // the end of the actual source before saving history or editing a range.
    // This is not a clamp: other invalid offsets are deliberately unchanged.
    if (range.cpMin == characters + 1) range.cpMin = static_cast<LONG>(characters);
    if (range.cpMax == characters + 1) range.cpMax = static_cast<LONG>(characters);
    return range;
}

void select(HWND control, std::int64_t start, std::int64_t end) {
    CHARRANGE range{static_cast<LONG>(start), static_cast<LONG>(end)};
    SendMessageW(control, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
}

void validate(const std::wstring& text, const std::vector<std::int64_t>& starts) {
    std::int64_t previous = -4;
    for (const auto start : starts) {
        if (start < previous + 4 || start < 0 || static_cast<std::uint64_t>(start) + 4 > text.size() ||
                text.substr(static_cast<std::size_t>(start), 4) != L"    ")
            throw std::invalid_argument("Invalid indentation origin metadata");
        previous = start;
    }
}

std::vector<std::int64_t> shifted_units(const std::vector<std::int64_t>& units,
    std::size_t start, std::size_t end, std::size_t count) {
    std::vector<std::int64_t> result;
    const auto delta = static_cast<std::int64_t>(count) - static_cast<std::int64_t>(end - start);
    for (const auto unit : units) {
        if (unit + 4 <= static_cast<std::int64_t>(start)) result.push_back(unit);
        else if (unit >= static_cast<std::int64_t>(end)) result.push_back(unit + delta);
        // Editing any part of a unit removes its identity; undo restores it.
    }
    return result;
}

std::size_t column_at(std::wstring_view text, std::size_t cursor) {
    auto start = text.rfind(L'\n', cursor ? cursor - 1 : 0);
    start = start == std::wstring_view::npos ? 0 : start + 1;
    std::size_t column = 0;
    for (auto index = start; index < cursor; ++index) {
        if (text[index] == L'\t') column += 4 - column % 4;
        else if (text[index] < 0xDC00 || text[index] > 0xDFFF) ++column;
    }
    return column;
}

enum class Lexical { code, string, character, line_comment, block_comment };
Lexical context_at(std::wstring_view text, std::size_t cursor) {
    auto context = Lexical::code;
    bool escape = false;
    for (std::size_t index = 0; index < cursor; ++index) {
        const auto character = text[index];
        const auto next = index + 1 < cursor ? text[index + 1] : L'\0';
        if (context == Lexical::line_comment) {
            if (character == L'\n') context = Lexical::code;
        } else if (context == Lexical::block_comment) {
            if (character == L'*' && next == L'/') { context = Lexical::code; ++index; }
        } else if (context == Lexical::string || context == Lexical::character) {
            if (escape) escape = false;
            else if (character == L'\\') escape = true;
            else if ((context == Lexical::string && character == L'"') ||
                (context == Lexical::character && character == L'\'')) context = Lexical::code;
        } else if (character == L'/' && next == L'/') { context = Lexical::line_comment; ++index; }
        else if (character == L'/' && next == L'*') { context = Lexical::block_comment; ++index; }
        else if (character == L'"') context = Lexical::string;
        else if (character == L'\'') context = Lexical::character;
    }
    return context;
}

} // namespace

EditorDocument predefined_code_document(std::string_view source) {
    const auto text = wide(source);
    EditorDocument document;
    document.text = source;
    bool indentation = true;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == L'\n' || text[index] == L'\r') { indentation = true; ++index; }
        else if (indentation && text[index] == L'\t') ++index;
        else if (indentation && text.substr(index, 4) == L"    ") {
            document.indentation_starts.push_back(static_cast<std::int64_t>(index));
            index += 4;
        } else { indentation = false; ++index; }
    }
    return document;
}

struct CodeEditor::Impl {
    struct OriginalRepresentation {
        std::wstring text;
        std::vector<std::int64_t> units;
        CHARRANGE cursor{};
        CHARRANGE view_cursor{};
    };
    struct Snapshot {
        std::wstring text;
        std::vector<std::int64_t> units;
        CHARRANGE cursor{};
        POINT scroll{};
        std::optional<OriginalRepresentation> original;
    };
    HWND window;
    bool multiline;
    bool code_assistance;
    std::function<void()> submit;
    bool applying = false;
    bool suppress_tab_char = false;
    bool suppress_backspace_char = false;
    bool suppress_enter_char = false;
    std::optional<CHARRANGE> pending_edit;
    bool pending_backspace = false;
    std::vector<Snapshot> history;
    std::size_t current = 0;
    std::unique_ptr<CodeGutter> gutter;

    Impl(HWND control, bool lines, std::function<void()> action, bool assistance)
        : window(control), multiline(lines), code_assistance(assistance), submit(std::move(action)) {}

    Snapshot& now() { return history[current]; }
    const Snapshot& now() const { return history[current]; }

    void cursor_snapshot() {
        if (applying) return;
        now().cursor = selection(window);
        SendMessageW(window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&now().scroll));
    }

    void commit(Snapshot next) {
        history.resize(current + 1);
        history.push_back(std::move(next));
        ++current;
        if (history.size() > 256) { history.erase(history.begin()); --current; }
    }

    void tab_stops() {
        if (!code_assistance) return;
        const auto dc = GetDC(window);
        if (!dc) return;
        auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        HFONT derived = nullptr;
        // RichEdit may return no HFONT for WM_GETFONT even after WM_SETFONT.
        // Its default CHARFORMAT is the actual formatting used for the text.
        if (!font) {
            CHARFORMAT2W current_format{};
            current_format.cbSize = sizeof(current_format);
            SendMessageW(window, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&current_format));
            derived = CreateFontW(-MulDiv(current_format.yHeight, GetDeviceCaps(dc, LOGPIXELSY), 1440),
                0, 0, 0, (current_format.dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL,
                (current_format.dwEffects & CFE_ITALIC) != 0, FALSE, FALSE,
                current_format.bCharSet, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                current_format.bPitchAndFamily, current_format.szFaceName);
            font = derived;
        }
        const auto old = font ? SelectObject(dc, font) : nullptr;
        SIZE space{};
        GetTextExtentPoint32W(dc, L" ", 1, &space);
        const auto dpi = GetDeviceCaps(dc, LOGPIXELSX);
        if (old) SelectObject(dc, old);
        if (derived) DeleteObject(derived);
        ReleaseDC(window, dc);
        if (space.cx <= 0 || dpi <= 0) return;
        const auto cursor = selection(window);
        POINT scroll{};
        SendMessageW(window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        const auto was_applying = std::exchange(applying, true);
        select(window, 0, -1);
        PARAFORMAT2 format{};
        format.cbSize = sizeof(format);
        format.dwMask = PFM_TABSTOPS;
        format.cTabCount = MAX_TAB_STOPS;
        const auto stop = MulDiv(space.cx * 4, 1440, dpi);
        for (int index = 0; index < MAX_TAB_STOPS; ++index) format.rgxTabs[index] = (index + 1) * stop;
        SendMessageW(window, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
        select(window, cursor.cpMin, cursor.cpMax);
        SendMessageW(window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        applying = was_applying;
    }

    void apply(const Snapshot& value, bool notify_parent = true) {
        const auto previous_applying = applying;
        const auto events = SendMessageW(window, EM_GETEVENTMASK, 0, 0);
        {
            struct Restore {
                HWND window;
                bool& target;
                bool previous;
                LRESULT events;
                ~Restore() {
                    SendMessageW(window, EM_SETEVENTMASK, 0, events);
                    target = previous;
                }
            } restore{window, applying, previous_applying, events};
            applying = true;
            // EN_CHANGE can synchronously reenter the parent during EM_STREAMIN.
            // Its syntax formatter must see a completed snapshot, never a stream
            // whose text, selection and indentation identity are still loading.
            SendMessageW(window, EM_SETEVENTMASK, 0, events & ~static_cast<LRESULT>(ENM_CHANGE));
            // EM_SETTEXTEX recognizes an RTF header even with codepage 1200.
            // Keep the existing explicit SF_TEXT | SF_UNICODE plain-text load.
            presentation::set_edit_text(window, utf8(value.text));
            tab_stops();
            select(window, value.cursor.cpMin, value.cursor.cpMax);
            SendMessageW(window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&value.scroll));
            SendMessageW(window, EM_EMPTYUNDOBUFFER, 0, 0);
        }
        if (notify_parent && !previous_applying && (events & ENM_CHANGE)) {
            if (const auto parent = GetParent(window)) {
                SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(window), EN_CHANGE),
                    reinterpret_cast<LPARAM>(window));
            }
        }
    }

    void capture() {
        if (applying || history.empty()) return;
        const auto text = control_text(window);
        if (text == now().text) { cursor_snapshot(); return; }
        std::size_t start = 0, old_end = 0, new_end = 0;
        bool exact_edit = false;
        if (pending_edit) {
            start = static_cast<std::size_t>(pending_edit->cpMin);
            old_end = static_cast<std::size_t>(pending_edit->cpMax);
            if (pending_backspace && start == old_end && text.size() < now().text.size()) {
                const auto removed = now().text.size() - text.size();
                if (removed <= start) start -= removed;
            }
            const auto inserted = static_cast<std::int64_t>(text.size()) -
                static_cast<std::int64_t>(now().text.size()) + static_cast<std::int64_t>(old_end - start);
            if (inserted >= 0 && start <= text.size()) {
                new_end = start + static_cast<std::size_t>(inserted);
                exact_edit = new_end <= text.size() && old_end <= now().text.size() &&
                    text.substr(0, start) == now().text.substr(0, start) &&
                    text.substr(new_end) == now().text.substr(old_end);
            }
        }
        pending_edit.reset();
        if (!exact_edit) {
            start = 0;
            while (start < text.size() && start < now().text.size() && text[start] == now().text[start]) ++start;
            old_end = now().text.size();
            new_end = text.size();
            while (old_end > start && new_end > start && now().text[old_end - 1] == text[new_end - 1]) {
                --old_end; --new_end;
            }
        }
        Snapshot next;
        next.text = text;
        next.units = shifted_units(now().units, start, old_end, new_end - start);
        next.cursor = selection(window);
        SendMessageW(window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&next.scroll));
        commit(std::move(next));
    }

    void replace(std::size_t start, std::size_t end, std::wstring_view replacement,
        std::size_t cursor_start, std::size_t cursor_end,
        std::optional<std::int64_t> completed_unit = {}) {
        cursor_snapshot();
        Snapshot next = now();
        next.original.reset();
        next.text.replace(start, end - start, replacement);
        next.units = shifted_units(now().units, start, end, replacement.size());
        if (completed_unit) {
            // A completion can absorb only the preceding manual spaces in
            // this tab stop. Never infer units from other manual whitespace.
            next.units.erase(std::remove_if(next.units.begin(), next.units.end(), [&](auto unit) {
                return unit < *completed_unit + 4 && unit + 4 > *completed_unit;
            }), next.units.end());
            next.units.push_back(*completed_unit);
            std::sort(next.units.begin(), next.units.end());
        }
        next.cursor = {static_cast<LONG>(cursor_start), static_cast<LONG>(cursor_end)};
        commit(std::move(next));
        apply(now());
    }

    void insert_tab() {
        if (!code_assistance) return;
        const auto range = selection(window);
        const auto start = static_cast<std::size_t>(range.cpMin);
        const auto end = static_cast<std::size_t>(range.cpMax);
        const auto remainder = column_at(now().text, start) % 4;
        const auto count = 4 - remainder;
        auto prior = start;
        while (prior && start - prior < remainder && now().text[prior - 1] == L' ') --prior;
        const auto origin = start - prior == remainder ?
            std::optional<std::int64_t>(static_cast<std::int64_t>(prior)) : std::nullopt;
        replace(start, end, std::wstring(count, L' '), start + count, start + count, origin);
        SetFocus(window);
    }

    bool erase_unit() {
        const auto range = selection(window);
        if (range.cpMin != range.cpMax || range.cpMin < 4) return false;
        const auto start = static_cast<std::int64_t>(range.cpMin) - 4;
        if (std::find(now().units.begin(), now().units.end(), start) == now().units.end()) return false;
        replace(static_cast<std::size_t>(start), static_cast<std::size_t>(start + 4), L"",
            static_cast<std::size_t>(start), static_cast<std::size_t>(start));
        return true;
    }

    void backspace() {
        if (code_assistance && erase_unit()) return;
        const auto range = selection(window);
        auto start = static_cast<std::size_t>(range.cpMin);
        const auto end = static_cast<std::size_t>(range.cpMax);
        if (start == end) {
            if (!start) return;
            --start;
            if (start && now().text[start] >= 0xDC00 && now().text[start] <= 0xDFFF &&
                    now().text[start - 1] >= 0xD800 && now().text[start - 1] <= 0xDBFF) --start;
        }
        replace(start, end, L"", start, start);
    }

    bool pair(wchar_t character) {
        const auto range = selection(window);
        const auto start = static_cast<std::size_t>(range.cpMin);
        const auto end = static_cast<std::size_t>(range.cpMax);
        const auto context = context_at(now().text, start);
        if (character == L'"' || character == L'\'') {
            std::size_t slashes = 0;
            for (auto index = start; index && now().text[index - 1] == L'\\'; --index) ++slashes;
            if (slashes % 2) return false;
        }
        if (start == end && start < now().text.size() && now().text[start] == character &&
                ((context == Lexical::code && (character == L')' || character == L']' || character == L'}')) ||
                    (character == L'"' && context == Lexical::string) ||
                    (character == L'\'' && context == Lexical::character))) {
            select(window, start + 1, start + 1);
            cursor_snapshot();
            return true;
        }
        if (context != Lexical::code) return false;
        wchar_t closing = 0;
        if (character == L'(') closing = L')';
        else if (character == L'[') closing = L']';
        else if (character == L'{') closing = L'}';
        else if (character == L'"') closing = L'"';
        else if (character == L'\'') {
            if (start && now().text[start - 1] >= L'0' && now().text[start - 1] <= L'9') return false;
            closing = L'\'';
        }
        if (!closing) return false;
        const auto replacement = std::wstring(1, character) + now().text.substr(start, end - start) + closing;
        replace(start, end, replacement, start + 1, end + 1);
        return true;
    }

    bool insert_shortcut(std::string_view text) {
        if (text.empty() || !IsWindow(window) || !IsWindowEnabled(window) || (GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY)) return false;
        const auto supplied=wide(text);
        if(supplied.find(L'\0')!=supplied.npos||(!multiline&&supplied.find_first_of(L"\r\n")!=supplied.npos))return false;
        capture();
        if(supplied==L"\t"&&code_assistance){insert_tab();return true;}
        if(supplied.size()==1&&code_assistance&&pair(supplied.front()))return true;
        const auto value=lf(supplied);const auto range=selection(window);
        const auto start=static_cast<std::size_t>(range.cpMin),end=static_cast<std::size_t>(range.cpMax);
        replace(start,end,value,start+value.size(),start+value.size());return true;
    }

    void undo() {
        if (current == 0) return;
        cursor_snapshot();
        --current;
        apply(now());
    }
    void redo() {
        if (current + 1 == history.size()) return;
        cursor_snapshot();
        ++current;
        apply(now());
    }

    static LRESULT CALLBACK subclass(HWND window, UINT message, WPARAM wparam,
        LPARAM lparam, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<Impl*>(data);
        try {
            if (message == WM_NCDESTROY) {
                RemoveWindowSubclass(window, subclass, 1);
                self.window = nullptr;
                return DefSubclassProc(window, message, wparam, lparam);
            }
            if (message == WM_GETDLGCODE) {
                auto result = DefSubclassProc(window, message, wparam, lparam) |
                    DLGC_WANTCHARS | DLGC_WANTARROWS;
                const auto key = lparam ? reinterpret_cast<MSG*>(lparam)->wParam : wparam;
                if (self.code_assistance) result |= DLGC_WANTTAB;
                else if (key == VK_TAB) result &= ~(DLGC_WANTTAB | DLGC_WANTALLKEYS);
                if (!self.multiline && (wparam == VK_RETURN ||
                        (lparam && reinterpret_cast<MSG*>(lparam)->wParam == VK_RETURN))) result |= DLGC_WANTALLKEYS;
                return result;
            }
            if (message == WM_UNDO || message == EM_UNDO) { self.undo(); return TRUE; }
            if (message == EM_REDO) { self.redo(); return TRUE; }
            if (message == EM_CANUNDO) return self.current != 0;
            if (message == EM_CANREDO) return self.current + 1 < self.history.size();
            if (message == WM_KEYDOWN) {
                self.cursor_snapshot();
                if (self.code_assistance && wparam == VK_TAB) {
                    self.insert_tab(); self.suppress_tab_char = true; return 0;
                }
                if (wparam == VK_BACK) {
                    self.suppress_backspace_char = true;
                    if (!(GetKeyState(VK_CONTROL) & 0x8000)) { self.backspace(); return 0; }
                }
                if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == L'Z') { self.undo(); return 0; }
                if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == L'Y') { self.redo(); return 0; }
                if (!self.multiline && wparam == VK_RETURN) {
                    self.suppress_enter_char = true;
                    if (self.submit) self.submit();
                    return 0;
                }
            }
            if (message == WM_KEYUP) {
                if (wparam == VK_TAB) self.suppress_tab_char = false;
                if (wparam == VK_BACK) self.suppress_backspace_char = false;
                if (wparam == VK_RETURN) self.suppress_enter_char = false;
            }
            if (message == WM_CHAR) {
                self.cursor_snapshot();
                if (self.code_assistance && wparam == L'\t') {
                    if (!std::exchange(self.suppress_tab_char, false)) self.insert_tab();
                    return 0;
                }
                if (wparam == L'\b') {
                    if (!std::exchange(self.suppress_backspace_char, false)) self.backspace();
                    return 0;
                }
                if (wparam == 0x1A || wparam == 0x19) return 0;
                if (!self.multiline && (wparam == L'\r' || wparam == L'\n')) {
                    if (!std::exchange(self.suppress_enter_char, false) && self.submit) self.submit();
                    return 0;
                }
                if (self.code_assistance && wparam >= L' ' && wparam <= 0xFFFF &&
                        self.pair(static_cast<wchar_t>(wparam))) return 0;
            }
            const auto mutating = message == WM_CHAR || message == WM_PASTE || message == WM_CUT ||
                message == WM_CLEAR || message == EM_REPLACESEL ||
                (message == WM_KEYDOWN && (wparam == VK_DELETE || wparam == VK_BACK));
            if (mutating) {
                self.cursor_snapshot();
                self.pending_edit = selection(window);
                self.pending_backspace = (message == WM_CHAR && wparam == L'\b') ||
                    (message == WM_KEYDOWN && wparam == VK_BACK);
                if (message == WM_KEYDOWN && wparam == VK_DELETE &&
                        self.pending_edit->cpMin == self.pending_edit->cpMax &&
                        static_cast<std::size_t>(self.pending_edit->cpMax) < self.now().text.size())
                    ++self.pending_edit->cpMax;
            }
            const auto result = DefSubclassProc(window, message, wparam, lparam);
            if (message == WM_SETFONT) self.tab_stops();
            if (mutating) {
                self.capture();
                self.pending_edit.reset();
            }
            return result;
        } catch (...) {
            // Exceptions must never cross a Win32 callback boundary. The edit
            // is declined; the existing source and identity remain available.
            MessageBeep(MB_ICONWARNING);
            return 0;
        }
    }
};

CodeEditor::CodeEditor(HWND rich_edit, EditorDocument document, bool multiline,
    std::function<void()> submit, bool code_assistance)
    : impl_(std::make_unique<Impl>(rich_edit, multiline, std::move(submit), code_assistance)) {
    if (!IsWindow(rich_edit)) throw std::invalid_argument("CodeEditor requires a RichEdit control");
    SendMessageW(rich_edit, EM_SETUNDOLIMIT, 0, 0);
    SendMessageW(rich_edit, EM_EXLIMITTEXT, 0, 4 * 1024 * 1024);
    SendMessageW(rich_edit, EM_SETEVENTMASK, 0, SendMessageW(rich_edit, EM_GETEVENTMASK, 0, 0) | ENM_CHANGE);
    reset(std::move(document));
    if (!SetWindowSubclass(rich_edit, Impl::subclass, 1, reinterpret_cast<DWORD_PTR>(impl_.get())))
        throw std::runtime_error("Cannot attach code editing behavior");
    try{if(multiline&&code_assistance)impl_->gutter=std::make_unique<CodeGutter>(rich_edit);}
    catch(...){RemoveWindowSubclass(rich_edit,Impl::subclass,1);throw;}
}

CodeEditor::~CodeEditor() {
    if (impl_ && impl_->window) RemoveWindowSubclass(impl_->window, Impl::subclass, 1);
}

void CodeEditor::capture_change() { impl_->capture(); }
EditorDocument CodeEditor::document() const {
    impl_->capture();
    impl_->cursor_snapshot();
    const auto& current = impl_->now();
    if (current.original) {
        const auto& original = *current.original;
        const auto unchanged_selection = current.cursor.cpMin == original.view_cursor.cpMin &&
            current.cursor.cpMax == original.view_cursor.cpMax;
        return {utf8(original.text), original.units,
            unchanged_selection ? original.cursor.cpMin : original_offset(original.text, current.cursor.cpMin),
            unchanged_selection ? original.cursor.cpMax : original_offset(original.text, current.cursor.cpMax)};
    }
    return {utf8(current.text), current.units, current.cursor.cpMin, current.cursor.cpMax};
}
void CodeEditor::reset(EditorDocument document) {
    const auto original = wide(document.text);
    auto text = lf(original);
    if (!impl_->multiline && text.find(L'\n') != std::wstring::npos)
        throw std::invalid_argument("A single-line response cannot contain line breaks");
    validate(original, document.indentation_starts);
    const auto original_end = static_cast<std::int64_t>(original.size());
    const CHARRANGE original_cursor{
        static_cast<LONG>(std::clamp(document.selection_start, std::int64_t{0}, original_end)),
        static_cast<LONG>(std::clamp(document.selection_end, std::int64_t{0}, original_end))};
    const CHARRANGE view_cursor{
        static_cast<LONG>(view_offset(original, original_cursor.cpMin)),
        static_cast<LONG>(view_offset(original, original_cursor.cpMax))};
    std::vector<std::int64_t> units;
    for (const auto unit : document.indentation_starts) units.push_back(view_offset(original, unit));
    std::optional<Impl::OriginalRepresentation> representation;
    if (original != text)
        representation = Impl::OriginalRepresentation{original, std::move(document.indentation_starts),
            original_cursor, view_cursor};
    Impl::Snapshot snapshot{std::move(text), std::move(units), view_cursor, {0, 0}, std::move(representation)};
    impl_->history.clear();
    impl_->history.push_back(std::move(snapshot));
    impl_->current = 0;
    // Initialization/reset is owned by the caller; the adapter may not yet be
    // attached to the parent's model. Only actual edits and undo/redo notify it.
    impl_->apply(impl_->now(), false);
}
void CodeEditor::insert_tab() { impl_->insert_tab(); }
bool CodeEditor::insert_shortcut(std::string_view text) { return impl_->insert_shortcut(text); }
void CodeEditor::undo() { impl_->undo(); }
void CodeEditor::redo() { impl_->redo(); }

} // namespace lgaed::windows
