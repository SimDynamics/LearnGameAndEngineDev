#pragma once

#include <windows.h>
#include <cstddef>
#include <memory>
#include <vector>

namespace lgaed::windows {
struct CodeGutterLine { std::size_t number=0;LONG character=0;POINTL text_position{}; };
struct CodeGutterSnapshot {
    bool active=false;
    int width=0;
    std::size_t logical_lines=0;
    std::vector<CodeGutterLine> visible;
};
// Draws only the native RichEdit margin. It owns no text, selection, history or
// input window, so all existing editor/lesson input routing stays in place.
class CodeGutter {
public:
    explicit CodeGutter(HWND rich_edit);
    ~CodeGutter();
    CodeGutter(const CodeGutter&)=delete;
    CodeGutter& operator=(const CodeGutter&)=delete;
    [[nodiscard]] CodeGutterSnapshot snapshot()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
[[nodiscard]] CodeGutterSnapshot code_gutter_snapshot(HWND rich_edit);
}
