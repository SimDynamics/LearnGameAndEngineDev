#pragma once
#include "core/lesson.hpp"
#include "presentation/windows/code_editor.hpp"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lgaed::presentation {
// A single flowing code surface with real editable/drop targets at authored
// holes. Hidden answers are never used to populate controls.
class CppFillView {
public:
    CppFillView(HWND parent,const curriculum::Block&,std::string_view saved_json,
        std::function<void()> changed,std::function<void()> submit,
        std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> lesson_gesture = {});
    ~CppFillView();
    [[nodiscard]] HWND window() const;
    [[nodiscard]] int height_for_width(int width);
    [[nodiscard]] std::vector<std::string> answers() const;
    [[nodiscard]] std::vector<std::size_t> slots() const;
    [[nodiscard]] std::string response_json() const;
    [[nodiscard]] windows::CodeEditor* editor(HWND focused);
    // Reparent the same token bank into the fullscreen tray. nullptr restores
    // its original vertical layout; no card, listener or draft is recreated.
    [[nodiscard]] HWND palette_window() const;
    void set_palette_host(HWND parent = nullptr);
    [[nodiscard]] int palette_height_for_width(int width);
    void set_enabled(bool);
    void set_completed(bool);
    void set_text_scale(int percentage);
    void set_wrap(bool);
    void scroll_horizontal_to(int offset);
    void reset();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
