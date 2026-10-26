#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::windows {

struct EditorDocument {
    std::string text;
    // UTF-16 offsets in text as supplied (CRLF counts twice). Each marks four spaces whose
    // origin is predefined indentation or a deliberate TAB completion.
    std::vector<std::int64_t> indentation_starts;
    std::int64_t selection_start = 0;
    std::int64_t selection_end = 0;
};

// Call only for the original source or an explicit Reset, never on a draft.
[[nodiscard]] EditorDocument predefined_code_document(std::string_view source);

class CodeEditor {
public:
    // WM_COMMAND notification after an adapter edit and its formatting have
    // finished native layout. Distinct from RichEdit's repaint EN_UPDATE.
    static constexpr WORD layout_ready_notification=0x7F01;
    CodeEditor(HWND rich_edit, EditorDocument document, bool multiline,
        std::function<void()> submit = {}, bool code_assistance = true,
        bool auto_indent = true, bool line_numbers = true);
    ~CodeEditor();
    CodeEditor(const CodeEditor&) = delete;
    CodeEditor& operator=(const CodeEditor&) = delete;

    // The parent forwards EN_CHANGE. Reentrant changes made by this adapter
    // are guarded, and each real edit creates one complete undo transaction.
    void capture_change();
    [[nodiscard]] EditorDocument document() const;
    // Replaces the document/history without EN_CHANGE; the owner handles reset.
    void reset(EditorDocument document);
    void insert_tab();
    // Inserts one configured shortcut as one existing editor transaction.
    // Single-character pairs retain the same assistance as physical typing.
    // Single-line fields reject line breaks without invoking their submit action.
    [[nodiscard]] bool insert_shortcut(std::string_view text);
    // Run before IsDialogMessage/TranslateMessage. Literal programming quotes
    // on international keyboards must not start native dead-key composition.
    // Returns true only for a handled key pulse in this adapter's own field.
    [[nodiscard]] static bool handle_key_message(const MSG& message);
    void undo();
    void redo();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lgaed::windows
