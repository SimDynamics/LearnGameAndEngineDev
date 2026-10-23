#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::presentation {
struct MathReadingRegion {
    std::string key, source_text;
    HWND target = nullptr;
};
struct MathPuzzleCallbacks {
    std::function<void()> changed;
    std::function<void(std::string)> submit;
    std::function<void()> layout_changed;
    std::function<std::string(std::string_view)> expression_request;
    std::function<void(HWND,bool)> expression_focus;
    std::function<void(HWND)> calculator_requested;
    // Called from the passive origin's subclass, preserving its local mouse
    // coordinates and DefSubclassProc context. The host owns gesture policy.
    std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> passive_gesture;
    std::function<void()> back_requested;
    std::function<void()> fullscreen_requested;
    std::function<void()> reset_requested;
};
// The existing curricular input configuration used by the shared math keyboard.
[[nodiscard]] std::string math_field_keyboard_context(HWND field);
// Android replaces the selected target with the exact returned LaTeX; no submit.
[[nodiscard]] bool apply_math_calculator_result(HWND field,std::string_view latex);
// Presents the existing math_puzzle composition. It never evaluates validators
// or exposes their expected answers; the host owns evaluation and feedback.
class MathPuzzleView {
public:
    MathPuzzleView(HWND parent,std::string original_block_json,std::string locale,
        std::string saved_answers_json,MathPuzzleCallbacks callbacks = {});
    ~MathPuzzleView();
    MathPuzzleView(const MathPuzzleView&) = delete;
    MathPuzzleView& operator=(const MathPuzzleView&) = delete;
    [[nodiscard]] HWND window() const;
    [[nodiscard]] int height_for_width(int width);
    void layout(int x,int y,int width);
    [[nodiscard]] std::string answers_json() const;
    // Presentation/caret drafts are saved, but never become another answer.
    [[nodiscard]] std::string answer_identity_json() const;
    [[nodiscard]] std::vector<MathReadingRegion> reading_regions() const;
    void set_enabled(bool);
    void set_text_scale(int percentage);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lgaed::presentation
