#pragma once
#include <windows.h>
#include <string>
#include <utility>
#include <vector>

namespace lgaed::presentation {
class ReadingShade {
public:
    ReadingShade(HWND,std::string source);
    ~ReadingShade();
    ReadingShade(const ReadingShade&)=delete;ReadingShade& operator=(const ReadingShade&)=delete;
    [[nodiscard]] bool matches(HWND,std::string_view)const;
    // Original-source UTF-16 offset. Every pending glyph uses neutral gray
    // against its original surface; read/restored ranges recover their ink.
    // Existing reference backgrounds are never changed by narration.
    // Never changes text, selection, geometry, emphasis or editing history.
    void read_through(std::size_t);
    void restore();
    [[nodiscard]] std::size_t length()const noexcept{return source_.size();}
private:
    struct Ink{long start,end;COLORREF color,background;bool automatic,automatic_background;};
    HWND window_;std::wstring source_,displayed_;std::vector<std::size_t> map_;std::vector<Ink> ink_;
    std::vector<std::pair<std::size_t,std::size_t>> references_;
    COLORREF surface_=0,semantic_surface_=0,prose_gray_=0,semantic_gray_=0;
    long boundary_=-1;bool valid_=false;
};
}
