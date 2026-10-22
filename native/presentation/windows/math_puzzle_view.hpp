#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
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
};
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
    [[nodiscard]] std::vector<MathReadingRegion> reading_regions() const;
    void set_enabled(bool);
    void set_text_scale(int percentage);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lgaed::presentation
