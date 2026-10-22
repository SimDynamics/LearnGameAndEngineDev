#pragma once

#include "presentation/windows/code_editor.hpp"

#include <windows.h>
#include <functional>
#include <memory>

namespace lgaed::presentation {

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
    void place(int x, int y, int width, int height);
    [[nodiscard]] bool visible() const;
    [[nodiscard]] HWND window() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lgaed::presentation
