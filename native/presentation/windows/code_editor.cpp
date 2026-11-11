#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/editor_render_transaction.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lgaed::windows {
namespace {
constexpr UINT literal_quote_message=WM_APP+0x434;

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

enum class Lexical { code, string, character, raw_string, line_comment, block_comment };
struct Delimiter { wchar_t character; std::size_t position; };
struct CodeContext {
    Lexical lexical = Lexical::code;
    std::vector<Delimiter> openings;
    std::size_t last_code = std::wstring_view::npos;
};

bool identifier_character(wchar_t character) {
    return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') ||
        (character >= L'0' && character <= L'9') || character == L'_';
}

bool numeric_token_before(std::wstring_view text, std::size_t position) {
    if(!position||!identifier_character(text[position-1]))return false;
    auto start = position;
    while (start && (identifier_character(text[start - 1]) || text[start - 1] == L'\'')) --start;
    return text[start] >= L'0' && text[start] <= L'9';
}

bool digit_separator(std::wstring_view text, std::size_t position) {
    return position+1<text.size()&&identifier_character(text[position+1])&&numeric_token_before(text,position);
}

wchar_t closing_for(wchar_t opening) {
    return opening == L'(' ? L')' : opening == L'[' ? L']' : opening == L'{' ? L'}' : L'\0';
}

CodeContext code_context(std::wstring_view text, std::size_t cursor,
    std::vector<std::pair<std::size_t,std::size_t>>* literals = nullptr) {
    CodeContext result;
    auto& context = result.lexical;
    bool escape = false;
    std::wstring raw_end;
    std::size_t literal_start=0;
    for (std::size_t index = 0; index < cursor; ++index) {
        const auto character = text[index];
        const auto next = index + 1 < cursor ? text[index + 1] : L'\0';
        if (context == Lexical::line_comment) {
            if (character == L'\n') context = Lexical::code;
        } else if (context == Lexical::block_comment) {
            if (character == L'*' && next == L'/') { context = Lexical::code; ++index; }
        } else if (context == Lexical::raw_string) {
            if (text.substr(index, raw_end.size()) == raw_end && index + raw_end.size() <= cursor) {
                index += raw_end.size() - 1;
                result.last_code = index;
                if(literals)literals->emplace_back(literal_start,index+1);
                context = Lexical::code;
            }
        } else if (context == Lexical::string || context == Lexical::character) {
            if (escape) escape = false;
            else if (character == L'\\') escape = true;
            else if ((context == Lexical::string && character == L'"') ||
                (context == Lexical::character && character == L'\'')) {
                context = Lexical::code;
                result.last_code = index;
                if(literals)literals->emplace_back(literal_start,index+1);
            }
        } else if (character == L'/' && next == L'/') { context = Lexical::line_comment; ++index; }
        else if (character == L'/' && next == L'*') { context = Lexical::block_comment; ++index; }
        else {
            if (character == L'R' && next == L'"') {
                const auto delimiter_end = text.find(L'(', index + 2);
                if (delimiter_end != text.npos && delimiter_end < cursor && delimiter_end - index - 2 <= 16 &&
                        text.substr(index + 2, delimiter_end - index - 2).find_first_of(L" ()\\\t\r\n") == text.npos) {
                    raw_end = L")" + std::wstring(text.substr(index + 2, delimiter_end - index - 2)) + L'"';
                    context = Lexical::raw_string;
                    literal_start=index;
                    result.last_code = index;
                    index = delimiter_end;
                    continue;
                }
            }
            if (character != L' ' && character != L'\t' && character != L'\r' && character != L'\n')
                result.last_code = index;
            if (character == L'"') { context = Lexical::string;literal_start=index; }
            else if (character == L'\'' && !digit_separator(text, index)) { context = Lexical::character;literal_start=index; }
            else if (closing_for(character)) result.openings.push_back({character, index});
            else if (!result.openings.empty() && closing_for(result.openings.back().character) == character)
                result.openings.pop_back();
        }
    }
    if(literals&&(context==Lexical::string||context==Lexical::character||context==Lexical::raw_string))
        literals->emplace_back(literal_start,cursor);
    return result;
}

Lexical context_at(std::wstring_view text, std::size_t cursor) {
    return code_context(text, cursor).lexical;
}

std::size_t line_start_at(std::wstring_view text, std::size_t cursor) {
    if (!cursor) return 0;
    const auto previous = text.rfind(L'\n', cursor - 1);
    return previous == text.npos ? 0 : previous + 1;
}

class FrozenText {
    ITextDocument* document_ = nullptr;
    bool frozen_ = false;
public:
    explicit FrozenText(HWND window) {
        IRichEditOle* ole = nullptr;
        if(SendMessageW(window,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole))&&ole) {
            ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void**>(&document_));
            ole->Release();
            if(document_) { LONG count=0;frozen_=SUCCEEDED(document_->Freeze(&count)); }
        }
    }
    ~FrozenText() {
        if(document_) {
            if(frozen_) { LONG count=0;document_->Unfreeze(&count); }
            document_->Release();
        }
    }
    FrozenText(const FrozenText&)=delete;
    FrozenText& operator=(const FrozenText&)=delete;
};

struct NativeCodeScrollbars {
    HWND window;
    bool enabled,previous;
    NativeCodeScrollbars(HWND target,bool code)
        :window(target),enabled(code),previous(presentation::reader_theme::native_scrollbar_theme(target)) {
        if(enabled)presentation::reader_theme::use_native_scrollbar_theme(window);
    }
    ~NativeCodeScrollbars() {
        if(enabled&&IsWindow(window))presentation::reader_theme::use_native_scrollbar_theme(window,previous);
    }
};

} // namespace

EditorDocument predefined_code_document(std::string_view source) {
    const auto text = wide(source);
    EditorDocument document;
    document.text = source;
    std::vector<std::pair<std::size_t,std::size_t>> literals;
    (void)code_context(text,text.size(),&literals);
    std::size_t literal_index=0;
    bool indentation = true;
    for (std::size_t index = 0; index < text.size();) {
        while(literal_index<literals.size()&&literals[literal_index].second<=index)++literal_index;
        const bool literal=literal_index<literals.size()&&literals[literal_index].first<=index;
        if (text[index] == L'\n' || text[index] == L'\r') { indentation = true; ++index; }
        else if (indentation && text[index] == L'\t') ++index;
        else if (indentation && !literal && text.substr(index, 4) == L"    ") {
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
    bool auto_indent;
    std::function<void()> submit;
    bool applying = false;
    bool suppress_tab_char = false;
    bool suppress_backspace_char = false;
    bool suppress_enter_char = false;
    std::optional<WPARAM> literal_quote_key;
    std::optional<CHARRANGE> pending_edit;
    bool pending_backspace = false;
    std::vector<Snapshot> history;
    std::size_t current = 0;
    NativeCodeScrollbars native_scrollbars;
    std::unique_ptr<CodeGutter> gutter;

    Impl(HWND control, bool lines, std::function<void()> action, bool assistance, bool indent)
        : window(control), multiline(lines), code_assistance(assistance), auto_indent(indent), submit(std::move(action)),
          native_scrollbars(control,assistance) {}

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

    void apply(const Snapshot& value, bool notify_parent = true, bool load_document = false, bool reveal_cursor = false) {
        const auto previous_applying = applying;
        const auto desired_scroll=value.scroll;
        const auto events = SendMessageW(window, EM_GETEVENTMASK, 0, 0);
        struct ApplyingScope {
            bool& target;
            bool previous;
            ~ApplyingScope(){target=previous;}
        } applying_scope{applying,previous_applying};
        applying=true;
        presentation::editor_render::Scope rendering(window);
        {
            // Keep the actual edit and the parent's syntax formatting in the
            // same native paint transaction. Nested formatters balance their
            // own Freeze calls without exposing an intermediate selection.
            FrozenText frozen(window);
            {
                struct Restore {
                    HWND window;
                    LRESULT events;
                    ~Restore() {
                        SendMessageW(window, EM_SETEVENTMASK, 0, events);
                    }
                } restore{window, events};
                SendMessageW(window, EM_SETEVENTMASK, 0,
                    events & ~static_cast<LRESULT>(ENM_CHANGE|ENM_SELCHANGE));
                if(load_document) {
                    // Only explicit initialization/reset streams the whole
                    // source. SF_TEXT prevents RTF-looking code from parsing.
                    presentation::set_edit_text(window, utf8(value.text));
                    tab_stops();
                } else {
                    const auto previous=control_text(window);
                    std::size_t first=0,old_end=previous.size(),new_end=value.text.size();
                    while(first<old_end&&first<new_end&&previous[first]==value.text[first])++first;
                    if(first&&first<previous.size()&&previous[first-1]>=0xD800&&previous[first-1]<=0xDBFF&&
                            previous[first]>=0xDC00&&previous[first]<=0xDFFF)--first;
                    while(old_end>first&&new_end>first&&previous[old_end-1]==value.text[new_end-1]) { --old_end;--new_end; }
                    if(old_end<previous.size()&&old_end>first&&previous[old_end]>=0xDC00&&previous[old_end]<=0xDFFF&&
                            previous[old_end-1]>=0xD800&&previous[old_end-1]<=0xDBFF) { ++old_end;++new_end; }
                    if(first!=old_end||first!=new_end) {
                        const auto replacement=value.text.substr(first,new_end-first);
                        select(window,first,old_end);
                        // EM_REPLACESEL always inserts literal Unicode text;
                        // unlike EM_SETTEXTEX it does not detect RTF headers.
                        SendMessageW(window,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(replacement.c_str()));
                        if(control_text(window)!=value.text)throw std::runtime_error("Native incremental edit was not accepted");
                    }
                }
                select(window, value.cursor.cpMin, value.cursor.cpMax);
                SendMessageW(window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&desired_scroll));
                SendMessageW(window, EM_EMPTYUNDOBUFFER, 0, 0);
            }
            if (notify_parent && !previous_applying && (events & ENM_CHANGE)) {
                if (const auto parent = GetParent(window)) {
                    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(window), EN_CHANGE),
                        reinterpret_cast<LPARAM>(window));
                }
            }
        }
        // TOM can defer layout/scroll work until its outer Unfreeze. Restore
        // the exact journal position after that work, then reveal a newly typed
        // line without changing the scroll recorded for Undo.
        // Keep snapshot capture guarded through the callback and Unfreeze:
        // document() is also an observer and must not overwrite this journal
        // entry with RichEdit's temporary, not-yet-restored native scroll.
        SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&desired_scroll));
        if(reveal_cursor) {
            SendMessageW(window,EM_SCROLLCARET,0,0);
        }
        SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&now().scroll));
        if(notify_parent&&!previous_applying&&(events&ENM_CHANGE)) {
            // EN_CHANGE intentionally runs inside Freeze so syntax coloring
            // cannot flash. Native line geometry is final only after Unfreeze;
            // The owner observing this edit can now refresh its viewport.
            // Keep this explicit: EN_UPDATE also covers native repaint/format
            // operations, which would turn viewport measurements into a loop.
            if(const auto parent=GetParent(window))
                SendMessageW(parent,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(window),CodeEditor::layout_ready_notification),
                    reinterpret_cast<LPARAM>(window));
        }
        // Native paragraph positions become authoritative after the outer
        // Unfreeze and final scroll/layout restoration, including Undo/Redo.
        if(!previous_applying&&gutter)gutter->refresh_content();
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
        // TOM/UI Automation can change the native story without WM_SETTEXT or
        // EM_REPLACESEL. The completed edit observed here owns its paragraph
        // refresh; cursor/format-only observations take the early return above.
        if(gutter)gutter->refresh_content();
    }

    void replace(std::size_t start, std::size_t end, std::wstring_view replacement,
        std::size_t cursor_start, std::size_t cursor_end,
        std::optional<std::int64_t> completed_unit = {},
        const std::vector<std::int64_t>& inherited_units = {}, bool reveal_cursor = true) {
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
        next.units.insert(next.units.end(), inherited_units.begin(), inherited_units.end());
        std::sort(next.units.begin(), next.units.end());
        next.cursor = {static_cast<LONG>(cursor_start), static_cast<LONG>(cursor_end)};
        commit(std::move(next));
        apply(now(),true,false,reveal_cursor);
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
            if (code_assistance && start < now().text.size()) {
                const auto opening=now().text[start-1],closing=now().text[start];
                if ((opening==L'('&&closing==L')')||(opening==L'['&&closing==L']')||
                    (opening==L'{'&&closing==L'}')||(opening==L'"'&&closing==L'"')||
                    (opening==L'\''&&closing==L'\'')) {
                    replace(start-1,start+1,L"",start-1,start-1);return;
                }
            }
            --start;
            if (start && now().text[start] >= 0xDC00 && now().text[start] <= 0xDFFF &&
                    now().text[start - 1] >= 0xD800 && now().text[start - 1] <= 0xDBFF) --start;
        }
        replace(start, end, L"", start, start);
    }

    bool newline() {
        if (!multiline) return false;
        const auto range=selection(window);
        const auto start=static_cast<std::size_t>(range.cpMin),end=static_cast<std::size_t>(range.cpMax);
        // Output predictions and plain input still own Enter when programming
        // indentation is disabled. Use the same complete undo transaction.
        if (!code_assistance || !auto_indent) {
            // This answer grows with its text. Forcing EM_SCROLLCARET before
            // the parent sizes the new empty row scrolls away line one even
            // when both rows fit after layout. Preserve the old viewport and
            // let the parent reveal genuinely clipped lines after resizing.
            replace(start,end,L"\n",start+1,start+1,{}, {}, false);
            return true;
        }
        const auto context = code_context(now().text, start);
        // Whitespace inside a literal is program data, including multiline raw
        // strings. Enter must not silently add indentation to that content.
        if (context.lexical == Lexical::string || context.lexical == Lexical::character ||
                context.lexical == Lexical::raw_string) {
            replace(start,end,L"\n",start+1,start+1);
            return true;
        }
        const auto line_start=line_start_at(now().text,start);
        auto indent_end=line_start;
        while(indent_end<start&&(now().text[indent_end]==L' '||now().text[indent_end]==L'\t'))++indent_end;
        auto base_start=line_start;
        const auto last=context.last_code;
        const bool code_line=context.lexical!=Lexical::block_comment;
        const bool opens=code_line&&last!=std::wstring::npos&&last>=line_start&&closing_for(now().text[last]);
        const auto opening=opens?now().text[last]:L'\0';
        // A continuation belongs to its original opening line. Repeating Enter
        // inside (...) or [...] must not add another level on every line.
        const bool continuation=code_line&&!context.openings.empty()&&
            (context.openings.back().character==L'('||context.openings.back().character==L'[');
        if(continuation) {
            base_start=line_start_at(now().text,context.openings.back().position);
            indent_end=base_start;
            while(indent_end<context.openings.back().position&&
                    (now().text[indent_end]==L' '||now().text[indent_end]==L'\t'))++indent_end;
        }
        const auto base=now().text.substr(base_start,indent_end-base_start);
        auto closing_position=end;
        while(closing_position<now().text.size()&&(now().text[closing_position]==L' '||now().text[closing_position]==L'\t'))
            ++closing_position;
        const bool split=opens&&closing_position<now().text.size()&&now().text[closing_position]==closing_for(opening);
        const auto child=base+((opens||continuation)?L"\t":L"");
        auto replacement=L"\n"+child;
        if(split)replacement+=L"\n"+base;
        std::vector<std::int64_t> inherited;
        for(const auto unit:now().units)if(unit>=static_cast<std::int64_t>(base_start)&&unit+4<=static_cast<std::int64_t>(indent_end)) {
            inherited.push_back(static_cast<std::int64_t>(start+1)+unit-static_cast<std::int64_t>(base_start));
            if(split)inherited.push_back(static_cast<std::int64_t>(start+child.size()+2)+unit-static_cast<std::int64_t>(base_start));
        }
        replace(start,split?closing_position:end,replacement,start+1+child.size(),start+1+child.size(),{},inherited);
        return true;
    }

    bool closing_delimiter(wchar_t character) {
        if (!multiline || !code_assistance || !auto_indent) return false;
        if(character!=L'}'&&character!=L')'&&character!=L']')return false;
        const auto range=selection(window);
        const auto start=static_cast<std::size_t>(range.cpMin),end=static_cast<std::size_t>(range.cpMax);
        const auto line_start=line_start_at(now().text,start);
        if(now().text.find_first_not_of(L" \t",line_start)<start)return false;
        const auto context=code_context(now().text,start);
        if(context.lexical!=Lexical::code)return false;
        auto base_start=line_start;
        auto base_end=start;
        if(!context.openings.empty()&&closing_for(context.openings.back().character)==character) {
            const auto opening_position=context.openings.back().position;
            base_start=line_start_at(now().text,opening_position);
            base_end=base_start;
            while(base_end<opening_position&&(now().text[base_end]==L' '||now().text[base_end]==L'\t'))++base_end;
        } else {
            // Retain Android's one-level fallback for an unfinished snippet
            // whose opening brace has not been written in this editor yet.
            if(character!=L'}'||start==line_start)return false;
            if(now().text[start-1]==L'\t')--base_end;
            else while(base_end>line_start&&start-base_end<4&&now().text[base_end-1]==L' ')--base_end;
        }
        const auto base=now().text.substr(base_start,base_end-base_start);
        const bool existing=start==end&&end<now().text.size()&&now().text[end]==character;
        if(existing&&base==now().text.substr(line_start,start-line_start)) {
            select(window,start+1,start+1);cursor_snapshot();return true;
        }
        std::vector<std::int64_t> inherited;
        for(const auto unit:now().units)if(unit>=static_cast<std::int64_t>(base_start)&&unit+4<=static_cast<std::int64_t>(base_end))
            inherited.push_back(static_cast<std::int64_t>(line_start)+unit-static_cast<std::int64_t>(base_start));
        const auto cursor=line_start+base.size()+1;
        replace(line_start,existing?end+1:end,base+character,cursor,cursor,{},inherited);
        return true;
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
                    (character == L'"' && context == Lexical::raw_string &&
                        context_at(now().text,start+1)==Lexical::code) ||
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
            if (numeric_token_before(now().text,start)) return false;
            closing = L'\'';
        }
        if (!closing) return false;
        const auto replacement = std::wstring(1, character) + now().text.substr(start, end - start) + closing;
        replace(start, end, replacement, start + 1, end + 1);
        return true;
    }

    wchar_t dead_quote_character(WPARAM key,LPARAM key_data) const {
        if(!code_assistance||key>255)return L'\0';
        std::array<BYTE,256> keyboard{};
        if(!GetKeyboardState(keyboard.data()))return L'\0';
        const auto layout=GetKeyboardLayout(0);
        auto scan=static_cast<UINT>((static_cast<std::uintptr_t>(key_data)>>16)&0xFF);
        if(!scan)scan=MapVirtualKeyExW(static_cast<UINT>(key),MAPVK_VK_TO_VSC,layout);
        std::array<wchar_t,8> translated{};
        const auto count=ToUnicodeEx(static_cast<UINT>(key),scan,keyboard.data(),translated.data(),
            static_cast<int>(translated.size()),4,layout);
        return count<0&&(translated.front()==L'\''||translated.front()==L'"')?translated.front():L'\0';
    }

    bool insert_shortcut(std::string_view text) {
        if (text.empty() || !IsWindow(window) || !IsWindowEnabled(window) || (GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY)) return false;
        const auto supplied=wide(text);
        if(supplied.find(L'\0')!=supplied.npos||(!multiline&&supplied.find_first_of(L"\r\n")!=supplied.npos))return false;
        capture();
        if(supplied==L"\t"&&code_assistance){insert_tab();return true;}
        if((supplied==L"\n"||supplied==L"\r"||supplied==L"\r\n")&&newline())return true;
        if(supplied.size()==1&&closing_delimiter(supplied.front()))return true;
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
            if(message==literal_quote_message) {
                const auto quote=static_cast<wchar_t>(wparam);
                return (quote==L'\''||quote==L'"')&&self.insert_shortcut(std::string(1,static_cast<char>(quote)));
            }
            if (message == WM_NCDESTROY) {
                RemoveWindowSubclass(window, subclass, 1);
                self.native_scrollbars.window = nullptr;
                self.window = nullptr;
                return DefSubclassProc(window, message, wparam, lparam);
            }
            if (message == WM_GETDLGCODE) {
                auto result = DefSubclassProc(window, message, wparam, lparam) |
                    DLGC_WANTCHARS | DLGC_WANTARROWS;
                const auto key = lparam ? reinterpret_cast<MSG*>(lparam)->wParam : wparam;
                if (self.code_assistance) result |= DLGC_WANTTAB;
                else if (key == VK_TAB) result &= ~(DLGC_WANTTAB | DLGC_WANTALLKEYS);
                if (key == VK_RETURN) result |= DLGC_WANTALLKEYS;
                return result;
            }
            if(message==WM_KILLFOCUS||message==WM_INPUTLANGCHANGE)self.literal_quote_key.reset();
            const bool editable=IsWindowEnabled(window)&&!(GetWindowLongPtrW(window,GWL_STYLE)&ES_READONLY);
            if(!editable&&((message==WM_CHAR&&(wparam>=L' '||wparam==L'\b'||wparam==L'\t'||
                    wparam==L'\r'||wparam==L'\n'||wparam==0x1A||wparam==0x19))||
                    (message==WM_KEYDOWN&&(wparam==VK_TAB||wparam==VK_BACK||wparam==VK_RETURN||
                        ((GetKeyState(VK_CONTROL)&0x8000)&&(wparam==L'Z'||wparam==L'Y'))))||
                    message==WM_UNDO||message==EM_UNDO||message==EM_REDO))return 0;
            // Programmatic key delivery must respect the same lock as the
            // native editor; custom newline insertion bypasses RichEdit's edit path.
            if (((message == WM_KEYDOWN && wparam == VK_RETURN) ||
                    (message == WM_CHAR && (wparam == L'\r' || wparam == L'\n'))) &&
                    (!IsWindowEnabled(window) || (GetWindowLongPtrW(window,GWL_STYLE) & ES_READONLY))) {
                self.suppress_enter_char = false;
                return 0;
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
                if (self.multiline && wparam == VK_RETURN && self.newline()) {
                    self.suppress_enter_char=true;return 0;
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
                if ((wparam == L'\r' || wparam == L'\n') && self.multiline) {
                    if(!std::exchange(self.suppress_enter_char,false))self.newline();return 0;
                }
                if(wparam<=0xFFFF&&self.closing_delimiter(static_cast<wchar_t>(wparam)))return 0;
                if (self.code_assistance && wparam >= L' ' && wparam <= 0xFFFF &&
                        self.pair(static_cast<wchar_t>(wparam))) return 0;
            }
            const bool paste_key=message==WM_KEYDOWN&&
                (((GetKeyState(VK_CONTROL)&0x8000)&&wparam==L'V')||
                    ((GetKeyState(VK_SHIFT)&0x8000)&&wparam==VK_INSERT));
            const auto mutating = message == WM_CHAR || message == WM_PASTE || message == EM_PASTESPECIAL || paste_key || message == WM_CUT ||
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
            std::optional<presentation::editor_render::Scope> rendering;
            if(mutating&&!self.applying)rendering.emplace(window);
            LRESULT result=0;
            if(mutating&&!self.applying){
                FrozenText frozen(window);
                result=DefSubclassProc(window,message,wparam,lparam);
            }else result=DefSubclassProc(window,message,wparam,lparam);
            if (message == WM_SETFONT) self.tab_stops();
            if (mutating) {
                self.capture();
                self.pending_edit.reset();
                if(!self.applying)if(const auto parent=GetParent(window))
                    SendMessageW(parent,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(window),CodeEditor::layout_ready_notification),reinterpret_cast<LPARAM>(window));
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
    std::function<void()> submit, bool code_assistance, bool auto_indent, bool line_numbers)
    : impl_(std::make_unique<Impl>(rich_edit, multiline, std::move(submit), code_assistance, auto_indent)) {
    if (!IsWindow(rich_edit)) throw std::invalid_argument("CodeEditor requires a RichEdit control");
    presentation::use_plain_text_input(rich_edit);
    SendMessageW(rich_edit, EM_SETUNDOLIMIT, 0, 0);
    SendMessageW(rich_edit, EM_EXLIMITTEXT, 0, 4 * 1024 * 1024);
    SendMessageW(rich_edit, EM_SETEVENTMASK, 0, SendMessageW(rich_edit, EM_GETEVENTMASK, 0, 0) | ENM_CHANGE);
    reset(std::move(document));
    if (!SetWindowSubclass(rich_edit, Impl::subclass, 1, reinterpret_cast<DWORD_PTR>(impl_.get())))
        throw std::runtime_error("Cannot attach code editing behavior");
    try{if(multiline&&code_assistance&&line_numbers)impl_->gutter=std::make_unique<CodeGutter>(rich_edit);}
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
    // Enter/paste policy applies to new input, not to historical answers.
    // Lesson fields use a multiline RichEdit even when Enter checks a one-line
    // answer; retain older documents verbatim when that control can show them.
    const bool display_multiline=(GetWindowLongPtrW(impl_->window,GWL_STYLE)&ES_MULTILINE)!=0;
    if (!impl_->multiline && !display_multiline && text.find(L'\n') != std::wstring::npos)
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
    impl_->apply(impl_->now(), false, true);
}
void CodeEditor::insert_tab() { impl_->insert_tab(); }
bool CodeEditor::insert_shortcut(std::string_view text) { return impl_->insert_shortcut(text); }
bool CodeEditor::handle_key_message(const MSG& message) {
    if(!message.hwnd||(message.message!=WM_KEYDOWN&&message.message!=WM_KEYUP))return false;
    DWORD_PTR data=0;
    if(!GetWindowSubclass(message.hwnd,Impl::subclass,1,&data)||!data)return false;
    auto& editor=*reinterpret_cast<Impl*>(data);
    if(message.wParam==VK_RETURN&&editor.multiline){
        if(message.message==WM_KEYUP){editor.suppress_enter_char=false;return true;}
        if(!IsWindowEnabled(message.hwnd)||(GetWindowLongPtrW(message.hwnd,GWL_STYLE)&ES_READONLY))return false;
        // Use the existing newline transaction before dialog/default-button
        // routing. No TranslateMessage follows a handled key, so insert once.
        editor.capture();
        SendMessageW(message.hwnd,WM_KEYDOWN,VK_RETURN,message.lParam);
        return true;
    }
    if(message.message==WM_KEYUP) {
        if(editor.literal_quote_key&&*editor.literal_quote_key==message.wParam) {
            editor.literal_quote_key.reset();return true;
        }
        return false;
    }
    if(!IsWindowEnabled(message.hwnd)||(GetWindowLongPtrW(message.hwnd,GWL_STYLE)&ES_READONLY))return false;
    const auto quote=editor.dead_quote_character(message.wParam,message.lParam);
    if(!quote)return false;
    // No TranslateMessage call has happened yet, so neither Windows nor
    // RichEdit has a dead character to consume/reset. Other accents and plain
    // input fields continue through their native message paths unchanged.
    editor.literal_quote_key=message.wParam;
    SendMessageW(message.hwnd,literal_quote_message,quote,0);
    return true;
}
void CodeEditor::undo() { impl_->undo(); }
void CodeEditor::redo() { impl_->redo(); }

} // namespace lgaed::windows
