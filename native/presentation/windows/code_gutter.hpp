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
    void set_appearance(COLORREF background,COLORREF foreground,int right_padding,int horizontal_padding=8);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
[[nodiscard]] CodeGutterSnapshot code_gutter_snapshot(HWND rich_edit);
// Android uses 8dp total horizontal room for an editor and 3dp for static code.
// The painted band has a separate 10dp gap before the text; snapshot().width
// reports only the band. This never changes the source text or its formatting.
void set_code_gutter_appearance(HWND rich_edit,COLORREF background,COLORREF foreground,int right_padding=3,int horizontal_padding=8);
}
