#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <richole.h>
#include <shlobj.h>

#include <array>
#include <cstring>
#include <cwchar>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace lgaed::windows;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

struct OleSession {
    HRESULT result = OleInitialize(nullptr);
    ~OleSession() { if (SUCCEEDED(result)) OleUninitialize(); }
};

template<class T> struct ComRelease {
    void operator()(T* value) const { if (value) value->Release(); }
};
template<class T> using ComOwner = std::unique_ptr<T, ComRelease<T>>;

struct ClipboardBlob {
    UINT format;
    std::vector<unsigned char> bytes;
};

template<class Character>
ClipboardBlob clipboard_string(UINT format, std::basic_string_view<Character> value) {
    ClipboardBlob result{format, std::vector<unsigned char>((value.size() + 1) * sizeof(Character))};
    std::memcpy(result.bytes.data(), value.data(), value.size() * sizeof(Character));
    return result;
}

HGLOBAL global_bytes(const std::vector<unsigned char>& bytes) {
    const auto handle = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    require(handle != nullptr, "Allocate native clipboard data");
    const auto destination = GlobalLock(handle);
    if (!destination) { GlobalFree(handle); throw std::runtime_error("Lock native clipboard data"); }
    std::memcpy(destination, bytes.data(), bytes.size());
    GlobalUnlock(handle);
    return handle;
}

UINT rtf_format() {
    const auto format = RegisterClipboardFormatW(L"Rich Text Format");
    require(format != 0, "Register the actual Windows RTF clipboard format");
    return format;
}

std::vector<ClipboardBlob> styled_payload(bool unicode = true) {
    // The same word is oversized, red and italic in RTF. Unicode and ANSI
    // deliberately differ so the test also observes the preferred text format.
    std::vector<ClipboardBlob> result{
        clipboard_string<char>(rtf_format(),
            "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times New Roman;}}"
            "{\\colortbl;\\red255\\green0\\blue0;}\\f0\\fs72\\cf1\\b\\i pegado}"),
        clipboard_string<char>(CF_TEXT, "ansi")};
    if (unicode) result.push_back(clipboard_string<wchar_t>(CF_UNICODETEXT, L"pegado"));
    return result;
}

std::vector<ClipboardBlob> image_payload() {
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header); header.biWidth = 1; header.biHeight = 1;
    header.biPlanes = 1; header.biBitCount = 32; header.biCompression = BI_RGB;
    header.biSizeImage = sizeof(DWORD);
    ClipboardBlob dib{CF_DIB, std::vector<unsigned char>(sizeof(header) + sizeof(DWORD))};
    const DWORD pixel = 0x00336699;
    std::memcpy(dib.bytes.data(), &header, sizeof(header));
    std::memcpy(dib.bytes.data() + sizeof(header), &pixel, sizeof(pixel));
    return {std::move(dib)};
}

// Publish this real IDataObject through OLE's clipboard, then exercise the
// user's native paste path. IRichEditOle::ImportDataObject is a trusted
// programmatic import API; it bypasses QueryAcceptData and read-only input.
class TransferData final : public IDataObject {
    LONG references_ = 1;
    std::vector<ClipboardBlob> formats_;
public:
    explicit TransferData(std::vector<ClipboardBlob> formats) : formats_(std::move(formats)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject) return E_NOINTERFACE;
        *value = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (!format) return E_POINTER;
        if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        if (format->lindex != -1) return DV_E_LINDEX;
        for (const auto& item : formats_) if (item.format == format->cfFormat) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (!medium) return E_POINTER;
        *medium = {};
        const auto available = QueryGetData(format);
        if (FAILED(available)) return available;
        try {
            for (const auto& item : formats_) if (item.format == format->cfFormat) {
                medium->hGlobal = global_bytes(item.bytes); medium->tymed = TYMED_HGLOBAL;
                return S_OK;
            }
        } catch (...) { return E_OUTOFMEMORY; }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* result) override {
        if (!result) return E_POINTER;
        result->ptd = nullptr; return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        try {
            std::vector<FORMATETC> formats;
            for (const auto& item : formats_)
                formats.push_back({static_cast<CLIPFORMAT>(item.format), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL});
            return SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), result);
        } catch (...) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

ComOwner<IRichEditOle> native_ole(HWND edit) {
    IRichEditOle* value = nullptr;
    require(SendMessageW(edit, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&value)) != 0 && value,
        "Obtain the real RichEdit OLE interface");
    return ComOwner<IRichEditOle>(value);
}

struct ClipboardHandle {
    UINT format = 0;
    HANDLE handle = nullptr;
    ClipboardHandle(UINT type, HANDLE value) : format(type), handle(value) {}
    ClipboardHandle(ClipboardHandle&& other) noexcept : format(other.format), handle(std::exchange(other.handle, nullptr)) {}
    ClipboardHandle(const ClipboardHandle&) = delete;
    ~ClipboardHandle() {
        if (!handle) return;
        STGMEDIUM medium{};
        medium.tymed = (format == CF_BITMAP || format == CF_DSPBITMAP || format == CF_PALETTE) ? TYMED_GDI :
            (format == CF_METAFILEPICT || format == CF_DSPMETAFILEPICT) ? TYMED_MFPICT :
            (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) ? TYMED_ENHMF : TYMED_HGLOBAL;
        medium.hGlobal = static_cast<HGLOBAL>(handle);
        ReleaseStgMedium(&medium);
    }
    ClipboardHandle duplicate() const {
        HANDLE copy = nullptr;
        if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE)
            copy = CopyEnhMetaFileW(static_cast<HENHMETAFILE>(handle), nullptr);
        else {
            const auto type = format == CF_DSPBITMAP ? CF_BITMAP :
                format == CF_DSPMETAFILEPICT ? CF_METAFILEPICT : format;
            copy = OleDuplicateData(handle, static_cast<CLIPFORMAT>(type), GMEM_MOVEABLE);
        }
        require(copy != nullptr, "Snapshot the complete original clipboard before testing paste");
        return ClipboardHandle(format, copy);
    }
};

struct OpenClipboardScope {
    explicit OpenClipboardScope(HWND owner) {
        // Clipboard ownership notifications briefly overlap other desktop
        // consumers. Retry acquisition before inspecting or changing any data.
        for(unsigned attempt=0;attempt<25;++attempt){
            if(OpenClipboard(owner))return;
            Sleep(10);
        }
        require(false,"Open the native clipboard for the isolated paste test after bounded retry");
    }
    ~OpenClipboardScope() { CloseClipboard(); }
};

bool clipboard_restore_failed = false;
struct ClipboardScope {
    HWND owner;
    HWND published_owner = nullptr;
    std::vector<ClipboardHandle> saved;
    DWORD saved_sequence = 0;
    bool changed = false;
    explicit ClipboardScope(HWND window) : owner(window) {
        OpenClipboardScope opened(owner);
        UINT format = 0;
        for (;;) {
            SetLastError(ERROR_SUCCESS);
            format = EnumClipboardFormats(format);
            if (!format) { require(GetLastError() == ERROR_SUCCESS, "Enumerate the original clipboard"); break; }
            // If a format cannot be duplicated faithfully, fail before clearing
            // anything. Ordinary text, RTF, HTML, DIB, files and GDI are retained.
            require(format != CF_OWNERDISPLAY && !(format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST) &&
                !(format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST),
                "Original clipboard has an owner-managed format; leave it untouched");
            const auto handle = GetClipboardData(format);
            require(handle != nullptr, "Materialize every original clipboard format before replacing it");
            ClipboardHandle borrowed(format, handle);
            try { saved.push_back(borrowed.duplicate()); }
            catch (...) { borrowed.handle = nullptr; throw; }
            borrowed.handle = nullptr;
        }
        saved_sequence = GetClipboardSequenceNumber();
    }
    void publish(const std::vector<ClipboardBlob>& payload) {
        std::vector<ClipboardHandle> pending;
        for (const auto& item : payload) pending.emplace_back(item.format, global_bytes(item.bytes));
        OpenClipboardScope opened(owner);
        require(GetClipboardSequenceNumber() == saved_sequence,
            "Clipboard changed after the snapshot; preserve the newer copy");
        require(EmptyClipboard() != FALSE, "Prepare the native paste source");
        changed = true;
        published_owner = owner;
        for (auto& item : pending) {
            require(SetClipboardData(item.format, item.handle) != nullptr, "Publish each native clipboard representation");
            item.handle = nullptr;
        }
    }
    void publish_ole(IDataObject* data) {
        require(data != nullptr && GetClipboardSequenceNumber() == saved_sequence,
            "Clipboard changed after the snapshot; preserve the newer OLE copy");
        const auto result = OleSetClipboard(data);
        // OLE deliberately owns delayed-rendered data through its own hidden
        // window, rather than this test's host. Track that exact owned window
        // so cleanup also runs on failure and never overwrites another copy.
        const auto current = GetClipboardOwner(); DWORD process = 0;
        if (current) GetWindowThreadProcessId(current,&process);
        if (process == GetCurrentProcessId()) published_owner = current;
        changed = SUCCEEDED(result) || GetClipboardSequenceNumber() != saved_sequence;
        require(result == S_OK && published_owner != nullptr,"Publish native OLE clipboard data under this test process's ownership");
    }
    bool restore() noexcept {
        if (!changed) return true;
        // Retain our copies until the entire restore succeeds, including when
        // another clipboard consumer temporarily has it open.
        for (unsigned attempt = 0; attempt != 10; ++attempt) {
            try {
                OpenClipboardScope opened(owner);
                if (!published_owner || GetClipboardOwner() != published_owner) { changed = false; return true; }
                require(EmptyClipboard() != FALSE, "Restore the original clipboard");
                for (const auto& item : saved) {
                    auto copy = item.duplicate();
                    require(SetClipboardData(copy.format, copy.handle) != nullptr, "Restore an original clipboard format");
                    copy.handle = nullptr;
                }
                changed = false;
                return true;
            } catch (...) { Sleep(20); }
        }
        return false;
    }
    ~ClipboardScope() {
        if (!restore()) { clipboard_restore_failed = true; std::cerr << "FAIL restore original clipboard\n"; }
    }
};

struct KeyboardScope {
    std::array<BYTE, 256> original{};
    KeyboardScope() { require(GetKeyboardState(original.data()) != FALSE, "Read test-thread keyboard state"); }
    ~KeyboardScope() { SetKeyboardState(original.data()); }
};

void paste_shortcut(HWND edit, bool control_v) {
    KeyboardScope saved;
    auto keys = saved.original;
    for (const auto key : {VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_SHIFT, VK_LSHIFT, VK_RSHIFT,
            VK_MENU, VK_LMENU, VK_RMENU}) keys[key] = 0;
    keys[control_v ? VK_CONTROL : VK_SHIFT] = 0x80;
    require(SetKeyboardState(keys.data()) != FALSE, "Set paste modifiers on this test thread only");
    SetFocus(edit);
    MSG message{edit, WM_KEYDOWN, static_cast<WPARAM>(control_v ? 'V' : VK_INSERT), 1};
    TranslateMessage(&message);
    DispatchMessageW(&message);
    MSG character{};
    while (PeekMessageW(&character, edit, WM_CHAR, WM_CHAR, PM_REMOVE)) DispatchMessageW(&character);
    SendMessageW(edit, WM_KEYUP, message.wParam, 1);
}

CHARRANGE selected_range(HWND edit) {
    CHARRANGE range{};
    SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    return range;
}

CHARFORMAT2W local_paste_format(HWND edit) {
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    format.yHeight = 260; format.crTextColor = RGB(30, 80, 130);
    std::wmemcpy(format.szFaceName, L"Consolas", 9);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
    return format;
}

void require_local_paste_format(HWND edit, LONG start, LONG end, const CHARFORMAT2W& expected) {
    const auto original = selected_range(edit);
    const CHARRANGE selection{start, end};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    CHARFORMAT2W actual{};
    actual.cbSize = sizeof(actual);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&actual));
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&original));
    require((actual.dwMask & expected.dwMask) == expected.dwMask &&
        actual.yHeight == expected.yHeight && actual.crTextColor == expected.crTextColor &&
        std::wstring_view(actual.szFaceName) == expected.szFaceName &&
        !(actual.dwEffects & (CFE_AUTOCOLOR | CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE)),
        "Paste imported the external font, size, color or emphasis instead of the field's local style: actual(mask/effects/height/color/face)="
        + std::to_string(actual.dwMask) + "/" + std::to_string(actual.dwEffects) + "/" + std::to_string(actual.yHeight)
        + "/" + std::to_string(actual.crTextColor) + "/" + lgaed::presentation::utf8(actual.szFaceName)
        + " expected(mask/effects/height/color/face)=" + std::to_string(expected.dwMask) + "/" + std::to_string(expected.dwEffects)
        + "/" + std::to_string(expected.yHeight) + "/" + std::to_string(expected.crTextColor) + "/" + lgaed::presentation::utf8(expected.szFaceName));
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
        std::function<void()> submit = {}, bool code_assistance = true, ChangeProbe* probe = nullptr,
        bool auto_indent = true, bool line_numbers = true) {
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
            std::move(submit), code_assistance, auto_indent, line_numbers);
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

void require_pasted_document(Fixture& field, const CHARFORMAT2W& format, std::string_view inserted = "pegado") {
    const auto expected = "A" + std::string(inserted) + "B";
    const auto end = static_cast<LONG>(1 + inserted.size());
    auto document = field.adapter->document();
    require(document.text == expected && document.selection_start == end && document.selection_end == end,
        "Native paste must replace only the selected text and place its caret after the insertion: document='" + document.text
        + "' selection=" + std::to_string(document.selection_start) + ":" + std::to_string(document.selection_end)
        + " visible='" + lgaed::presentation::edit_text(field.edit) + "' native-selection="
        + std::to_string(selected_range(field.edit).cpMin) + ":" + std::to_string(selected_range(field.edit).cpMax)
        + " expected='" + expected + "' selection=" + std::to_string(end) + ":" + std::to_string(end));
    require_local_paste_format(field.edit, 1, end, format);
    require(SendMessageW(field.edit, EM_CANUNDO, 0, 0) != 0, "Paste must create a real editor undo transaction");
    field.adapter->undo();
    document = field.adapter->document();
    require(document.text == "AXXB" && document.selection_start == 1 && document.selection_end == 3,
        "One Undo must restore the exact text and selection replaced by paste");
    require(!SendMessageW(field.edit, EM_CANUNDO, 0, 0), "Paste must not create duplicate undo transactions");
    field.adapter->redo();
    document = field.adapter->document();
    require(document.text == expected && document.selection_start == end && document.selection_end == end,
        "One Redo must restore the same pasted text and caret");
    require_local_paste_format(field.edit, 1, end, format);
}

void require_unchanged(Fixture& field, const EditorDocument& expected, bool can_undo, bool can_redo) {
    const auto actual = field.adapter->document();
    require(actual.text == expected.text && actual.selection_start == expected.selection_start &&
        actual.selection_end == expected.selection_end && actual.indentation_starts == expected.indentation_starts,
        "Declined paste must retain text, selection and indentation identity: document='" + actual.text
        + "' selection=" + std::to_string(actual.selection_start) + ":" + std::to_string(actual.selection_end)
        + " visible='" + lgaed::presentation::edit_text(field.edit) + "' native-selection="
        + std::to_string(selected_range(field.edit).cpMin) + ":" + std::to_string(selected_range(field.edit).cpMax)
        + " expected='" + expected.text + "' selection=" + std::to_string(expected.selection_start)
        + ":" + std::to_string(expected.selection_end) + " indentation-count(actual/expected)="
        + std::to_string(actual.indentation_starts.size()) + "/" + std::to_string(expected.indentation_starts.size()));
    require((SendMessageW(field.edit, EM_CANUNDO, 0, 0) != 0) == can_undo &&
        (SendMessageW(field.edit, EM_CANREDO, 0, 0) != 0) == can_redo,
        "Declined paste must preserve the current undo and redo branches");
    require(native_ole(field.edit)->GetObjectCount() == 0, "An image or OLE object entered a text-only input");
}

} // namespace

int main() {
    OleSession ole;
    if (FAILED(ole.result)) { std::cerr << "Cannot initialize native OLE paste tests\n"; return 1; }
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

    test("OLE clipboard paste prefers plain text over explicit RTF and retains local formatting and history", [] {
        for (const bool unicode : {true, false}) for (const bool multiline : {false, true}) {
            std::cout << "QA OLE paste unicode=" << unicode << " multiline=" << multiline << '\n';
            Fixture field({"AXXB", {}, 1, 3}, multiline, {}, true, nullptr, false, false);
            const auto format = local_paste_format(field.edit);
            field.cursor(1, 3);
            ComOwner<TransferData> data(new TransferData(styled_payload(unicode)));
            ClipboardScope clipboard(field.host); clipboard.publish_ole(data.get());
            SendMessageW(field.edit,EM_PASTESPECIAL,rtf_format(),0);
            require_pasted_document(field, format, unicode ? "pegado" : "ansi");
            require(clipboard.restore(),"Restore the original clipboard after OLE paste");
        }
    });

    test("Clipboard and keyboard paste preserve selected text undo and the field style", [] {
        for (const auto route : {0, 1, 2, 3}) {
            std::cout << "QA clipboard paste route=" << route << '\n';
            Fixture field({"AXXB", {}, 1, 3}, true, {}, true, nullptr, false, false);
            const auto format = local_paste_format(field.edit);
            field.cursor(1, 3);
            ClipboardScope clipboard(field.host);
            clipboard.publish(styled_payload());
            if (route == 0) SendMessageW(field.edit, WM_PASTE, 0, 0);
            else if (route == 1) SendMessageW(field.edit, EM_PASTESPECIAL, rtf_format(), 0);
            else paste_shortcut(field.edit, route == 2); // Ctrl+V, then Shift+Insert.
            require_pasted_document(field, format);
            require(clipboard.restore(), "Restore the original clipboard after native paste");
        }
    });

    test("Image-only native and OLE clipboard pastes leave text selection and redo untouched", [] {
        Fixture field({"AXXB", {}, 1, 3}, true, {}, true, nullptr, false, false);
        field.character(L'x');
        field.adapter->undo();
        const auto before = field.adapter->document();
        require(SendMessageW(field.edit, EM_CANREDO, 0, 0) != 0, "Prepare an existing redo branch before rejected paste");
        ClipboardScope clipboard(field.host);
        clipboard.publish(image_payload());
        SendMessageW(field.edit, WM_PASTE, 0, 0);
        require_unchanged(field, before, false, true);
        SendMessageW(field.edit, EM_PASTESPECIAL, CF_DIB, 0);
        require_unchanged(field, before, false, true);
        require(clipboard.restore(), "Restore original clipboard after rejected image");
        {
            ComOwner<TransferData> image(new TransferData(image_payload()));
            ClipboardScope ole_clipboard(field.host); ole_clipboard.publish_ole(image.get());
            SendMessageW(field.edit,EM_PASTESPECIAL,CF_DIB,0);
            require_unchanged(field, before, false, true);
            require(ole_clipboard.restore(),"Restore original clipboard after rejected OLE image");
        }
        field.adapter->redo();
        require(field.adapter->document().text == "AxB", "Rejected images must not erase the earlier redo operation");
    });

    test("Disabled and read-only editors decline text and RTF through every user paste entry point", [] {
        for (const bool readonly : {false, true}) {
            Fixture field({"AXXB", {}, 1, 3}, true, {}, true, nullptr, false, false);
            field.character(L'x'); field.adapter->undo();
            const auto before = field.adapter->document();
            if (readonly) SendMessageW(field.edit, EM_SETREADONLY, TRUE, 0);
            else EnableWindow(field.edit, FALSE);
            ClipboardScope clipboard(field.host);
            clipboard.publish(styled_payload());
            SendMessageW(field.edit, WM_PASTE, 0, 0);
            require_unchanged(field, before, false, true);
            SendMessageW(field.edit, EM_PASTESPECIAL, rtf_format(), 0);
            require_unchanged(field, before, false, true);
            require(clipboard.restore(), "Restore original clipboard after locked-field paste");
            ComOwner<TransferData> data(new TransferData(styled_payload()));
            ClipboardScope ole_clipboard(field.host); ole_clipboard.publish_ole(data.get());
            SendMessageW(field.edit,EM_PASTESPECIAL,rtf_format(),0);
            require_unchanged(field, before, false, true);
            require(ole_clipboard.restore(),"Restore original clipboard after locked-field OLE paste");
        }
    });

    test("Themed stdin RichEdit without CodeEditor accepts plain paste and rejects image objects", [] {
        Fixture owner;
        const auto input = CreateWindowExW(0, MSFTEDIT_CLASS, L"AXXB",
            WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 300, 80, owner.host,
            reinterpret_cast<HMENU>(102), GetModuleHandleW(nullptr), nullptr);
        require(input != nullptr && !GetPropW(input, L"lgaed.test.editor"), "Create an independent stdin field without CodeEditor");
        lgaed::presentation::reader_theme::apply(input, lgaed::presentation::reader_theme::Surface::field);
        lgaed::presentation::set_edit_hint(input,"Escribe la salida");
        const auto format = local_paste_format(input);
        const CHARRANGE selected{1, 3};
        SendMessageW(input, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selected));
        SendMessageW(input, EM_EMPTYUNDOBUFFER, 0, 0);
        ClipboardScope clipboard(owner.host);
        clipboard.publish(styled_payload());
        SendMessageW(input, EM_PASTESPECIAL, rtf_format(), 0);
        require(lgaed::presentation::edit_text(input) == "ApegadoB", "The theme must install text-only paste without CodeEditor: actual='"
            + lgaed::presentation::edit_text(input) + "' expected='ApegadoB' selection=" + std::to_string(selected_range(input).cpMin)
            + ":" + std::to_string(selected_range(input).cpMax));
        require_local_paste_format(input, 1, 7, format);
        const auto selection = selected_range(input);
        const auto can_undo = SendMessageW(input, EM_CANUNDO, 0, 0);
        require(can_undo != 0, "The standalone input must retain native RichEdit undo");
        ComOwner<TransferData> image(new TransferData(image_payload()));
        {
            ClipboardScope image_clipboard(owner.host); image_clipboard.publish_ole(image.get());
            SendMessageW(input,EM_PASTESPECIAL,CF_DIB,0);
            require(image_clipboard.restore(),"Restore the styled clipboard after rejected standalone OLE image");
        }
        const auto after = selected_range(input);
        require(lgaed::presentation::edit_text(input) == "ApegadoB" && after.cpMin == selection.cpMin &&
            after.cpMax == selection.cpMax && SendMessageW(input, EM_CANUNDO, 0, 0) == can_undo &&
            native_ole(input)->GetObjectCount() == 0,
            "Image rejection must preserve standalone stdin text, caret, history and object count");
        SendMessageW(input, EM_UNDO, 0, 0);
        require(lgaed::presentation::edit_text(input) == "AXXB", "Standalone stdin paste keeps native Undo");
        SendMessageW(input, EM_REDO, 0, 0);
        require(lgaed::presentation::edit_text(input) == "ApegadoB", "Standalone stdin paste keeps native Redo");
        require_local_paste_format(input, 1, 7, format);
        lgaed::presentation::set_edit_text(input,"");
        SendMessageW(input,EM_EMPTYUNDOBUFFER,0,0);
        SendMessageW(input,WM_PASTE,0,0);
        require(lgaed::presentation::edit_text(input)=="pegado" && GetWindowTextLengthW(input)==6 &&
            selected_range(input).cpMin==6 && selected_range(input).cpMax==6,
            "Pasting into an empty hinted answer must insert only plain text and retain its caret");
        SendMessageW(input,EM_UNDO,0,0);
        require(lgaed::presentation::edit_text(input).empty() && selected_range(input).cpMin==0 && selected_range(input).cpMax==0,
            "Undoing a paste into a hinted answer must restore the empty insertion point");
        SendMessageW(input,EM_REDO,0,0);
        require(lgaed::presentation::edit_text(input)=="pegado" && native_ole(input)->GetObjectCount()==0,
            "Redoing a paste into a hinted answer must preserve text and reject embedded objects");
        require(clipboard.restore(), "Restore original clipboard after standalone stdin test");
    });

    test("OLE clipboard ownership and every prior text representation restore after a failed operation", [] {
        Fixture owner;
        ClipboardScope original(owner.host); const auto payload = styled_payload(); original.publish(payload);
        bool reached_failure = false;
        try {
            ClipboardScope nested(owner.host);
            ComOwner<TransferData> image(new TransferData(image_payload())); nested.publish_ole(image.get());
            reached_failure = true;
            throw std::runtime_error("Deliberate failure after publishing owned OLE data");
        } catch (const std::runtime_error&) {}
        require(reached_failure && !clipboard_restore_failed && GetClipboardOwner() == owner.host,
            "An exception while owning OLE clipboard data failed to restore the previous native owner");
        {
            OpenClipboardScope opened(owner.host);
            for (const auto& item : payload) {
                const auto data = static_cast<HGLOBAL>(GetClipboardData(item.format));
                require(data && GlobalSize(data) >= item.bytes.size(),"The failed OLE operation lost a prior clipboard representation");
                const auto bytes = GlobalLock(data); require(bytes != nullptr,"Read the restored prior clipboard representation");
                const bool matches = std::memcmp(bytes,item.bytes.data(),item.bytes.size()) == 0; GlobalUnlock(data);
                require(matches,"The failed OLE operation changed prior Unicode, ANSI or RTF clipboard bytes");
            }
        }
        require(original.restore(),"Restore the user's original clipboard after the controlled OLE failure regression");
    });

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

    test("Physical Enter without ES_WANTRETURN inserts one plain newline without autoindent", [] {
        for (const bool assistance : {false, true}) {
            unsigned submitted = 0;
            ChangeProbe probe;
            Fixture field({"    {", {}, 5, 5}, true, [&] { ++submitted; }, assistance, &probe, false, false);
            // Lesson answers use a multiline RichEdit without ES_WANTRETURN.
            SetWindowLongPtrW(field.edit, GWL_STYLE, GetWindowLongPtrW(field.edit, GWL_STYLE) & ~ES_WANTRETURN);
            SetFocus(field.edit);
            require(GetFocus() == field.edit, "Focus the real output prediction editor");
            MSG key{field.edit, WM_KEYDOWN, VK_RETURN, 1};
            if (!IsDialogMessageW(field.host, &key)) {
                TranslateMessage(&key);
                DispatchMessageW(&key);
            }
            MSG character{};
            while (PeekMessageW(&character, field.edit, WM_CHAR, WM_CHAR, PM_REMOVE)) DispatchMessageW(&character);
            SendMessageW(field.edit, WM_KEYUP, VK_RETURN, 1);
            const auto result = field.adapter->document();
            require(result.text == "    {\n" && result.selection_start == 6 && result.selection_end == 6,
                "Physical Enter failed to insert exactly one LF without copying indentation");
            require(submitted == 0 && GetFocus() == field.edit && probe.notifications == 1 &&
                probe.notifications_during_stream == 0 && !probe.callback_failed,
                "Enter submitted, moved focus or notified before the completed edit");
            field.adapter->undo();
            require(field.adapter->document().text == "    {" && field.adapter->document().selection_start == 5,
                "One Undo did not restore the text and caret before plain Enter");
            field.adapter->redo();
            require(field.adapter->document().text == result.text && field.adapter->document().selection_start == 6,
                "One Redo did not restore the same plain newline");
        }
    });

    test("Standalone Enter characters replace selections without requiring autoindent", [] {
        for (const auto character : {L'\r', L'\n'}) {
            unsigned submitted = 0;
            Fixture field({"EntradaSalida", {}, 7, 13}, true, [&] { ++submitted; }, true, nullptr, false, false);
            field.character(character);
            const auto result = field.adapter->document();
            require(result.text == "Entrada\n" && result.selection_start == 8 && result.selection_end == 8 && submitted == 0,
                "A direct Enter character failed to replace the exact selection with one LF");
            field.adapter->undo();
            const auto original = field.adapter->document();
            require(original.text == "EntradaSalida" && original.selection_start == 7 && original.selection_end == 13,
                "Undo did not restore the selected output characters");
        }
    });

    test("Single-line answers still submit once when autoindent is disabled", [] {
        for (const bool assistance : {false, true}) {
            unsigned submitted = 0;
            Fixture field({"<<", {}, 2, 2}, false, [&] { ++submitted; }, assistance, nullptr, false, false);
            field.key(VK_RETURN, L'\r');
            require(submitted == 1 && field.adapter->document().text == "<<",
                "A single-line key press inserted a newline or submitted twice");
            field.character(L'\n');
            require(submitted == 2 && field.adapter->document().text == "<<" && !SendMessageW(field.edit, EM_CANUNDO, 0, 0),
                "Standalone single-line Enter edited the answer or lost its submit action");
        }
    });

    test("Enter cannot modify or submit disabled and read-only answers", [] {
        for (const bool multiline : {false, true}) for (const bool readonly : {false, true}) {
            unsigned submitted = 0;
            Fixture field({"answer", {}, 6, 6}, multiline, [&] { ++submitted; }, true, nullptr, false, false);
            if (readonly) SendMessageW(field.edit, EM_SETREADONLY, TRUE, 0);
            else EnableWindow(field.edit, FALSE);
            field.key(VK_RETURN, L'\r');
            field.character(L'\n');
            require(field.adapter->document().text == "answer" && submitted == 0 && !SendMessageW(field.edit, EM_CANUNDO, 0, 0),
                "A locked answer accepted a newline or submitted through the custom editor");
        }
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

    test("Deleting an empty assisted pair is one reversible transaction in either input kind", [] {
        for(const auto multiline:{false,true})for(const auto opening:{L'(',L'[',L'{',L'"',L'\''}) {
            Fixture field({},multiline);field.character(opening);
            const auto paired=field.adapter->document();field.key(VK_BACK,L'\b');
            require(field.adapter->document().text.empty(),"Backspace left the closing half of an empty pair");
            field.adapter->undo();require(field.adapter->document().text==paired.text&&field.adapter->document().selection_start==1,
                "Undo did not restore both delimiters and their inner cursor");
            field.adapter->redo();require(field.adapter->document().text.empty(),"Redo did not delete the same complete pair");
        }
        Fixture plain({"()",{},1,1},true,{},false);plain.key(VK_BACK,L'\b');
        require(plain.adapter->document().text==")","A plain stdin-like input acquired programming pair deletion");
    });

    test("Multiline Enter inherits only proven indentation identities and splits braces", [] {
        const std::string original="    if (ready) {}";
        auto document=predefined_code_document(original);document.selection_start=document.selection_end=static_cast<std::int64_t>(original.size()-1);
        Fixture field(document);field.key(VK_RETURN,L'\r');
        auto after=field.adapter->document();
        require(after.text=="    if (ready) {\n    \t\n    }"&&after.selection_start==22,
            "Enter failed to inherit indentation, add one real tab and split the closing brace");
        require(after.indentation_starts==std::vector<std::int64_t>({0,17,23}),
            "Newline did not preserve predefined indentation identity on both inherited lines");
        field.key(VK_BACK,L'\b');require(field.adapter->document().text=="    if (ready) {\n    \n    }",
            "The new real TAB did not delete in one Backspace");
        field.key(VK_BACK,L'\b');require(field.adapter->document().text=="    if (ready) {\n\n    }",
            "Inherited predefined indentation lost its complete-unit Backspace behavior");
        field.adapter->undo();field.adapter->undo();field.adapter->undo();
        require(field.adapter->document().text==original&&field.adapter->document().indentation_starts==document.indentation_starts,
            "Undo did not restore the original source and indentation identity");
        Fixture manual({"    value;",{},10,10});manual.key(VK_RETURN,L'\r');manual.key(VK_BACK,L'\b');
        require(manual.adapter->document().text=="    value;\n   "&&manual.adapter->document().indentation_starts.empty(),
            "Copying manual indentation fabricated a tab unit");
    });

    test("Closing brace dedents a logical indentation while prediction disables only autoindent", [] {
        Fixture brace({"\t\t",{},2,2});brace.character(L'}');
        require(brace.adapter->document().text=="\t}"&&brace.adapter->document().selection_start==2,
            "A closing brace did not remove exactly one leading real tab");
        brace.adapter->undo();require(brace.adapter->document().text=="\t\t","Brace dedent lost its original Undo transaction");
        Fixture prediction({"    {",{},5,5},true,{},true,nullptr,false,false);
        prediction.key(VK_RETURN,L'\r');require(prediction.adapter->document().text=="    {\n",
            "An output prediction introduced programming indentation into the expected output");
        prediction.character(L'(');require(prediction.adapter->document().text=="    {\n()",
            "Disabling output autoindent also disabled shared pair assistance");
        prediction.key(VK_BACK,L'\b');prediction.adapter->insert_tab();
        require(prediction.adapter->document().text=="    {\n    "&&prediction.adapter->document().indentation_starts==std::vector<std::int64_t>{6},
            "Prediction TAB no longer shares the existing completion and origin journal");
        Fixture input({"  \t \n",{},5,5},true,{},false,nullptr,false,false);input.key(VK_RETURN,L'\r');
        require(input.adapter->document().text=="  \t \n\n","Plain stdin indentation or intentional whitespace was rewritten");
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

    test("Configured multi-character tokens preserve indentation origin and one complete undo",[] {
        const auto original=predefined_code_document("    return 0;");Fixture field(original);field.cursor(4);
        require(field.adapter->insert_shortcut("std::"),"A valid technical token was declined");
        require(field.adapter->document().text=="    std::return 0;"&&field.adapter->document().indentation_starts==original.indentation_starts,
            "A configured token changed adjacent predefined tab identities");
        field.adapter->undo();require(field.adapter->document().text==original.text&&field.adapter->document().indentation_starts==original.indentation_starts,
            "One Undo did not restore the full token and its original indentation metadata");
        field.key(VK_BACK,L'\b');require(field.adapter->document().text=="return 0;","Backspace after token Undo lost a predefined TAB unit");
        field.adapter->undo();require(field.adapter->document().indentation_starts==original.indentation_starts,"Undo did not restore a predefined unit after shortcut editing");
        Fixture manual(EditorDocument{"    return 0;"});manual.cursor(4);require(manual.adapter->insert_shortcut("<<"),"Insert a configured operator into manual spaces");
        manual.adapter->undo();manual.key(VK_BACK,L'\b');require(manual.adapter->document().text=="   return 0;"&&manual.adapter->document().indentation_starts.empty(),
            "A multi-character shortcut inferred a TAB from four deliberate manual spaces");
    });
    test("Shortcut line breaks are literal only in multiline code and never invoke submit",[] {
        unsigned submits=0;Fixture single(EditorDocument{"abc",{},1,2},false,[&]{++submits;});
        require(!single.adapter->insert_shortcut("x\ny")&&!single.adapter->insert_shortcut("\r\n")&&single.adapter->document().text=="abc"&&submits==0,
            "A single-line shortcut inserted a line break or simulated Enter checking");
        Fixture multi;require(multi.adapter->insert_shortcut("x\ny")&&multi.adapter->document().text=="x\ny","A multiline shortcut was not inserted as a literal snippet");
        multi.adapter->undo();require(multi.adapter->document().text.empty(),"A multiline snippet required more than one Undo");
        multi.adapter->redo();require(multi.adapter->document().text=="x\ny","A multiline snippet did not redo as one transaction");
    });
    if (clipboard_restore_failed) ++failed;
    UnregisterClassW(type.lpszClassName, type.hInstance);
    FreeLibrary(rich_edit);
    std::cout << "Native code editor: " << passed << " passed, " << failed << " failed.\n";
    return failed ? 1 : 0;
}
