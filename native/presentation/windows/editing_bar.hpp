#pragma once

#include "presentation/windows/code_editor.hpp"

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lgaed::state { struct StateData; }

namespace lgaed::presentation {
// Reads keyboard_shortcut_settings/cpp_shortcuts, using the original Android
// defaults only for an absent/invalid JSON array. It never writes preferences.
[[nodiscard]] std::vector<std::string> parse_state_shortcuts(const state::StateData&);

// A contextual strip over the lesson viewport. The owner resolves only its
// current assisted source/answer controls, excluding stdin and ordinary text.
class EditingBar {
public:
    EditingBar(HWND parent, std::function<windows::CodeEditor*(HWND)> resolve,
        std::function<void()> visibility_changed = {});
    ~EditingBar();
    EditingBar(const EditingBar&) = delete;
    EditingBar& operator=(const EditingBar&) = delete;

    void refresh();
    // Called by the owner at creation and after a settings save, never by the
    // focus timer. TAB and Undo/Redo remain available independently of this list.
    void set_shortcuts(std::vector<std::string> shortcuts);
    void place(int x, int y, int width, int height);
    [[nodiscard]] bool visible() const;
    [[nodiscard]] HWND window() const;
    [[nodiscard]] int logical_height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lgaed::presentation
