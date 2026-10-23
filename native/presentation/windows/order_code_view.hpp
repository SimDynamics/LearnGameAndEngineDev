#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace lgaed::presentation {
// One set of real rows: click in the requested order, drag to reorder, or use
// Alt+Up/Down. Row identity and source bytes survive all presentation changes.
class OrderCodeView {
public:
    OrderCodeView(HWND parent,std::vector<std::string> fragments,std::vector<std::size_t> order,std::function<void()> changed);
    ~OrderCodeView();
    [[nodiscard]] HWND window()const;
    [[nodiscard]] int height_for_width(int width);
    [[nodiscard]] std::vector<std::size_t> order()const;
    [[nodiscard]] std::vector<std::pair<HWND,std::string>> reading_regions()const;
    void set_enabled(bool);
    void set_text_scale(int percentage);
    void set_wrap(bool);
    void restart_tap_sequence();
    void reset();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
