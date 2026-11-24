#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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

// In-memory transfer between compatible controls for the same owned field.
// Carries the complete editor journal, including Redo, without serializing it
// or retaining a window/callback. Only CodeEditor can create its contents.
class EditorSession {
public:
    EditorSession() = default;
private:
    struct State;
    std::shared_ptr<const State> state_;
    friend class CodeEditor;
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
    // Reconcile native scroll with the final viewport after the parent updates
    // RichEdit's formatting rectangle; never changes source or saved grades.
    void normalize_viewport();
    [[nodiscard]] EditorDocument document() const;
    [[nodiscard]] EditorSession session() const;
    // Restore after the replacement control's layout is ready. Reuses the
    // existing journal without creating an edit or notifying the parent.
    void restore_session(EditorSession session);
    // The same transfer for owned descendant controls whose adapter is private
    // to a composite view. An ordinary/non-editor HWND has no session.
    [[nodiscard]] static std::optional<EditorSession> session(HWND rich_edit);
    [[nodiscard]] static bool restore_session(HWND rich_edit, EditorSession session);
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
    [[nodiscard]] static EditorSession capture_session(Impl& editor);
    static void restore_session(Impl& editor, EditorSession session);
    std::unique_ptr<Impl> impl_;
};

} // namespace lgaed::windows
