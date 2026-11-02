#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace lgaed::presentation {
class ReadingShade {
public:
    ReadingShade(HWND,std::string source);
    ~ReadingShade();
    ReadingShade(const ReadingShade&)=delete;ReadingShade& operator=(const ReadingShade&)=delete;
    [[nodiscard]] bool matches(HWND,std::string_view)const;
    // Original-source UTF-16 offset. Pending prose uses neutral ink; underlined
    // references retain their ink and use a theme-derived background cue.
    // Never changes text, selection, geometry, emphasis or editing history.
    void read_through(std::size_t);
    void restore();
    [[nodiscard]] std::size_t length()const noexcept{return source_.size();}
private:
    struct Ink{long start,end;COLORREF color,background;bool automatic,automatic_background,underlined;};
    HWND window_;std::wstring source_,displayed_;std::vector<std::size_t> map_;std::vector<Ink> ink_;
    long boundary_=-1;bool valid_=false;
};
}
