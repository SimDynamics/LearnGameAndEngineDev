#include "presentation/windows/attempt_indicator.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace lgaed::presentation;
constexpr int width=192,height=160;
constexpr COLORREF background=RGB(34,34,34),accent=RGB(222,43,66),pending=RGB(255,255,255);
struct Raster {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    std::uint32_t* pixels=nullptr;
    Raster() {
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* data=nullptr;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
        if(!dc||!bitmap||!data)throw std::runtime_error("Cannot create native pixel probe");
        pixels=static_cast<std::uint32_t*>(data);previous=SelectObject(dc,bitmap);
        RECT all{0,0,width,height};const auto brush=CreateSolidBrush(background);
        FillRect(dc,&all,brush);DeleteObject(brush);
    }
    ~Raster(){if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    Raster(const Raster&)=delete;Raster& operator=(const Raster&)=delete;
};
void single(Raster& target,RECT glyph,Icon icon,COLORREF color) {
    const auto saved=SaveDC(target.dc);IntersectClipRect(target.dc,glyph.left,glyph.top,glyph.right,glyph.bottom);
    draw_icon(target.dc,glyph,icon,color);RestoreDC(target.dc,saved);
}
}
int main() {
    std::cout<<std::unitbuf;
    try {
        std::size_t layouts=0,rasters=0,failures=0;
        for(const int dpi:{96,120,144,168,192}) {
            const auto px=[&](int value){return MulDiv(value,dpi,96);};
            const RECT button{px(7),px(3),px(7+69),px(3+64)};
            const int side=MulDiv(px(43),4,5),x=button.left+(button.right-button.left-side)/2;
            const RECT glyph{x,button.top+px(12),x+side,button.top+px(12)+side};
            const auto timer=attempt_indicator::recovery_bounds(button,glyph);
            if(timer.top!=glyph.bottom||timer.left!=button.left||timer.right!=button.right||
                    timer.bottom!=button.bottom||timer.bottom-timer.top<px(12)) {
                ++failures;std::cerr<<"FAIL recovery label is not anchored immediately below its glyph at "<<dpi<<" DPI\n";
            }
            ++layouts;
            // Include rectangular popup slots and scrolled/clipped slots, not
            // only a square badge at the origin. Compare real GDI+ pixels.
            for(const auto icon:{Icon::terminal,Icon::attempt_math})for(const int offset:{0,-11}) {
                const int cell=px(43),inset=std::max(1,cell/12);
                const RECT slot{12+offset+inset,9+inset,12+offset+cell-inset,9+px(44)-inset};
                for(const double progress:{0.,0.001,0.125,0.5,0.875,0.999,1.}) {
                    Raster red,white,mixed;
                    const RECT clip{3,4,width-7,height-5};
                    for(auto dc:{red.dc,white.dc,mixed.dc})IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);
                    single(red,slot,icon,accent);single(white,slot,icon,pending);
                    const auto before=CreateRectRgn(0,0,0,0),after=CreateRectRgn(0,0,0,0);
                    const auto had_clip=GetClipRgn(mixed.dc,before);
                    attempt_indicator::draw(mixed.dc,slot,icon,accent,pending,progress);
                    const auto has_clip=GetClipRgn(mixed.dc,after);
                    if(had_clip!=has_clip||!EqualRgn(before,after))throw std::runtime_error("Attempt drawing changed its caller's clip");
                    DeleteObject(before);DeleteObject(after);GdiFlush();
                    const int split=slot.left+static_cast<int>(std::lround((slot.right-slot.left)*progress));
                    std::size_t mismatch=0,visible=0;
                    for(int y=0;y<height;++y)for(int col=0;col<width;++col) {
                        const auto index=y*width+col;
                        const auto expected=(col<split?red.pixels[index]:white.pixels[index])&0xffffffu;
                        mismatch+=(mixed.pixels[index]&0xffffffu)!=expected;
                        visible+=(white.pixels[index]&0xffffffu)!=0x222222u;
                    }
                    if(visible<15)throw std::runtime_error("The original Android attempt glyph did not render");
                    if(mismatch){++failures;std::cerr<<"FAIL overlapping red/white edges: "<<mismatch<<" pixels at "<<dpi<<" DPI, progress "<<progress<<'\n';}
                    ++rasters;
                }
            }
        }
        if(failures){std::cerr<<"FAIL "<<failures<<" attempt indicator checks\n";return 1;}
        std::cout<<"PASS "<<layouts<<" recovery layouts and "<<rasters<<" real GDI+ pixel comparisons: both routes, 100-200 percent DPI, fractional/full/empty reserves, scrolled slots, identical red/white geometry and preserved clips\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL attempt indicator: "<<error.what()<<'\n';return 1;}
}
