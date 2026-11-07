#pragma once
#include "core/image_resource.hpp"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>

namespace lgaed::presentation {
struct ImageViewport {
    double scale=1;
    int x=0,y=0;
    bool fit=true;
    void layout(unsigned image_width,unsigned image_height,int width,int height);
    void zoom(double next,unsigned image_width,unsigned image_height,int width,int height);
};
class CurricularImageView {
public:
    CurricularImageView(HWND parent,int id,const std::filesystem::path& lesson_source,const curriculum::ImageResource&);
    ~CurricularImageView();
    CurricularImageView(const CurricularImageView&)=delete;
    HWND window() const;
    int height_for_width(int width) const;
    bool loaded() const;
    unsigned image_width() const;
    unsigned image_height() const;
    const std::string& error() const;
    // Opens an internal child on explicit activation, retaining the source
    // page and its complete drafts/progress. It never launches an OS viewer.
    void show_detail(HWND owner);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
